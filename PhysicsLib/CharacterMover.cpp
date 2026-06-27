#include "PhysicsLib.h"

#include "PhysicsLibInternal.h"

#include <stdexcept>

namespace PhysicsLib
{
namespace
{
constexpr float kDeltaSeconds = 1.0f / 60.0f;
constexpr float kPseudoInertiaDuration = 0.5f;
}

CharacterMover::CharacterMover()
    : m_position(0.0f, 0.0f, 0.0f),
      m_velocity(0.0f, 0.0f, 0.0f),
      m_groundNormal(0.0f, 1.0f, 0.0f),
      m_isGrounded(true),
      m_isTouchingWall(false),
      m_isCrushed(false),
      m_supportObjectId(-1),
      m_remainingAirJumps(0),
      m_remainingAirDashes(1),
      m_dashTimer(0.0f),
      m_isDashing(false),
      m_hasPendingDashRequest(false),
      m_dashDirection(0.0f, 0.0f, -1.0f),
      m_pendingDashDirection(0.0f, 0.0f, 0.0f),
      m_isPseudoInertiaStopping(false),
      m_isPseudoInertiaFullStopping(false),
      m_pseudoInertiaStopAcceleration(0.0f),
      m_chargeJumpTimer(0.0f),
      m_isChargingJump(false),
      m_chargeJumpWasGroundJump(false),
      m_landingTimer(0.0f),
      m_isInLanding(false),
      m_didJump(false),
      m_justJumped(false)
{
}

CharacterMover::CharacterMover(const D3DXVECTOR3& position)
    : m_position(position),
      m_velocity(0.0f, 0.0f, 0.0f),
      m_groundNormal(0.0f, 1.0f, 0.0f),
      m_isGrounded(true),
      m_isTouchingWall(false),
      m_isCrushed(false),
      m_supportObjectId(-1),
      m_remainingAirJumps(0),
      m_remainingAirDashes(1),
      m_dashTimer(0.0f),
      m_isDashing(false),
      m_hasPendingDashRequest(false),
      m_dashDirection(0.0f, 0.0f, -1.0f),
      m_pendingDashDirection(0.0f, 0.0f, 0.0f),
      m_isPseudoInertiaStopping(false),
      m_isPseudoInertiaFullStopping(false),
      m_pseudoInertiaStopAcceleration(0.0f),
      m_chargeJumpTimer(0.0f),
      m_isChargingJump(false),
      m_chargeJumpWasGroundJump(false),
      m_landingTimer(0.0f),
      m_isInLanding(false),
      m_didJump(false),
      m_justJumped(false)
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
    m_groundNormal = D3DXVECTOR3(0.0f, 1.0f, 0.0f);
    m_isGrounded = true;
    m_isTouchingWall = false;
    m_isCrushed = false;
    m_supportObjectId = -1;
    m_remainingAirJumps = 1;
    m_remainingAirDashes = 1;
    m_dashTimer = 0.0f;
    m_isDashing = false;
    m_hasPendingDashRequest = false;
    m_dashDirection = D3DXVECTOR3(0.0f, 0.0f, -1.0f);
    m_pendingDashDirection = D3DXVECTOR3(0.0f, 0.0f, 0.0f);
    m_isPseudoInertiaStopping = false;
    m_isPseudoInertiaFullStopping = false;
    m_pseudoInertiaStopAcceleration = 0.0f;
    m_chargeJumpTimer = 0.0f;
    m_isChargingJump = false;
    m_chargeJumpWasGroundJump = false;
    m_landingTimer = 0.0f;
    m_isInLanding = false;
    m_didJump = false;
    m_justJumped = false;
    m_debugInfo = DebugInfo();
    m_booster.Deactivate();
}

void CharacterMover::SetPosition(const D3DXVECTOR3& position)
{
    m_position = position;
}

D3DXVECTOR3 CharacterMover::GetPosition() const
{
    return m_position;
}

void CharacterMover::RequestDash(const D3DXVECTOR3& direction)
{
    D3DXVECTOR3 horizontalDirection(direction.x, 0.0f, direction.z);
    if (D3DXVec3LengthSq(&horizontalDirection) <= 0.0001f)
    {
        return;
    }

    D3DXVec3Normalize(&horizontalDirection, &horizontalDirection);
    m_pendingDashDirection = horizontalDirection;
    m_hasPendingDashRequest = true;
}

void CharacterMover::ApplyUpwardVelocity(const float upwardVelocity)
{
    if (upwardVelocity <= 0.0f)
    {
        return;
    }

    m_velocity.y = upwardVelocity;
    m_isGrounded = false;
    m_supportObjectId = -1;
    m_isChargingJump = false;
    m_isInLanding = false;
    m_didJump = true;
    m_justJumped = true;
}

void CharacterMover::ApplyDashBooster(const D3DXVECTOR3& direction, float speed, float duration)
{
    m_booster.Activate(direction, speed, duration);
    m_isDashing = false;
    m_hasPendingDashRequest = false;
}

bool CharacterMover::IsBoosted() const
{
    return m_booster.IsActive();
}

D3DXVECTOR3 CharacterMover::GetVelocity() const
{
    return m_velocity;
}

bool CharacterMover::IsGrounded() const
{
    return m_isGrounded && !m_isInLanding;
}

bool CharacterMover::IsTouchingWall() const
{
    return m_isTouchingWall;
}

bool CharacterMover::IsChargingJump() const
{
    return m_isChargingJump;
}

bool CharacterMover::IsJumping() const
{
    return m_didJump && (m_isChargingJump || !IsGrounded());
}

bool CharacterMover::IsCrushed() const
{
    return m_isCrushed;
}

bool CharacterMover::IsDashing() const
{
    return m_isDashing;
}

bool CharacterMover::JustJumped() const
{
    return m_justJumped;
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

void CharacterMover::MoveVelocityToward(D3DXVECTOR3* velocity,
                                         const D3DXVECTOR3& targetVelocity,
                                         float acceleration)
{
    if (velocity == nullptr || acceleration <= 0.0f)
    {
        return;
    }

    D3DXVECTOR3 difference = targetVelocity - *velocity;
    const float differenceLength = D3DXVec3Length(&difference);
    if (differenceLength <= 0.0001f)
    {
        *velocity = targetVelocity;
        return;
    }

    const float maxDelta = acceleration * kDeltaSeconds;
    if (differenceLength <= maxDelta)
    {
        *velocity = targetVelocity;
        return;
    }

    difference /= differenceLength;
    difference *= maxDelta;
    *velocity += difference;
}

D3DXVECTOR3 CharacterMover::ProjectVectorOnPlane(const D3DXVECTOR3& vector,
                                                 const D3DXVECTOR3& normal)
{
    D3DXVECTOR3 normalizedNormal = normal;
    if (D3DXVec3Length(&normalizedNormal) <= 0.0001f)
    {
        return vector;
    }

    D3DXVec3Normalize(&normalizedNormal, &normalizedNormal);
    const float normalAmount = D3DXVec3Dot(&vector, &normalizedNormal);
    return vector - normalizedNormal * normalAmount;
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

    m_justJumped = false;
    m_isCrushed = false;
    bool crushedThisFrame = false;

    const PhysicsLib::ShapeType shapeType = SettingsState::GetShapeType();
    float radius = SettingsState::GetRadius();
    float height = m_settings.height;
    if (shapeType == PhysicsLib::ShapeType::Cylinder)
    {
        radius = SettingsState::GetCylinderRadius();
        height = SettingsState::GetCylinderHeight();
    }
    if (shapeType == PhysicsLib::ShapeType::Cuboid)
    {
        const D3DXVECTOR3 inputMove(inputDirection.x, 0.0f, inputDirection.z);
        const float inputLength = D3DXVec3Length(&inputMove);
        if (inputLength > 0.0001f)
        {
            const float facingYaw = atan2f(inputMove.x, inputMove.z);
            SettingsState::SetPlayerFacingYaw(facingYaw);
        }
    }

    if (m_isGrounded && SettingsState::IsMovingFloorEnabled() && m_supportObjectId >= 0)
    {
        const PhysicsLib::Transform supportTransform = PhysicsLib::GetTransform(m_supportObjectId);
        const D3DXVECTOR3 supportVelocity = supportTransform.velocity;
        const D3DXVECTOR3 horizontalSupportVelocity(supportVelocity.x, 0.0f, supportVelocity.z);
        if (D3DXVec3Length(&horizontalSupportVelocity) > 0.0001f)
        {
            m_position += horizontalSupportVelocity * kDeltaSeconds;
        }
        if (supportVelocity.y > 0.0001f)
        {
            const D3DXVECTOR3 collisionPosition = m_position + D3DXVECTOR3(0.0f, m_settings.collisionCenterY, 0.0f);
            D3DXVECTOR3 carriedCollisionPosition = collisionPosition;
            D3DXVECTOR3 unusedNextMoveVector = supportVelocity;
            bool carriedCrushed = false;
            PhysicsLib::CheckCollide(collisionPosition,
                                     D3DXVECTOR3(0.0f, supportVelocity.y, 0.0f),
                                     shapeType,
                                     &carriedCollisionPosition,
                                     &unusedNextMoveVector,
                                     nullptr,
                                     nullptr,
                                     radius,
                                     height,
                                     nullptr,
                                     nullptr,
                                     nullptr,
                                     nullptr,
                                     nullptr,
                                     nullptr,
                                     nullptr,
                                     &carriedCrushed);
            m_position = carriedCollisionPosition - D3DXVECTOR3(0.0f, m_settings.collisionCenterY, 0.0f);
            if (carriedCrushed)
            {
                crushedThisFrame = true;
            }
        }
        else if (supportVelocity.y < -0.0001f)
        {
            m_position.y += supportVelocity.y * kDeltaSeconds;
        }
    }

    if (m_booster.IsActive())
    {
        m_isPseudoInertiaStopping = false;
        m_isPseudoInertiaFullStopping = false;
        m_pseudoInertiaStopAcceleration = 0.0f;
        D3DXVECTOR3 boosterVelocity(0.0f, 0.0f, 0.0f);
        const bool boosterStillActive = m_booster.Update(kDeltaSeconds, &boosterVelocity);
        m_velocity = boosterVelocity;
        if (!boosterStillActive && SettingsState::GetInertiaMode() == InertiaMode::PseudoInertia)
        {
            m_isPseudoInertiaStopping = true;
            m_isPseudoInertiaFullStopping = true;
            m_pseudoInertiaStopAcceleration = D3DXVec3Length(&m_velocity) / kPseudoInertiaDuration;
        }
        m_isGrounded = false;
        m_isChargingJump = false;
        m_isInLanding = false;
        m_supportObjectId = -1;
    }

    if (!m_isDashing && !m_booster.IsActive() && m_hasPendingDashRequest)
    {
        bool canStartDash = false;
        if (m_isGrounded && !m_isInLanding && SettingsState::IsGroundDashEnabled())
        {
            canStartDash = true;
        }
        else if (!m_isGrounded && SettingsState::IsAirDashEnabled() && m_remainingAirDashes > 0)
        {
            canStartDash = true;
            --m_remainingAirDashes;
        }

        if (canStartDash)
        {
            m_isPseudoInertiaStopping = false;
            m_isPseudoInertiaFullStopping = false;
            m_pseudoInertiaStopAcceleration = 0.0f;
            m_isDashing = true;
            m_dashTimer = SettingsState::GetDashDuration();
            m_dashDirection = m_pendingDashDirection;
            m_isChargingJump = false;
            m_isInLanding = false;
            m_velocity = m_dashDirection * SettingsState::GetDashSpeed();
            m_velocity.y = 0.0f;
            m_supportObjectId = -1;
        }

        m_hasPendingDashRequest = false;
    }

    if (m_isDashing)
    {
        m_dashTimer -= kDeltaSeconds;
        if (m_dashTimer <= 0.0f)
        {
            m_isDashing = false;
            m_dashTimer = 0.0f;
        }
        else
        {
            m_velocity = m_dashDirection * SettingsState::GetDashSpeed();
            m_velocity.y = 0.0f;
        }
    }

    const bool canChangeMoveDirection = m_isGrounded || SettingsState::IsAirMoveEnabled();
    D3DXVECTOR3 inputMove(inputDirection.x, 0.0f, inputDirection.z);
    if (m_isDashing)
    {
        inputMove = D3DXVECTOR3(0.0f, 0.0f, 0.0f);
    }
    else if (!canChangeMoveDirection)
    {
        inputMove = D3DXVECTOR3(0.0f, 0.0f, 0.0f);
    }
    else if (D3DXVec3Length(&inputMove) > 0.0001f)
    {
        if (SettingsState::IsTangentMoveEnabled() && m_isGrounded)
        {
            inputMove = ProjectVectorOnPlane(inputMove, m_groundNormal);
        }

        if (D3DXVec3Length(&inputMove) > 0.0001f)
        {
            D3DXVec3Normalize(&inputMove, &inputMove);
        }
        inputMove *= m_settings.moveSpeed;
    }

    const InertiaMode inertiaMode = SettingsState::GetInertiaMode();
    if (!m_isDashing && !m_booster.IsActive() && inertiaMode != InertiaMode::None)
    {
        float moveAcceleration = m_settings.groundAcceleration;
        float stopAcceleration = m_settings.groundAcceleration;
        if (inertiaMode == InertiaMode::PseudoInertia)
        {
            moveAcceleration = m_settings.moveSpeed / kPseudoInertiaDuration;
            stopAcceleration = m_settings.moveSpeed / kPseudoInertiaDuration;
        }

        if (canChangeMoveDirection && D3DXVec3Length(&inputMove) > 0.0001f)
        {
            m_isPseudoInertiaStopping = false;
            m_isPseudoInertiaFullStopping = false;
            m_pseudoInertiaStopAcceleration = 0.0f;
            if (SettingsState::IsTangentMoveEnabled() && m_isGrounded)
            {
                MoveVelocityToward(&m_velocity, inputMove, moveAcceleration);
            }
            else
            {
                MoveHorizontalVelocityToward(&m_velocity, inputMove, moveAcceleration);
            }
        }
        else
        {
            if (inertiaMode == InertiaMode::PseudoInertia && m_isPseudoInertiaFullStopping)
            {
                stopAcceleration = m_pseudoInertiaStopAcceleration;
                MoveVelocityToward(&m_velocity,
                                   D3DXVECTOR3(0.0f, 0.0f, 0.0f),
                                   stopAcceleration);
                if (D3DXVec3Length(&m_velocity) <= 0.0001f)
                {
                    m_isPseudoInertiaStopping = false;
                    m_isPseudoInertiaFullStopping = false;
                    m_pseudoInertiaStopAcceleration = 0.0f;
                }
            }
            else if (SettingsState::IsTangentMoveEnabled() && m_isGrounded)
            {
                if (inertiaMode == InertiaMode::PseudoInertia)
                {
                    if (!m_isPseudoInertiaStopping)
                    {
                        m_pseudoInertiaStopAcceleration = D3DXVec3Length(&m_velocity) / kPseudoInertiaDuration;
                        m_isPseudoInertiaStopping = true;
                    }
                    stopAcceleration = m_pseudoInertiaStopAcceleration;
                }
                MoveVelocityToward(&m_velocity,
                                   D3DXVECTOR3(0.0f, 0.0f, 0.0f),
                                   stopAcceleration);
                if (inertiaMode == InertiaMode::PseudoInertia &&
                    D3DXVec3Length(&m_velocity) <= 0.0001f)
                {
                    m_isPseudoInertiaStopping = false;
                    m_isPseudoInertiaFullStopping = false;
                    m_pseudoInertiaStopAcceleration = 0.0f;
                }
            }
            else
            {
                if (inertiaMode == InertiaMode::PseudoInertia)
                {
                    if (!m_isPseudoInertiaStopping)
                    {
                        const D3DXVECTOR3 horizontalVelocity(m_velocity.x, 0.0f, m_velocity.z);
                        m_pseudoInertiaStopAcceleration = D3DXVec3Length(&horizontalVelocity) / kPseudoInertiaDuration;
                        m_isPseudoInertiaStopping = true;
                    }
                    stopAcceleration = m_pseudoInertiaStopAcceleration;
                }
                MoveHorizontalVelocityToward(&m_velocity,
                                             D3DXVECTOR3(0.0f, 0.0f, 0.0f),
                                             stopAcceleration);
                if (inertiaMode == InertiaMode::PseudoInertia)
                {
                    const D3DXVECTOR3 horizontalVelocity(m_velocity.x, 0.0f, m_velocity.z);
                    if (D3DXVec3Length(&horizontalVelocity) <= 0.0001f)
                    {
                        m_isPseudoInertiaStopping = false;
                        m_isPseudoInertiaFullStopping = false;
                        m_pseudoInertiaStopAcceleration = 0.0f;
                    }
                }
            }
        }
        if (inertiaMode == InertiaMode::Legacy &&
            m_isGrounded &&
            D3DXVec3Length(&inputMove) < 0.0001f)
        {
            const float strength = SettingsState::GetInertiaStrength();
            m_velocity.x *= strength;
            m_velocity.z *= strength;
        }
    }
    else if (!m_isDashing && !m_booster.IsActive())
    {
        if (canChangeMoveDirection)
        {
            m_velocity.x = inputMove.x;
            if (SettingsState::IsTangentMoveEnabled() && m_isGrounded)
            {
                m_velocity.y = inputMove.y;
            }
            m_velocity.z = inputMove.z;
        }
    }

    const bool isGroundJump = jump && m_isGrounded && !m_isInLanding;
    if (m_isInLanding)
    {
        m_landingTimer -= kDeltaSeconds;
        if (m_landingTimer <= 0.0f)
        {
            m_isInLanding = false;
            m_didJump = false;
        }
    }
    if (!m_isDashing && !m_booster.IsActive() && m_isChargingJump)
    {
        m_chargeJumpTimer -= kDeltaSeconds;
        if (m_chargeJumpTimer <= 0.0f)
        {
            m_isChargingJump = false;
            if (m_chargeJumpWasGroundJump)
            {
                m_remainingAirJumps = 1;
            }
            m_velocity.y = m_settings.jumpVelocity;
            m_isGrounded = false;
        }
    }
    else if (!m_isDashing && !m_booster.IsActive())
    {
        bool canJump = false;
        if (jump && m_isGrounded && !m_isInLanding)
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
            if (SettingsState::IsChargeJumpEnabled())
            {
                m_isChargingJump = true;
                m_chargeJumpTimer = 0.5f;
                m_chargeJumpWasGroundJump = isGroundJump;
                m_didJump = true;
                m_justJumped = true;
            }
            else
            {
                if (isGroundJump)
                {
                    m_remainingAirJumps = 1;
                }
                m_velocity.y = m_settings.jumpVelocity;
                m_isGrounded = false;
                m_didJump = true;
                m_justJumped = true;
            }
        }
    }

    if (m_isDashing)
    {
        m_velocity.y = 0.0f;
    }
    else if (m_booster.IsActive())
    {
    }
    else if (m_isPseudoInertiaFullStopping)
    {
    }
    else if (SettingsState::IsGravityEnabled())
    {
        m_velocity.y -= 9.8f * kDeltaSeconds;
    }
    else
    {
        m_velocity.y = 0.0f;
    }

    const D3DXVECTOR3 collisionPosition = m_position + D3DXVECTOR3(0.0f, m_settings.collisionCenterY, 0.0f);
    D3DXVECTOR3 nextCollisionPosition = collisionPosition;
    D3DXVECTOR3 nextVelocity = m_velocity;
    float lastNormalMove = 0.0f;
    D3DXVECTOR3 lastHitNormal(0.0f, 0.0f, 0.0f);
    float lastHitDistance = 0.0f;
    D3DXVECTOR3 lastSlideMove(0.0f, 0.0f, 0.0f);
    int slideCount = 0;
    int supportObjectId = -1;
    bool crushed = false;
    const bool collided = PhysicsLib::CheckCollide(collisionPosition,
                                                    m_velocity,
                                                    shapeType,
                                                   &nextCollisionPosition,
                                                   &nextVelocity,
                                                   outPassThroughIds,
                                                   outSolidIds,
                                                    radius,
                                                    height,
                                                   &lastNormalMove,
                                                   &lastHitNormal,
                                                   &lastHitDistance,
                                                   &lastSlideMove,
                                                   &slideCount,
                                                   &supportObjectId,
                                                   nullptr,
                                                   &crushed);
    m_position = nextCollisionPosition - D3DXVECTOR3(0.0f, m_settings.collisionCenterY, 0.0f);
    m_velocity = nextVelocity;
    m_isCrushed = crushedThisFrame || crushed;
    const bool wasInAir = !m_isGrounded;
    if (SettingsState::IsGravityEnabled())
    {
        m_isGrounded = false;
        m_groundNormal = D3DXVECTOR3(0.0f, 1.0f, 0.0f);
    }
    if (collided && lastHitNormal.y > 0.0f)
    {
        if (wasInAir)
        {
            if (SettingsState::IsLandingStiffnessEnabled())
            {
                m_isInLanding = true;
                m_landingTimer = 0.5f;
            }
            else
            {
                m_didJump = false;
            }
            m_isChargingJump = false;
        }
        m_isGrounded = true;
        m_remainingAirJumps = 1;
        m_remainingAirDashes = 1;
        m_supportObjectId = supportObjectId;
        m_groundNormal = lastHitNormal;
        D3DXVec3Normalize(&m_groundNormal, &m_groundNormal);
    }
    m_isTouchingWall = false;
    if (!m_isGrounded)
    {
        m_supportObjectId = -1;
    }
    m_debugInfo = DebugInfo();
    if (collided)
    {
        m_debugInfo.collideCheckCount = 1;
        m_debugInfo.hitCount = 1;
    }
    m_debugInfo.lastNormalMove = lastNormalMove;
    m_debugInfo.lastHitNormal = lastHitNormal;
    m_debugInfo.lastHitDistance = lastHitDistance;
    m_debugInfo.lastSlideMove = lastSlideMove;
    m_debugInfo.slideCount = slideCount;
    m_debugInfo.crushed = m_isCrushed;
    return collided;
}
}
