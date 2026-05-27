#include "PhysicsLib.h"

#include <d3d9.h>

#include "PhysicsLibInternal.h"

#include <cmath>
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

struct LoadedObject
{
    int id = 0;
    PhysicsLib::ObjectType objectType = PhysicsLib::ObjectType::Slide;
    LPD3DXMESH mesh = NULL;
    PhysicsLib::Transform transform;
};

struct RaycastHit
{
    bool hit = false;
    float distance = 0.0f;
    D3DXVECTOR3 point = D3DXVECTOR3(0.0f, 0.0f, 0.0f);
    D3DXVECTOR3 normal = D3DXVECTOR3(0.0f, 1.0f, 0.0f);
    D3DXVECTOR3 surfaceNormal = D3DXVECTOR3(0.0f, 1.0f, 0.0f);
    int objectId = -1;
    PhysicsLib::ObjectType objectType = PhysicsLib::ObjectType::Slide;
};

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

void SafeRelease(IUnknown* object)
{
    if (object != NULL)
    {
        object->Release();
    }
}

D3DXMATRIX BuildWorldMatrix(const PhysicsLib::Transform& transform)
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

bool ExtractFaceNormal(const LoadedObject& object, DWORD faceIndex, D3DXVECTOR3* outNormal)
{
    if (object.mesh == NULL)
    {
        return false;
    }

    void* vertexBuffer = NULL;
    void* indexBuffer = NULL;
    const DWORD stride = object.mesh->GetNumBytesPerVertex();

    HRESULT result = object.mesh->LockVertexBuffer(D3DLOCK_READONLY, &vertexBuffer);
    if (FAILED(result))
    {
        return false;
    }

    result = object.mesh->LockIndexBuffer(D3DLOCK_READONLY, &indexBuffer);
    if (FAILED(result))
    {
        object.mesh->UnlockVertexBuffer();
        return false;
    }

    const BYTE* vertices = static_cast<const BYTE*>(vertexBuffer);
    const DWORD* indices32 = static_cast<const DWORD*>(indexBuffer);
    const WORD* indices16 = static_cast<const WORD*>(indexBuffer);
    const bool use32BitIndices = (object.mesh->GetOptions() & D3DXMESH_32BIT) != 0;

    const DWORD i0 = use32BitIndices ? indices32[faceIndex * 3 + 0] : indices16[faceIndex * 3 + 0];
    const DWORD i1 = use32BitIndices ? indices32[faceIndex * 3 + 1] : indices16[faceIndex * 3 + 1];
    const DWORD i2 = use32BitIndices ? indices32[faceIndex * 3 + 2] : indices16[faceIndex * 3 + 2];

    const D3DXVECTOR3* p0 = reinterpret_cast<const D3DXVECTOR3*>(vertices + i0 * stride);
    const D3DXVECTOR3* p1 = reinterpret_cast<const D3DXVECTOR3*>(vertices + i1 * stride);
    const D3DXVECTOR3* p2 = reinterpret_cast<const D3DXVECTOR3*>(vertices + i2 * stride);

    D3DXVECTOR3 edge1 = *p1 - *p0;
    D3DXVECTOR3 edge2 = *p2 - *p0;
    D3DXVec3Cross(outNormal, &edge1, &edge2);
    D3DXVec3Normalize(outNormal, outNormal);

    object.mesh->UnlockIndexBuffer();
    object.mesh->UnlockVertexBuffer();
    return true;
}

bool RaycastObject(const LoadedObject& object,
                   const D3DXVECTOR3& rayOriginWorld,
                   const D3DXVECTOR3& rayEndWorld,
                   RaycastHit* outHit)
{
    if (object.mesh == NULL)
    {
        return false;
    }

    D3DXMATRIX worldMatrix = BuildWorldMatrix(object.transform);
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
    HRESULT result = D3DXIntersect(object.mesh,
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
    if (!ExtractFaceNormal(object, faceIndex, &localNormal))
    {
        return false;
    }

    D3DXMATRIX inverseTransposeWorld;
    D3DXMatrixTranspose(&inverseTransposeWorld, &inverseWorldMatrix);
    D3DXVECTOR3 worldNormal;
    D3DXVec3TransformNormal(&worldNormal, &localNormal, &inverseTransposeWorld);
    D3DXVec3Normalize(&worldNormal, &worldNormal);
    D3DXVECTOR3 surfaceNormal = worldNormal;

    const D3DXVECTOR3 rayVectorWorld = rayEndWorld - rayOriginWorld;
    D3DXVECTOR3 rayDirectionWorld = rayVectorWorld;
    D3DXVec3Normalize(&rayDirectionWorld, &rayDirectionWorld);
    const float normalDirection = D3DXVec3Dot(&worldNormal, &rayDirectionWorld);
    if (normalDirection > 0.0f)
    {
        worldNormal = -worldNormal;
    }
    else if (std::abs(normalDirection) <= 0.0001f &&
             std::abs(worldNormal.y) > 0.5f &&
             worldNormal.y < 0.0f)
    {
        worldNormal = -worldNormal;
    }

    D3DXVECTOR3 worldHitOffset = worldHitPoint - rayOriginWorld;

    outHit->hit = true;
    outHit->distance = D3DXVec3Length(&worldHitOffset);
    outHit->point = worldHitPoint;
    outHit->normal = worldNormal;
    outHit->surfaceNormal = surfaceNormal;
    outHit->objectId = object.id;
    outHit->objectType = object.objectType;
    return true;
}

void MoveHorizontalVelocityToward(D3DXVECTOR3* velocity,
                                  const D3DXVECTOR3& targetVelocity,
                                  float acceleration)
{
    if (velocity == nullptr || acceleration <= 0.0f)
    {
        return;
    }

    D3DXVECTOR3 currentVelocity(velocity->x, 0.0f, velocity->z);
    D3DXVECTOR3 difference = targetVelocity - currentVelocity;
    const float differenceLength = D3DXVec3Length(&difference);
    if (differenceLength <= 0.0001f)
    {
        velocity->x = targetVelocity.x;
        velocity->z = targetVelocity.z;
        return;
    }

    const float maxDelta = acceleration * kDeltaSeconds;
    if (differenceLength <= maxDelta)
    {
        velocity->x = targetVelocity.x;
        velocity->z = targetVelocity.z;
        return;
    }

    difference /= differenceLength;
    difference *= maxDelta;
    velocity->x += difference.x;
    velocity->z += difference.z;
}

void LoadMesh(const TCHAR* modelPath, LPD3DXMESH* outMesh)
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

}


bool IsDoubleJumpEnabled()
{
    return g_doubleJumpEnabled;
}

void SetDoubleJumpEnabled(bool enabled)
{
    g_doubleJumpEnabled = enabled;
}

bool IsInfiniteJumpEnabled()
{
    return g_infiniteJumpEnabled;
}

void SetInfiniteJumpEnabled(bool enabled)
{
    g_infiniteJumpEnabled = enabled;
}

bool IsGravityEnabled()
{
    return g_gravityEnabled;
}

void SetGravityEnabled(bool enabled)
{
    g_gravityEnabled = enabled;
}

bool IsInertiaEnabled()
{
    return g_inertiaEnabled;
}

void SetInertiaEnabled(bool enabled)
{
    g_inertiaEnabled = enabled;
}

bool IsContactEnabled()
{
    return g_contactEnabled;
}

void SetContactEnabled(bool enabled)
{
    g_contactEnabled = enabled;
}

bool IsSurfaceContactEnabled()
{
    return g_surfaceContactEnabled;
}

void SetSurfaceContactEnabled(bool enabled)
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
    DestroySettingsDialog();
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

    if (IsSurfaceContactEnabled())
    {
        const D3DXVECTOR3 frameMove = nextPosition - currentPosition;
        const float frameMoveLength = D3DXVec3Length(&frameMove);
        if (frameMoveLength > 0.0001f)
        {
            RaycastHit nearestHit;
            bool foundHit = false;
            float nearestDistance = std::numeric_limits<float>::max();
            for (size_t i = 0; i < g_simpleObjects.size(); ++i)
            {
                if (g_simpleObjects[i].objectType == ObjectType::PassThrough || g_simpleObjects[i].mesh == NULL)
                {
                    continue;
                }

                LoadedObject object;
                object.id = g_simpleObjects[i].id;
                object.objectType = PhysicsLib::ObjectType::Slide;
                object.mesh = g_simpleObjects[i].mesh;
                object.transform.position = g_simpleObjects[i].transform.position;
                object.transform.rotation = g_simpleObjects[i].transform.rotation;
                object.transform.scale = g_simpleObjects[i].transform.scale;
                object.transform.velocity = g_simpleObjects[i].transform.velocity;

                RaycastHit hit;
                if (RaycastObject(object, currentPosition, nextPosition, &hit) &&
                    hit.distance < nearestDistance)
                {
                    const float normalMove = D3DXVec3Dot(&frameMove, &hit.surfaceNormal);
                    if (normalMove > 0.0f)
                    {
                        continue;
                    }

                    foundHit = true;
                    nearestDistance = hit.distance;
                    nearestHit = hit;
                }
            }

            if (foundHit)
            {
                nextPosition = nearestHit.point;
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
    if (!IsContactEnabled() || distance < 0.0f)
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

CameraMover::CameraMover()
{
}

D3DXVECTOR3 CameraMover::ResolvePosition(const D3DXVECTOR3& targetPosition,
                                         const D3DXVECTOR3& desiredCameraPosition) const
{
    UNREFERENCED_PARAMETER(targetPosition);
    return desiredCameraPosition;
}

CharacterMover::CharacterMover()
    : m_position(0.0f, 0.0f, 0.0f),
      m_velocity(0.0f, 0.0f, 0.0f),
      m_isGrounded(true),
      m_isTouchingWall(false),
      m_supportObjectId(-1),
      m_remainingAirJumps(0)
{
}

CharacterMover::CharacterMover(const D3DXVECTOR3& position)
    : m_position(position),
      m_velocity(0.0f, 0.0f, 0.0f),
      m_isGrounded(true),
      m_isTouchingWall(false),
      m_supportObjectId(-1),
      m_remainingAirJumps(0)
{
}

void CharacterMover::SetSettings(const Settings& settings)
{
    if (settings.radius < 0.0f)
    {
        throw std::out_of_range("CharacterMover radius must not be negative.");
    }

    if (settings.height < 0.0f)
    {
        throw std::out_of_range("CharacterMover height must not be negative.");
    }

    if (settings.groundDamping < 0.0f || settings.airDamping < 0.0f)
    {
        throw std::out_of_range("CharacterMover damping must not be negative.");
    }

    if (settings.groundAcceleration < 0.0f || settings.airAcceleration < 0.0f)
    {
        throw std::out_of_range("CharacterMover acceleration must not be negative.");
    }

    m_settings = settings;
}

CharacterMover::Settings CharacterMover::GetSettings() const
{
    return m_settings;
}

void CharacterMover::Reset(const D3DXVECTOR3& position)
{
    m_position = position;
    m_velocity = D3DXVECTOR3(0.0f, 0.0f, 0.0f);
    m_isGrounded = true;
    m_isTouchingWall = false;
    m_supportObjectId = -1;
    m_remainingAirJumps = 1;
    m_debugInfo = DebugInfo();
}

void CharacterMover::SetPosition(const D3DXVECTOR3& position)
{
    m_position = position;
}

D3DXVECTOR3 CharacterMover::GetPosition() const
{
    return m_position;
}

D3DXVECTOR3 CharacterMover::GetVelocity() const
{
    return m_velocity;
}

bool CharacterMover::IsGrounded() const
{
    return m_isGrounded;
}

bool CharacterMover::IsTouchingWall() const
{
    return m_isTouchingWall;
}

int CharacterMover::GetSupportObjectId() const
{
    return m_supportObjectId;
}

CharacterMover::DebugInfo CharacterMover::GetDebugInfo() const
{
    return m_debugInfo;
}

bool CharacterMover::Update(const D3DXVECTOR3& inputDirection,
                            bool jump,
                            std::vector<int>* outPassThroughIds,
                            std::vector<int>* outSolidIds)
{
    if (outPassThroughIds != nullptr)
    {
        outPassThroughIds->clear();
    }
    if (outSolidIds != nullptr)
    {
        outSolidIds->clear();
    }

    D3DXVECTOR3 inputMove(inputDirection.x, 0.0f, inputDirection.z);
    if (D3DXVec3Length(&inputMove) > 0.0001f)
    {
        D3DXVec3Normalize(&inputMove, &inputMove);
        inputMove *= m_settings.moveSpeed;
    }

    if (IsInertiaEnabled())
    {
        if (D3DXVec3Length(&inputMove) > 0.0001f)
        {
            MoveHorizontalVelocityToward(&m_velocity, inputMove, m_settings.groundAcceleration);
        }
    }
    else
    {
        m_velocity.x = inputMove.x;
        m_velocity.z = inputMove.z;
    }

    const bool isGroundJump = jump && m_isGrounded;
    bool canJump = false;
    if (jump && m_isGrounded)
    {
        canJump = true;
    }
    else if (jump && IsInfiniteJumpEnabled())
    {
        canJump = true;
    }
    else if (jump && IsDoubleJumpEnabled() && m_remainingAirJumps > 0)
    {
        canJump = true;
        --m_remainingAirJumps;
    }

    if (canJump)
    {
        if (isGroundJump)
        {
            m_remainingAirJumps = 1;
        }

        m_velocity.y = m_settings.jumpVelocity;
        m_isGrounded = false;
    }

    if (IsGravityEnabled())
    {
        m_velocity.y -= 9.8f * kDeltaSeconds;
    }
    else
    {
        m_velocity.y = 0.0f;
    }
    D3DXVECTOR3 nextPosition = m_position;
    D3DXVECTOR3 nextVelocity = m_velocity;
    const D3DXVECTOR3 attemptedVelocity = m_velocity;
    const bool collided = PhysicsLib::CheckCollide(m_position,
                                                   m_velocity,
                                                   m_settings.shapeType,
                                                   &nextPosition,
                                                   &nextVelocity,
                                                   outPassThroughIds,
                                                   outSolidIds,
                                                   m_settings.radius,
                                                   m_settings.height);
    m_position = nextPosition;
    m_velocity = nextVelocity;
    if (IsGravityEnabled())
    {
        m_isGrounded = false;
    }
    if (collided && attemptedVelocity.y <= 0.0f)
    {
        m_isGrounded = true;
        m_remainingAirJumps = 1;
    }
    m_isTouchingWall = false;
    m_supportObjectId = -1;
    m_debugInfo = DebugInfo();
    if (collided)
    {
        m_debugInfo.collideCheckCount = 1;
        m_debugInfo.hitCount = 1;
    }
    return collided;
}
}
