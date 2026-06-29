#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <d3dx9.h>
#include <tchar.h>

#include <vector>

#include "DashBooster.h"

namespace PhysicsLib
{

// 衝突用オブジェクトの読み込み、更新、簡易的な接触判定を管理する。
class PhysicsLib
{
public:

    // 衝突オブジェクトの扱い方である。
    enum class ObjectType
    {

        // 通過可能なオブジェクトである。接触判定やアイテム取得などに使う。
        PassThrough,

        // 静止している衝突オブジェクトである。
        Slide,

        // 速度に従って移動する衝突オブジェクトである。
        MovingSlide,
    };

    // プレイヤー側の判定形状である。
    enum class ShapeType
    {

        // 点として判定する。
        Point,

        // 球として判定する。
        Sphere,

        // 円柱として判定する。
        Cylinder,

        // 直方体として判定する。
        Cuboid,
    };

    // 衝突オブジェクトの位置、回転、拡大率、速度である。
    struct Transform
    {
        D3DXVECTOR3 position = D3DXVECTOR3(0.0f, 0.0f, 0.0f);
        D3DXVECTOR3 rotation = D3DXVECTOR3(0.0f, 0.0f, 0.0f);
        D3DXVECTOR3 scale = D3DXVECTOR3(1.0f, 1.0f, 1.0f);
        D3DXVECTOR3 velocity = D3DXVECTOR3(0.0f, 0.0f, 0.0f);
    };

    static void Initialize();
    static void Finalize();

    // MovingSlide の位置を速度に従って更新する。
    static void Update(float deltaSeconds = 1.0f / 60.0f);

    // ライブラリ側の設定ダイアログを表示する。
    static void ShowSettingsDialog(HWND ownerWindow);

    // フォーカスモードが有効かどうかを取得する。
    static bool IsFocusModeEnabled();

    // 現在のプレイヤー判定形状を取得する。
    static ShapeType GetShapeType();

    // 現在の判定半径を取得する。
    static float GetRadius();

    // 円柱判定用の半径を取得する。
    static float GetCylinderRadius();

    // 円柱判定用の高さを取得する。
    static float GetCylinderHeight();

    // 直方体判定用の横を取得する。
    static float GetCuboidWidth();

    // 直方体判定用の縦を取得する。
    static float GetCuboidHeight();

    // 直方体判定用の高さを取得する。
    static float GetCuboidDepth();

    // 直方体判定用の X 軸回転を取得する。
    static float GetCuboidRotX();

    // 直方体判定用の Y 軸回転を取得する。
    static float GetCuboidRotY();

    // 直方体判定用の Z 軸回転を取得する。
    static float GetCuboidRotZ();

    // 歩行速度を取得する。
    static float GetWalkSpeed();

    // ダッシュ速度を取得する。
    static float GetDashSpeed();

    // 設定ダイアログの初期化ボタンから呼ばれるリセット処理を登録する。
    static void SetResetCallback(void (*callback)());

    // CSV ファイルから衝突オブジェクトを一括読み込みする。
    static void LoadFromCsv(const TCHAR* csvPath);

    // 登録済みの衝突オブジェクトをすべて削除する。
    static void ClearObjects();

    // CSV の ID に対応するファイル名を取得する。
    static const TCHAR* GetCsvFileName(int id);

    // CSV の ID に対応する物理オブジェクト ID を取得する。
    static int GetCsvObjectId(int csvId);

    // CSV の ID に対応する物理オブジェクトの Transform を更新する。
    static void UpdateCsvTransform(int csvId,
                                   const D3DXVECTOR3& position,
                                   const D3DXVECTOR3& rotation,
                                   const D3DXVECTOR3& scale);

    // Xファイルを読み込み、衝突オブジェクトとして登録する。戻り値は登録IDである。
    static int Load(const TCHAR* modelPath, ObjectType objectType, float friction);

    // 登録済みオブジェクトの位置、回転、拡大率を設定する。
    static void SetTransform(int id,
                             const D3DXVECTOR3& position,
                             const D3DXVECTOR3& rotation = D3DXVECTOR3(0.0f, 0.0f, 0.0f),
                             const D3DXVECTOR3& scale = D3DXVECTOR3(1.0f, 1.0f, 1.0f));

    // MovingSlide など、登録済みオブジェクトの速度を設定する。
    static void SetVelocity(int id, const D3DXVECTOR3& velocity);

    // 登録済みオブジェクトの現在の Transform を取得する。
    static Transform GetTransform(int id);

    // currentPosition から moveVector 方向へ移動したときの接面判定を行う。
    static bool CheckCollide(const D3DXVECTOR3& currentPosition,
                             const D3DXVECTOR3& moveVector,
                             ShapeType shapeType,
                             D3DXVECTOR3* outPosition,
                             D3DXVECTOR3* outNextMoveVector,
                             std::vector<int>* outPassThroughIds,
                             std::vector<int>* outSolidIds,
                             float radius = 0.0f,
                             float height = 0.0f,
                             float* outNormalMove = nullptr,
                             D3DXVECTOR3* outHitNormal = nullptr,
                             float* outHitDistance = nullptr,
                             D3DXVECTOR3* outSlideMove = nullptr,
                             int* outSlideCount = nullptr,
                             int* outSupportObjectId = nullptr,
                             D3DXVECTOR3* outSupportVelocity = nullptr,
                             bool* outCrushed = nullptr);

    // 指定IDのオブジェクトと position の距離が distance 以下かを判定する。
    static bool CheckContact(int id, const D3DXVECTOR3& position, float distance);

    // 注視点と希望カメラ位置の間にある障害物を避けたカメラ位置を求める。
    static bool ResolveCameraCollision(const D3DXVECTOR3& targetPosition,
                                       const D3DXVECTOR3& desiredCameraPosition,
                                       float minimumDistance,
                                       float obstacleOffset,
                                       D3DXVECTOR3* outPosition);

private:
    struct Aabb2D
    {
        float minX = 0.0f;
        float minZ = 0.0f;
        float maxX = 0.0f;
        float maxZ = 0.0f;
    };

    struct Aabb3D
    {
        float minX = 0.0f;
        float minY = 0.0f;
        float minZ = 0.0f;
        float maxX = 0.0f;
        float maxY = 0.0f;
        float maxZ = 0.0f;
    };

    struct QuadTreeNode
    {
        Aabb2D bounds;
        int depth = 0;
        std::vector<size_t> objectIndices;
        std::vector<QuadTreeNode> children;
    };

    // COMオブジェクトを安全に解放するための補助関数である。
    static void SafeRelease(IUnknown* object);

    // Transform からワールド行列を組み立てる補助関数である。
    static D3DXMATRIX BuildWorldMatrix(const Transform& transform);

    // メッシュの指定面から法線を取り出す補助関数である。
    static bool ExtractFaceNormal(LPD3DXMESH mesh, DWORD faceIndex, D3DXVECTOR3* outNormal);

    // ワールド座標系の線分と単一メッシュの接面判定を行う補助関数である。
    static bool RayCastObject(LPD3DXMESH mesh,
                              const Transform& transform,
                              const D3DXVECTOR3& rayOriginWorld,
                              const D3DXVECTOR3& rayEndWorld,
                              D3DXVECTOR3* outPoint,
                              D3DXVECTOR3* outSurfaceNormal,
                              float* outDistance);

    // プレイヤー形状を代表点の集合として扱い、線分と単一メッシュの接面判定を行う補助関数である。
    static bool RayCastShapeObject(LPD3DXMESH mesh,
                                   const Transform& transform,
                                   const D3DXVECTOR3& rayOriginWorld,
                                   const D3DXVECTOR3& rayEndWorld,
                                   ShapeType shapeType,
                                   float radius,
                                   float height,
                                   D3DXVECTOR3* outPoint,
                                   D3DXVECTOR3* outSurfaceNormal,
                                   float* outDistance);

    // 速度から接触面へ向かう成分だけを取り除く補助関数である。
    static D3DXVECTOR3 RemoveIntoSurfaceVelocity(const D3DXVECTOR3& velocity,
                                                 const D3DXVECTOR3& surfaceNormal);

    static bool IntersectsAabb2D(const Aabb2D& a, const Aabb2D& b);
    static bool ContainsAabb2D(const Aabb2D& outer, const Aabb2D& inner);
    static Aabb2D MakeSegmentAabb2D(const D3DXVECTOR3& start, const D3DXVECTOR3& end, float padding = 0.0f);
    static Aabb2D MakeWorldAabb2D(const D3DXVECTOR3& localBoundsMin,
                                  const D3DXVECTOR3& localBoundsMax,
                                  const Transform& transform);
    static Aabb3D MakeWorldAabb3D(const D3DXVECTOR3& localBoundsMin,
                                  const D3DXVECTOR3& localBoundsMax,
                                  const Transform& transform);
    static Aabb3D MakeShapeAabb3D(const D3DXVECTOR3& position,
                                  ShapeType shapeType,
                                  float radius,
                                  float height);
    static bool IntersectsAabb3D(const Aabb3D& a, const Aabb3D& b);
    static bool IsShapeBlockedOppositePush(size_t pushingObjectIndex,
                                           const Aabb3D& shapeBounds,
                                           const D3DXVECTOR3& pushVector);
    static bool ResolveMovingSlidePenetration(const D3DXVECTOR3& currentPosition,
                                              ShapeType shapeType,
                                              float radius,
                                              float height,
                                              D3DXVECTOR3* inOutPosition,
                                              D3DXVECTOR3* outPushNormal,
                                              int* outSupportObjectId,
                                              D3DXVECTOR3* outSupportVelocity,
                                              bool* outCrushed);
    static bool ComputeMeshLocalBounds(LPD3DXMESH mesh, D3DXVECTOR3* outMin, D3DXVECTOR3* outMax);
    static void SplitQuadTreeNode(QuadTreeNode* node);
    static bool InsertIntoChildIfContained(QuadTreeNode* node,
                                           size_t objectIndex,
                                           const Aabb2D& objectBounds,
                                           const std::vector<Aabb2D>& allBounds);
    static void InsertQuadTreeObject(QuadTreeNode* node,
                                     size_t objectIndex,
                                     const Aabb2D& objectBounds,
                                     const std::vector<Aabb2D>& allBounds);
    static void QueryQuadTree(const QuadTreeNode& node,
                              const Aabb2D& queryBounds,
                              std::vector<size_t>* outIndices);
    static std::vector<D3DXVECTOR3> BuildShapeCastOffsets(ShapeType shapeType, float radius, float height);
    static std::vector<size_t> BuildCollisionCandidateIndices(const D3DXVECTOR3& start,
                                                              const D3DXVECTOR3& end,
                                                              float padding = 0.0f);

    // Xファイルからメッシュを読み込む補助関数である。
    static void LoadMesh(const TCHAR* modelPath, LPD3DXMESH* outMesh);
};


// 入力方向、重力、ジャンプ、接面判定をまとめてプレイヤー位置へ反映する。
class CharacterMover
{
public:

    // 画面表示や調査用のデバッグ情報である。
    struct DebugInfo
    {
        int collideCheckCount = 0;
        int hitCount = 0;
        int slideCount = 0;
        D3DXVECTOR3 lastHitNormal = D3DXVECTOR3(0.0f, 0.0f, 0.0f);
        D3DXVECTOR3 lastSlideMove = D3DXVECTOR3(0.0f, 0.0f, 0.0f);
        float lastHitDistance = 0.0f;
        float lastNormalMove = 0.0f;
        bool crushed = false;
    };

    // プレイヤー移動の設定値である。
    struct Settings
    {

        // プレイヤーの接面判定に使う形状である。
        PhysicsLib::ShapeType shapeType = PhysicsLib::ShapeType::Sphere;

        // Sphere/Cylinder 用の半径である。
        float radius = 0.5f;

        // 衝突判定の中心 Y 座標（足元からの高さ）である。
        float collisionCenterY = 0.5f;

        // Cylinder 用の高さである。
        float height = 1.0f;

        // 水平方向の最高速度である。
        float moveSpeed = 6.0f;

        // 地上で目標速度へ近づく加速度である。
        float groundAcceleration = 6.0f;

        // 空中で目標速度へ近づく加速度である。
        float airAcceleration = 2.0f;

        // ジャンプ時に設定される上向き速度である。
        float jumpVelocity = 2.0f;

        // true の場合、空中でも入力方向へ速度を変える。
        bool airControlEnabled = false;

        // true の場合、空中で1回だけ追加ジャンプできる。
        bool doubleJumpEnabled = false;

        // true の場合、ジャンプ時に水平速度を維持する。
        bool keepHorizontalVelocityOnJump = true;

        // 地上の減衰率である。
        float groundDamping = 1.0f;

        // 空中の減衰率である。
        float airDamping = 1.0f;
    };

    CharacterMover();
    explicit CharacterMover(const D3DXVECTOR3& position);

    void SetSettings(const Settings& settings);
    Settings GetSettings() const;

    // 位置と速度、接地状態、ジャンプ回数を初期状態へ戻す。
    void Reset(const D3DXVECTOR3& position);

    void SetPosition(const D3DXVECTOR3& position);
    D3DXVECTOR3 GetPosition() const;

    // 現在向いている方向へのダッシュを次の Update() で要求する。
    void RequestDash(const D3DXVECTOR3& direction);

    // 現在の水平速度を維持したまま、上向き速度を与えて空中状態にする。
    void ApplyUpwardVelocity(float upwardVelocity);

    // 指定方向へ一定時間射出するブースターを起動する。
    void ApplyDashBooster(const D3DXVECTOR3& direction,
                           float speed,
                           float duration,
                           bool chargeEnabled = true);

    // ブースト中かどうかを返す。
    bool IsBoosted() const;

    // ダッシュブースターの溜め中かどうかを返す。
    bool IsDashBoosterCharging() const;

    D3DXVECTOR3 GetVelocity() const;

    bool IsGrounded() const;
    bool IsTouchingWall() const;
    bool IsChargingJump() const;
    bool IsJumping() const;
    bool IsCrushed() const;
    bool IsDashing() const;

    // Update() でジャンプが実際に成功したフレームでのみ true を返す。
    bool JustJumped() const;

    // 乗っている移動床などのIDである。未接触時は -1 である。
    int GetSupportObjectId() const;
    DebugInfo GetDebugInfo() const;

    // 1フレーム分の移動を計算する。jump は押した瞬間だけ true にする。
    bool Update(const D3DXVECTOR3& inputDirection,
                bool jump,
                std::vector<int>* outPassThroughIds = nullptr,
                std::vector<int>* outSolidIds = nullptr);

private:
    static void MoveHorizontalVelocityToward(D3DXVECTOR3* velocity,
                                             const D3DXVECTOR3& targetVelocity,
                                             float acceleration);

    // 速度を3次元の目標速度へ近づける補助関数である。
    static void MoveVelocityToward(D3DXVECTOR3* velocity,
                                   const D3DXVECTOR3& targetVelocity,
                                   float acceleration);

    // ベクトルを指定法線の接平面へ投影する補助関数である。
    static D3DXVECTOR3 ProjectVectorOnPlane(const D3DXVECTOR3& vector,
                                            const D3DXVECTOR3& normal);

    Settings m_settings;
    D3DXVECTOR3 m_position;
    D3DXVECTOR3 m_velocity;

    // 接地中の面法線である。
    D3DXVECTOR3 m_groundNormal;
    bool m_isGrounded;
    bool m_isTouchingWall;
    bool m_isCrushed;
    int m_supportObjectId;

    // 空中であと何回ジャンプできるかを表す。主に2段ジャンプ用である。
    int m_remainingAirJumps;

    // 空中であと何回ダッシュできるかを表す。
    int m_remainingAirDashes;

    // ダッシュ残り時間（秒）である。
    float m_dashTimer;

    // ダッシュ中かどうかである。
    bool m_isDashing;

    DashBooster m_booster;

    // 次の Update() でダッシュ要求を処理するかどうかである。
    bool m_hasPendingDashRequest;

    // ダッシュ方向である。
    D3DXVECTOR3 m_dashDirection;

    // 要求されたダッシュ方向である。
    D3DXVECTOR3 m_pendingDashDirection;

    bool m_isPseudoInertiaStopping;
    bool m_isPseudoInertiaFullStopping;
    float m_pseudoInertiaStopAcceleration;

    // ためジャンプの残り時間（秒）である。
    float m_chargeJumpTimer;

    // ためジャンプ中かどうかである。
    bool m_isChargingJump;

    // ためジャンプが地上ジャンプとして始まったかどうかである。
    bool m_chargeJumpWasGroundJump;

    // 着地後のランディング残り時間（秒）である。
    float m_landingTimer;

    // 着地後のランディング中かどうかである。
    bool m_isInLanding;

    // ジャンプ操作によって空中にいるかどうかである。
    bool m_didJump;

    // Update() 内でジャンプが成功したフレームでのみ true。
    bool m_justJumped;

    DebugInfo m_debugInfo;
};


// カメラ位置を管理する。現在は希望位置をそのまま返す。
class CameraMover
{
public:
    struct Settings
    {

        // 注視点へ近づける場合の最短距離である。
        float minimumDistance = 0.5f;

        // 障害物から少し手前に止めるための余白である。
        float obstacleOffset = 0.1f;
    };

    CameraMover();

    // 注視点と希望カメラ位置から、実際に使うカメラ位置を返す。
    D3DXVECTOR3 ResolvePosition(const D3DXVECTOR3& targetPosition,
                                const D3DXVECTOR3& desiredCameraPosition) const;

private:
    Settings m_settings;
};
}
