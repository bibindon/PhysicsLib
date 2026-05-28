#include "PhysicsLib.h"

#include <d3d9.h>

#include "PhysicsLibInternal.h"

#include <algorithm>
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
constexpr float kGroundContactOffset = 0.0005f;
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
bool g_inertiaEnabled = true;
bool g_slideEnabled = true;
bool g_slideCheckEnabled = true;
bool g_tangentMoveEnabled = true;
bool g_airMoveEnabled = true;
bool g_optimizationEnabled = true;
bool g_movingFloorEnabled = true;
bool g_cameraAutoMoveEnabled = false;
bool g_contactEnabled = true;
bool g_surfaceContactEnabled = true;

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

PhysicsLib::Aabb2D PhysicsLib::MakeSegmentAabb2D(const D3DXVECTOR3& start, const D3DXVECTOR3& end)
{
    Aabb2D bounds;
    bounds.minX = std::min(start.x, end.x) - kGroundContactOffset;
    bounds.maxX = std::max(start.x, end.x) + kGroundContactOffset;
    bounds.minZ = std::min(start.z, end.z) - kGroundContactOffset;
    bounds.maxZ = std::max(start.z, end.z) + kGroundContactOffset;
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

std::vector<size_t> PhysicsLib::BuildCollisionCandidateIndices(const D3DXVECTOR3& start, const D3DXVECTOR3& end)
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

    QueryQuadTree(root, MakeSegmentAabb2D(start, end), &candidates);
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

bool SettingsState::IsInertiaEnabled()
{
    return g_inertiaEnabled;
}

void SettingsState::SetInertiaEnabled(bool enabled)
{
    g_inertiaEnabled = enabled;
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
                              D3DXVECTOR3* outSupportVelocity)
{
    UNREFERENCED_PARAMETER(shapeType);
    UNREFERENCED_PARAMETER(radius);
    UNREFERENCED_PARAMETER(height);

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

            const std::vector<size_t> candidateIndices = BuildCollisionCandidateIndices(currentPosition, nextPosition);
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
                        D3DXVECTOR3 slideEndPosition = nextPosition + slideMove;
                        D3DXVECTOR3 slideHitPoint;
                        D3DXVECTOR3 slideHitNormal;
                        float slideHitDistance = 0.0f;
                        bool slideBlocked = false;
                        D3DXVECTOR3 nearestSlidePoint = slideEndPosition;
                        D3DXVECTOR3 nearestSlideNormal(0.0f, 1.0f, 0.0f);
                        float nearestSlideDistance = std::numeric_limits<float>::max();
                        const std::vector<size_t> slideCandidateIndices =
                            BuildCollisionCandidateIndices(nextPosition, slideEndPosition);
                        for (size_t candidateIndex = 0; candidateIndex < slideCandidateIndices.size(); ++candidateIndex)
                        {
                            const size_t i = slideCandidateIndices[candidateIndex];
                            if (g_simpleObjects[i].objectType == ObjectType::PassThrough || g_simpleObjects[i].mesh == NULL)
                            {
                                continue;
                            }

                            if (RayCastObject(g_simpleObjects[i].mesh,
                                              g_simpleObjects[i].transform,
                                              nextPosition,
                                              slideEndPosition,
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

                                nearestSlidePoint = slideHitPoint;
                                nearestSlideNormal = slideHitNormal;
                                nearestSlideDistance = slideHitDistance;
                                slideBlocked = true;
                            }
                        }

                        ++slideCount;
                        if (slideBlocked)
                        {
                            nextPosition = nearestSlidePoint + nearestSlideNormal * kGroundContactOffset;
                            nextMoveVector = RemoveIntoSurfaceVelocity(nextMoveVector, nearestSlideNormal);
                            lastHitNormal = nearestSlideNormal;
                            lastHitDistance = nearestSlideDistance;
                        }
                        else
                        {
                            if (SettingsState::IsSlideCheckEnabled())
                            {
                                D3DXVECTOR3 secondSlideHitPoint;
                                D3DXVECTOR3 secondSlideHitNormal;
                                float secondSlideHitDistance = 0.0f;
                                bool secondSlideBlocked = false;
                                D3DXVECTOR3 nearestSecondSlidePoint = slideEndPosition;
                                D3DXVECTOR3 nearestSecondSlideNormal(0.0f, 1.0f, 0.0f);
                                float nearestSecondSlideDistance = std::numeric_limits<float>::max();
                                const std::vector<size_t> secondSlideCandidateIndices =
                                    BuildCollisionCandidateIndices(currentPosition, slideEndPosition);
                                for (size_t candidateIndex = 0; candidateIndex < secondSlideCandidateIndices.size(); ++candidateIndex)
                                {
                                    const size_t i = secondSlideCandidateIndices[candidateIndex];
                                    if (g_simpleObjects[i].objectType == ObjectType::PassThrough || g_simpleObjects[i].mesh == NULL)
                                    {
                                        continue;
                                    }

                                    if (RayCastObject(g_simpleObjects[i].mesh,
                                                      g_simpleObjects[i].transform,
                                                      currentPosition,
                                                      slideEndPosition,
                                                      &secondSlideHitPoint,
                                                      &secondSlideHitNormal,
                                                      &secondSlideHitDistance))
                                    {
                                        if (secondSlideHitDistance >= nearestSecondSlideDistance)
                                        {
                                            continue;
                                        }

                                        nearestSecondSlidePoint = secondSlideHitPoint;
                                        nearestSecondSlideNormal = secondSlideHitNormal;
                                        nearestSecondSlideDistance = secondSlideHitDistance;
                                        secondSlideBlocked = true;
                                    }
                                }

                                ++slideCount;
                                if (secondSlideBlocked)
                                {
                                    nextMoveVector = D3DXVECTOR3(0.0f, 0.0f, 0.0f);
                                    lastHitNormal = nearestSecondSlideNormal;
                                    lastHitDistance = nearestSecondSlideDistance;
                                }
                                else
                                {
                                    nextPosition = slideEndPosition;
                                }
                            }
                            else
                            {
                                nextPosition = slideEndPosition;
                            }
                        }
                    }
                }
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
