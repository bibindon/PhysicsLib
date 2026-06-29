#include "PhysicsLib.h"

#include <d3d9.h>

#include "PhysicsLibInternal.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>
#include <stdexcept>
#include <vector>

#pragma comment(lib, "d3d9.lib")
#if defined(DEBUG) || defined(_DEBUG)
#pragma comment(lib, "d3dx9d.lib")
#else
#pragma comment(lib, "d3dx9.lib")
#endif

namespace PhysicsLib
{
namespace
{
constexpr float kDeltaSeconds = 1.0f / 60.0f;
constexpr float kGroundContactOffset = 0.0005f;
constexpr float kSlideCastLookAhead = kGroundContactOffset * 48.0f;
constexpr float kMovingSlidePenetrationPushSpeed = 3.0f;
constexpr int kQuadTreeMaxDepth = 5;
constexpr size_t kQuadTreeNodeCapacity = 4;

LPDIRECT3D9 g_direct3d = NULL;
LPDIRECT3DDEVICE9 g_device = NULL;
bool g_initialized = false;

struct SimpleObject
{
    int id = 0;
    PhysicsLib::ObjectType objectType = PhysicsLib::ObjectType::Slide;
    LPD3DXMESH mesh = NULL;
    PhysicsLib::Transform transform;
    D3DXVECTOR3 localBoundsMin = D3DXVECTOR3(0.0f, 0.0f, 0.0f);
    D3DXVECTOR3 localBoundsMax = D3DXVECTOR3(0.0f, 0.0f, 0.0f);
};

std::vector<SimpleObject> g_simpleObjects;
int g_simpleNextId = 1;
bool g_doubleJumpEnabled = false;
bool g_infiniteJumpEnabled = true;
bool g_gravityEnabled = true;
InertiaMode g_inertiaMode = InertiaMode::PseudoInertia;
bool g_slideEnabled = true;
bool g_slideCheckEnabled = true;
bool g_tangentMoveEnabled = true;
bool g_airMoveEnabled = true;
bool g_optimizationEnabled = true;
bool g_movingFloorEnabled = true;
bool g_cameraAutoMoveEnabled = false;
bool g_focusModeEnabled = false;
bool g_chargeJumpEnabled = false;
bool g_landingStiffnessEnabled = false;
bool g_contactEnabled = true;
bool g_surfaceContactEnabled = true;
PhysicsLib::ShapeType g_shapeType = PhysicsLib::ShapeType::Sphere;
float g_radius = 0.5f;
float g_cylinderRadius = 0.5f;
float g_cylinderHeight = 1.0f;
float g_cuboidWidth = 1.0f;
float g_cuboidHeight = 1.0f;
float g_cuboidDepth = 1.0f;
float g_cuboidRotX = 0.0f;
float g_cuboidRotY = 0.0f;
float g_cuboidRotZ = 0.0f;
float g_playerFacingYaw = 0.0f;
float g_inertiaStrength = 0.85f;
float g_walkSpeed = 6.0f;
bool g_groundDashEnabled = false;
bool g_airDashEnabled = false;
float g_dashSpeed = 18.0f;
float g_dashDuration = 0.2f;

std::map<int, std::basic_string<TCHAR> > g_csvFileNames;
std::map<int, int> g_csvObjectIds;
std::map<int, D3DXVECTOR3> g_csvPrevPositions;

std::basic_string<TCHAR> MakePathWithExtension(const TCHAR* path, const TCHAR* extension)
{
    std::basic_string<TCHAR> result = path;
    const size_t slashPos = result.find_last_of(_T("\\/"));
    const size_t dotPos = result.find_last_of(_T('.'));
    if (dotPos != std::basic_string<TCHAR>::npos &&
        (slashPos == std::basic_string<TCHAR>::npos || dotPos > slashPos))
    {
        result.erase(dotPos);
    }
    result += extension;
    return result;
}

std::basic_string<TCHAR> ResolveCsvRelativePath(const TCHAR* csvPath, const TCHAR* relativePath)
{
    if (GetFileAttributes(relativePath) != INVALID_FILE_ATTRIBUTES)
    {
        return relativePath;
    }

    std::basic_string<TCHAR> result = csvPath;
    const size_t slashPos = result.find_last_of(_T("\\/"));
    if (slashPos == std::basic_string<TCHAR>::npos)
    {
        return relativePath;
    }

    result.erase(slashPos + 1);
    result += relativePath;
    return result;
}

bool IsYesToken(const TCHAR* token)
{
    if (token == NULL)
    {
        return false;
    }

    return token[0] == _T('y') || token[0] == _T('Y');
}

int LoadCsvObject(const TCHAR* fileName,
                  PhysicsLib::ObjectType objectType,
                  const D3DXVECTOR3& position,
                  const D3DXVECTOR3& rotation,
                  const D3DXVECTOR3& scale)
{
    const int id = PhysicsLib::Load(fileName, objectType, 0.0f);
    PhysicsLib::SetTransform(id, position, rotation, scale);
    return id;
}

bool LoadInstancedCsvObjects(const TCHAR* physicsCsvPath,
                             const TCHAR* fileName,
                             PhysicsLib::ObjectType objectType,
                             const D3DXVECTOR3& basePosition,
                             const D3DXVECTOR3& baseRotation,
                             const D3DXVECTOR3& baseScale,
                             int* outFirstId)
{
    if (outFirstId != nullptr)
    {
        *outFirstId = -1;
    }

    const std::basic_string<TCHAR> instancingCsvName = MakePathWithExtension(fileName, _T(".csv"));
    const std::basic_string<TCHAR> instancingCsvPath = ResolveCsvRelativePath(physicsCsvPath, instancingCsvName.c_str());

    FILE* instancingFile = NULL;
    if (_tfopen_s(&instancingFile, instancingCsvPath.c_str(), _T("rt")) != 0 || instancingFile == NULL)
    {
        return false;
    }

    D3DXMATRIX baseScaleMatrix;
    D3DXMATRIX baseRotationMatrix;
    D3DXMATRIX baseTranslationMatrix;
    D3DXMatrixScaling(&baseScaleMatrix, baseScale.x, baseScale.y, baseScale.z);
    D3DXMatrixRotationYawPitchRoll(&baseRotationMatrix, baseRotation.y, baseRotation.x, baseRotation.z);
    D3DXMatrixTranslation(&baseTranslationMatrix, basePosition.x, basePosition.y, basePosition.z);
    const D3DXMATRIX baseMatrix = baseScaleMatrix * baseRotationMatrix * baseTranslationMatrix;

    bool loaded = false;
    TCHAR line[512];
    _fgetts(line, 512, instancingFile);

    while (_fgetts(line, 512, instancingFile) != NULL)
    {
        if (line[0] == _T('#'))
        {
            continue;
        }

        TCHAR* context = NULL;
        TCHAR* token = _tcstok_s(line, _T(",\n"), &context);
        if (token == NULL)
        {
            continue;
        }
        const float localX = static_cast<float>(_tstof(token));

        token = _tcstok_s(NULL, _T(",\n"), &context);
        if (token == NULL)
        {
            continue;
        }
        const float localY = static_cast<float>(_tstof(token));

        token = _tcstok_s(NULL, _T(",\n"), &context);
        if (token == NULL)
        {
            continue;
        }
        const float localZ = static_cast<float>(_tstof(token));

        token = _tcstok_s(NULL, _T(",\n"), &context);
        if (token == NULL)
        {
            continue;
        }
        const float localRotY = static_cast<float>(_tstof(token));

        token = _tcstok_s(NULL, _T(",\n"), &context);
        if (token == NULL)
        {
            continue;
        }
        const float localScale = static_cast<float>(_tstof(token));

        D3DXVECTOR3 position;
        const D3DXVECTOR3 localPosition(localX, localY, localZ);
        D3DXVec3TransformCoord(&position, &localPosition, &baseMatrix);

        const D3DXVECTOR3 rotation(baseRotation.x,
                                   baseRotation.y + D3DXToRadian(localRotY),
                                   baseRotation.z);
        const D3DXVECTOR3 scale(baseScale.x * localScale,
                                baseScale.y * localScale,
                                baseScale.z * localScale);
        const int id = LoadCsvObject(fileName, objectType, position, rotation, scale);
        if (!loaded && outFirstId != nullptr)
        {
            *outFirstId = id;
        }
        loaded = true;
    }

    fclose(instancingFile);
    return loaded;
}

SimpleObject* FindSimpleObjectById(int id)
{
    for (size_t i = 0; i < g_simpleObjects.size(); ++i)
    {
        if (g_simpleObjects[i].id == id)
        {
            return &g_simpleObjects[i];
        }
    }

    return nullptr;
}

}

bool PhysicsLib::IntersectsAabb2D(const Aabb2D& a, const Aabb2D& b)
{
    if (a.maxX < b.minX || b.maxX < a.minX)
    {
        return false;
    }

    if (a.maxZ < b.minZ || b.maxZ < a.minZ)
    {
        return false;
    }

    return true;
}

bool PhysicsLib::ContainsAabb2D(const Aabb2D& outer, const Aabb2D& inner)
{
    return inner.minX >= outer.minX &&
           inner.maxX <= outer.maxX &&
           inner.minZ >= outer.minZ &&
           inner.maxZ <= outer.maxZ;
}

PhysicsLib::Aabb2D PhysicsLib::MakeSegmentAabb2D(const D3DXVECTOR3& start, const D3DXVECTOR3& end, float padding)
{
    Aabb2D bounds;
    const float totalPadding = kGroundContactOffset + padding;
    bounds.minX = std::min(start.x, end.x) - totalPadding;
    bounds.maxX = std::max(start.x, end.x) + totalPadding;
    bounds.minZ = std::min(start.z, end.z) - totalPadding;
    bounds.maxZ = std::max(start.z, end.z) + totalPadding;
    return bounds;
}

PhysicsLib::Aabb2D PhysicsLib::MakeWorldAabb2D(const D3DXVECTOR3& localBoundsMin,
                                               const D3DXVECTOR3& localBoundsMax,
                                               const Transform& transform)
{
    D3DXMATRIX scaleMatrix;
    D3DXMATRIX rotationMatrix;
    D3DXMATRIX translationMatrix;

    D3DXMatrixScaling(&scaleMatrix,
                      transform.scale.x,
                      transform.scale.y,
                      transform.scale.z);
    D3DXMatrixRotationYawPitchRoll(&rotationMatrix,
                                   transform.rotation.y,
                                   transform.rotation.x,
                                   transform.rotation.z);
    D3DXMatrixTranslation(&translationMatrix,
                          transform.position.x,
                          transform.position.y,
                          transform.position.z);

    const D3DXMATRIX worldMatrix = scaleMatrix * rotationMatrix * translationMatrix;
    const D3DXVECTOR3 corners[8] =
    {
        D3DXVECTOR3(localBoundsMin.x, localBoundsMin.y, localBoundsMin.z),
        D3DXVECTOR3(localBoundsMax.x, localBoundsMin.y, localBoundsMin.z),
        D3DXVECTOR3(localBoundsMin.x, localBoundsMax.y, localBoundsMin.z),
        D3DXVECTOR3(localBoundsMax.x, localBoundsMax.y, localBoundsMin.z),
        D3DXVECTOR3(localBoundsMin.x, localBoundsMin.y, localBoundsMax.z),
        D3DXVECTOR3(localBoundsMax.x, localBoundsMin.y, localBoundsMax.z),
        D3DXVECTOR3(localBoundsMin.x, localBoundsMax.y, localBoundsMax.z),
        D3DXVECTOR3(localBoundsMax.x, localBoundsMax.y, localBoundsMax.z),
    };

    Aabb2D bounds;
    bounds.minX = std::numeric_limits<float>::max();
    bounds.minZ = std::numeric_limits<float>::max();
    bounds.maxX = -std::numeric_limits<float>::max();
    bounds.maxZ = -std::numeric_limits<float>::max();

    for (int i = 0; i < 8; ++i)
    {
        D3DXVECTOR3 worldCorner;
        D3DXVec3TransformCoord(&worldCorner, &corners[i], &worldMatrix);
        bounds.minX = std::min(bounds.minX, worldCorner.x);
        bounds.minZ = std::min(bounds.minZ, worldCorner.z);
        bounds.maxX = std::max(bounds.maxX, worldCorner.x);
        bounds.maxZ = std::max(bounds.maxZ, worldCorner.z);
    }

    return bounds;
}

PhysicsLib::Aabb3D PhysicsLib::MakeWorldAabb3D(const D3DXVECTOR3& localBoundsMin,
                                               const D3DXVECTOR3& localBoundsMax,
                                               const Transform& transform)
{
    D3DXMATRIX worldMatrix = BuildWorldMatrix(transform);
    const D3DXVECTOR3 corners[8] =
    {
        D3DXVECTOR3(localBoundsMin.x, localBoundsMin.y, localBoundsMin.z),
        D3DXVECTOR3(localBoundsMax.x, localBoundsMin.y, localBoundsMin.z),
        D3DXVECTOR3(localBoundsMin.x, localBoundsMax.y, localBoundsMin.z),
        D3DXVECTOR3(localBoundsMax.x, localBoundsMax.y, localBoundsMin.z),
        D3DXVECTOR3(localBoundsMin.x, localBoundsMin.y, localBoundsMax.z),
        D3DXVECTOR3(localBoundsMax.x, localBoundsMin.y, localBoundsMax.z),
        D3DXVECTOR3(localBoundsMin.x, localBoundsMax.y, localBoundsMax.z),
        D3DXVECTOR3(localBoundsMax.x, localBoundsMax.y, localBoundsMax.z),
    };

    Aabb3D bounds;
    bounds.minX = std::numeric_limits<float>::max();
    bounds.minY = std::numeric_limits<float>::max();
    bounds.minZ = std::numeric_limits<float>::max();
    bounds.maxX = -std::numeric_limits<float>::max();
    bounds.maxY = -std::numeric_limits<float>::max();
    bounds.maxZ = -std::numeric_limits<float>::max();

    for (int i = 0; i < 8; ++i)
    {
        D3DXVECTOR3 worldCorner;
        D3DXVec3TransformCoord(&worldCorner, &corners[i], &worldMatrix);
        bounds.minX = std::min(bounds.minX, worldCorner.x);
        bounds.minY = std::min(bounds.minY, worldCorner.y);
        bounds.minZ = std::min(bounds.minZ, worldCorner.z);
        bounds.maxX = std::max(bounds.maxX, worldCorner.x);
        bounds.maxY = std::max(bounds.maxY, worldCorner.y);
        bounds.maxZ = std::max(bounds.maxZ, worldCorner.z);
    }

    return bounds;
}

PhysicsLib::Aabb3D PhysicsLib::MakeShapeAabb3D(const D3DXVECTOR3& position,
                                               ShapeType shapeType,
                                               float radius,
                                               float height)
{
    Aabb3D bounds;
    if (shapeType == ShapeType::Cuboid)
    {
        const float halfWidth = SettingsState::GetCuboidWidth() * 0.5f;
        const float halfHeight = SettingsState::GetCuboidHeight() * 0.5f;
        const float halfDepth = SettingsState::GetCuboidDepth() * 0.5f;
        bounds.minX = position.x - halfWidth;
        bounds.maxX = position.x + halfWidth;
        bounds.minY = position.y - halfHeight;
        bounds.maxY = position.y + halfHeight;
        bounds.minZ = position.z - halfDepth;
        bounds.maxZ = position.z + halfDepth;
        return bounds;
    }

    if (shapeType == ShapeType::Cylinder)
    {
        const float halfHeight = height * 0.5f;
        bounds.minX = position.x - radius;
        bounds.maxX = position.x + radius;
        bounds.minY = position.y - halfHeight;
        bounds.maxY = position.y + halfHeight;
        bounds.minZ = position.z - radius;
        bounds.maxZ = position.z + radius;
        return bounds;
    }

    if (shapeType == ShapeType::Sphere)
    {
        bounds.minX = position.x - radius;
        bounds.maxX = position.x + radius;
        bounds.minY = position.y - radius;
        bounds.maxY = position.y + radius;
        bounds.minZ = position.z - radius;
        bounds.maxZ = position.z + radius;
        return bounds;
    }

    bounds.minX = position.x;
    bounds.maxX = position.x;
    bounds.minY = position.y;
    bounds.maxY = position.y;
    bounds.minZ = position.z;
    bounds.maxZ = position.z;
    return bounds;
}

bool PhysicsLib::IntersectsAabb3D(const Aabb3D& a, const Aabb3D& b)
{
    if (a.maxX < b.minX || b.maxX < a.minX)
    {
        return false;
    }
    if (a.maxY < b.minY || b.maxY < a.minY)
    {
        return false;
    }
    if (a.maxZ < b.minZ || b.maxZ < a.minZ)
    {
        return false;
    }

    return true;
}

bool PhysicsLib::IsShapeBlockedOppositePush(size_t pushingObjectIndex,
                                            const Aabb3D& shapeBounds,
                                            const D3DXVECTOR3& pushVector)
{
    const float pushLength = D3DXVec3Length(&pushVector);
    if (pushLength <= 0.0001f)
    {
        return false;
    }

    const float contactTolerance = kGroundContactOffset * 4.0f;
    for (size_t i = 0; i < g_simpleObjects.size(); ++i)
    {
        if (i == pushingObjectIndex ||
            g_simpleObjects[i].objectType == ObjectType::PassThrough ||
            g_simpleObjects[i].mesh == NULL)
        {
            continue;
        }

        const Aabb3D objectBounds = MakeWorldAabb3D(g_simpleObjects[i].localBoundsMin,
                                                    g_simpleObjects[i].localBoundsMax,
                                                    g_simpleObjects[i].transform);

        if (fabsf(pushVector.x) >= fabsf(pushVector.y) &&
            fabsf(pushVector.x) >= fabsf(pushVector.z))
        {
            const bool overlapsY = shapeBounds.maxY > objectBounds.minY + contactTolerance &&
                                   shapeBounds.minY < objectBounds.maxY - contactTolerance;
            const bool overlapsZ = shapeBounds.maxZ > objectBounds.minZ + contactTolerance &&
                                   shapeBounds.minZ < objectBounds.maxZ - contactTolerance;
            if (!overlapsY || !overlapsZ)
            {
                continue;
            }

            if (pushVector.x > 0.0f &&
                shapeBounds.maxX >= objectBounds.minX - contactTolerance &&
                shapeBounds.minX < objectBounds.minX)
            {
                return true;
            }
            if (pushVector.x < 0.0f &&
                shapeBounds.minX <= objectBounds.maxX + contactTolerance &&
                shapeBounds.maxX > objectBounds.maxX)
            {
                return true;
            }
        }
        else if (fabsf(pushVector.y) >= fabsf(pushVector.z))
        {
            const bool overlapsX = shapeBounds.maxX > objectBounds.minX + contactTolerance &&
                                   shapeBounds.minX < objectBounds.maxX - contactTolerance;
            const bool overlapsZ = shapeBounds.maxZ > objectBounds.minZ + contactTolerance &&
                                   shapeBounds.minZ < objectBounds.maxZ - contactTolerance;
            if (!overlapsX || !overlapsZ)
            {
                continue;
            }

            if (pushVector.y > 0.0f &&
                shapeBounds.maxY >= objectBounds.minY - contactTolerance &&
                shapeBounds.minY < objectBounds.minY)
            {
                return true;
            }
            if (pushVector.y < 0.0f &&
                shapeBounds.minY <= objectBounds.maxY + contactTolerance &&
                shapeBounds.maxY > objectBounds.maxY)
            {
                return true;
            }
        }
        else
        {
            const bool overlapsX = shapeBounds.maxX > objectBounds.minX + contactTolerance &&
                                   shapeBounds.minX < objectBounds.maxX - contactTolerance;
            const bool overlapsY = shapeBounds.maxY > objectBounds.minY + contactTolerance &&
                                   shapeBounds.minY < objectBounds.maxY - contactTolerance;
            if (!overlapsX || !overlapsY)
            {
                continue;
            }

            if (pushVector.z > 0.0f &&
                shapeBounds.maxZ >= objectBounds.minZ - contactTolerance &&
                shapeBounds.minZ < objectBounds.minZ)
            {
                return true;
            }
            if (pushVector.z < 0.0f &&
                shapeBounds.minZ <= objectBounds.maxZ + contactTolerance &&
                shapeBounds.maxZ > objectBounds.maxZ)
            {
                return true;
            }
        }
    }

    return false;
}

bool PhysicsLib::ResolveMovingSlidePenetration(const D3DXVECTOR3& currentPosition,
                                               ShapeType shapeType,
                                               float radius,
                                               float height,
                                               D3DXVECTOR3* inOutPosition,
                                               D3DXVECTOR3* outPushNormal,
                                               int* outSupportObjectId,
                                               D3DXVECTOR3* outSupportVelocity,
                                               bool* outCrushed)
{
    if (inOutPosition == nullptr)
    {
        return false;
    }

    bool pushed = false;
    D3DXVECTOR3 position = *inOutPosition;
    D3DXVECTOR3 lastNormal(0.0f, 0.0f, 0.0f);
    int supportObjectId = -1;
    D3DXVECTOR3 supportVelocity(0.0f, 0.0f, 0.0f);
    bool crushed = false;
    float remainingPushDistance = kMovingSlidePenetrationPushSpeed * kDeltaSeconds;

    for (int pass = 0; pass < 4; ++pass)
    {
        if (remainingPushDistance <= 0.0001f)
        {
            break;
        }

        bool pushedThisPass = false;
        Aabb3D shapeBounds = MakeShapeAabb3D(position, shapeType, radius, height);
        const Aabb3D currentShapeBounds = MakeShapeAabb3D(currentPosition, shapeType, radius, height);

        for (size_t i = 0; i < g_simpleObjects.size(); ++i)
        {
            if (g_simpleObjects[i].objectType != ObjectType::MovingSlide ||
                g_simpleObjects[i].mesh == NULL)
            {
                continue;
            }

            const D3DXVECTOR3 objectVelocity = g_simpleObjects[i].transform.velocity;
            if (D3DXVec3Length(&objectVelocity) <= 0.0001f)
            {
                continue;
            }

            const Aabb3D objectBounds = MakeWorldAabb3D(g_simpleObjects[i].localBoundsMin,
                                                        g_simpleObjects[i].localBoundsMax,
                                                        g_simpleObjects[i].transform);
            if (!IntersectsAabb3D(shapeBounds, objectBounds))
            {
                continue;
            }

            const float positiveX = objectBounds.maxX - shapeBounds.minX + kGroundContactOffset;
            const float negativeX = objectBounds.minX - shapeBounds.maxX - kGroundContactOffset;
            const float positiveY = objectBounds.maxY - shapeBounds.minY + kGroundContactOffset;
            const float negativeY = objectBounds.minY - shapeBounds.maxY - kGroundContactOffset;
            const float positiveZ = objectBounds.maxZ - shapeBounds.minZ + kGroundContactOffset;
            const float negativeZ = objectBounds.minZ - shapeBounds.maxZ - kGroundContactOffset;
            const float contactTolerance = kGroundContactOffset * 8.0f;
            const bool wasAboveObject = currentShapeBounds.minY >= objectBounds.maxY - contactTolerance;
            const bool wasBelowObject = currentShapeBounds.maxY <= objectBounds.minY + contactTolerance;
            const bool overlapsObjectTop = shapeBounds.minY <= objectBounds.maxY + contactTolerance &&
                                           shapeBounds.maxY > objectBounds.maxY + contactTolerance;
            const bool overlapsObjectBottom = shapeBounds.maxY >= objectBounds.minY - contactTolerance &&
                                              shapeBounds.minY < objectBounds.minY - contactTolerance;
            const bool centerAboveTop = position.y >= objectBounds.maxY - contactTolerance;
            const bool centerBelowBottom = position.y <= objectBounds.minY + contactTolerance;

            D3DXVECTOR3 pushVector(positiveX, 0.0f, 0.0f);
            float bestAmount = fabsf(positiveX);
            if ((wasAboveObject || centerAboveTop) && overlapsObjectTop)
            {
                pushVector = D3DXVECTOR3(0.0f, positiveY, 0.0f);
            }
            else if ((wasBelowObject || centerBelowBottom) && overlapsObjectBottom)
            {
                pushVector = D3DXVECTOR3(0.0f, negativeY, 0.0f);
            }
            else
            {
                if (fabsf(negativeX) < bestAmount)
                {
                    pushVector = D3DXVECTOR3(negativeX, 0.0f, 0.0f);
                    bestAmount = fabsf(negativeX);
                }
                if (fabsf(positiveZ) < bestAmount)
                {
                    pushVector = D3DXVECTOR3(0.0f, 0.0f, positiveZ);
                    bestAmount = fabsf(positiveZ);
                }
                if (fabsf(negativeZ) < bestAmount)
                {
                    pushVector = D3DXVECTOR3(0.0f, 0.0f, negativeZ);
                }
            }

            const float pushLength = D3DXVec3Length(&pushVector);
            if (pushLength > remainingPushDistance && pushLength > 0.0001f)
            {
                pushVector *= remainingPushDistance / pushLength;
            }

            position += pushVector;
            remainingPushDistance -= D3DXVec3Length(&pushVector);
            lastNormal = pushVector;
            if (D3DXVec3Length(&lastNormal) > 0.0001f)
            {
                D3DXVec3Normalize(&lastNormal, &lastNormal);
            }
            if (lastNormal.y > 0.0f)
            {
                supportObjectId = g_simpleObjects[i].id;
                supportVelocity = objectVelocity;
            }

            shapeBounds = MakeShapeAabb3D(position, shapeType, radius, height);
            if (IsShapeBlockedOppositePush(i, shapeBounds, pushVector))
            {
                crushed = true;
            }
            pushed = true;
            pushedThisPass = true;
        }

        if (!pushedThisPass)
        {
            break;
        }
    }

    if (pushed)
    {
        *inOutPosition = position;
        if (outPushNormal != nullptr)
        {
            *outPushNormal = lastNormal;
        }
        if (outSupportObjectId != nullptr && supportObjectId >= 0)
        {
            *outSupportObjectId = supportObjectId;
        }
        if (outSupportVelocity != nullptr && supportObjectId >= 0)
        {
            *outSupportVelocity = supportVelocity;
        }
        if (outCrushed != nullptr)
        {
            *outCrushed = crushed;
        }
    }

    return pushed;
}

bool PhysicsLib::ComputeMeshLocalBounds(LPD3DXMESH mesh, D3DXVECTOR3* outMin, D3DXVECTOR3* outMax)
{
    if (mesh == NULL || outMin == nullptr || outMax == nullptr)
    {
        return false;
    }

    void* vertexBuffer = NULL;
    HRESULT result = mesh->LockVertexBuffer(D3DLOCK_READONLY, &vertexBuffer);
    if (FAILED(result))
    {
        return false;
    }

    const BYTE* vertices = static_cast<const BYTE*>(vertexBuffer);
    const DWORD stride = mesh->GetNumBytesPerVertex();
    const DWORD vertexCount = mesh->GetNumVertices();
    *outMin = D3DXVECTOR3(std::numeric_limits<float>::max(),
                          std::numeric_limits<float>::max(),
                          std::numeric_limits<float>::max());
    *outMax = D3DXVECTOR3(-std::numeric_limits<float>::max(),
                          -std::numeric_limits<float>::max(),
                          -std::numeric_limits<float>::max());

    for (DWORD i = 0; i < vertexCount; ++i)
    {
        const D3DXVECTOR3* position = reinterpret_cast<const D3DXVECTOR3*>(vertices + i * stride);
        outMin->x = std::min(outMin->x, position->x);
        outMin->y = std::min(outMin->y, position->y);
        outMin->z = std::min(outMin->z, position->z);
        outMax->x = std::max(outMax->x, position->x);
        outMax->y = std::max(outMax->y, position->y);
        outMax->z = std::max(outMax->z, position->z);
    }

    mesh->UnlockVertexBuffer();
    return vertexCount > 0;
}

void PhysicsLib::SplitQuadTreeNode(QuadTreeNode* node)
{
    if (node == nullptr || !node->children.empty())
    {
        return;
    }

    const float centerX = (node->bounds.minX + node->bounds.maxX) * 0.5f;
    const float centerZ = (node->bounds.minZ + node->bounds.maxZ) * 0.5f;
    node->children.resize(4);
    node->children[0].bounds = { node->bounds.minX, node->bounds.minZ, centerX, centerZ };
    node->children[1].bounds = { centerX, node->bounds.minZ, node->bounds.maxX, centerZ };
    node->children[2].bounds = { node->bounds.minX, centerZ, centerX, node->bounds.maxZ };
    node->children[3].bounds = { centerX, centerZ, node->bounds.maxX, node->bounds.maxZ };

    for (size_t i = 0; i < node->children.size(); ++i)
    {
        node->children[i].depth = node->depth + 1;
    }
}

void PhysicsLib::InsertQuadTreeObject(QuadTreeNode* node,
                                      size_t objectIndex,
                                      const Aabb2D& objectBounds,
                                      const std::vector<Aabb2D>& allBounds)
{
    if (node == nullptr)
    {
        return;
    }

    if (InsertIntoChildIfContained(node, objectIndex, objectBounds, allBounds))
    {
        return;
    }

    node->objectIndices.push_back(objectIndex);
    if (node->objectIndices.size() <= kQuadTreeNodeCapacity || node->depth >= kQuadTreeMaxDepth)
    {
        return;
    }

    SplitQuadTreeNode(node);
    for (size_t i = 0; i < node->objectIndices.size();)
    {
        const size_t storedIndex = node->objectIndices[i];
        if (InsertIntoChildIfContained(node, storedIndex, allBounds[storedIndex], allBounds))
        {
            node->objectIndices.erase(node->objectIndices.begin() + i);
        }
        else
        {
            ++i;
        }
    }
}

bool PhysicsLib::InsertIntoChildIfContained(QuadTreeNode* node,
                                            size_t objectIndex,
                                            const Aabb2D& objectBounds,
                                            const std::vector<Aabb2D>& allBounds)
{
    if (node == nullptr || node->children.empty())
    {
        return false;
    }

    for (size_t i = 0; i < node->children.size(); ++i)
    {
        if (ContainsAabb2D(node->children[i].bounds, objectBounds))
        {
            InsertQuadTreeObject(&node->children[i], objectIndex, objectBounds, allBounds);
            return true;
        }
    }

    return false;
}

void PhysicsLib::QueryQuadTree(const QuadTreeNode& node,
                               const Aabb2D& queryBounds,
                               std::vector<size_t>* outIndices)
{
    if (outIndices == nullptr || !IntersectsAabb2D(node.bounds, queryBounds))
    {
        return;
    }

    for (size_t i = 0; i < node.objectIndices.size(); ++i)
    {
        outIndices->push_back(node.objectIndices[i]);
    }

    for (size_t i = 0; i < node.children.size(); ++i)
    {
        QueryQuadTree(node.children[i], queryBounds, outIndices);
    }
}

std::vector<size_t> PhysicsLib::BuildCollisionCandidateIndices(const D3DXVECTOR3& start,
                                                               const D3DXVECTOR3& end,
                                                               float padding)
{
    std::vector<size_t> candidates;
    if (!SettingsState::IsOptimizationEnabled())
    {
        for (size_t i = 0; i < g_simpleObjects.size(); ++i)
        {
            candidates.push_back(i);
        }
        return candidates;
    }

    std::vector<Aabb2D> objectBounds(g_simpleObjects.size());
    bool hasRootBounds = false;
    Aabb2D rootBounds;
    for (size_t i = 0; i < g_simpleObjects.size(); ++i)
    {
        objectBounds[i] = MakeWorldAabb2D(g_simpleObjects[i].localBoundsMin,
                                          g_simpleObjects[i].localBoundsMax,
                                          g_simpleObjects[i].transform);
        if (g_simpleObjects[i].objectType == PhysicsLib::ObjectType::PassThrough ||
            g_simpleObjects[i].mesh == NULL)
        {
            continue;
        }

        if (!hasRootBounds)
        {
            rootBounds = objectBounds[i];
            hasRootBounds = true;
        }
        else
        {
            rootBounds.minX = std::min(rootBounds.minX, objectBounds[i].minX);
            rootBounds.minZ = std::min(rootBounds.minZ, objectBounds[i].minZ);
            rootBounds.maxX = std::max(rootBounds.maxX, objectBounds[i].maxX);
            rootBounds.maxZ = std::max(rootBounds.maxZ, objectBounds[i].maxZ);
        }
    }

    if (!hasRootBounds)
    {
        return candidates;
    }

    rootBounds.minX -= kGroundContactOffset;
    rootBounds.minZ -= kGroundContactOffset;
    rootBounds.maxX += kGroundContactOffset;
    rootBounds.maxZ += kGroundContactOffset;

    QuadTreeNode root;
    root.bounds = rootBounds;
    for (size_t i = 0; i < g_simpleObjects.size(); ++i)
    {
        if (g_simpleObjects[i].objectType == PhysicsLib::ObjectType::PassThrough ||
            g_simpleObjects[i].mesh == NULL)
        {
            continue;
        }

        InsertQuadTreeObject(&root, i, objectBounds[i], objectBounds);
    }

    QueryQuadTree(root, MakeSegmentAabb2D(start, end, padding), &candidates);
    return candidates;
}

// COMオブジェクトを安全に解放する処理である。
void PhysicsLib::SafeRelease(IUnknown* object)
{
    if (object != NULL)
    {
        object->Release();
    }
}

// Transform からワールド行列を生成する処理である。
D3DXMATRIX PhysicsLib::BuildWorldMatrix(const Transform& transform)
{
    D3DXMATRIX scaleMatrix;
    D3DXMATRIX rotationMatrix;
    D3DXMATRIX translationMatrix;

    D3DXMatrixScaling(&scaleMatrix, transform.scale.x, transform.scale.y, transform.scale.z);
    D3DXMatrixRotationYawPitchRoll(&rotationMatrix,
                                   transform.rotation.y,
                                   transform.rotation.x,
                                   transform.rotation.z);
    D3DXMatrixTranslation(&translationMatrix,
                          transform.position.x,
                          transform.position.y,
                          transform.position.z);

    return scaleMatrix * rotationMatrix * translationMatrix;
}

// メッシュの指定面からローカル法線を取り出す処理である。
bool PhysicsLib::ExtractFaceNormal(LPD3DXMESH mesh, DWORD faceIndex, D3DXVECTOR3* outNormal)
{
    if (mesh == NULL)
    {
        return false;
    }

    void* vertexBuffer = NULL;
    void* indexBuffer = NULL;
    const DWORD stride = mesh->GetNumBytesPerVertex();

    HRESULT result = mesh->LockVertexBuffer(D3DLOCK_READONLY, &vertexBuffer);
    if (FAILED(result))
    {
        return false;
    }

    result = mesh->LockIndexBuffer(D3DLOCK_READONLY, &indexBuffer);
    if (FAILED(result))
    {
        mesh->UnlockVertexBuffer();
        return false;
    }

    const BYTE* vertices = static_cast<const BYTE*>(vertexBuffer);
    const DWORD* indices32 = static_cast<const DWORD*>(indexBuffer);
    const WORD* indices16 = static_cast<const WORD*>(indexBuffer);
    const bool use32BitIndices = (mesh->GetOptions() & D3DXMESH_32BIT) != 0;

    DWORD i0 = 0;
    DWORD i1 = 0;
    DWORD i2 = 0;
    if (use32BitIndices)
    {
        i0 = indices32[faceIndex * 3 + 0];
        i1 = indices32[faceIndex * 3 + 1];
        i2 = indices32[faceIndex * 3 + 2];
    }
    else
    {
        i0 = indices16[faceIndex * 3 + 0];
        i1 = indices16[faceIndex * 3 + 1];
        i2 = indices16[faceIndex * 3 + 2];
    }

    const D3DXVECTOR3* p0 = reinterpret_cast<const D3DXVECTOR3*>(vertices + i0 * stride);
    const D3DXVECTOR3* p1 = reinterpret_cast<const D3DXVECTOR3*>(vertices + i1 * stride);
    const D3DXVECTOR3* p2 = reinterpret_cast<const D3DXVECTOR3*>(vertices + i2 * stride);

    D3DXVECTOR3 edge1 = *p1 - *p0;
    D3DXVECTOR3 edge2 = *p2 - *p0;
    D3DXVec3Cross(outNormal, &edge1, &edge2);
    D3DXVec3Normalize(outNormal, outNormal);

    mesh->UnlockIndexBuffer();
    mesh->UnlockVertexBuffer();
    return true;
}

// 線分と単一メッシュの接面判定をワールド座標系で行う処理である。

// 判定対象のメッシュを受け取る。
bool PhysicsLib::RayCastObject(LPD3DXMESH mesh,

                               // 判定対象の位置、回転、拡大率を受け取る。
                               const Transform& transform,

                               // ワールド座標系での線分の始点を受け取る。
                               const D3DXVECTOR3& rayOriginWorld,

                               // ワールド座標系での線分の終点を受け取る。
                               const D3DXVECTOR3& rayEndWorld,

                               // ヒット位置の出力先を受け取る。
                               D3DXVECTOR3* outPoint,

                               // ヒット面の法線の出力先を受け取る。
                               D3DXVECTOR3* outSurfaceNormal,

                               // ヒット距離の出力先を受け取る。
                               float* outDistance)
{
    // メッシュが無効なら判定できない。
    if (mesh == NULL)
    {
        // 判定失敗として終了する。
        return false;
    }


    // オブジェクトの Transform からワールド行列を作る。
    D3DXMATRIX worldMatrix = BuildWorldMatrix(transform);

    // ワールド座標からローカル座標へ戻す逆行列を用意する。
    D3DXMATRIX inverseWorldMatrix;

    // ワールド行列の逆行列を計算する。
    D3DXMatrixInverse(&inverseWorldMatrix, NULL, &worldMatrix);


    // 線分始点のローカル座標を格納する。
    D3DXVECTOR3 originLocal;

    // 線分終点のローカル座標を格納する。
    D3DXVECTOR3 endLocal;

    // 線分始点をローカル座標系へ変換する。
    D3DXVec3TransformCoord(&originLocal, &rayOriginWorld, &inverseWorldMatrix);

    // 線分終点をローカル座標系へ変換する。
    D3DXVec3TransformCoord(&endLocal, &rayEndWorld, &inverseWorldMatrix);


    // ローカル座標系での線分ベクトルを求める。
    D3DXVECTOR3 rayVectorLocal = endLocal - originLocal;

    // ローカル座標系での線分長を求める。
    const float maxDistanceLocal = D3DXVec3Length(&rayVectorLocal);

    // 線分長がほぼゼロならレイを作れない。
    if (maxDistanceLocal <= 0.0001f)
    {
        // 判定失敗として終了する。
        return false;
    }


    // D3DXIntersect に渡すため、線分方向を正規化する。
    D3DXVECTOR3 rayDirectionLocal = rayVectorLocal / maxDistanceLocal;


    // ヒット有無の受け取り先を初期化する。
    BOOL hit = FALSE;

    // ヒットした面インデックスの受け取り先を初期化する。
    DWORD faceIndex = 0;

    // バリセントリック座標 U の受け取り先を初期化する。
    FLOAT barycentricU = 0.0f;

    // バリセントリック座標 V の受け取り先を初期化する。
    FLOAT barycentricV = 0.0f;

    // ローカル座標系でのヒット距離の受け取り先を初期化する。
    FLOAT distanceLocal = 0.0f;

    // メッシュとレイの交差判定を実行する。
    HRESULT result = D3DXIntersect(mesh,

                                   // ローカル座標系のレイ始点を渡す。
                                   &originLocal,

                                   // ローカル座標系のレイ方向を渡す。
                                   &rayDirectionLocal,

                                   // ヒット有無の受け取り先を渡す。
                                   &hit,

                                   // 面インデックスの受け取り先を渡す。
                                   &faceIndex,

                                   // バリセントリック座標 U の受け取り先を渡す。
                                   &barycentricU,

                                   // バリセントリック座標 V の受け取り先を渡す。
                                   &barycentricV,

                                   // ヒット距離の受け取り先を渡す。
                                   &distanceLocal,

                                   // 全ヒット情報の配列は使わない。
                                   NULL,

                                   // 全ヒット数の受け取り先も使わない。
                                   NULL);


    // API失敗、未ヒット、または線分終点より先のヒットは無効とする。
    if (FAILED(result) || !hit || distanceLocal > maxDistanceLocal)
    {
        // 判定失敗として終了する。
        return false;
    }


    // ローカル座標系でのヒット位置を求める。
    D3DXVECTOR3 localHitPoint = originLocal + rayDirectionLocal * distanceLocal;

    // ワールド座標系でのヒット位置を格納する。
    D3DXVECTOR3 worldHitPoint;

    // ヒット位置をワールド座標系へ戻す。
    D3DXVec3TransformCoord(&worldHitPoint, &localHitPoint, &worldMatrix);


    // ローカル座標系での面法線を格納する。
    D3DXVECTOR3 localNormal;

    // ヒット面の法線を取り出せなければ失敗とする。
    if (!ExtractFaceNormal(mesh, faceIndex, &localNormal))
    {
        // 判定失敗として終了する。
        return false;
    }


    // 法線変換用に逆行列の転置行列を用意する。
    D3DXMATRIX inverseTransposeWorld;

    // 逆行列を転置して法線変換用行列を作る。
    D3DXMatrixTranspose(&inverseTransposeWorld, &inverseWorldMatrix);

    // ワールド座標系へ変換した法線を格納する。
    D3DXVECTOR3 surfaceNormal;

    // ローカル法線をワールド座標系へ変換する。
    D3DXVec3TransformNormal(&surfaceNormal, &localNormal, &inverseTransposeWorld);

    // 変換後の法線を正規化する。
    D3DXVec3Normalize(&surfaceNormal, &surfaceNormal);


    // ワールド座標系での始点からヒット位置までの差分を求める。
    D3DXVECTOR3 worldHitOffset = worldHitPoint - rayOriginWorld;


    // ヒット位置の出力先があれば書き込む。
    if (outPoint != nullptr)
    {
        // ヒット位置を呼び出し元へ返す。
        *outPoint = worldHitPoint;
    }

    // 法線の出力先があれば書き込む。
    if (outSurfaceNormal != nullptr)
    {
        // ヒット面の法線を呼び出し元へ返す。
        *outSurfaceNormal = surfaceNormal;
    }

    // 距離の出力先があれば書き込む。
    if (outDistance != nullptr)
    {
        // 始点からヒット位置までのワールド距離を呼び出し元へ返す。
        *outDistance = D3DXVec3Length(&worldHitOffset);
    }

    // ここまで到達したので判定成功である。
    return true;
}

std::vector<D3DXVECTOR3> PhysicsLib::BuildShapeCastOffsets(ShapeType shapeType, float radius, float height)
{
    std::vector<D3DXVECTOR3> offsets;
    offsets.push_back(D3DXVECTOR3(0.0f, 0.0f, 0.0f));

    if (shapeType == ShapeType::Point || radius <= 0.0f)
    {
        return offsets;
    }

    const float diagonal = radius * 0.70710678f;
    if (shapeType == ShapeType::Sphere)
    {
        offsets.push_back(D3DXVECTOR3(radius, 0.0f, 0.0f));
        offsets.push_back(D3DXVECTOR3(-radius, 0.0f, 0.0f));
        offsets.push_back(D3DXVECTOR3(0.0f, radius, 0.0f));
        offsets.push_back(D3DXVECTOR3(0.0f, -radius, 0.0f));
        offsets.push_back(D3DXVECTOR3(0.0f, 0.0f, radius));
        offsets.push_back(D3DXVECTOR3(0.0f, 0.0f, -radius));
        offsets.push_back(D3DXVECTOR3(diagonal, 0.0f, diagonal));
        offsets.push_back(D3DXVECTOR3(diagonal, 0.0f, -diagonal));
        offsets.push_back(D3DXVECTOR3(-diagonal, 0.0f, diagonal));
        offsets.push_back(D3DXVECTOR3(-diagonal, 0.0f, -diagonal));
        return offsets;
    }

    const float halfHeight = height * 0.5f;
    const float yLevels[] = { -halfHeight, 0.0f, halfHeight };

    if (shapeType == ShapeType::Cuboid)
    {
        const float hx = SettingsState::GetCuboidWidth() * 0.5f;
        const float hy = SettingsState::GetCuboidHeight() * 0.5f;
        const float hz = SettingsState::GetCuboidDepth() * 0.5f;
        const float cx[2] = { -hx, hx };
        const float cy[2] = { -hy, hy };
        const float cz[2] = { -hz, hz };

        D3DXMATRIX rotationMatrix;
        D3DXMatrixRotationYawPitchRoll(&rotationMatrix,
                                       D3DXToRadian(SettingsState::GetPlayerFacingYaw() + SettingsState::GetCuboidRotY()),
                                       D3DXToRadian(SettingsState::GetCuboidRotX()),
                                       D3DXToRadian(SettingsState::GetCuboidRotZ()));
        for (int xi = 0; xi < 2; ++xi)
        {
            for (int yi = 0; yi < 2; ++yi)
            {
                for (int zi = 0; zi < 2; ++zi)
                {
                    D3DXVECTOR3 localOffset(cx[xi], cy[yi], cz[zi]);
                    D3DXVECTOR3 rotatedOffset;
                    D3DXVec3TransformCoord(&rotatedOffset, &localOffset, &rotationMatrix);
                    offsets.push_back(rotatedOffset);
                }
            }
        }
    }
    else
    {
        for (int yIndex = 0; yIndex < 3; ++yIndex)
        {
            const float y = yLevels[yIndex];
            offsets.push_back(D3DXVECTOR3(0.0f, y, 0.0f));
            offsets.push_back(D3DXVECTOR3(radius, y, 0.0f));
            offsets.push_back(D3DXVECTOR3(-radius, y, 0.0f));
            offsets.push_back(D3DXVECTOR3(0.0f, y, radius));
            offsets.push_back(D3DXVECTOR3(0.0f, y, -radius));
            offsets.push_back(D3DXVECTOR3(diagonal, y, diagonal));
            offsets.push_back(D3DXVECTOR3(diagonal, y, -diagonal));
            offsets.push_back(D3DXVECTOR3(-diagonal, y, diagonal));
            offsets.push_back(D3DXVECTOR3(-diagonal, y, -diagonal));
        }
    }

    return offsets;
}

bool PhysicsLib::RayCastShapeObject(LPD3DXMESH mesh,
                                    const Transform& transform,
                                    const D3DXVECTOR3& rayOriginWorld,
                                    const D3DXVECTOR3& rayEndWorld,
                                    ShapeType shapeType,
                                    float radius,
                                    float height,
                                    D3DXVECTOR3* outPoint,
                                    D3DXVECTOR3* outSurfaceNormal,
                                    float* outDistance)
{
    const std::vector<D3DXVECTOR3> offsets = BuildShapeCastOffsets(shapeType, radius, height);
    bool foundHit = false;
    D3DXVECTOR3 nearestPoint = rayEndWorld;
    D3DXVECTOR3 nearestNormal(0.0f, 1.0f, 0.0f);
    float nearestDistance = std::numeric_limits<float>::max();

    for (size_t i = 0; i < offsets.size(); ++i)
    {
        D3DXVECTOR3 offsetHitPoint;
        D3DXVECTOR3 offsetHitNormal;
        float offsetHitDistance = 0.0f;
        if (RayCastObject(mesh,
                          transform,
                          rayOriginWorld + offsets[i],
                          rayEndWorld + offsets[i],
                          &offsetHitPoint,
                          &offsetHitNormal,
                          &offsetHitDistance))
        {
            const D3DXVECTOR3 centerHitPoint = offsetHitPoint - offsets[i];
            const D3DXVECTOR3 centerHitOffset = centerHitPoint - rayOriginWorld;
            const float centerHitDistance = D3DXVec3Length(&centerHitOffset);
            if (centerHitDistance >= nearestDistance)
            {
                continue;
            }

            nearestPoint = centerHitPoint;
            nearestNormal = offsetHitNormal;
            nearestDistance = centerHitDistance;
            foundHit = true;
        }
    }

    if (!foundHit)
    {
        return false;
    }

    if (outPoint != nullptr)
    {
        *outPoint = nearestPoint;
    }
    if (outSurfaceNormal != nullptr)
    {
        *outSurfaceNormal = nearestNormal;
    }
    if (outDistance != nullptr)
    {
        *outDistance = nearestDistance;
    }

    return true;
}

// 速度から接触面へ向かう成分だけを取り除く処理である。
D3DXVECTOR3 PhysicsLib::RemoveIntoSurfaceVelocity(const D3DXVECTOR3& velocity,
                                                  const D3DXVECTOR3& surfaceNormal)
{
    D3DXVECTOR3 normalizedNormal = surfaceNormal;
    if (D3DXVec3Length(&normalizedNormal) <= 0.0001f)
    {
        return velocity;
    }

    D3DXVec3Normalize(&normalizedNormal, &normalizedNormal);
    const float intoSurface = D3DXVec3Dot(&velocity, &normalizedNormal);
    if (intoSurface >= 0.0f)
    {
        return velocity;
    }

    return velocity - normalizedNormal * intoSurface;
}

// Xファイルから衝突用メッシュを読み込む処理である。
void PhysicsLib::LoadMesh(const TCHAR* modelPath, LPD3DXMESH* outMesh)
{
    LPD3DXBUFFER materialBuffer = NULL;
    DWORD materialCount = 0;

    HRESULT result = D3DXLoadMeshFromX(modelPath,
                                       D3DXMESH_SYSTEMMEM,
                                       g_device,
                                       NULL,
                                       &materialBuffer,
                                       NULL,
                                       &materialCount,
                                       outMesh);

    SafeRelease(materialBuffer);

    if (FAILED(result) || *outMesh == NULL)
    {
        throw std::runtime_error("Failed to load collision mesh.");
    }
}

bool SettingsState::IsDoubleJumpEnabled()
{
    return g_doubleJumpEnabled;
}

void SettingsState::SetDoubleJumpEnabled(bool enabled)
{
    g_doubleJumpEnabled = enabled;
}

bool SettingsState::IsInfiniteJumpEnabled()
{
    return g_infiniteJumpEnabled;
}

void SettingsState::SetInfiniteJumpEnabled(bool enabled)
{
    g_infiniteJumpEnabled = enabled;
}

bool SettingsState::IsGravityEnabled()
{
    return g_gravityEnabled;
}

void SettingsState::SetGravityEnabled(bool enabled)
{
    g_gravityEnabled = enabled;
}

InertiaMode SettingsState::GetInertiaMode()
{
    return g_inertiaMode;
}

void SettingsState::SetInertiaMode(InertiaMode mode)
{
    g_inertiaMode = mode;
}

bool SettingsState::IsSlideEnabled()
{
    return g_slideEnabled;
}

void SettingsState::SetSlideEnabled(bool enabled)
{
    g_slideEnabled = enabled;
}

bool SettingsState::IsSlideCheckEnabled()
{
    return g_slideCheckEnabled;
}

void SettingsState::SetSlideCheckEnabled(bool enabled)
{
    g_slideCheckEnabled = enabled;
}

bool SettingsState::IsTangentMoveEnabled()
{
    return g_tangentMoveEnabled;
}

void SettingsState::SetTangentMoveEnabled(bool enabled)
{
    g_tangentMoveEnabled = enabled;
}

bool SettingsState::IsAirMoveEnabled()
{
    return g_airMoveEnabled;
}

void SettingsState::SetAirMoveEnabled(bool enabled)
{
    g_airMoveEnabled = enabled;
}

bool SettingsState::IsOptimizationEnabled()
{
    return g_optimizationEnabled;
}

void SettingsState::SetOptimizationEnabled(bool enabled)
{
    g_optimizationEnabled = enabled;
}

bool SettingsState::IsMovingFloorEnabled()
{
    return g_movingFloorEnabled;
}

void SettingsState::SetMovingFloorEnabled(bool enabled)
{
    g_movingFloorEnabled = enabled;
}

bool SettingsState::IsCameraAutoMoveEnabled()
{
    return g_cameraAutoMoveEnabled;
}

void SettingsState::SetCameraAutoMoveEnabled(bool enabled)
{
    g_cameraAutoMoveEnabled = enabled;
}

bool SettingsState::IsFocusModeEnabled()
{
    return g_focusModeEnabled;
}

void SettingsState::SetFocusModeEnabled(bool enabled)
{
    g_focusModeEnabled = enabled;
}

bool SettingsState::IsChargeJumpEnabled()
{
    return g_chargeJumpEnabled;
}

void SettingsState::SetChargeJumpEnabled(bool enabled)
{
    g_chargeJumpEnabled = enabled;
}

bool SettingsState::IsLandingStiffnessEnabled()
{
    return g_landingStiffnessEnabled;
}

void SettingsState::SetLandingStiffnessEnabled(bool enabled)
{
    g_landingStiffnessEnabled = enabled;
}

bool SettingsState::IsContactEnabled()
{
    return g_contactEnabled;
}

void SettingsState::SetContactEnabled(bool enabled)
{
    g_contactEnabled = enabled;
}

bool SettingsState::IsSurfaceContactEnabled()
{
    return g_surfaceContactEnabled;
}

void SettingsState::SetSurfaceContactEnabled(bool enabled)
{
    g_surfaceContactEnabled = enabled;
}

PhysicsLib::ShapeType SettingsState::GetShapeType()
{
    return g_shapeType;
}

void SettingsState::SetShapeType(PhysicsLib::ShapeType shapeType)
{
    g_shapeType = shapeType;
}

float SettingsState::GetRadius()
{
    return g_radius;
}

void SettingsState::SetRadius(float radius)
{
    g_radius = radius;
}

float SettingsState::GetCylinderRadius()
{
    return g_cylinderRadius;
}

void SettingsState::SetCylinderRadius(float cylinderRadius)
{
    g_cylinderRadius = cylinderRadius;
}

float SettingsState::GetCylinderHeight()
{
    return g_cylinderHeight;
}

void SettingsState::SetCylinderHeight(float cylinderHeight)
{
    g_cylinderHeight = cylinderHeight;
}

float SettingsState::GetCuboidWidth()
{
    return g_cuboidWidth;
}

void SettingsState::SetCuboidWidth(float cuboidWidth)
{
    g_cuboidWidth = cuboidWidth;
}

float SettingsState::GetCuboidHeight()
{
    return g_cuboidHeight;
}

void SettingsState::SetCuboidHeight(float cuboidHeight)
{
    g_cuboidHeight = cuboidHeight;
}

float SettingsState::GetCuboidDepth()
{
    return g_cuboidDepth;
}

void SettingsState::SetCuboidDepth(float cuboidDepth)
{
    g_cuboidDepth = cuboidDepth;
}

float SettingsState::GetCuboidRotX()
{
    return g_cuboidRotX;
}

void SettingsState::SetCuboidRotX(float cuboidRotX)
{
    g_cuboidRotX = cuboidRotX;
}

float SettingsState::GetCuboidRotY()
{
    return g_cuboidRotY;
}

void SettingsState::SetCuboidRotY(float cuboidRotY)
{
    g_cuboidRotY = cuboidRotY;
}

float SettingsState::GetCuboidRotZ()
{
    return g_cuboidRotZ;
}

void SettingsState::SetCuboidRotZ(float cuboidRotZ)
{
    g_cuboidRotZ = cuboidRotZ;
}

float SettingsState::GetPlayerFacingYaw()
{
    return g_playerFacingYaw;
}

void SettingsState::SetPlayerFacingYaw(float playerFacingYaw)
{
    g_playerFacingYaw = playerFacingYaw;
}

float SettingsState::GetInertiaStrength()
{
    return g_inertiaStrength;
}

void SettingsState::SetInertiaStrength(float strength)
{
    g_inertiaStrength = (std::max)(0.0f, (std::min)(strength, 1.0f));
}

float SettingsState::GetWalkSpeed()
{
    return g_walkSpeed;
}

void SettingsState::SetWalkSpeed(float speed)
{
    g_walkSpeed = speed;
}

bool SettingsState::IsGroundDashEnabled()
{
    return g_groundDashEnabled;
}

void SettingsState::SetGroundDashEnabled(bool enabled)
{
    g_groundDashEnabled = enabled;
}

bool SettingsState::IsAirDashEnabled()
{
    return g_airDashEnabled;
}

void SettingsState::SetAirDashEnabled(bool enabled)
{
    g_airDashEnabled = enabled;
}

float SettingsState::GetDashSpeed()
{
    return g_dashSpeed;
}

void SettingsState::SetDashSpeed(float speed)
{
    g_dashSpeed = speed;
}

float SettingsState::GetDashDuration()
{
    return g_dashDuration;
}

void SettingsState::SetDashDuration(float duration)
{
    g_dashDuration = duration;
}

bool PhysicsLib::IsFocusModeEnabled()
{
    return SettingsState::IsFocusModeEnabled();
}

PhysicsLib::ShapeType PhysicsLib::GetShapeType()
{
    return SettingsState::GetShapeType();
}

float PhysicsLib::GetRadius()
{
    return SettingsState::GetRadius();
}

float PhysicsLib::GetCylinderRadius()
{
    return SettingsState::GetCylinderRadius();
}

float PhysicsLib::GetCylinderHeight()
{
    return SettingsState::GetCylinderHeight();
}

float PhysicsLib::GetCuboidWidth()
{
    return SettingsState::GetCuboidWidth();
}

float PhysicsLib::GetCuboidHeight()
{
    return SettingsState::GetCuboidHeight();
}

float PhysicsLib::GetCuboidDepth()
{
    return SettingsState::GetCuboidDepth();
}

float PhysicsLib::GetCuboidRotX()
{
    return SettingsState::GetCuboidRotX();
}

float PhysicsLib::GetCuboidRotY()
{
    return SettingsState::GetCuboidRotY();
}

float PhysicsLib::GetCuboidRotZ()
{
    return SettingsState::GetCuboidRotZ();
}

float PhysicsLib::GetWalkSpeed()
{
    return SettingsState::GetWalkSpeed();
}

float PhysicsLib::GetDashSpeed()
{
    return SettingsState::GetDashSpeed();
}

void PhysicsLib::LoadFromCsv(const TCHAR* csvPath)
{
    g_csvFileNames.clear();
    g_csvObjectIds.clear();
    g_csvPrevPositions.clear();

    FILE* file = NULL;
    if (_tfopen_s(&file, csvPath, _T("rt")) != 0 || file == NULL)
    {
        return;
    }

    TCHAR line[512];
    if (_fgetts(line, 512, file) == NULL)
    {
        fclose(file);
        return;
    }

    while (_fgetts(line, 512, file) != NULL)
    {
        TCHAR* context = NULL;
        TCHAR* token = _tcstok_s(line, _T(",\n"), &context);
        if (token == NULL)
        {
            continue;
        }
        const int csvId = _tstoi(token);
        token = _tcstok_s(NULL, _T(",\n"), &context);
        if (token == NULL)
        {
            continue;
        }
        const TCHAR* fileName = token;

        token = _tcstok_s(NULL, _T(",\n"), &context);
        const float posX = token != NULL ? static_cast<float>(_tstof(token)) : 0.0f;
        token = _tcstok_s(NULL, _T(",\n"), &context);
        const float posY = token != NULL ? static_cast<float>(_tstof(token)) : 0.0f;
        token = _tcstok_s(NULL, _T(",\n"), &context);
        const float posZ = token != NULL ? static_cast<float>(_tstof(token)) : 0.0f;
        token = _tcstok_s(NULL, _T(",\n"), &context);
        const float rotX = token != NULL ? static_cast<float>(_tstof(token)) : 0.0f;
        token = _tcstok_s(NULL, _T(",\n"), &context);
        const float rotY = token != NULL ? static_cast<float>(_tstof(token)) : 0.0f;
        token = _tcstok_s(NULL, _T(",\n"), &context);
        const float rotZ = token != NULL ? static_cast<float>(_tstof(token)) : 0.0f;
        token = _tcstok_s(NULL, _T(",\n"), &context);
        const float scaleX = token != NULL ? static_cast<float>(_tstof(token)) : 1.0f;
        token = _tcstok_s(NULL, _T(",\n"), &context);

        PhysicsLib::ObjectType objectType = PhysicsLib::ObjectType::Slide;
        if (token != NULL && _tcsstr(token, _T("NonCollision")) != NULL)
        {
            objectType = PhysicsLib::ObjectType::PassThrough;
        }

        token = _tcstok_s(NULL, _T(",\n"), &context);
        if (IsYesToken(token))
        {
            objectType = PhysicsLib::ObjectType::MovingSlide;
        }

        token = _tcstok_s(NULL, _T(",\n"), &context);
        const bool isInstancing = IsYesToken(token);

        const D3DXVECTOR3 position(posX, posY, posZ);
        const D3DXVECTOR3 rotation(D3DXToRadian(rotX), D3DXToRadian(rotY), D3DXToRadian(rotZ));
        const D3DXVECTOR3 scale(scaleX, scaleX, scaleX);

        int id = -1;
        bool loaded = false;
        if (isInstancing)
        {
            loaded = LoadInstancedCsvObjects(csvPath,
                                             fileName,
                                             objectType,
                                             position,
                                             rotation,
                                             scale,
                                             &id);
        }

        if (!loaded)
        {
            id = LoadCsvObject(fileName, objectType, position, rotation, scale);
        }

        g_csvFileNames[csvId] = fileName;
        g_csvObjectIds[csvId] = id;
    }

    fclose(file);
}

void PhysicsLib::ClearObjects()
{
    for (size_t i = 0; i < g_simpleObjects.size(); ++i)
    {
        SafeRelease(g_simpleObjects[i].mesh);
        g_simpleObjects[i].mesh = NULL;
    }

    g_simpleObjects.clear();
    g_simpleNextId = 1;
    g_csvFileNames.clear();
    g_csvObjectIds.clear();
    g_csvPrevPositions.clear();
}

const TCHAR* PhysicsLib::GetCsvFileName(int id)
{
    std::map<int, std::basic_string<TCHAR> >::const_iterator it = g_csvFileNames.find(id);
    if (it != g_csvFileNames.end())
    {
        return it->second.c_str();
    }
    return NULL;
}

int PhysicsLib::GetCsvObjectId(int csvId)
{
    std::map<int, int>::const_iterator it = g_csvObjectIds.find(csvId);
    if (it != g_csvObjectIds.end())
    {
        return it->second;
    }
    return -1;
}

void PhysicsLib::UpdateCsvTransform(int csvId,
                                    const D3DXVECTOR3& position,
                                    const D3DXVECTOR3& rotation,
                                    const D3DXVECTOR3& scale)
{
    std::map<int, int>::const_iterator it = g_csvObjectIds.find(csvId);
    if (it != g_csvObjectIds.end())
    {
        const int id = it->second;
        PhysicsLib::SetTransform(id, position, rotation, scale);

        std::map<int, D3DXVECTOR3>::const_iterator prevIt = g_csvPrevPositions.find(csvId);
        if (prevIt != g_csvPrevPositions.end())
        {
            const D3DXVECTOR3 delta = position - prevIt->second;
            const D3DXVECTOR3 velocity = delta * 60.0f;
            PhysicsLib::SetVelocity(id, velocity);
        }
        g_csvPrevPositions[csvId] = position;
    }
}

void PhysicsLib::Initialize()
{
    if (g_initialized)
    {
        return;
    }

    g_direct3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (g_direct3d == NULL)
    {
        throw std::runtime_error("Direct3DCreate9 failed.");
    }

    D3DPRESENT_PARAMETERS presentParameters;
    ZeroMemory(&presentParameters, sizeof(presentParameters));
    presentParameters.Windowed = TRUE;
    presentParameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
    presentParameters.BackBufferFormat = D3DFMT_UNKNOWN;
    presentParameters.BackBufferWidth = 1;
    presentParameters.BackBufferHeight = 1;
    presentParameters.hDeviceWindow = GetDesktopWindow();

    HRESULT result = g_direct3d->CreateDevice(D3DADAPTER_DEFAULT,
                                              D3DDEVTYPE_HAL,
                                              presentParameters.hDeviceWindow,
                                              D3DCREATE_SOFTWARE_VERTEXPROCESSING,
                                              &presentParameters,
                                              &g_device);

    if (FAILED(result))
    {
        SafeRelease(g_direct3d);
        g_direct3d = NULL;
        throw std::runtime_error("Failed to create internal Direct3D device.");
    }

    g_initialized = true;
    g_simpleObjects.clear();
    g_simpleNextId = 1;
}

void PhysicsLib::Finalize()
{
    SettingsDialog::Destroy();
    for (size_t i = 0; i < g_simpleObjects.size(); ++i)
    {
        SafeRelease(g_simpleObjects[i].mesh);
        g_simpleObjects[i].mesh = NULL;
    }
    g_simpleObjects.clear();
    g_simpleNextId = 1;
    g_initialized = false;

    SafeRelease(g_device);
    SafeRelease(g_direct3d);
    g_device = NULL;
    g_direct3d = NULL;
}

void PhysicsLib::Update(float deltaSeconds)
{
    for (size_t i = 0; i < g_simpleObjects.size(); ++i)
    {
        if (g_simpleObjects[i].objectType == ObjectType::MovingSlide)
        {
            g_simpleObjects[i].transform.position += g_simpleObjects[i].transform.velocity * deltaSeconds;
        }
    }
}

int PhysicsLib::Load(const TCHAR* modelPath, ObjectType objectType, float friction)
{
    UNREFERENCED_PARAMETER(friction);

    SimpleObject object;
    object.id = g_simpleNextId++;
    object.objectType = objectType;
    if (modelPath != nullptr && modelPath[0] != _T('\0'))
    {
        LoadMesh(modelPath, &object.mesh);
        ComputeMeshLocalBounds(object.mesh, &object.localBoundsMin, &object.localBoundsMax);
    }
    g_simpleObjects.push_back(object);
    return object.id;
}

void PhysicsLib::SetTransform(int id,
                              const D3DXVECTOR3& position,
                              const D3DXVECTOR3& rotation,
                              const D3DXVECTOR3& scale)
{
    for (size_t i = 0; i < g_simpleObjects.size(); ++i)
    {
        if (g_simpleObjects[i].id == id)
        {
            g_simpleObjects[i].transform.position = position;
            g_simpleObjects[i].transform.rotation = rotation;
            g_simpleObjects[i].transform.scale = scale;
            return;
        }
    }
}

void PhysicsLib::SetVelocity(int id, const D3DXVECTOR3& velocity)
{
    for (size_t i = 0; i < g_simpleObjects.size(); ++i)
    {
        if (g_simpleObjects[i].id == id)
        {
            g_simpleObjects[i].transform.velocity = velocity;
            return;
        }
    }
}

PhysicsLib::Transform PhysicsLib::GetTransform(int id)
{
    for (size_t i = 0; i < g_simpleObjects.size(); ++i)
    {
        if (g_simpleObjects[i].id == id)
        {
            return g_simpleObjects[i].transform;
        }
    }

    return Transform();
}

bool PhysicsLib::CheckCollide(const D3DXVECTOR3& currentPosition,
                              const D3DXVECTOR3& moveVector,
                              ShapeType shapeType,
                              D3DXVECTOR3* outPosition,
                              D3DXVECTOR3* outNextMoveVector,
                              std::vector<int>* outPassThroughIds,
                              std::vector<int>* outSolidIds,
                              float radius,
                              float height,
                              float* outNormalMove,
                              D3DXVECTOR3* outHitNormal,
                              float* outHitDistance,
                              D3DXVECTOR3* outSlideMove,
                              int* outSlideCount,
                              int* outSupportObjectId,
                              D3DXVECTOR3* outSupportVelocity,
                              bool* outCrushed)
{
    D3DXVECTOR3 nextPosition = currentPosition + moveVector * kDeltaSeconds;
    D3DXVECTOR3 nextMoveVector = moveVector;
    bool collided = false;
    float lastNormalMove = 0.0f;
    D3DXVECTOR3 lastHitNormal(0.0f, 0.0f, 0.0f);
    float lastHitDistance = 0.0f;
    D3DXVECTOR3 lastSlideMove(0.0f, 0.0f, 0.0f);
    int slideCount = 0;
    int supportObjectId = -1;
    D3DXVECTOR3 supportVelocity(0.0f, 0.0f, 0.0f);
    bool crushed = false;

    if (outPassThroughIds != nullptr)
    {
        outPassThroughIds->clear();
    }
    if (outSolidIds != nullptr)
    {
        outSolidIds->clear();
    }

    if (SettingsState::IsSurfaceContactEnabled())
    {
        const D3DXVECTOR3 frameMove = nextPosition - currentPosition;
        const float frameMoveLength = D3DXVec3Length(&frameMove);
        if (frameMoveLength > 0.0001f)
        {
            D3DXVECTOR3 nearestPoint = currentPosition;
            D3DXVECTOR3 nearestNormal(0.0f, 1.0f, 0.0f);
            bool foundHit = false;
            float nearestDistance = std::numeric_limits<float>::max();
            int nearestObjectId = -1;
            ObjectType nearestObjectType = ObjectType::Slide;
            D3DXVECTOR3 nearestVelocity(0.0f, 0.0f, 0.0f);

            const std::vector<size_t> candidateIndices = BuildCollisionCandidateIndices(currentPosition, nextPosition, radius);
            for (size_t candidateIndex = 0; candidateIndex < candidateIndices.size(); ++candidateIndex)
            {
                const size_t i = candidateIndices[candidateIndex];
                if (g_simpleObjects[i].objectType == ObjectType::PassThrough || g_simpleObjects[i].mesh == NULL)
                {
                    continue;
                }

                D3DXVECTOR3 hitPoint;
                D3DXVECTOR3 surfaceNormal;
                float hitDistance = 0.0f;

                if (RayCastShapeObject(g_simpleObjects[i].mesh,
                                       g_simpleObjects[i].transform,
                                       currentPosition,
                                       nextPosition,
                                       shapeType,
                                       radius,
                                       height,
                                       &hitPoint,
                                       &surfaceNormal,
                                       &hitDistance) &&
                    hitDistance < nearestDistance)
                {
                    const float normalMove = D3DXVec3Dot(&frameMove, &surfaceNormal);
                    lastNormalMove = normalMove;
                    if (normalMove > 0.0f)
                    {
                        continue;
                    }

                    foundHit = true;
                    nearestDistance = hitDistance;
                    nearestPoint = hitPoint;
                    nearestNormal = surfaceNormal;
                    nearestObjectId = g_simpleObjects[i].id;
                    nearestObjectType = g_simpleObjects[i].objectType;
                    nearestVelocity = g_simpleObjects[i].transform.velocity;
                }
            }

            if (foundHit)
            {
                nextPosition = nearestPoint;
                nextPosition += nearestNormal * kGroundContactOffset;
                nextMoveVector = RemoveIntoSurfaceVelocity(moveVector, nearestNormal);
                lastHitNormal = nearestNormal;
                lastHitDistance = nearestDistance;
                if (nearestNormal.y > 0.0f && nearestObjectType == ObjectType::MovingSlide)
                {
                    supportObjectId = nearestObjectId;
                    supportVelocity = nearestVelocity;
                }

                if (SettingsState::IsSlideEnabled())
                {
                    const D3DXVECTOR3 hitMove = nearestPoint - currentPosition;
                    const D3DXVECTOR3 remainingMove = frameMove - hitMove;
                    const float slideNormalMove = D3DXVec3Dot(&remainingMove, &nearestNormal);
                    D3DXVECTOR3 slideMove = remainingMove - nearestNormal * slideNormalMove;
                    lastSlideMove = slideMove;
                    if (D3DXVec3Length(&slideMove) > 0.0001f)
                    {
                        D3DXVECTOR3 slideDirection = slideMove;
                        D3DXVec3Normalize(&slideDirection, &slideDirection);
                        D3DXVECTOR3 slideEndPosition = nextPosition + slideMove;
                        const D3DXVECTOR3 slideCastEndPosition =
                            slideEndPosition + slideDirection * kSlideCastLookAhead;
                        D3DXVECTOR3 slideHitPoint;
                        D3DXVECTOR3 slideHitNormal;
                        float slideHitDistance = 0.0f;
                        bool slideBlocked = false;
                        D3DXVECTOR3 nearestSlidePoint = slideEndPosition;
                        D3DXVECTOR3 nearestSlideNormal(0.0f, 1.0f, 0.0f);
                        float nearestSlideDistance = std::numeric_limits<float>::max();
                        const std::vector<size_t> slideCandidateIndices =
                            BuildCollisionCandidateIndices(nextPosition, slideCastEndPosition, radius);
                        for (size_t candidateIndex = 0; candidateIndex < slideCandidateIndices.size(); ++candidateIndex)
                        {
                            const size_t i = slideCandidateIndices[candidateIndex];
                            if (g_simpleObjects[i].objectType == ObjectType::PassThrough || g_simpleObjects[i].mesh == NULL)
                            {
                                continue;
                            }

                            if (RayCastShapeObject(g_simpleObjects[i].mesh,
                                                   g_simpleObjects[i].transform,
                                                   nextPosition,
                                                   slideCastEndPosition,
                                                   shapeType,
                                                   radius,
                                                   height,
                                                   &slideHitPoint,
                                                   &slideHitNormal,
                                                   &slideHitDistance))
                            {
                                if (slideHitDistance >= nearestSlideDistance)
                                {
                                    continue;
                                }

                                const float slideHitNormalMove = D3DXVec3Dot(&slideMove, &slideHitNormal);
                                if (slideHitNormalMove > 0.0f)
                                {
                                    continue;
                                }

                                float safeSlideDistance = slideHitDistance - kSlideCastLookAhead;
                                if (safeSlideDistance < 0.0f)
                                {
                                    safeSlideDistance = 0.0f;
                                }
                                nearestSlidePoint = nextPosition + slideDirection * safeSlideDistance;
                                nearestSlideNormal = slideHitNormal;
                                nearestSlideDistance = slideHitDistance;
                                slideBlocked = true;
                            }
                        }

                        ++slideCount;
                        if (slideBlocked)
                        {
                            if (SettingsState::IsSlideCheckEnabled())
                            {
                                const D3DXVECTOR3 slideHitMove = nearestSlidePoint - nextPosition;
                                const D3DXVECTOR3 secondRemainingMove = slideMove - slideHitMove;
                                const float secondSlideNormalMove =
                                    D3DXVec3Dot(&secondRemainingMove, &nearestSlideNormal);
                                D3DXVECTOR3 secondSlideMove =
                                    secondRemainingMove - nearestSlideNormal * secondSlideNormalMove;
                                const float firstSlideNormalMove =
                                    D3DXVec3Dot(&secondSlideMove, &nearestNormal);
                                if (firstSlideNormalMove < 0.0f)
                                {
                                    secondSlideMove -= nearestNormal * firstSlideNormalMove;
                                }

                                const D3DXVECTOR3 secondSlideStartPosition =
                                    nearestSlidePoint + nearestSlideNormal * kGroundContactOffset;
                                nextPosition = secondSlideStartPosition;
                                nextMoveVector = RemoveIntoSurfaceVelocity(nextMoveVector, nearestSlideNormal);
                                lastHitNormal = nearestSlideNormal;
                                lastHitDistance = nearestSlideDistance;
                                lastSlideMove = secondSlideMove;

                                if (D3DXVec3Length(&secondSlideMove) > 0.0001f)
                                {
                                    D3DXVECTOR3 secondSlideDirection = secondSlideMove;
                                    D3DXVec3Normalize(&secondSlideDirection, &secondSlideDirection);
                                    const D3DXVECTOR3 secondSlideEndPosition =
                                        secondSlideStartPosition + secondSlideMove;
                                    const D3DXVECTOR3 secondSlideCastEndPosition =
                                        secondSlideEndPosition + secondSlideDirection * kSlideCastLookAhead;
                                    D3DXVECTOR3 secondSlideHitPoint;
                                    D3DXVECTOR3 secondSlideHitNormal;
                                    float secondSlideHitDistance = 0.0f;
                                    bool secondSlideBlocked = false;
                                    D3DXVECTOR3 nearestSecondSlideNormal(0.0f, 1.0f, 0.0f);
                                    float nearestSecondSlideDistance = std::numeric_limits<float>::max();
                                    const std::vector<size_t> secondSlideCandidateIndices =
                                        BuildCollisionCandidateIndices(secondSlideStartPosition,
                                                                       secondSlideCastEndPosition,
                                                                       radius);
                                    for (size_t candidateIndex = 0;
                                         candidateIndex < secondSlideCandidateIndices.size();
                                         ++candidateIndex)
                                    {
                                        const size_t i = secondSlideCandidateIndices[candidateIndex];
                                        if (g_simpleObjects[i].objectType == ObjectType::PassThrough ||
                                            g_simpleObjects[i].mesh == NULL)
                                        {
                                            continue;
                                        }

                                        if (RayCastShapeObject(g_simpleObjects[i].mesh,
                                                               g_simpleObjects[i].transform,
                                                               secondSlideStartPosition,
                                                               secondSlideCastEndPosition,
                                                               shapeType,
                                                               radius,
                                                               height,
                                                               &secondSlideHitPoint,
                                                               &secondSlideHitNormal,
                                                               &secondSlideHitDistance))
                                        {
                                            if (secondSlideHitDistance >= nearestSecondSlideDistance)
                                            {
                                                continue;
                                            }

                                            const float secondSlideHitNormalMove =
                                                D3DXVec3Dot(&secondSlideMove, &secondSlideHitNormal);
                                            if (secondSlideHitNormalMove > 0.0f)
                                            {
                                                continue;
                                            }

                                            nearestSecondSlideNormal = secondSlideHitNormal;
                                            nearestSecondSlideDistance = secondSlideHitDistance;
                                            secondSlideBlocked = true;
                                        }
                                    }

                                    ++slideCount;
                                    if (secondSlideBlocked)
                                    {
                                        nextPosition = currentPosition;
                                        nextMoveVector = D3DXVECTOR3(0.0f, 0.0f, 0.0f);
                                        lastHitNormal = nearestSecondSlideNormal;
                                        lastHitDistance = nearestSecondSlideDistance;
                                    }
                                    else
                                    {
                                        nextPosition = secondSlideEndPosition;
                                    }
                                }
                            }
                            else
                            {
                                nextPosition = nearestSlidePoint + nearestSlideNormal * kGroundContactOffset;
                                nextMoveVector = RemoveIntoSurfaceVelocity(nextMoveVector, nearestSlideNormal);
                                lastHitNormal = nearestSlideNormal;
                                lastHitDistance = nearestSlideDistance;
                            }
                        }
                        else
                        {
                            nextPosition = slideEndPosition;
                        }
                    }
                }
                collided = true;
            }
        }
    }

    D3DXVECTOR3 pushNormal(0.0f, 0.0f, 0.0f);
    int pushSupportObjectId = -1;
    D3DXVECTOR3 pushSupportVelocity(0.0f, 0.0f, 0.0f);
    if (ResolveMovingSlidePenetration(currentPosition,
                                      shapeType,
                                      radius,
                                      height,
                                      &nextPosition,
                                      &pushNormal,
                                      &pushSupportObjectId,
                                      &pushSupportVelocity,
                                      &crushed))
    {
        collided = true;
        lastHitNormal = pushNormal;
        lastHitDistance = 0.0f;
        nextMoveVector = RemoveIntoSurfaceVelocity(nextMoveVector, pushNormal);
        if (pushSupportObjectId >= 0)
        {
            supportObjectId = pushSupportObjectId;
            supportVelocity = pushSupportVelocity;
        }
    }

    if (outPosition != nullptr)
    {
        *outPosition = nextPosition;
    }
    if (outNextMoveVector != nullptr)
    {
        *outNextMoveVector = nextMoveVector;
    }
    if (outNormalMove != nullptr)
    {
        *outNormalMove = lastNormalMove;
    }
    if (outHitNormal != nullptr)
    {
        *outHitNormal = lastHitNormal;
    }
    if (outHitDistance != nullptr)
    {
        *outHitDistance = lastHitDistance;
    }
    if (outSlideMove != nullptr)
    {
        *outSlideMove = lastSlideMove;
    }
    if (outSlideCount != nullptr)
    {
        *outSlideCount = slideCount;
    }
    if (outSupportObjectId != nullptr)
    {
        *outSupportObjectId = supportObjectId;
    }
    if (outSupportVelocity != nullptr)
    {
        *outSupportVelocity = supportVelocity;
    }
    if (outCrushed != nullptr)
    {
        *outCrushed = crushed;
    }

    return collided;
}

bool PhysicsLib::CheckContact(int id, const D3DXVECTOR3& position, float distance)
{
    if (!SettingsState::IsContactEnabled() || distance < 0.0f)
    {
        return false;
    }

    for (size_t i = 0; i < g_simpleObjects.size(); ++i)
    {
        if (g_simpleObjects[i].id != id)
        {
            continue;
        }

        const D3DXVECTOR3 difference = g_simpleObjects[i].transform.position - position;
        return D3DXVec3Length(&difference) <= distance;
    }

    return false;
}

bool PhysicsLib::ResolveCameraCollision(const D3DXVECTOR3& targetPosition,
                                        const D3DXVECTOR3& desiredCameraPosition,
                                        float minimumDistance,
                                        float obstacleOffset,
                                        D3DXVECTOR3* outPosition)
{
    if (outPosition == nullptr)
    {
        return false;
    }

    D3DXVECTOR3 cameraVector = desiredCameraPosition - targetPosition;
    const float desiredDistance = D3DXVec3Length(&cameraVector);
    if (desiredDistance <= 0.0001f)
    {
        *outPosition = desiredCameraPosition;
        return false;
    }

    cameraVector /= desiredDistance;

    float nearestDistance = std::numeric_limits<float>::max();
    bool foundHit = false;
    const std::vector<size_t> candidateIndices =
        BuildCollisionCandidateIndices(targetPosition, desiredCameraPosition);
    for (size_t candidateIndex = 0; candidateIndex < candidateIndices.size(); ++candidateIndex)
    {
        const size_t i = candidateIndices[candidateIndex];
        if (g_simpleObjects[i].objectType == ObjectType::PassThrough || g_simpleObjects[i].mesh == NULL)
        {
            continue;
        }

        D3DXVECTOR3 hitPoint;
        D3DXVECTOR3 hitNormal;
        float hitDistance = 0.0f;
        if (RayCastObject(g_simpleObjects[i].mesh,
                          g_simpleObjects[i].transform,
                          targetPosition,
                          desiredCameraPosition,
                          &hitPoint,
                          &hitNormal,
                          &hitDistance))
        {
            if (hitDistance >= nearestDistance)
            {
                continue;
            }

            nearestDistance = hitDistance;
            foundHit = true;
        }
    }

    if (!foundHit)
    {
        *outPosition = desiredCameraPosition;
        return false;
    }

    float cameraDistance = nearestDistance - obstacleOffset;
    if (cameraDistance < minimumDistance)
    {
        cameraDistance = minimumDistance;
    }
    if (cameraDistance > desiredDistance)
    {
        cameraDistance = desiredDistance;
    }

    *outPosition = targetPosition + cameraVector * cameraDistance;
    return true;
}

}
