#include "PhysicsLib.h"

#include <d3d9.h>

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#if defined(_OPENMP)
#include <omp.h>
#endif

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
constexpr float kGravityPerFrame = 9.8f * kDeltaSeconds;
constexpr float kHorizontalDamping = 0.5f;
constexpr float kSkinWidth = 0.01f;
constexpr int kMaxSlideIterations = 3;

struct LoadedObject
{
    int id = 0;
    PhysicsLib::ObjectType objectType = PhysicsLib::ObjectType::Slide;
    float friction = 0.0f;
    LPD3DXMESH mesh = NULL;
    PhysicsLib::Transform transform;
};

struct RaycastHit
{
    bool hit = false;
    float distance = 0.0f;
    D3DXVECTOR3 point = D3DXVECTOR3(0.0f, 0.0f, 0.0f);
    D3DXVECTOR3 normal = D3DXVECTOR3(0.0f, 1.0f, 0.0f);
    int objectId = -1;
    PhysicsLib::ObjectType objectType = PhysicsLib::ObjectType::Slide;
};

LPDIRECT3D9 g_direct3d = NULL;
LPDIRECT3DDEVICE9 g_device = NULL;
std::vector<LoadedObject> g_objects;
int g_nextId = 1;
bool g_initialized = false;
bool g_intersectMultithreadEnabled = true;

struct HitCollection
{
    std::unordered_set<int> passThroughIds;
    std::unordered_set<int> solidIds;
    bool foundSolid = false;
    float nearestDistance = std::numeric_limits<float>::max();
    RaycastHit nearestHit;
};

void SafeRelease(IUnknown* object)
{
    if (object != NULL)
    {
        object->Release();
    }
}

void ReleaseLoadedObjects()
{
    for (size_t i = 0; i < g_objects.size(); ++i)
    {
        SafeRelease(g_objects[i].mesh);
        g_objects[i].mesh = NULL;
    }

    g_objects.clear();
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

void GetShapeOffsets(PhysicsLib::ShapeType shapeType,
                     float radius,
                     float height,
                     std::vector<D3DXVECTOR3>* offsets)
{
    offsets->clear();
    offsets->push_back(D3DXVECTOR3(0.0f, 0.0f, 0.0f));

    if (shapeType == PhysicsLib::ShapeType::Sphere)
    {
        offsets->push_back(D3DXVECTOR3(radius, 0.0f, 0.0f));
        offsets->push_back(D3DXVECTOR3(-radius, 0.0f, 0.0f));
        offsets->push_back(D3DXVECTOR3(0.0f, radius, 0.0f));
        offsets->push_back(D3DXVECTOR3(0.0f, -radius, 0.0f));
        offsets->push_back(D3DXVECTOR3(0.0f, 0.0f, radius));
        offsets->push_back(D3DXVECTOR3(0.0f, 0.0f, -radius));
    }
    else if (shapeType == PhysicsLib::ShapeType::Cylinder)
    {
        const float halfHeight = height * 0.5f;
        offsets->push_back(D3DXVECTOR3(0.0f, halfHeight, 0.0f));
        offsets->push_back(D3DXVECTOR3(0.0f, -halfHeight, 0.0f));
        offsets->push_back(D3DXVECTOR3(radius, halfHeight, 0.0f));
        offsets->push_back(D3DXVECTOR3(-radius, halfHeight, 0.0f));
        offsets->push_back(D3DXVECTOR3(0.0f, halfHeight, radius));
        offsets->push_back(D3DXVECTOR3(0.0f, halfHeight, -radius));
        offsets->push_back(D3DXVECTOR3(radius, -halfHeight, 0.0f));
        offsets->push_back(D3DXVECTOR3(-radius, -halfHeight, 0.0f));
        offsets->push_back(D3DXVECTOR3(0.0f, -halfHeight, radius));
        offsets->push_back(D3DXVECTOR3(0.0f, -halfHeight, -radius));
    }
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

    D3DXVECTOR3 worldHitOffset = worldHitPoint - rayOriginWorld;

    outHit->hit = true;
    outHit->distance = D3DXVec3Length(&worldHitOffset);
    outHit->point = worldHitPoint;
    outHit->normal = worldNormal;
    outHit->objectId = object.id;
    outHit->objectType = object.objectType;
    return true;
}

void AccumulateRaycast(const LoadedObject& object,
                       const D3DXVECTOR3& rayStart,
                       const D3DXVECTOR3& rayEnd,
                       HitCollection* inOutCollection)
{
    RaycastHit hit;
    if (!RaycastObject(object, rayStart, rayEnd, &hit))
    {
        return;
    }

    if (hit.objectType == PhysicsLib::ObjectType::PassThrough)
    {
        inOutCollection->passThroughIds.insert(hit.objectId);
        return;
    }

    inOutCollection->solidIds.insert(hit.objectId);

    if (!inOutCollection->foundSolid || hit.distance < inOutCollection->nearestDistance)
    {
        inOutCollection->foundSolid = true;
        inOutCollection->nearestDistance = hit.distance;
        inOutCollection->nearestHit = hit;
    }
}

void AccumulateObjectHits(const LoadedObject& object,
                          const D3DXVECTOR3& startPosition,
                          const D3DXVECTOR3& endPosition,
                          const std::vector<D3DXVECTOR3>& offsets,
                          HitCollection* inOutCollection)
{
    for (size_t offsetIndex = 0; offsetIndex < offsets.size(); ++offsetIndex)
    {
        const D3DXVECTOR3 rayStart = startPosition + offsets[offsetIndex];
        const D3DXVECTOR3 rayEnd = endPosition + offsets[offsetIndex];
        AccumulateRaycast(object, rayStart, rayEnd, inOutCollection);
    }
}

void MergeHitCollection(const HitCollection& source,
                        std::vector<int>* outPassThroughIds,
                        std::vector<int>* outSolidIds,
                        bool* inOutFoundSolid,
                        float* inOutNearestDistance,
                        RaycastHit* outNearestSolidHit)
{
    if (outPassThroughIds != nullptr)
    {
        for (std::unordered_set<int>::const_iterator it = source.passThroughIds.begin();
             it != source.passThroughIds.end();
             ++it)
        {
            if (std::find(outPassThroughIds->begin(), outPassThroughIds->end(), *it) == outPassThroughIds->end())
            {
                outPassThroughIds->push_back(*it);
            }
        }
    }

    if (outSolidIds != nullptr)
    {
        for (std::unordered_set<int>::const_iterator it = source.solidIds.begin();
             it != source.solidIds.end();
             ++it)
        {
            if (std::find(outSolidIds->begin(), outSolidIds->end(), *it) == outSolidIds->end())
            {
                outSolidIds->push_back(*it);
            }
        }
    }

    if (source.foundSolid && source.nearestDistance < *inOutNearestDistance)
    {
        *inOutNearestDistance = source.nearestDistance;
        *outNearestSolidHit = source.nearestHit;
        *inOutFoundSolid = true;
    }
}

bool FindNearestHit(const D3DXVECTOR3& startPosition,
                    const D3DXVECTOR3& moveVector,
                    PhysicsLib::ShapeType shapeType,
                    float radius,
                    float height,
                    std::vector<int>* outPassThroughIds,
                    std::vector<int>* outSolidIds,
                    RaycastHit* outNearestSolidHit)
{
    std::vector<D3DXVECTOR3> offsets;
    GetShapeOffsets(shapeType, radius, height, &offsets);

    const D3DXVECTOR3 endPosition = startPosition + moveVector;
    bool foundSolid = false;
    float nearestDistance = std::numeric_limits<float>::max();
    std::unordered_map<LPD3DXMESH, std::vector<size_t> > meshGroups;
    std::vector<LPD3DXMESH> meshOrder;
    meshGroups.reserve(g_objects.size());
    meshOrder.reserve(g_objects.size());

    for (size_t objectIndex = 0; objectIndex < g_objects.size(); ++objectIndex)
    {
        const LPD3DXMESH mesh = g_objects[objectIndex].mesh;
        if (meshGroups.find(mesh) == meshGroups.end())
        {
            meshOrder.push_back(mesh);
        }

        meshGroups[mesh].push_back(objectIndex);
    }

#if defined(_OPENMP)
    if (g_intersectMultithreadEnabled && meshOrder.size() > 1)
    {
#pragma omp parallel for schedule(static)
        for (int meshGroupIndex = 0; meshGroupIndex < static_cast<int>(meshOrder.size()); ++meshGroupIndex)
        {
            HitCollection localCollection;
            const std::vector<size_t>& objectIndices = meshGroups[meshOrder[meshGroupIndex]];
            for (size_t objectListIndex = 0; objectListIndex < objectIndices.size(); ++objectListIndex)
            {
                const LoadedObject& object = g_objects[objectIndices[objectListIndex]];
                AccumulateObjectHits(object, startPosition, endPosition, offsets, &localCollection);
            }

#pragma omp critical(FindNearestHitMerge)
            {
                MergeHitCollection(localCollection,
                                   outPassThroughIds,
                                   outSolidIds,
                                   &foundSolid,
                                   &nearestDistance,
                                   outNearestSolidHit);
            }
        }

        return foundSolid;
    }
#endif

    for (size_t meshGroupIndex = 0; meshGroupIndex < meshOrder.size(); ++meshGroupIndex)
    {
        HitCollection localCollection;
        const std::vector<size_t>& objectIndices = meshGroups[meshOrder[meshGroupIndex]];
        for (size_t objectListIndex = 0; objectListIndex < objectIndices.size(); ++objectListIndex)
        {
            const LoadedObject& object = g_objects[objectIndices[objectListIndex]];
            AccumulateObjectHits(object, startPosition, endPosition, offsets, &localCollection);
        }

        MergeHitCollection(localCollection,
                           outPassThroughIds,
                           outSolidIds,
                           &foundSolid,
                           &nearestDistance,
                           outNearestSolidHit);
    }

    return foundSolid;
}

D3DXVECTOR3 ResolveSlide(const D3DXVECTOR3& moveVector, const D3DXVECTOR3& normal)
{
    const float projection = D3DXVec3Dot(&moveVector, &normal);
    D3DXVECTOR3 slideVector = moveVector;
    if (projection < 0.0f)
    {
        slideVector -= normal * projection;
    }

    return slideVector;
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

    ReleaseLoadedObjects();
    g_nextId = 1;
    g_initialized = true;
}

void PhysicsLib::Finalize()
{
    ReleaseLoadedObjects();
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

void PhysicsLib::SetIntersectMultithreadEnabled(bool enabled)
{
    g_intersectMultithreadEnabled = enabled;
}

bool PhysicsLib::IsIntersectMultithreadEnabled()
{
    return g_intersectMultithreadEnabled;
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

    LoadMesh(modelPath, &object.mesh);
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
                              D3DXVECTOR3* outNextMoveVector,
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

    if (outNextMoveVector == nullptr)
    {
        throw std::invalid_argument("outNextMoveVector must not be null.");
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

    D3DXVECTOR3 nextMoveVector = moveVector;
    nextMoveVector.y -= kGravityPerFrame;

    D3DXVECTOR3 frameMove = nextMoveVector * kDeltaSeconds;
    D3DXVECTOR3 nextPosition = currentPosition + frameMove;
    bool collided = false;

    D3DXVECTOR3 currentPositionForSlide = currentPosition;
    D3DXVECTOR3 remainingMove = frameMove;

    for (int iteration = 0; iteration < kMaxSlideIterations; ++iteration)
    {
        const float totalMoveLength = D3DXVec3Length(&remainingMove);
        if (totalMoveLength <= 0.0001f)
        {
            break;
        }

        RaycastHit nearestHit;
        if (!FindNearestHit(currentPositionForSlide,
                            remainingMove,
                            shapeType,
                            radius,
                            height,
                            outPassThroughIds,
                            outSolidIds,
                            &nearestHit))
        {
            nextPosition = currentPositionForSlide + remainingMove;
            remainingMove = D3DXVECTOR3(0.0f, 0.0f, 0.0f);
            break;
        }

        collided = true;

        const D3DXVECTOR3 moveDirection = remainingMove / totalMoveLength;
        const float safeDistance = std::max(0.0f, nearestHit.distance - kSkinWidth);
        currentPositionForSlide += moveDirection * safeDistance;
        nextPosition = currentPositionForSlide;

        D3DXVECTOR3 unresolvedMove = remainingMove - moveDirection * nearestHit.distance;
        D3DXVECTOR3 slideMove = ResolveSlide(unresolvedMove, nearestHit.normal);

        if (nearestHit.normal.y > 0.5f && nextMoveVector.y < 0.0f)
        {
            nextMoveVector.y = 0.0f;
            slideMove.y = 0.0f;
        }

        if (iteration == kMaxSlideIterations - 1)
        {
            remainingMove = D3DXVECTOR3(0.0f, 0.0f, 0.0f);
            break;
        }

        remainingMove = slideMove;
    }

    if (D3DXVec3Length(&remainingMove) > 0.0001f)
    {
        nextPosition = currentPositionForSlide + remainingMove;
    }

    if (D3DXVec3Length(&frameMove) > 0.0001f)
    {
        D3DXVECTOR3 actualFrameMove = nextPosition - currentPosition;
        nextMoveVector.x = actualFrameMove.x / kDeltaSeconds;
        nextMoveVector.z = actualFrameMove.z / kDeltaSeconds;
    }

    nextMoveVector.x *= kHorizontalDamping;
    nextMoveVector.z *= kHorizontalDamping;

    *outPosition = nextPosition;
    *outNextMoveVector = nextMoveVector;
    return collided;
}
}
