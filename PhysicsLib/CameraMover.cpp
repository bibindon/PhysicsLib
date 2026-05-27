#include "PhysicsLib.h"

#include "PhysicsLibInternal.h"

namespace PhysicsLib
{
CameraMover::CameraMover()
{
}

D3DXVECTOR3 CameraMover::ResolvePosition(const D3DXVECTOR3& targetPosition,
                                         const D3DXVECTOR3& desiredCameraPosition) const
{
    if (SettingsState::IsCameraAutoMoveEnabled())
    {
        D3DXVECTOR3 resolvedPosition = desiredCameraPosition;
        if (PhysicsLib::ResolveCameraCollision(targetPosition,
                                               desiredCameraPosition,
                                               m_settings.minimumDistance,
                                               m_settings.obstacleOffset,
                                               &resolvedPosition))
        {
            return resolvedPosition;
        }
    }

    return desiredCameraPosition;
}
}
