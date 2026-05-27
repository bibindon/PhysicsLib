#include "PhysicsLib.h"

#include "PhysicsLibInternal.h"

#include <stdexcept>

namespace PhysicsLib
{
namespace
{
constexpr float kDeltaSeconds = 1.0f / 60.0f;
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

void CharacterMover::MoveHorizontalVelocityToward(D3DXVECTOR3* velocity,
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

    if (SettingsState::IsInertiaEnabled())
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
    else if (jump && SettingsState::IsInfiniteJumpEnabled())
    {
        canJump = true;
    }
    else if (jump && SettingsState::IsDoubleJumpEnabled() && m_remainingAirJumps > 0)
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

    if (SettingsState::IsGravityEnabled())
    {
        m_velocity.y -= 9.8f * kDeltaSeconds;
    }
    else
    {
        m_velocity.y = 0.0f;
    }

    D3DXVECTOR3 nextPosition = m_position;
    D3DXVECTOR3 nextVelocity = m_velocity;
    float lastNormalMove = 0.0f;
    const D3DXVECTOR3 attemptedVelocity = m_velocity;
    const bool collided = PhysicsLib::CheckCollide(m_position,
                                                   m_velocity,
                                                   m_settings.shapeType,
                                                   &nextPosition,
                                                   &nextVelocity,
                                                   outPassThroughIds,
                                                   outSolidIds,
                                                   &lastNormalMove,
                                                   m_settings.radius,
                                                   m_settings.height);
    m_position = nextPosition;
    m_velocity = nextVelocity;
    if (SettingsState::IsGravityEnabled())
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
    m_debugInfo.lastNormalMove = lastNormalMove;
    return collided;
}
}
