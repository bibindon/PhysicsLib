#pragma once

#include <d3dx9.h>

namespace PhysicsLib
{

// 指定方向へ一定時間射出する状態を管理する。
class DashBooster
{
public:
    DashBooster();

    // 指定方向へ指定速度で射出を開始する。
    void Activate(const D3DXVECTOR3& direction, float speed, float duration);

    // 毎フレームの更新。ブースト中なら outVelocity に速度を設定して true を返す。
    bool Update(float deltaSeconds, D3DXVECTOR3* outVelocity);

    bool IsActive() const;
    void Deactivate();
    float GetRemainingTime() const;

private:
    bool m_active;
    float m_timer;
    float m_speed;
    D3DXVECTOR3 m_direction;
};

}
