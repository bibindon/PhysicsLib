#include "PhysicsLib.h"

namespace PhysicsLib
{
CameraMover::CameraMover()
{
}

D3DXVECTOR3 CameraMover::ResolvePosition(const D3DXVECTOR3& targetPosition,
                                         const D3DXVECTOR3& desiredCameraPosition) const
{
    UNREFERENCED_PARAMETER(targetPosition);
    return desiredCameraPosition;
}
}
