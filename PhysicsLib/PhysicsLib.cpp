#include "PhysicsLib.h"

#include <d3d9.h>

#include <algorithm>
#include <limits>
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
constexpr float kGravityPerFrame = 9.8f / 3600.0f;

struct Aabb
{
    D3DXVECTOR3 min;
    D3DXVECTOR3 max;
};

struct LoadedObject
{
    int id = 0;
    PhysicsLib::ObjectType objectType = PhysicsLib::ObjectType::Slide;
    float friction = 0.0f;
    D3DXVECTOR3 localMin = D3DXVECTOR3(0.0f, 0.0f, 0.0f);
    D3DXVECTOR3 localMax = D3DXVECTOR3(0.0f, 0.0f, 0.0f);
    PhysicsLib::Transform transform;
};

LPDIRECT3D9 g_direct3d = NULL;
LPDIRECT3DDEVICE9 g_device = NULL;
std::vector<LoadedObject> g_objects;
int g_nextId = 1;
bool g_initialized = false;

void SafeRelease(IUnknown* object)
{
    if (object != NULL)
    {
        object->Release();
    }
}

void EnsureInitialized()
{
    if (!g_initialized)
    {
        throw std::runtime_error("PhysicsLib is not initialized.");
    }
}

LoadedObject& FindObject(int id)
{
    for (size_t i = 0; i < g_objects.size(); ++i)
    {
        if (g_objects[i].id == id)
        {
            return g_objects[i];
        }
    }

    throw std::out_of_range("Invalid collision object id.");
}

const LoadedObject& FindObject(int id, const std::vector<LoadedObject>& objects)
{
    for (size_t i = 0; i < objects.size(); ++i)
    {
        if (objects[i].id == id)
        {
            return objects[i];
        }
    }

    throw std::out_of_range("Invalid collision object id.");
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

Aabb BuildWorldAabb(const LoadedObject& object)
{
    const D3DXVECTOR3 corners[8] =
    {
        D3DXVECTOR3(object.localMin.x, object.localMin.y, object.localMin.z),
        D3DXVECTOR3(object.localMax.x, object.localMin.y, object.localMin.z),
        D3DXVECTOR3(object.localMin.x, object.localMax.y, object.localMin.z),
        D3DXVECTOR3(object.localMax.x, object.localMax.y, object.localMin.z),
        D3DXVECTOR3(object.localMin.x, object.localMin.y, object.localMax.z),
        D3DXVECTOR3(object.localMax.x, object.localMin.y, object.localMax.z),
        D3DXVECTOR3(object.localMin.x, object.localMax.y, object.localMax.z),
        D3DXVECTOR3(object.localMax.x, object.localMax.y, object.localMax.z),
    };

    D3DXMATRIX worldMatrix = BuildWorldMatrix(object.transform);
    Aabb worldAabb;
    worldAabb.min = D3DXVECTOR3(std::numeric_limits<float>::max(),
                                std::numeric_limits<float>::max(),
                                std::numeric_limits<float>::max());
    worldAabb.max = D3DXVECTOR3(-std::numeric_limits<float>::max(),
                                -std::numeric_limits<float>::max(),
                                -std::numeric_limits<float>::max());

    for (size_t i = 0; i < 8; ++i)
    {
        D3DXVECTOR4 transformed;
        D3DXVec3Transform(&transformed, &corners[i], &worldMatrix);

        worldAabb.min.x = std::min(worldAabb.min.x, transformed.x);
        worldAabb.min.y = std::min(worldAabb.min.y, transformed.y);
        worldAabb.min.z = std::min(worldAabb.min.z, transformed.z);
        worldAabb.max.x = std::max(worldAabb.max.x, transformed.x);
        worldAabb.max.y = std::max(worldAabb.max.y, transformed.y);
        worldAabb.max.z = std::max(worldAabb.max.z, transformed.z);
    }

    return worldAabb;
}

Aabb BuildActorAabb(const D3DXVECTOR3& position,
                    PhysicsLib::ShapeType shapeType,
                    float radius,
                    float height)
{
    Aabb actor;
    actor.min = position;
    actor.max = position;

    switch (shapeType)
    {
    case PhysicsLib::ShapeType::Point:
        break;

    case PhysicsLib::ShapeType::Sphere:
        actor.min = position - D3DXVECTOR3(radius, radius, radius);
        actor.max = position + D3DXVECTOR3(radius, radius, radius);
        break;

    case PhysicsLib::ShapeType::Cylinder:
    {
        const float halfHeight = height * 0.5f;
        actor.min = position - D3DXVECTOR3(radius, halfHeight, radius);
        actor.max = position + D3DXVECTOR3(radius, halfHeight, radius);
        break;
    }
    }

    return actor;
}

bool OverlapAabb(const Aabb& lhs, const Aabb& rhs)
{
    return lhs.min.x <= rhs.max.x && lhs.max.x >= rhs.min.x &&
           lhs.min.y <= rhs.max.y && lhs.max.y >= rhs.min.y &&
           lhs.min.z <= rhs.max.z && lhs.max.z >= rhs.min.z;
}

void AppendUnique(std::vector<int>* values, int id)
{
    if (values == nullptr)
    {
        return;
    }

    if (std::find(values->begin(), values->end(), id) == values->end())
    {
        values->push_back(id);
    }
}

bool ResolveSolidCollision(const LoadedObject& object,
                           PhysicsLib::ShapeType shapeType,
                           float radius,
                           float height,
                           D3DXVECTOR3* position)
{
    Aabb solid = BuildWorldAabb(object);
    Aabb actor = BuildActorAabb(*position, shapeType, radius, height);

    if (!OverlapAabb(actor, solid))
    {
        return false;
    }

    const float overlapX = std::min(actor.max.x, solid.max.x) - std::max(actor.min.x, solid.min.x);
    const float overlapY = std::min(actor.max.y, solid.max.y) - std::max(actor.min.y, solid.min.y);
    const float overlapZ = std::min(actor.max.z, solid.max.z) - std::max(actor.min.z, solid.min.z);

    const D3DXVECTOR3 actorCenter = (actor.min + actor.max) * 0.5f;
    const D3DXVECTOR3 solidCenter = (solid.min + solid.max) * 0.5f;

    if (overlapX <= overlapY && overlapX <= overlapZ)
    {
        position->x += (actorCenter.x < solidCenter.x) ? -overlapX : overlapX;
    }
    else if (overlapY <= overlapX && overlapY <= overlapZ)
    {
        position->y += (actorCenter.y < solidCenter.y) ? -overlapY : overlapY;
    }
    else
    {
        position->z += (actorCenter.z < solidCenter.z) ? -overlapZ : overlapZ;
    }

    return true;
}

void LoadMeshBounds(const TCHAR* modelPath, D3DXVECTOR3* outMin, D3DXVECTOR3* outMax)
{
    LPD3DXMESH mesh = NULL;
    LPD3DXBUFFER materialBuffer = NULL;
    DWORD materialCount = 0;

    HRESULT result = D3DXLoadMeshFromX(modelPath,
                                       D3DXMESH_SYSTEMMEM,
                                       g_device,
                                       NULL,
                                       &materialBuffer,
                                       NULL,
                                       &materialCount,
                                       &mesh);

    if (FAILED(result))
    {
        throw std::runtime_error("Failed to load collision mesh.");
    }

    void* vertexData = NULL;
    result = mesh->LockVertexBuffer(D3DLOCK_READONLY, &vertexData);
    if (FAILED(result))
    {
        SafeRelease(materialBuffer);
        SafeRelease(mesh);
        throw std::runtime_error("Failed to read collision mesh vertex buffer.");
    }

    result = D3DXComputeBoundingBox(static_cast<const D3DXVECTOR3*>(vertexData),
                                    mesh->GetNumVertices(),
                                    mesh->GetNumBytesPerVertex(),
                                    outMin,
                                    outMax);

    mesh->UnlockVertexBuffer();
    SafeRelease(materialBuffer);
    SafeRelease(mesh);

    if (FAILED(result))
    {
        throw std::runtime_error("Failed to compute collision mesh bounds.");
    }
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
    presentParameters.EnableAutoDepthStencil = FALSE;
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

    g_objects.clear();
    g_nextId = 1;
    g_initialized = true;
}

void PhysicsLib::Finalize()
{
    g_objects.clear();
    g_nextId = 1;
    g_initialized = false;

    SafeRelease(g_device);
    SafeRelease(g_direct3d);
    g_device = NULL;
    g_direct3d = NULL;
}

void PhysicsLib::Update(float deltaSeconds)
{
    EnsureInitialized();

    for (size_t i = 0; i < g_objects.size(); ++i)
    {
        if (g_objects[i].objectType == ObjectType::MovingSlide)
        {
            g_objects[i].transform.position += g_objects[i].transform.velocity * deltaSeconds;
        }
    }
}

int PhysicsLib::Load(const TCHAR* modelPath, ObjectType objectType, float friction)
{
    EnsureInitialized();

    if (modelPath == nullptr || modelPath[0] == _T('\0'))
    {
        throw std::invalid_argument("modelPath must not be empty.");
    }

    if (friction < 0.0f || friction > 1.0f)
    {
        throw std::out_of_range("friction must be within 0.0f to 1.0f.");
    }

    LoadedObject object;
    object.id = g_nextId++;
    object.objectType = objectType;
    object.friction = friction;

    LoadMeshBounds(modelPath, &object.localMin, &object.localMax);
    g_objects.push_back(object);
    return object.id;
}

void PhysicsLib::SetTransform(int id,
                              const D3DXVECTOR3& position,
                              const D3DXVECTOR3& rotation,
                              const D3DXVECTOR3& scale)
{
    EnsureInitialized();

    LoadedObject& object = FindObject(id);
    object.transform.position = position;
    object.transform.rotation = rotation;
    object.transform.scale = scale;
}

void PhysicsLib::SetVelocity(int id, const D3DXVECTOR3& velocity)
{
    EnsureInitialized();

    LoadedObject& object = FindObject(id);
    object.transform.velocity = velocity;
}

PhysicsLib::Transform PhysicsLib::GetTransform(int id)
{
    EnsureInitialized();
    return FindObject(id).transform;
}

bool PhysicsLib::CheckCollide(const D3DXVECTOR3& currentPosition,
                              const D3DXVECTOR3& moveVector,
                              ShapeType shapeType,
                              D3DXVECTOR3* outPosition,
                              std::vector<int>* outPassThroughIds,
                              std::vector<int>* outSolidIds,
                              float radius,
                              float height)
{
    EnsureInitialized();

    if (outPosition == nullptr)
    {
        throw std::invalid_argument("outPosition must not be null.");
    }

    if (shapeType == ShapeType::Sphere && radius < 0.0f)
    {
        throw std::out_of_range("Sphere radius must not be negative.");
    }

    if (shapeType == ShapeType::Cylinder && (radius < 0.0f || height < 0.0f))
    {
        throw std::out_of_range("Cylinder radius and height must not be negative.");
    }

    if (outPassThroughIds != nullptr)
    {
        outPassThroughIds->clear();
    }

    if (outSolidIds != nullptr)
    {
        outSolidIds->clear();
    }

    D3DXVECTOR3 correctedMove = moveVector;
    correctedMove.y -= kGravityPerFrame;

    D3DXVECTOR3 nextPosition = currentPosition + correctedMove;

    for (size_t i = 0; i < g_objects.size(); ++i)
    {
        const Aabb objectAabb = BuildWorldAabb(g_objects[i]);
        const Aabb actorAabb = BuildActorAabb(nextPosition, shapeType, radius, height);

        if (g_objects[i].objectType == ObjectType::PassThrough)
        {
            if (OverlapAabb(actorAabb, objectAabb))
            {
                AppendUnique(outPassThroughIds, g_objects[i].id);
            }
        }
    }

    bool collided = false;

    for (size_t pass = 0; pass < 2; ++pass)
    {
        bool resolvedOnThisPass = false;

        for (size_t i = 0; i < g_objects.size(); ++i)
        {
            if (g_objects[i].objectType == ObjectType::PassThrough)
            {
                continue;
            }

            if (ResolveSolidCollision(g_objects[i], shapeType, radius, height, &nextPosition))
            {
                AppendUnique(outSolidIds, g_objects[i].id);
                collided = true;
                resolvedOnThisPass = true;
            }
        }

        if (!resolvedOnThisPass)
        {
            break;
        }
    }

    *outPosition = nextPosition;
    return collided;
}
}
