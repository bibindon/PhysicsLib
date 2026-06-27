#include "DashBooster.h"

#include <stdexcept>

namespace PhysicsLib
{

DashBooster::DashBooster()
    : m_active(false)
    , m_timer(0.0f)
    , m_speed(0.0f)
    , m_direction(0.0f, 0.0f, 0.0f)
{
}

void DashBooster::Activate(const D3DXVECTOR3& direction, float speed, float duration)
{
    if (speed < 0.0f)
    {
        throw std::out_of_range("DashBooster speed must not be negative.");
    }

    if (duration <= 0.0f)
    {
        throw std::out_of_range("DashBooster duration must be positive.");
    }

    m_direction = direction;
    const float directionLength = D3DXVec3Length(&m_direction);
    if (directionLength <= 0.0001f)
    {
        throw std::out_of_range("DashBooster direction must not be zero-length.");
    }

    D3DXVec3Normalize(&m_direction, &m_direction);
    m_speed = speed;
    m_timer = duration;
    m_active = true;
}

bool DashBooster::Update(float deltaSeconds, D3DXVECTOR3* outVelocity)
{
    if (outVelocity == nullptr)
    {
        return false;
    }

    if (!m_active)
    {
        return false;
    }

    m_timer -= deltaSeconds;
    if (m_timer <= 0.0f)
    {
        m_active = false;
        m_timer = 0.0f;
        *outVelocity = m_direction * m_speed;
        return false;
    }

    *outVelocity = m_direction * m_speed;
    return true;
}

bool DashBooster::IsActive() const
{
    return m_active;
}

void DashBooster::Deactivate()
{
    m_active = false;
    m_timer = 0.0f;
}

float DashBooster::GetRemainingTime() const
{
    if (!m_active)
    {
        return 0.0f;
    }

    return m_timer;
}

}
