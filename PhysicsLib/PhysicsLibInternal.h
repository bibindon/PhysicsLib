#pragma once

#include <windows.h>
#include "PhysicsLib.h"

namespace PhysicsLib
{
class SettingsState
{
public:
    static bool IsDoubleJumpEnabled();
    static void SetDoubleJumpEnabled(bool enabled);
    static bool IsInfiniteJumpEnabled();
    static void SetInfiniteJumpEnabled(bool enabled);
    static bool IsGravityEnabled();
    static void SetGravityEnabled(bool enabled);
    static bool IsInertiaEnabled();
    static void SetInertiaEnabled(bool enabled);
    static bool IsSlideEnabled();
    static void SetSlideEnabled(bool enabled);
    static bool IsSlideCheckEnabled();
    static void SetSlideCheckEnabled(bool enabled);
    static bool IsTangentMoveEnabled();
    static void SetTangentMoveEnabled(bool enabled);
    static bool IsAirMoveEnabled();
    static void SetAirMoveEnabled(bool enabled);
    static bool IsOptimizationEnabled();
    static void SetOptimizationEnabled(bool enabled);
    static bool IsMovingFloorEnabled();
    static void SetMovingFloorEnabled(bool enabled);
    static bool IsCameraAutoMoveEnabled();
    static void SetCameraAutoMoveEnabled(bool enabled);
    static bool IsFocusModeEnabled();
    static void SetFocusModeEnabled(bool enabled);
    static bool IsContactEnabled();
    static void SetContactEnabled(bool enabled);
    static bool IsSurfaceContactEnabled();
    static void SetSurfaceContactEnabled(bool enabled);
    static PhysicsLib::ShapeType GetShapeType();
    static void SetShapeType(PhysicsLib::ShapeType shapeType);
};

class SettingsDialog
{
public:
    static LRESULT CALLBACK Proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    static void Destroy();
};
}
