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
    static float GetRadius();
    static void SetRadius(float radius);
    static float GetCylinderRadius();
    static void SetCylinderRadius(float cylinderRadius);
    static float GetCylinderHeight();
    static void SetCylinderHeight(float cylinderHeight);
    static float GetCuboidWidth();
    static void SetCuboidWidth(float cuboidWidth);
    static float GetCuboidHeight();
    static void SetCuboidHeight(float cuboidHeight);
    static float GetCuboidDepth();
    static void SetCuboidDepth(float cuboidDepth);
    static float GetCuboidRotX();
    static void SetCuboidRotX(float cuboidRotX);
    static float GetCuboidRotY();
    static void SetCuboidRotY(float cuboidRotY);
    static float GetCuboidRotZ();
    static void SetCuboidRotZ(float cuboidRotZ);
    static float GetPlayerFacingYaw();
    static void SetPlayerFacingYaw(float playerFacingYaw);
    static float GetInertiaStrength();
    static void SetInertiaStrength(float strength);
    static float GetWalkSpeed();
    static void SetWalkSpeed(float speed);
    static float GetDashSpeed();
    static void SetDashSpeed(float speed);
};

class SettingsDialog
{
public:
    static LRESULT CALLBACK Proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    static void Destroy();
};
}
