#include "PhysicsLib.h"

#include <d3d9.h>

#include "PhysicsLibInternal.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <new>
#include <stdexcept>
#include <string>
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

// プロファイリング用の累計カウンタと時間計測である。
std::chrono::steady_clock::duration g_profileRayCastObjectDuration = std::chrono::steady_clock::duration::zero();
std::chrono::steady_clock::duration g_profileRayCastShapeObjectDuration = std::chrono::steady_clock::duration::zero();
std::chrono::steady_clock::duration g_profileCheckCollideDuration = std::chrono::steady_clock::duration::zero();
int g_profileRayCastObjectCount = 0;
int g_profileRayCastShapeObjectCount = 0;
int g_profileCheckCollideCount = 0;

// 関数の呼び出し時間をスコープ終了時に累計へ加算する計測ヘルパーである。
class ProfileScope
{
public:
    ProfileScope(std::chrono::steady_clock::duration* outDuration, int* outCount)
        : m_outDuration(outDuration)
        , m_start(std::chrono::steady_clock::now())
    {
        if (outCount != nullptr)
        {
            ++(*outCount);
        }
    }

    ~ProfileScope()
    {
        *m_outDuration += std::chrono::steady_clock::now() - m_start;
    }

private:
    std::chrono::steady_clock::duration* m_outDuration;
    std::chrono::steady_clock::time_point m_start;
};

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
// レイ-メッシュ交差判定は2つの実装を用意し、コンパイル時マクロで切り替える。
// PHYSICSLIB_USE_D3DX_INTERSECT を定義すると D3DXIntersect を使う従来実装になり、
// 未定義なら自前の Möller-Trumbore 実装（高速化版）になる。
#if defined(PHYSICSLIB_USE_D3DX_INTERSECT)

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
    // 計測スコープを開始する。
    ProfileScope profileScope(&g_profileRayCastObjectDuration, &g_profileRayCastObjectCount);

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

#else

// 自前のレイ-三角形交差（Möller-Trumbore）による高速化版。
// 物理メッシュは低ポリのため、頂点/インデックスバッファを一度だけロックして
// 全トライアングルを走査し、最近接ヒットを返す。

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
    // 計測スコープを開始する。
    ProfileScope profileScope(&g_profileRayCastObjectDuration, &g_profileRayCastObjectCount);

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


    // 交差判定に使うため、線分方向を正規化する。
    D3DXVECTOR3 rayDirectionLocal = rayVectorLocal / maxDistanceLocal;


    // 頂点バッファとインデックスバッファを一度だけロックして全トライアングルを走査する。
    void* vertexBuffer = NULL;
    void* indexBuffer = NULL;
    const DWORD stride = mesh->GetNumBytesPerVertex();

    // 頂点バッファをロックする。
    HRESULT result = mesh->LockVertexBuffer(D3DLOCK_READONLY, &vertexBuffer);
    if (FAILED(result))
    {
        // ロック失敗なら判定失敗として終了する。
        return false;
    }

    // インデックスバッファをロックする。
    result = mesh->LockIndexBuffer(D3DLOCK_READONLY, &indexBuffer);
    if (FAILED(result))
    {
        // ロック失敗なら頂点バッファを解放して判定失敗として終了する。
        mesh->UnlockVertexBuffer();
        return false;
    }


    const BYTE* vertices = static_cast<const BYTE*>(vertexBuffer);
    const DWORD* indices32 = static_cast<const DWORD*>(indexBuffer);
    const WORD* indices16 = static_cast<const WORD*>(indexBuffer);
    const bool use32BitIndices = (mesh->GetOptions() & D3DXMESH_32BIT) != 0;
    const DWORD faceCount = mesh->GetNumFaces();


    // 最近接ヒットのローカル距離を保持する。
    float nearestDistanceLocal = std::numeric_limits<float>::max();

    // 最近接ヒットのローカル法線を保持する。
    D3DXVECTOR3 nearestLocalNormal(0.0f, 0.0f, 0.0f);

    // ヒットが一つでも見つかったかどうかを保持する。
    bool foundHit = false;


    // 全トライアングルを走査する。
    for (DWORD faceIndex = 0; faceIndex < faceCount; ++faceIndex)
    {
        // 面を構成する3頂点のインデックスを取り出す。
        DWORD index0 = 0;
        DWORD index1 = 0;
        DWORD index2 = 0;
        if (use32BitIndices)
        {
            index0 = indices32[faceIndex * 3 + 0];
            index1 = indices32[faceIndex * 3 + 1];
            index2 = indices32[faceIndex * 3 + 2];
        }
        else
        {
            index0 = indices16[faceIndex * 3 + 0];
            index1 = indices16[faceIndex * 3 + 1];
            index2 = indices16[faceIndex * 3 + 2];
        }

        // 三角形の頂点座標を取り出す。
        const D3DXVECTOR3* p0 = reinterpret_cast<const D3DXVECTOR3*>(vertices + index0 * stride);
        const D3DXVECTOR3* p1 = reinterpret_cast<const D3DXVECTOR3*>(vertices + index1 * stride);
        const D3DXVECTOR3* p2 = reinterpret_cast<const D3DXVECTOR3*>(vertices + index2 * stride);

        // 三角形の2辺を求める。
        const D3DXVECTOR3 edge1 = *p1 - *p0;
        const D3DXVECTOR3 edge2 = *p2 - *p0;

        // レイ方向と辺2の外積を求める。
        D3DXVECTOR3 pvec;
        D3DXVec3Cross(&pvec, &rayDirectionLocal, &edge2);

        // 行列式を求める。ほぼ0ならレイと三角形は平行なのでスキップする。
        const float determinant = D3DXVec3Dot(&edge1, &pvec);
        if (determinant > -0.000001f && determinant < 0.000001f)
        {
            continue;
        }

        const float inverseDeterminant = 1.0f / determinant;

        // レイ始点から三角形頂点0へのベクトルを求める。
        const D3DXVECTOR3 tvec = originLocal - *p0;

        // バリセントリック座標 U を求める。
        const float u = D3DXVec3Dot(&tvec, &pvec) * inverseDeterminant;
        if (u < 0.0f || u > 1.0f)
        {
            continue;
        }

        // バリセントリック座標 V を求める。
        D3DXVECTOR3 qvec;
        D3DXVec3Cross(&qvec, &tvec, &edge1);
        const float v = D3DXVec3Dot(&rayDirectionLocal, &qvec) * inverseDeterminant;
        if (v < 0.0f || u + v > 1.0f)
        {
            continue;
        }

        // ヒット距離を求める。線分の範囲外、または既知の最近接より遠い場合は無視する。
        const float distanceLocal = D3DXVec3Dot(&edge2, &qvec) * inverseDeterminant;
        if (distanceLocal < 0.0f ||
            distanceLocal > maxDistanceLocal ||
            distanceLocal >= nearestDistanceLocal)
        {
            continue;
        }

        // 最近接ヒットを更新する。
        nearestDistanceLocal = distanceLocal;
        foundHit = true;

        // ヒット面のローカル法線を求める。
        D3DXVec3Cross(&nearestLocalNormal, &edge1, &edge2);
        D3DXVec3Normalize(&nearestLocalNormal, &nearestLocalNormal);
    }


    // ロックしたバッファを解放する。
    mesh->UnlockIndexBuffer();
    mesh->UnlockVertexBuffer();


    // ヒットがなければ判定失敗として終了する。
    if (!foundHit)
    {
        return false;
    }


    // ローカル座標系でのヒット位置を求める。
    const D3DXVECTOR3 localHitPoint = originLocal + rayDirectionLocal * nearestDistanceLocal;

    // ワールド座標系でのヒット位置を格納する。
    D3DXVECTOR3 worldHitPoint;

    // ヒット位置をワールド座標系へ戻す。
    D3DXVec3TransformCoord(&worldHitPoint, &localHitPoint, &worldMatrix);


    // 法線変換用に逆行列の転置行列を用意する。
    D3DXMATRIX inverseTransposeWorld;

    // 逆行列を転置して法線変換用行列を作る。
    D3DXMatrixTranspose(&inverseTransposeWorld, &inverseWorldMatrix);

    // ワールド座標系へ変換した法線を格納する。
    D3DXVECTOR3 surfaceNormal;

    // ローカル法線をワールド座標系へ変換する。
    D3DXVec3TransformNormal(&surfaceNormal, &nearestLocalNormal, &inverseTransposeWorld);

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

#endif

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
    // 計測スコープを開始する。
    ProfileScope profileScope(&g_profileRayCastShapeObjectDuration, &g_profileRayCastShapeObjectCount);

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

void PhysicsLib::ResetProfileAccumulators()
{
    g_profileRayCastObjectDuration = std::chrono::steady_clock::duration::zero();
    g_profileRayCastShapeObjectDuration = std::chrono::steady_clock::duration::zero();
    g_profileCheckCollideDuration = std::chrono::steady_clock::duration::zero();
    g_profileRayCastObjectCount = 0;
    g_profileRayCastShapeObjectCount = 0;
    g_profileCheckCollideCount = 0;
}

void PhysicsLib::GetProfileCounters(int* outRayCastObjectCount,
                                    int* outRayCastShapeObjectCount,
                                    int* outCheckCollideCount,
                                    double* outRayCastObjectMilliseconds,
                                    double* outRayCastShapeObjectMilliseconds,
                                    double* outCheckCollideMilliseconds)
{
    if (outRayCastObjectCount != nullptr)
    {
        *outRayCastObjectCount = g_profileRayCastObjectCount;
    }
    if (outRayCastShapeObjectCount != nullptr)
    {
        *outRayCastShapeObjectCount = g_profileRayCastShapeObjectCount;
    }
    if (outCheckCollideCount != nullptr)
    {
        *outCheckCollideCount = g_profileCheckCollideCount;
    }
    if (outRayCastObjectMilliseconds != nullptr)
    {
        *outRayCastObjectMilliseconds =
            std::chrono::duration<double, std::milli>(g_profileRayCastObjectDuration).count();
    }
    if (outRayCastShapeObjectMilliseconds != nullptr)
    {
        *outRayCastShapeObjectMilliseconds =
            std::chrono::duration<double, std::milli>(g_profileRayCastShapeObjectDuration).count();
    }
    if (outCheckCollideMilliseconds != nullptr)
    {
        *outCheckCollideMilliseconds =
            std::chrono::duration<double, std::milli>(g_profileCheckCollideDuration).count();
    }
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

namespace
{

class CollisionMeshHierarchyAllocator : public ID3DXAllocateHierarchy
{
public:
    STDMETHOD(CreateFrame)(LPCSTR name, LPD3DXFRAME* outFrame)
    {
        if (outFrame == NULL)
        {
            return E_POINTER;
        }

        *outFrame = NULL;
        D3DXFRAME* frame = new (std::nothrow) D3DXFRAME();
        if (frame == NULL)
        {
            return E_OUTOFMEMORY;
        }

        D3DXMatrixIdentity(&frame->TransformationMatrix);
        if (name != NULL)
        {
            const size_t nameLength = std::strlen(name);
            frame->Name = new (std::nothrow) char[nameLength + 1];
            if (frame->Name == NULL)
            {
                delete frame;
                return E_OUTOFMEMORY;
            }
            strcpy_s(frame->Name, nameLength + 1, name);
        }

        *outFrame = frame;
        return S_OK;
    }

    STDMETHOD(CreateMeshContainer)(LPCSTR name,
                                   const D3DXMESHDATA* meshData,
                                   const D3DXMATERIAL*,
                                   const D3DXEFFECTINSTANCE*,
                                   DWORD,
                                   const DWORD*,
                                   LPD3DXSKININFO skinInfo,
                                   LPD3DXMESHCONTAINER* outMeshContainer)
    {
        if (meshData == NULL ||
            meshData->Type != D3DXMESHTYPE_MESH ||
            meshData->pMesh == NULL ||
            outMeshContainer == NULL)
        {
            return E_INVALIDARG;
        }

        *outMeshContainer = NULL;
        D3DXMESHCONTAINER* container = new (std::nothrow) D3DXMESHCONTAINER();
        if (container == NULL)
        {
            return E_OUTOFMEMORY;
        }

        if (name != NULL)
        {
            const size_t nameLength = std::strlen(name);
            container->Name = new (std::nothrow) char[nameLength + 1];
            if (container->Name == NULL)
            {
                delete container;
                return E_OUTOFMEMORY;
            }
            strcpy_s(container->Name, nameLength + 1, name);
        }

        container->MeshData.Type = D3DXMESHTYPE_MESH;
        container->MeshData.pMesh = meshData->pMesh;
        container->MeshData.pMesh->AddRef();
        container->pSkinInfo = skinInfo;
        if (container->pSkinInfo != NULL)
        {
            container->pSkinInfo->AddRef();
        }

        *outMeshContainer = container;
        return S_OK;
    }

    STDMETHOD(DestroyFrame)(LPD3DXFRAME frame)
    {
        if (frame != NULL)
        {
            delete[] frame->Name;
            delete frame;
        }
        return S_OK;
    }

    STDMETHOD(DestroyMeshContainer)(LPD3DXMESHCONTAINER meshContainer)
    {
        if (meshContainer != NULL)
        {
            delete[] meshContainer->Name;
            if (meshContainer->MeshData.Type == D3DXMESHTYPE_MESH &&
                meshContainer->MeshData.pMesh != NULL)
            {
                meshContainer->MeshData.pMesh->Release();
            }
            if (meshContainer->pSkinInfo != NULL)
            {
                meshContainer->pSkinInfo->Release();
            }
            delete meshContainer;
        }
        return S_OK;
    }
};

bool UsesBlenderOfficialAxisTransform(const TCHAR* modelPath)
{
    FILE* file = NULL;
    if (_tfopen_s(&file, modelPath, _T("rb")) != 0 || file == NULL)
    {
        return false;
    }

    const size_t maxHeaderSize = 64 * 1024;
    std::vector<char> fileHeader(maxHeaderSize);
    const size_t readSize = fread(fileHeader.data(), 1, fileHeader.size(), file);
    fclose(file);

    std::string compactHeader;
    compactHeader.reserve(readSize);
    for (size_t i = 0; i < readSize; ++i)
    {
        const unsigned char character = static_cast<unsigned char>(fileHeader[i]);
        if (std::isspace(character) == 0)
        {
            compactHeader.push_back(fileHeader[i]);
        }
    }

    std::string normalizedHeader = compactHeader;
    std::string::size_type negativeZeroPos = normalizedHeader.find("-0.000000");
    while (negativeZeroPos != std::string::npos)
    {
        normalizedHeader.replace(negativeZeroPos, 9, "0.000000");
        negativeZeroPos = normalizedHeader.find("-0.000000", negativeZeroPos);
    }

    const std::string blenderAxisTransform =
        "FrameTransformMatrix{"
        "1.000000,0.000000,0.000000,0.000000,"
        "0.000000,0.000000,-1.000000,0.000000,"
        "0.000000,1.000000,0.000000,0.000000,";
    const std::string blenderAxisTransformWithMirroredX =
        "FrameTransformMatrix{"
        "-1.000000,0.000000,0.000000,0.000000,"
        "0.000000,0.000000,1.000000,0.000000,"
        "0.000000,1.000000,0.000000,0.000000,";
    return normalizedHeader.find(blenderAxisTransform) != std::string::npos ||
           normalizedHeader.find(blenderAxisTransformWithMirroredX) != std::string::npos;
}

void CorrectBlenderOfficialAxisTransforms(LPD3DXFRAME frame, bool skipCurrentFrame)
{
    if (frame == NULL)
    {
        return;
    }

    if (!skipCurrentFrame)
    {
        D3DXMATRIX blenderAxisConversion;
        D3DXMatrixIdentity(&blenderAxisConversion);
        blenderAxisConversion._22 = 0.0f;
        blenderAxisConversion._23 = 1.0f;
        blenderAxisConversion._32 = 1.0f;
        blenderAxisConversion._33 = 0.0f;
        frame->TransformationMatrix =
            blenderAxisConversion * frame->TransformationMatrix;
        frame->TransformationMatrix._43 = -frame->TransformationMatrix._43;
    }

    CorrectBlenderOfficialAxisTransforms(frame->pFrameSibling, false);
    CorrectBlenderOfficialAxisTransforms(frame->pFrameFirstChild, false);
}

void GatherHierarchyMeshes(LPD3DXFRAME frame,
                           const D3DXMATRIX* parentMatrix,
                           std::vector<LPD3DXMESH>* meshes,
                           std::vector<D3DXMATRIX>* transforms)
{
    if (frame == NULL)
    {
        return;
    }

    D3DXMATRIX combinedMatrix = frame->TransformationMatrix;
    if (parentMatrix != NULL)
    {
        combinedMatrix = frame->TransformationMatrix * (*parentMatrix);
    }

    LPD3DXMESHCONTAINER container = frame->pMeshContainer;
    while (container != NULL)
    {
        if (container->MeshData.Type == D3DXMESHTYPE_MESH &&
            container->MeshData.pMesh != NULL)
        {
            meshes->push_back(container->MeshData.pMesh);
            transforms->push_back(combinedMatrix);
        }
        container = container->pNextMeshContainer;
    }

    GatherHierarchyMeshes(frame->pFrameSibling, parentMatrix, meshes, transforms);
    GatherHierarchyMeshes(frame->pFrameFirstChild, &combinedMatrix, meshes, transforms);
}

HRESULT LoadBlenderOfficialCollisionMesh(const TCHAR* modelPath, LPD3DXMESH* outMesh)
{
    CollisionMeshHierarchyAllocator allocator;
    LPD3DXFRAME frameRoot = NULL;
    HRESULT result = D3DXLoadMeshHierarchyFromX(modelPath,
                                                D3DXMESH_SYSTEMMEM,
                                                g_device,
                                                &allocator,
                                                NULL,
                                                &frameRoot,
                                                NULL);
    if (FAILED(result) || frameRoot == NULL)
    {
        return result;
    }

    bool hasSyntheticRoot = false;
    if (frameRoot->pMeshContainer == NULL &&
        (frameRoot->Name == NULL || frameRoot->Name[0] == '\0'))
    {
        hasSyntheticRoot = true;
    }
    CorrectBlenderOfficialAxisTransforms(frameRoot, hasSyntheticRoot);

    std::vector<LPD3DXMESH> meshes;
    std::vector<D3DXMATRIX> transforms;
    GatherHierarchyMeshes(frameRoot, NULL, &meshes, &transforms);
    if (meshes.empty())
    {
        D3DXFrameDestroy(frameRoot, &allocator);
        return E_FAIL;
    }

    // D3DXConcatenateMeshes は全入力メッシュの頂点宣言が一致している必要がある。
    // Blender 公式エクスポーターは UV を持つメッシュと持たないメッシュを混在して出力するため
    // （FVF が D3DFVF_XYZ|D3DFVF_NORMAL|D3DFVF_TEX1 と D3DFVF_XYZ|D3DFVF_NORMAL に分かれる）、
    // そのまま結合すると D3DERR_INVALIDCALL で失敗する。
    // 衝突判定には位置のみが必要なので、全メッシュを D3DFVF_XYZ|D3DFVF_NORMAL に統一してから結合する。
    const DWORD unifiedFvf = D3DFVF_XYZ | D3DFVF_NORMAL;
    std::vector<LPD3DXMESH> unifiedMeshes;
    unifiedMeshes.reserve(meshes.size());
    for (LPD3DXMESH mesh : meshes)
    {
        LPD3DXMESH clonedMesh = NULL;
        if (mesh->GetFVF() == unifiedFvf)
        {
            clonedMesh = mesh;
            clonedMesh->AddRef();
        }
        else
        {
            result = mesh->CloneMeshFVF(mesh->GetOptions(), unifiedFvf, g_device, &clonedMesh);
            if (FAILED(result) || clonedMesh == NULL)
            {
                for (LPD3DXMESH releasedMesh : unifiedMeshes)
                {
                    releasedMesh->Release();
                }
                D3DXFrameDestroy(frameRoot, &allocator);
                return result;
            }
        }
        unifiedMeshes.push_back(clonedMesh);
    }

    D3DVERTEXELEMENT9 declaration[MAX_FVF_DECL_SIZE];
    result = unifiedMeshes[0]->GetDeclaration(declaration);
    if (SUCCEEDED(result))
    {
        result = D3DXConcatenateMeshes(unifiedMeshes.data(),
                                       static_cast<UINT>(unifiedMeshes.size()),
                                       D3DXMESH_SYSTEMMEM | D3DXMESH_32BIT,
                                       transforms.data(),
                                       NULL,
                                       declaration,
                                       g_device,
                                       outMesh);
    }

    for (LPD3DXMESH clonedMesh : unifiedMeshes)
    {
        clonedMesh->Release();
    }

    D3DXFrameDestroy(frameRoot, &allocator);
    if (FAILED(result) || *outMesh == NULL)
    {
        return result;
    }

    void* indexBuffer = NULL;
    result = (*outMesh)->LockIndexBuffer(0, &indexBuffer);
    if (FAILED(result) || indexBuffer == NULL)
    {
        (*outMesh)->Release();
        *outMesh = NULL;
        return result;
    }

    const DWORD faceCount = (*outMesh)->GetNumFaces();
    if (((*outMesh)->GetOptions() & D3DXMESH_32BIT) != 0)
    {
        DWORD* indices = static_cast<DWORD*>(indexBuffer);
        for (DWORD faceIndex = 0; faceIndex < faceCount; ++faceIndex)
        {
            std::swap(indices[faceIndex * 3 + 1], indices[faceIndex * 3 + 2]);
        }
    }
    else
    {
        WORD* indices = static_cast<WORD*>(indexBuffer);
        for (DWORD faceIndex = 0; faceIndex < faceCount; ++faceIndex)
        {
            std::swap(indices[faceIndex * 3 + 1], indices[faceIndex * 3 + 2]);
        }
    }

    result = (*outMesh)->UnlockIndexBuffer();
    if (FAILED(result))
    {
        (*outMesh)->Release();
        *outMesh = NULL;
    }
    return result;
}

}

// Xファイルから衝突用メッシュを読み込む処理である。
void PhysicsLib::LoadMesh(const TCHAR* modelPath, LPD3DXMESH* outMesh)
{
    *outMesh = NULL;
    if (UsesBlenderOfficialAxisTransform(modelPath))
    {
        const HRESULT blenderResult = LoadBlenderOfficialCollisionMesh(modelPath, outMesh);
        if (FAILED(blenderResult) || *outMesh == NULL)
        {
            throw std::runtime_error("Failed to load Blender collision mesh.");
        }
        return;
    }

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

bool PhysicsLib::TryMovePushable(int id,
                                 const D3DXVECTOR3& movement,
                                 D3DXVECTOR3* outMovedMovement)
{
    if (outMovedMovement == nullptr)
    {
        return false;
    }

    *outMovedMovement = D3DXVECTOR3(0.0f, 0.0f, 0.0f);
    if (D3DXVec3LengthSq(&movement) <= 0.0000001f)
    {
        return false;
    }

    size_t pushableIndex = g_simpleObjects.size();
    for (size_t i = 0; i < g_simpleObjects.size(); ++i)
    {
        if (g_simpleObjects[i].id == id)
        {
            pushableIndex = i;
            break;
        }
    }

    if (pushableIndex >= g_simpleObjects.size() ||
        g_simpleObjects[pushableIndex].objectType != ObjectType::Pushable ||
        g_simpleObjects[pushableIndex].mesh == NULL)
    {
        return false;
    }

    const float kCollisionTolerance = kGroundContactOffset * 4.0f;
    D3DXVECTOR3 requestedMovement(movement.x, 0.0f, movement.z);
    D3DXVECTOR3 movedMovement(0.0f, 0.0f, 0.0f);
    const float requestedAxisMovement[2] = { requestedMovement.x, requestedMovement.z };

    for (int axis = 0; axis < 2; ++axis)
    {
        const float requested = requestedAxisMovement[axis];
        if (fabsf(requested) <= 0.0001f)
        {
            continue;
        }

        const Aabb3D currentBounds =
            MakeWorldAabb3D(g_simpleObjects[pushableIndex].localBoundsMin,
                            g_simpleObjects[pushableIndex].localBoundsMax,
                            g_simpleObjects[pushableIndex].transform);
        float allowed = requested;

        for (size_t i = 0; i < g_simpleObjects.size(); ++i)
        {
            if (i == pushableIndex ||
                g_simpleObjects[i].objectType == ObjectType::PassThrough ||
                g_simpleObjects[i].mesh == NULL)
            {
                continue;
            }

            const Aabb3D obstacleBounds =
                MakeWorldAabb3D(g_simpleObjects[i].localBoundsMin,
                                g_simpleObjects[i].localBoundsMax,
                                g_simpleObjects[i].transform);
            const bool overlapsY =
                currentBounds.maxY > obstacleBounds.minY + kCollisionTolerance &&
                currentBounds.minY < obstacleBounds.maxY - kCollisionTolerance;
            if (!overlapsY)
            {
                continue;
            }

            if (axis == 0)
            {
                const bool overlapsZ =
                    currentBounds.maxZ > obstacleBounds.minZ + kCollisionTolerance &&
                    currentBounds.minZ < obstacleBounds.maxZ - kCollisionTolerance;
                if (!overlapsZ)
                {
                    continue;
                }

                if (requested > 0.0f &&
                    currentBounds.maxX <= obstacleBounds.minX + kCollisionTolerance &&
                    currentBounds.maxX + requested > obstacleBounds.minX)
                {
                    const float candidate =
                        obstacleBounds.minX - currentBounds.maxX - kCollisionTolerance;
                    if (candidate < allowed)
                    {
                        allowed = (std::max)(0.0f, candidate);
                    }
                }
                else if (requested < 0.0f &&
                         currentBounds.minX >= obstacleBounds.maxX - kCollisionTolerance &&
                         currentBounds.minX + requested < obstacleBounds.maxX)
                {
                    const float candidate =
                        obstacleBounds.maxX - currentBounds.minX + kCollisionTolerance;
                    if (candidate > allowed)
                    {
                        allowed = (std::min)(0.0f, candidate);
                    }
                }
            }
            else
            {
                const bool overlapsX =
                    currentBounds.maxX > obstacleBounds.minX + kCollisionTolerance &&
                    currentBounds.minX < obstacleBounds.maxX - kCollisionTolerance;
                if (!overlapsX)
                {
                    continue;
                }

                if (requested > 0.0f &&
                    currentBounds.maxZ <= obstacleBounds.minZ + kCollisionTolerance &&
                    currentBounds.maxZ + requested > obstacleBounds.minZ)
                {
                    const float candidate =
                        obstacleBounds.minZ - currentBounds.maxZ - kCollisionTolerance;
                    if (candidate < allowed)
                    {
                        allowed = (std::max)(0.0f, candidate);
                    }
                }
                else if (requested < 0.0f &&
                         currentBounds.minZ >= obstacleBounds.maxZ - kCollisionTolerance &&
                         currentBounds.minZ + requested < obstacleBounds.maxZ)
                {
                    const float candidate =
                        obstacleBounds.maxZ - currentBounds.minZ + kCollisionTolerance;
                    if (candidate > allowed)
                    {
                        allowed = (std::min)(0.0f, candidate);
                    }
                }
            }
        }

        if (axis == 0)
        {
            g_simpleObjects[pushableIndex].transform.position.x += allowed;
            movedMovement.x += allowed;
        }
        else
        {
            g_simpleObjects[pushableIndex].transform.position.z += allowed;
            movedMovement.z += allowed;
        }
    }

    *outMovedMovement = movedMovement;
    return D3DXVec3LengthSq(&movedMovement) > 0.0000001f;
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
    // 計測スコープを開始する。
    ProfileScope profileScope(&g_profileCheckCollideDuration, &g_profileCheckCollideCount);

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

bool PhysicsLib::CheckContactShape(int id,
                                   const D3DXVECTOR3& position,
                                   ShapeType shapeType,
                                   float radius,
                                   float height,
                                   float upOffset,
                                   float downOffset)
{
    if (upOffset < 0.0f || downOffset < 0.0f)
    {
        return false;
    }

    // position is the foot position; shape casting uses the collision center.
    float collisionCenterOffsetY = 0.0f;
    if (shapeType == ShapeType::Cylinder)
    {
        collisionCenterOffsetY = height * 0.5f;
    }
    else if (shapeType == ShapeType::Sphere)
    {
        collisionCenterOffsetY = radius;
    }
    else if (shapeType == ShapeType::Cuboid)
    {
        collisionCenterOffsetY = SettingsState::GetCuboidHeight() * 0.5f;
    }

    const D3DXVECTOR3 collisionCenterPosition =
        position + D3DXVECTOR3(0.0f, collisionCenterOffsetY, 0.0f);

    for (size_t i = 0; i < g_simpleObjects.size(); ++i)
    {
        if (g_simpleObjects[i].id != id)
        {
            continue;
        }

        if (g_simpleObjects[i].mesh == NULL)
        {
            return false;
        }

        // メッシュのワールドAABBと判定位置が十分離れている場合は、
        // レイキャストせずに接触なしとみなす。
        {
            const Aabb3D worldBounds = MakeWorldAabb3D(g_simpleObjects[i].localBoundsMin,
                                                       g_simpleObjects[i].localBoundsMax,
                                                       g_simpleObjects[i].transform);
            const float margin = (std::max)(radius, 0.5f);
            if (position.x > worldBounds.maxX + margin ||
                position.x < worldBounds.minX - margin ||
                position.z > worldBounds.maxZ + margin ||
                position.z < worldBounds.minZ - margin)
            {
                return false;
            }
            if (position.y + height < worldBounds.minY - margin ||
                position.y > worldBounds.maxY + margin)
            {
                return false;
            }
        }

        const D3DXVECTOR3 sweepStart =
            collisionCenterPosition + D3DXVECTOR3(0.0f, upOffset, 0.0f);
        const D3DXVECTOR3 sweepEnd =
            collisionCenterPosition - D3DXVECTOR3(0.0f, downOffset, 0.0f);
        D3DXVECTOR3 hitPoint;
        D3DXVECTOR3 hitNormal;
        float hitDistance = 0.0f;
        return RayCastShapeObject(g_simpleObjects[i].mesh,
                                  g_simpleObjects[i].transform,
                                  sweepStart,
                                  sweepEnd,
                                  shapeType,
                                  radius,
                                  height,
                                  &hitPoint,
                                  &hitNormal,
                                  &hitDistance);
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
