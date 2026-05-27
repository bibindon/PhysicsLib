#include "PhysicsLib.h"
#include "PhysicsLibInternal.h"

#include <windows.h>

namespace PhysicsLib
{
namespace
{
HWND g_settingsDialog = NULL;
void (*g_resetCallback)() = nullptr;

const int kSettingsCheckboxStartId = 4100;
const int kDoubleJumpCheckboxId = kSettingsCheckboxStartId + 0;
const int kInfiniteJumpCheckboxId = kSettingsCheckboxStartId + 1;
const int kGravityCheckboxId = kSettingsCheckboxStartId + 2;
const int kSlideCheckboxId = kSettingsCheckboxStartId + 3;
const int kTangentMoveCheckboxId = kSettingsCheckboxStartId + 9;
const int kInertiaCheckboxId = kSettingsCheckboxStartId + 6;
const int kContactCheckboxId = kSettingsCheckboxStartId + 7;
const int kSurfaceContactCheckboxId = kSettingsCheckboxStartId + 8;
const int kSettingsResetButtonId = 4200;

const TCHAR* kSettingsCheckboxLabels[] =
{
    _T("2段ジャンプ"),
    _T("多段ジャンプ"),
    _T("重力"),
    _T("スライド"),
    _T("高速化"),
    _T("初期化"),
    _T("慣性"),
    _T("接触判定"),
    _T("接面判定"),
    _T("接平面移動"),
};

}

LRESULT CALLBACK SettingsDialog::Proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_COMMAND)
    {
        if (LOWORD(wParam) == kSettingsResetButtonId && HIWORD(wParam) == BN_CLICKED)
        {
            if (g_resetCallback != nullptr)
            {
                g_resetCallback();
            }
            return 0;
        }

        if (LOWORD(wParam) == kGravityCheckboxId && HIWORD(wParam) == BN_CLICKED)
        {
            const LRESULT checkState = SendMessage(reinterpret_cast<HWND>(lParam), BM_GETCHECK, 0, 0);
            SettingsState::SetGravityEnabled(checkState == BST_CHECKED);
            return 0;
        }

        if (LOWORD(wParam) == kDoubleJumpCheckboxId && HIWORD(wParam) == BN_CLICKED)
        {
            const LRESULT checkState = SendMessage(reinterpret_cast<HWND>(lParam), BM_GETCHECK, 0, 0);
            SettingsState::SetDoubleJumpEnabled(checkState == BST_CHECKED);
            return 0;
        }

        if (LOWORD(wParam) == kInfiniteJumpCheckboxId && HIWORD(wParam) == BN_CLICKED)
        {
            const LRESULT checkState = SendMessage(reinterpret_cast<HWND>(lParam), BM_GETCHECK, 0, 0);
            SettingsState::SetInfiniteJumpEnabled(checkState == BST_CHECKED);
            return 0;
        }

        if (LOWORD(wParam) == kInertiaCheckboxId && HIWORD(wParam) == BN_CLICKED)
        {
            const LRESULT checkState = SendMessage(reinterpret_cast<HWND>(lParam), BM_GETCHECK, 0, 0);
            SettingsState::SetInertiaEnabled(checkState == BST_CHECKED);
            return 0;
        }

        if (LOWORD(wParam) == kSlideCheckboxId && HIWORD(wParam) == BN_CLICKED)
        {
            const LRESULT checkState = SendMessage(reinterpret_cast<HWND>(lParam), BM_GETCHECK, 0, 0);
            SettingsState::SetSlideEnabled(checkState == BST_CHECKED);
            return 0;
        }

        if (LOWORD(wParam) == kTangentMoveCheckboxId && HIWORD(wParam) == BN_CLICKED)
        {
            const LRESULT checkState = SendMessage(reinterpret_cast<HWND>(lParam), BM_GETCHECK, 0, 0);
            SettingsState::SetTangentMoveEnabled(checkState == BST_CHECKED);
            return 0;
        }

        if (LOWORD(wParam) == kContactCheckboxId && HIWORD(wParam) == BN_CLICKED)
        {
            const LRESULT checkState = SendMessage(reinterpret_cast<HWND>(lParam), BM_GETCHECK, 0, 0);
            SettingsState::SetContactEnabled(checkState == BST_CHECKED);
            return 0;
        }

        if (LOWORD(wParam) == kSurfaceContactCheckboxId && HIWORD(wParam) == BN_CLICKED)
        {
            const LRESULT checkState = SendMessage(reinterpret_cast<HWND>(lParam), BM_GETCHECK, 0, 0);
            SettingsState::SetSurfaceContactEnabled(checkState == BST_CHECKED);
            return 0;
        }
    }

    if (message == WM_CLOSE)
    {
        ShowWindow(window, SW_HIDE);
        return 0;
    }

    return DefWindowProc(window, message, wParam, lParam);
}

void PhysicsLib::ShowSettingsDialog(HWND ownerWindow)
{
    if (g_settingsDialog != NULL)
    {
        ShowWindow(g_settingsDialog, SW_SHOW);
        SetForegroundWindow(g_settingsDialog);
        return;
    }

    HINSTANCE instance = GetModuleHandle(NULL);
    const TCHAR* className = _T("PhysicsLibSettingsDialog");

    WNDCLASSEX windowClass = {};
    windowClass.cbSize = sizeof(WNDCLASSEX);
    windowClass.lpfnWndProc = SettingsDialog::Proc;
    windowClass.hInstance = instance;
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    windowClass.lpszClassName = className;
    windowClass.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassEx(&windowClass);

    g_settingsDialog = CreateWindowEx(WS_EX_TOOLWINDOW,
                                      className,
                                      _T("PhysicsLib Settings"),
                                      WS_CAPTION | WS_BORDER | WS_VISIBLE,
                                       40,
                                       40,
                                       340,
                                       420,
                                      ownerWindow,
                                      NULL,
                                      instance,
                                      NULL);
    if (g_settingsDialog == NULL)
    {
        return;
    }

    for (int i = 0; i < static_cast<int>(sizeof(kSettingsCheckboxLabels) / sizeof(kSettingsCheckboxLabels[0])); ++i)
    {
        HWND checkbox = CreateWindow(_T("BUTTON"),
                                     kSettingsCheckboxLabels[i],
                                     WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                     16,
                                     18 + i * 30,
                                     210,
                                     24,
                                     g_settingsDialog,
                                     reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSettingsCheckboxStartId + i)),
                                     instance,
                                     NULL);
        if (kSettingsCheckboxStartId + i == kGravityCheckboxId)
        {
            LRESULT checkState = BST_UNCHECKED;
            if (SettingsState::IsGravityEnabled())
            {
                checkState = BST_CHECKED;
            }
            SendMessage(checkbox, BM_SETCHECK, checkState, 0);
        }
        else if (kSettingsCheckboxStartId + i == kDoubleJumpCheckboxId)
        {
            LRESULT checkState = BST_UNCHECKED;
            if (SettingsState::IsDoubleJumpEnabled())
            {
                checkState = BST_CHECKED;
            }
            SendMessage(checkbox, BM_SETCHECK, checkState, 0);
        }
        else if (kSettingsCheckboxStartId + i == kInfiniteJumpCheckboxId)
        {
            LRESULT checkState = BST_UNCHECKED;
            if (SettingsState::IsInfiniteJumpEnabled())
            {
                checkState = BST_CHECKED;
            }
            SendMessage(checkbox, BM_SETCHECK, checkState, 0);
        }
        else if (kSettingsCheckboxStartId + i == kInertiaCheckboxId)
        {
            LRESULT checkState = BST_UNCHECKED;
            if (SettingsState::IsInertiaEnabled())
            {
                checkState = BST_CHECKED;
            }
            SendMessage(checkbox, BM_SETCHECK, checkState, 0);
        }
        else if (kSettingsCheckboxStartId + i == kSlideCheckboxId)
        {
            LRESULT checkState = BST_UNCHECKED;
            if (SettingsState::IsSlideEnabled())
            {
                checkState = BST_CHECKED;
            }
            SendMessage(checkbox, BM_SETCHECK, checkState, 0);
        }
        else if (kSettingsCheckboxStartId + i == kTangentMoveCheckboxId)
        {
            LRESULT checkState = BST_UNCHECKED;
            if (SettingsState::IsTangentMoveEnabled())
            {
                checkState = BST_CHECKED;
            }
            SendMessage(checkbox, BM_SETCHECK, checkState, 0);
        }
        else if (kSettingsCheckboxStartId + i == kContactCheckboxId)
        {
            LRESULT checkState = BST_UNCHECKED;
            if (SettingsState::IsContactEnabled())
            {
                checkState = BST_CHECKED;
            }
            SendMessage(checkbox, BM_SETCHECK, checkState, 0);
        }
        else if (kSettingsCheckboxStartId + i == kSurfaceContactCheckboxId)
        {
            LRESULT checkState = BST_UNCHECKED;
            if (SettingsState::IsSurfaceContactEnabled())
            {
                checkState = BST_CHECKED;
            }
            SendMessage(checkbox, BM_SETCHECK, checkState, 0);
        }
    }

    CreateWindow(_T("BUTTON"),
                 _T("リセット"),
                 WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                 16,
                 330,
                 130,
                 32,
                 g_settingsDialog,
                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSettingsResetButtonId)),
                 instance,
                 NULL);
}

void PhysicsLib::SetResetCallback(void (*callback)())
{
    g_resetCallback = callback;
}

void SettingsDialog::Destroy()
{
    if (g_settingsDialog != NULL)
    {
        DestroyWindow(g_settingsDialog);
        g_settingsDialog = NULL;
    }
}
}
