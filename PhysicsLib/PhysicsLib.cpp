#include "PhysicsLib.h"

#include <d3d9.h>

#include "PhysicsLibInternal.h"

#include <limits>
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

LPDIRECT3D9 g_direct3d = NULL;
LPDIRECT3DDEVICE9 g_device = NULL;
bool g_initialized = false;

struct SimpleObject
{
    int id = 0;
    PhysicsLib::ObjectType objectType = PhysicsLib::ObjectType::Slide;
    LPD3DXMESH mesh = NULL;
    PhysicsLib::Transform transform;
};

std::vector<SimpleObject> g_simpleObjects;
int g_simpleNextId = 1;
bool g_doubleJumpEnabled = false;
bool g_infiniteJumpEnabled = true;
bool g_gravityEnabled = true;
bool g_inertiaEnabled = false;
bool g_contactEnabled = true;
bool g_surfaceContactEnabled = true;

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
bool PhysicsLib::RayCastObject(LPD3DXMESH mesh,
                               const Transform& transform,
                               const D3DXVECTOR3& rayOriginWorld,
                               const D3DXVECTOR3& rayEndWorld,
                               D3DXVECTOR3* outPoint,
                               D3DXVECTOR3* outSurfaceNormal,
                               float* outDistance)
{
    if (mesh == NULL)
    {
        return false;
    }

    D3DXMATRIX worldMatrix = BuildWorldMatrix(transform);
    D3DXMATRIX inverseWorldMatrix;
    D3DXMatrixInverse(&inverseWorldMatrix, NULL, &worldMatrix);

    D3DXVECTOR3 originLocal;
    D3DXVECTOR3 endLocal;
    D3DXVec3TransformCoord(&originLocal, &rayOriginWorld, &inverseWorldMatrix);
    D3DXVec3TransformCoord(&endLocal, &rayEndWorld, &inverseWorldMatrix);

    D3DXVECTOR3 rayVectorLocal = endLocal - originLocal;
    const float maxDistanceLocal = D3DXVec3Length(&rayVectorLocal);
    if (maxDistanceLocal <= 0.0001f)
    {
        return false;
    }

    D3DXVECTOR3 rayDirectionLocal = rayVectorLocal / maxDistanceLocal;

    BOOL hit = FALSE;
    DWORD faceIndex = 0;
    FLOAT barycentricU = 0.0f;
    FLOAT barycentricV = 0.0f;
    FLOAT distanceLocal = 0.0f;
    HRESULT result = D3DXIntersect(mesh,
                                   &originLocal,
                                   &rayDirectionLocal,
                                   &hit,
                                   &faceIndex,
                                   &barycentricU,
                                   &barycentricV,
                                   &distanceLocal,
                                   NULL,
                                   NULL);

    if (FAILED(result) || !hit || distanceLocal > maxDistanceLocal)
    {
        return false;
    }

    D3DXVECTOR3 localHitPoint = originLocal + rayDirectionLocal * distanceLocal;
    D3DXVECTOR3 worldHitPoint;
    D3DXVec3TransformCoord(&worldHitPoint, &localHitPoint, &worldMatrix);

    D3DXVECTOR3 localNormal;
    if (!ExtractFaceNormal(mesh, faceIndex, &localNormal))
    {
        return false;
    }

    D3DXMATRIX inverseTransposeWorld;
    D3DXMatrixTranspose(&inverseTransposeWorld, &inverseWorldMatrix);
    D3DXVECTOR3 surfaceNormal;
    D3DXVec3TransformNormal(&surfaceNormal, &localNormal, &inverseTransposeWorld);
    D3DXVec3Normalize(&surfaceNormal, &surfaceNormal);

    D3DXVECTOR3 worldHitOffset = worldHitPoint - rayOriginWorld;

    if (outPoint != nullptr)
    {
        *outPoint = worldHitPoint;
    }
    if (outSurfaceNormal != nullptr)
    {
        *outSurfaceNormal = surfaceNormal;
    }
    if (outDistance != nullptr)
    {
        *outDistance = D3DXVec3Length(&worldHitOffset);
    }
    return true;
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

bool SettingsState::IsInertiaEnabled()
{
    return g_inertiaEnabled;
}

void SettingsState::SetInertiaEnabled(bool enabled)
{
    g_inertiaEnabled = enabled;
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
                              float height)
{
    UNREFERENCED_PARAMETER(shapeType);
    UNREFERENCED_PARAMETER(radius);
    UNREFERENCED_PARAMETER(height);

    D3DXVECTOR3 nextPosition = currentPosition + moveVector * kDeltaSeconds;
    D3DXVECTOR3 nextMoveVector = moveVector;
    bool collided = false;

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
            bool foundHit = false;
            float nearestDistance = std::numeric_limits<float>::max();
            for (size_t i = 0; i < g_simpleObjects.size(); ++i)
            {
                if (g_simpleObjects[i].objectType == ObjectType::PassThrough || g_simpleObjects[i].mesh == NULL)
                {
                    continue;
                }

                D3DXVECTOR3 hitPoint;
                D3DXVECTOR3 surfaceNormal;
                float hitDistance = 0.0f;
                if (RayCastObject(g_simpleObjects[i].mesh,
                                  g_simpleObjects[i].transform,
                                  currentPosition,
                                  nextPosition,
                                  &hitPoint,
                                  &surfaceNormal,
                                  &hitDistance) &&
                    hitDistance < nearestDistance)
                {
                    const float normalMove = D3DXVec3Dot(&frameMove, &surfaceNormal);
                    if (normalMove > 0.0f)
                    {
                        continue;
                    }

                    foundHit = true;
                    nearestDistance = hitDistance;
                    nearestPoint = hitPoint;
                }
            }

            if (foundHit)
            {
                nextPosition = nearestPoint;
                nextMoveVector = D3DXVECTOR3(0.0f, 0.0f, 0.0f);
                collided = true;
            }
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

}
