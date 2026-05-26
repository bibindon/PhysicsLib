#include "PhysicsLib.h"

#include <d3d9.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
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
constexpr float kHorizontalDamping = 1.0f;
constexpr float kSkinWidth = 0.01f;
constexpr float kGroundNormalY = 0.5f;
constexpr int kMaxSlideIterations = 3;
constexpr int kQuadTreeLevel = 6;

// マルチスレッド化。ほぼ効果なし。やる価値なし。
#define THREAD_NUM 2

struct LoadedObject
{
    int id = 0;
    PhysicsLib::ObjectType objectType = PhysicsLib::ObjectType::Slide;
    float friction = 0.0f;
    LPD3DXMESH mesh = NULL;
    D3DXVECTOR3 localBoundsMin = D3DXVECTOR3(0.0f, 0.0f, 0.0f);
    D3DXVECTOR3 localBoundsMax = D3DXVECTOR3(0.0f, 0.0f, 0.0f);
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
bool g_intersectMultithreadEnabled = false;

struct HitCollection
{
    std::unordered_set<int> passThroughIds;
    std::unordered_set<int> solidIds;
    bool foundSolid = false;
    float nearestDistance = std::numeric_limits<float>::max();
    RaycastHit nearestHit;
};

struct XzBounds
{
    float minX = std::numeric_limits<float>::max();
    float minZ = std::numeric_limits<float>::max();
    float maxX = -std::numeric_limits<float>::max();
    float maxZ = -std::numeric_limits<float>::max();
};

void IncludePoint(XzBounds* bounds, float x, float z)
{
    bounds->minX = std::min(bounds->minX, x);
    bounds->minZ = std::min(bounds->minZ, z);
    bounds->maxX = std::max(bounds->maxX, x);
    bounds->maxZ = std::max(bounds->maxZ, z);
}

void IncludeBounds(XzBounds* bounds, const XzBounds& other)
{
    IncludePoint(bounds, other.minX, other.minZ);
    IncludePoint(bounds, other.maxX, other.maxZ);
}

void ExpandBounds(XzBounds* bounds, float amount)
{
    bounds->minX -= amount;
    bounds->minZ -= amount;
    bounds->maxX += amount;
    bounds->maxZ += amount;
}

bool Intersects(const XzBounds& a, const XzBounds& b)
{
    return a.minX <= b.maxX &&
           a.maxX >= b.minX &&
           a.minZ <= b.maxZ &&
           a.maxZ >= b.minZ;
}

uint32_t BitSeparate32(uint32_t value)
{
    value = (value | (value << 8)) & 0x00ff00ff;
    value = (value | (value << 4)) & 0x0f0f0f0f;
    value = (value | (value << 2)) & 0x33333333;
    return (value | (value << 1)) & 0x55555555;
}

uint32_t GetMortonNumber(uint32_t x, uint32_t z)
{
    return BitSeparate32(x) | (BitSeparate32(z) << 1);
}

uint32_t GetLevelStartElement(int level)
{
    uint32_t result = 0;
    uint32_t pow4 = 1;
    for (int i = 0; i < level; ++i)
    {
        result += pow4;
        pow4 *= 4;
    }

    return result;
}

class LinearQuadTree
{
public:
    void Build(const std::vector<XzBounds>& objectBounds, const XzBounds& queryBounds)
    {
        rootBounds = queryBounds;
        for (size_t i = 0; i < objectBounds.size(); ++i)
        {
            IncludeBounds(&rootBounds, objectBounds[i]);
        }

        const float width = rootBounds.maxX - rootBounds.minX;
        const float depth = rootBounds.maxZ - rootBounds.minZ;
        const float size = std::max(std::max(width, depth), 1.0f);
        rootBounds.maxX = rootBounds.minX + size;
        rootBounds.maxZ = rootBounds.minZ + size;
        ExpandBounds(&rootBounds, kSkinWidth);

        cells.clear();
        cells.resize(GetLevelStartElement(kQuadTreeLevel + 1));

        for (size_t i = 0; i < objectBounds.size(); ++i)
        {
            RegisterObject(objectBounds[i], i);
        }
    }

    void Query(const XzBounds& bounds, std::vector<size_t>* outObjectIndices) const
    {
        outObjectIndices->clear();
        if (cells.empty() || !Intersects(rootBounds, bounds))
        {
            return;
        }

        QueryNode(0, 0, rootBounds, bounds, outObjectIndices);
    }

private:
    struct Cell
    {
        std::vector<size_t> objectIndices;
        size_t subtreeObjectCount = 0;
    };

    uint32_t CoordinateToCell(float value, float minValue, float unitSize) const
    {
        const int cellCount = 1 << kQuadTreeLevel;
        int cell = static_cast<int>((value - minValue) / unitSize);
        cell = std::max(0, std::min(cell, cellCount - 1));
        return static_cast<uint32_t>(cell);
    }

    uint32_t GetElementIndex(const XzBounds& bounds) const
    {
        const float unitX = (rootBounds.maxX - rootBounds.minX) / static_cast<float>(1 << kQuadTreeLevel);
        const float unitZ = (rootBounds.maxZ - rootBounds.minZ) / static_cast<float>(1 << kQuadTreeLevel);
        const uint32_t minMorton = GetMortonNumber(CoordinateToCell(bounds.minX, rootBounds.minX, unitX),
                                                   CoordinateToCell(bounds.minZ, rootBounds.minZ, unitZ));
        const uint32_t maxMorton = GetMortonNumber(CoordinateToCell(bounds.maxX, rootBounds.minX, unitX),
                                                   CoordinateToCell(bounds.maxZ, rootBounds.minZ, unitZ));
        const uint32_t xored = minMorton ^ maxMorton;

        int shift = 0;
        for (int bitPair = 0; bitPair < kQuadTreeLevel; ++bitPair)
        {
            if (((xored >> (bitPair * 2)) & 0x3) != 0)
            {
                shift = (bitPair + 1) * 2;
            }
        }

        const int level = kQuadTreeLevel - shift / 2;
        const uint32_t mortonAtLevel = maxMorton >> shift;
        return GetLevelStartElement(level) + mortonAtLevel;
    }

    void RegisterObject(const XzBounds& bounds, size_t objectIndex)
    {
        uint32_t element = GetElementIndex(bounds);
        cells[element].objectIndices.push_back(objectIndex);

        while (true)
        {
            ++cells[element].subtreeObjectCount;
            if (element == 0)
            {
                break;
            }

            element = (element - 1) >> 2;
        }
    }

    void QueryNode(uint32_t element,
                   int level,
                   const XzBounds& nodeBounds,
                   const XzBounds& queryBounds,
                   std::vector<size_t>* outObjectIndices) const
    {
        if (element >= cells.size() ||
            cells[element].subtreeObjectCount == 0 ||
            !Intersects(nodeBounds, queryBounds))
        {
            return;
        }

        const Cell& cell = cells[element];
        outObjectIndices->insert(outObjectIndices->end(),
                                 cell.objectIndices.begin(),
                                 cell.objectIndices.end());

        if (level >= kQuadTreeLevel)
        {
            return;
        }

        const float midX = (nodeBounds.minX + nodeBounds.maxX) * 0.5f;
        const float midZ = (nodeBounds.minZ + nodeBounds.maxZ) * 0.5f;
        for (uint32_t child = 0; child < 4; ++child)
        {
            XzBounds childBounds;
            childBounds.minX = (child & 0x1) ? midX : nodeBounds.minX;
            childBounds.maxX = (child & 0x1) ? nodeBounds.maxX : midX;
            childBounds.minZ = (child & 0x2) ? midZ : nodeBounds.minZ;
            childBounds.maxZ = (child & 0x2) ? nodeBounds.maxZ : midZ;
            QueryNode(element * 4 + 1 + child, level + 1, childBounds, queryBounds, outObjectIndices);
        }
    }

    XzBounds rootBounds;
    std::vector<Cell> cells;
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

bool ComputeMeshBounds(LPD3DXMESH mesh, D3DXVECTOR3* outMin, D3DXVECTOR3* outMax)
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

    result = D3DXComputeBoundingBox(static_cast<D3DXVECTOR3*>(vertexBuffer),
                                    mesh->GetNumVertices(),
                                    mesh->GetNumBytesPerVertex(),
                                    outMin,
                                    outMax);

    mesh->UnlockVertexBuffer();
    return SUCCEEDED(result);
}

XzBounds ComputeWorldXzBounds(const LoadedObject& object)
{
    XzBounds bounds;
    const D3DXMATRIX worldMatrix = BuildWorldMatrix(object.transform);

    for (int x = 0; x < 2; ++x)
    {
        for (int y = 0; y < 2; ++y)
        {
            for (int z = 0; z < 2; ++z)
            {
                const D3DXVECTOR3 localCorner(x == 0 ? object.localBoundsMin.x : object.localBoundsMax.x,
                                              y == 0 ? object.localBoundsMin.y : object.localBoundsMax.y,
                                              z == 0 ? object.localBoundsMin.z : object.localBoundsMax.z);
                D3DXVECTOR3 worldCorner;
                D3DXVec3TransformCoord(&worldCorner, &localCorner, &worldMatrix);
                IncludePoint(&bounds, worldCorner.x, worldCorner.z);
            }
        }
    }

    return bounds;
}

XzBounds ComputeSweptShapeXzBounds(const D3DXVECTOR3& startPosition,
                                   const D3DXVECTOR3& endPosition,
                                   const std::vector<D3DXVECTOR3>& offsets)
{
    XzBounds bounds;
    for (size_t i = 0; i < offsets.size(); ++i)
    {
        const D3DXVECTOR3 rayStart = startPosition + offsets[i];
        const D3DXVECTOR3 rayEnd = endPosition + offsets[i];
        IncludePoint(&bounds, rayStart.x, rayStart.z);
        IncludePoint(&bounds, rayEnd.x, rayEnd.z);
    }

    ExpandBounds(&bounds, kSkinWidth);
    return bounds;
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

    const D3DXVECTOR3 rayVectorWorld = rayEndWorld - rayOriginWorld;
    D3DXVECTOR3 rayDirectionWorld = rayVectorWorld;
    D3DXVec3Normalize(&rayDirectionWorld, &rayDirectionWorld);
    const float normalDirection = D3DXVec3Dot(&worldNormal, &rayDirectionWorld);
    if (normalDirection > 0.0f)
    {
        worldNormal = -worldNormal;
    }
    else if (std::abs(normalDirection) <= 0.0001f &&
             std::abs(worldNormal.y) > kGroundNormalY &&
             worldNormal.y < 0.0f)
    {
        worldNormal = -worldNormal;
    }

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

    const D3DXVECTOR3 rayMove = rayEnd - rayStart;
    const float normalMove = D3DXVec3Dot(&rayMove, &hit.normal);
    const bool isGroundContact = hit.normal.y > kGroundNormalY && hit.distance <= kSkinWidth;
    if (isGroundContact && normalMove >= -0.0001f)
    {
        inOutCollection->solidIds.insert(hit.objectId);
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
    const XzBounds sweptShapeBounds = ComputeSweptShapeXzBounds(startPosition, endPosition, offsets);
    std::vector<XzBounds> objectBounds;
    objectBounds.reserve(g_objects.size());
    for (size_t objectIndex = 0; objectIndex < g_objects.size(); ++objectIndex)
    {
        objectBounds.push_back(ComputeWorldXzBounds(g_objects[objectIndex]));
    }

    LinearQuadTree quadTree;
    quadTree.Build(objectBounds, sweptShapeBounds);

    std::vector<size_t> candidateObjectIndices;
    quadTree.Query(sweptShapeBounds, &candidateObjectIndices);
    if (candidateObjectIndices.empty())
    {
        return false;
    }

    bool foundSolid = false;
    float nearestDistance = std::numeric_limits<float>::max();
    std::unordered_map<LPD3DXMESH, std::vector<size_t> > meshGroups;
    std::vector<LPD3DXMESH> meshOrder;
    meshGroups.reserve(candidateObjectIndices.size());
    meshOrder.reserve(candidateObjectIndices.size());

    for (size_t candidateIndex = 0; candidateIndex < candidateObjectIndices.size(); ++candidateIndex)
    {
        const size_t objectIndex = candidateObjectIndices[candidateIndex];
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
        std::vector<HitCollection> threadCollections(THREAD_NUM);
#pragma omp parallel num_threads(THREAD_NUM)
        {
            HitCollection& threadCollection = threadCollections[omp_get_thread_num()];
#pragma omp for schedule(static)
            for (int meshGroupIndex = 0; meshGroupIndex < static_cast<int>(meshOrder.size()); ++meshGroupIndex)
            {
                const std::vector<size_t>& objectIndices = meshGroups[meshOrder[meshGroupIndex]];
                for (size_t objectListIndex = 0; objectListIndex < objectIndices.size(); ++objectListIndex)
                {
                    const LoadedObject& object = g_objects[objectIndices[objectListIndex]];
                    AccumulateObjectHits(object, startPosition, endPosition, offsets, &threadCollection);
                }
            }
        }

        for (size_t threadIndex = 0; threadIndex < threadCollections.size(); ++threadIndex)
        {
            MergeHitCollection(threadCollections[threadIndex],
                               outPassThroughIds,
                               outSolidIds,
                               &foundSolid,
                               &nearestDistance,
                               outNearestSolidHit);
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

bool CheckCollideInternal(const D3DXVECTOR3& currentPosition,
                          const D3DXVECTOR3& moveVector,
                          PhysicsLib::ShapeType shapeType,
                          D3DXVECTOR3* outPosition,
                          D3DXVECTOR3* outNextMoveVector,
                          std::vector<int>* outPassThroughIds,
                          std::vector<int>* outSolidIds,
                          float radius,
                          float height,
                          bool applyHorizontalDamping,
                          bool stopOnNonGroundHit,
                          bool* outGroundContact,
                          bool* outWallContact)
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

    if (shapeType == PhysicsLib::ShapeType::Sphere && radius < 0.0f)
    {
        throw std::out_of_range("Sphere radius must not be negative.");
    }

    if (shapeType == PhysicsLib::ShapeType::Cylinder && (radius < 0.0f || height < 0.0f))
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
    bool groundContact = false;
    bool wallContact = false;

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

        const bool isGroundContact = nearestHit.normal.y > kGroundNormalY && nearestHit.distance <= kSkinWidth;
        if (isGroundContact)
        {
            groundContact = true;
            D3DXVECTOR3 slideMove = ResolveSlide(remainingMove, nearestHit.normal);
            nextPosition = currentPositionForSlide;
            if (nextMoveVector.y < 0.0f)
            {
                nextMoveVector.y = 0.0f;
                slideMove.y = 0.0f;
            }

            remainingMove = slideMove;
            continue;
        }

        const D3DXVECTOR3 moveDirection = remainingMove / totalMoveLength;
        const float safeDistance = std::max(0.0f, nearestHit.distance - kSkinWidth);
        currentPositionForSlide += moveDirection * safeDistance;
        nextPosition = currentPositionForSlide;

        D3DXVECTOR3 unresolvedMove = remainingMove - moveDirection * nearestHit.distance;
        D3DXVECTOR3 slideMove = ResolveSlide(unresolvedMove, nearestHit.normal);
        if (stopOnNonGroundHit && nearestHit.normal.y <= kGroundNormalY)
        {
            wallContact = true;
            remainingMove = D3DXVECTOR3(0.0f, 0.0f, 0.0f);
            break;
        }

        if (nearestHit.normal.y > kGroundNormalY && nextMoveVector.y < 0.0f)
        {
            groundContact = true;
            nextMoveVector.y = 0.0f;
            slideMove.y = 0.0f;
        }
        else if (nearestHit.normal.y <= kGroundNormalY)
        {
            wallContact = true;
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

    if (D3DXVec3Length(&frameMove) > 0.0001f && (!groundContact || wallContact))
    {
        D3DXVECTOR3 actualFrameMove = nextPosition - currentPosition;
        nextMoveVector.x = actualFrameMove.x / kDeltaSeconds;
        nextMoveVector.z = actualFrameMove.z / kDeltaSeconds;
    }

    if (applyHorizontalDamping)
    {
        nextMoveVector.x *= kHorizontalDamping;
        nextMoveVector.z *= kHorizontalDamping;
    }

    *outPosition = nextPosition;
    *outNextMoveVector = nextMoveVector;
    if (outGroundContact != nullptr)
    {
        *outGroundContact = groundContact;
    }
    if (outWallContact != nullptr)
    {
        *outWallContact = wallContact;
    }
    return collided;
}
}

void PhysicsLib::Initialize()
{
#if defined(_OPENMP)
    omp_set_num_threads(THREAD_NUM);
#endif

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
    if (!ComputeMeshBounds(object.mesh, &object.localBoundsMin, &object.localBoundsMax))
    {
        SafeRelease(object.mesh);
        throw std::runtime_error("Failed to compute collision mesh bounds.");
    }

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
    return CheckCollideInternal(currentPosition,
                                moveVector,
                                shapeType,
                                outPosition,
                                outNextMoveVector,
                                outPassThroughIds,
                                outSolidIds,
                                radius,
                                height,
                                true,
                                false,
                                nullptr,
                                nullptr);
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
    m_remainingAirJumps = 0;
}

void CharacterMover::SetPosition(const D3DXVECTOR3& position)
{
    m_position = position;
}

D3DXVECTOR3 CharacterMover::GetPosition() const
{
    return m_position;
}

void CharacterMover::SetVelocity(const D3DXVECTOR3& velocity)
{
    m_velocity = velocity;
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

bool CharacterMover::Update(const D3DXVECTOR3& inputDirection,
                            bool jump,
                            std::vector<int>* outPassThroughIds,
                            std::vector<int>* outSolidIds)
{
    D3DXVECTOR3 inputMove(inputDirection.x, 0.0f, inputDirection.z);
    if (D3DXVec3Length(&inputMove) > 0.0001f)
    {
        D3DXVec3Normalize(&inputMove, &inputMove);
        inputMove *= m_settings.moveSpeed;
    }

    if (m_settings.airControlEnabled || m_isGrounded)
    {
        float acceleration = m_settings.groundAcceleration;
        if (!m_isGrounded)
        {
            acceleration = m_settings.airAcceleration;
        }

        MoveHorizontalVelocityToward(&m_velocity, inputMove, acceleration);
    }

    bool canJump = false;
    if (jump && m_isGrounded)
    {
        canJump = true;
    }
    else if (jump && m_settings.doubleJumpEnabled && m_remainingAirJumps > 0)
    {
        canJump = true;
        --m_remainingAirJumps;
    }

    if (canJump)
    {
        if (!m_settings.keepHorizontalVelocityOnJump)
        {
            m_velocity.x = inputMove.x;
            m_velocity.z = inputMove.z;
        }

        m_velocity.y = m_settings.jumpVelocity;
        m_isGrounded = false;
        m_supportObjectId = -1;
    }

    const D3DXVECTOR3 shapePosition = m_position + m_settings.shapeOffset;
    D3DXVECTOR3 correctedShapePosition = shapePosition;
    D3DXVECTOR3 nextVelocity = m_velocity;

    const bool wasGrounded = m_isGrounded;
    bool groundContact = false;
    bool wallContact = false;
    const bool collided = CheckCollideInternal(shapePosition,
                                              m_velocity,
                                              m_settings.shapeType,
                                              &correctedShapePosition,
                                              &nextVelocity,
                                              outPassThroughIds,
                                              outSolidIds,
                                              m_settings.radius,
                                              m_settings.height,
                                              false,
                                              !wasGrounded,
                                              &groundContact,
                                              &wallContact);

    m_isGrounded = nextVelocity.y == 0.0f;
    m_isTouchingWall = wallContact;
    m_supportObjectId = -1;
    if (m_isGrounded && outSolidIds != nullptr && !outSolidIds->empty())
    {
        m_supportObjectId = outSolidIds->front();
    }

    if (m_isGrounded)
    {
        m_remainingAirJumps = 1;
    }

    if (!wasGrounded && collided && !m_isGrounded)
    {
        nextVelocity = D3DXVECTOR3(0.0f, 0.0f, 0.0f);
    }

    float damping = m_settings.groundDamping;
    if (!m_isGrounded)
    {
        damping = m_settings.airDamping;
    }

    nextVelocity.x *= damping;
    nextVelocity.z *= damping;

    m_position = correctedShapePosition - m_settings.shapeOffset;
    m_velocity = nextVelocity;
    return collided;
}
}
