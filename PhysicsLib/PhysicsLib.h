#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <d3dx9.h>
#include <tchar.h>

#include <vector>

namespace PhysicsLib
{
class PhysicsLib
{
public:
    enum class ObjectType
    {
        PassThrough,
        Slide,
        MovingSlide,
    };

    enum class ShapeType
    {
        Point,
        Sphere,
        Cylinder,
    };

    struct Transform
    {
        D3DXVECTOR3 position = D3DXVECTOR3(0.0f, 0.0f, 0.0f);
        D3DXVECTOR3 rotation = D3DXVECTOR3(0.0f, 0.0f, 0.0f);
        D3DXVECTOR3 scale = D3DXVECTOR3(1.0f, 1.0f, 1.0f);
        D3DXVECTOR3 velocity = D3DXVECTOR3(0.0f, 0.0f, 0.0f);
    };

    static void Initialize();
    static void Finalize();
    static void Update(float deltaSeconds = 1.0f / 60.0f);
    static void ShowSettingsDialog(HWND ownerWindow);
    static void SetResetCallback(void (*callback)());

    static int Load(const TCHAR* modelPath, ObjectType objectType, float friction);

    static void SetTransform(int id,
                             const D3DXVECTOR3& position,
                             const D3DXVECTOR3& rotation = D3DXVECTOR3(0.0f, 0.0f, 0.0f),
                             const D3DXVECTOR3& scale = D3DXVECTOR3(1.0f, 1.0f, 1.0f));

    static void SetVelocity(int id, const D3DXVECTOR3& velocity);
    static Transform GetTransform(int id);

    static bool CheckCollide(const D3DXVECTOR3& currentPosition,
                             const D3DXVECTOR3& moveVector,
                             ShapeType shapeType,
                             D3DXVECTOR3* outPosition,
                             D3DXVECTOR3* outNextMoveVector,
                             std::vector<int>* outPassThroughIds,
                             std::vector<int>* outSolidIds,
                             float radius = 0.0f,
                             float height = 0.0f);
    static bool CheckContact(int id, const D3DXVECTOR3& position, float distance);
};

class CharacterMover
{
public:
    struct DebugInfo
    {
        int collideCheckCount = 0;
        int hitCount = 0;
        int slideCount = 0;
        D3DXVECTOR3 lastHitNormal = D3DXVECTOR3(0.0f, 0.0f, 0.0f);
        D3DXVECTOR3 lastSlideMove = D3DXVECTOR3(0.0f, 0.0f, 0.0f);
        float lastHitDistance = 0.0f;
    };

    struct Settings
    {
        PhysicsLib::ShapeType shapeType = PhysicsLib::ShapeType::Sphere;
        D3DXVECTOR3 shapeOffset = D3DXVECTOR3(0.0f, 0.5f, 0.0f);
        float radius = 0.5f;
        float height = 0.0f;
        float moveSpeed = 6.0f;
        float groundAcceleration = 6.0f;
        float airAcceleration = 2.0f;
        float jumpVelocity = 2.0f;
        bool airControlEnabled = false;
        bool doubleJumpEnabled = false;
        bool keepHorizontalVelocityOnJump = true;
        float groundDamping = 1.0f;
        float airDamping = 1.0f;
    };

    CharacterMover();
    explicit CharacterMover(const D3DXVECTOR3& position);

    void SetSettings(const Settings& settings);
    Settings GetSettings() const;

    void Reset(const D3DXVECTOR3& position);

    void SetPosition(const D3DXVECTOR3& position);
    D3DXVECTOR3 GetPosition() const;

    void SetVelocity(const D3DXVECTOR3& velocity);
    D3DXVECTOR3 GetVelocity() const;

    bool IsGrounded() const;
    bool IsTouchingWall() const;
    int GetSupportObjectId() const;
    DebugInfo GetDebugInfo() const;

    bool Update(const D3DXVECTOR3& inputDirection,
                bool jump,
                std::vector<int>* outPassThroughIds = nullptr,
                std::vector<int>* outSolidIds = nullptr);

private:
    Settings m_settings;
    D3DXVECTOR3 m_position;
    D3DXVECTOR3 m_velocity;
    bool m_isGrounded;
    bool m_isTouchingWall;
    int m_supportObjectId;
    int m_remainingAirJumps;
    DebugInfo m_debugInfo;
};

class CameraMover
{
public:
    struct Settings
    {
        float minimumDistance = 2.0f;
        float obstacleOffset = 0.1f;
    };

    CameraMover();

    void SetSettings(const Settings& settings);
    Settings GetSettings() const;

    D3DXVECTOR3 ResolvePosition(const D3DXVECTOR3& targetPosition,
                                const D3DXVECTOR3& desiredCameraPosition) const;

private:
    Settings m_settings;
};
}
