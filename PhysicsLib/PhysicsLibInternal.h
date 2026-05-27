#pragma once

#include <windows.h>

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
    static bool IsContactEnabled();
    static void SetContactEnabled(bool enabled);
    static bool IsSurfaceContactEnabled();
    static void SetSurfaceContactEnabled(bool enabled);
};

class SettingsDialog
{
public:
    static LRESULT CALLBACK Proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    static void Destroy();
};
}
