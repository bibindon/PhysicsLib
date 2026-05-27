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
const int kInertiaCheckboxId = kSettingsCheckboxStartId + 6;
const int kContactCheckboxId = kSettingsCheckboxStartId + 7;
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
};

LRESULT CALLBACK SettingsDialogProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
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
            SetGravityEnabled(checkState == BST_CHECKED);
            return 0;
        }

        if (LOWORD(wParam) == kDoubleJumpCheckboxId && HIWORD(wParam) == BN_CLICKED)
        {
            const LRESULT checkState = SendMessage(reinterpret_cast<HWND>(lParam), BM_GETCHECK, 0, 0);
            SetDoubleJumpEnabled(checkState == BST_CHECKED);
            return 0;
        }

        if (LOWORD(wParam) == kInfiniteJumpCheckboxId && HIWORD(wParam) == BN_CLICKED)
        {
            const LRESULT checkState = SendMessage(reinterpret_cast<HWND>(lParam), BM_GETCHECK, 0, 0);
            SetInfiniteJumpEnabled(checkState == BST_CHECKED);
            return 0;
        }

        if (LOWORD(wParam) == kInertiaCheckboxId && HIWORD(wParam) == BN_CLICKED)
        {
            const LRESULT checkState = SendMessage(reinterpret_cast<HWND>(lParam), BM_GETCHECK, 0, 0);
            SetInertiaEnabled(checkState == BST_CHECKED);
            return 0;
        }

        if (LOWORD(wParam) == kContactCheckboxId && HIWORD(wParam) == BN_CLICKED)
        {
            const LRESULT checkState = SendMessage(reinterpret_cast<HWND>(lParam), BM_GETCHECK, 0, 0);
            SetContactEnabled(checkState == BST_CHECKED);
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
    windowClass.lpfnWndProc = SettingsDialogProc;
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
                                      360,
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
            SendMessage(checkbox, BM_SETCHECK, IsGravityEnabled() ? BST_CHECKED : BST_UNCHECKED, 0);
        }
        else if (kSettingsCheckboxStartId + i == kDoubleJumpCheckboxId)
        {
            SendMessage(checkbox, BM_SETCHECK, IsDoubleJumpEnabled() ? BST_CHECKED : BST_UNCHECKED, 0);
        }
        else if (kSettingsCheckboxStartId + i == kInfiniteJumpCheckboxId)
        {
            SendMessage(checkbox, BM_SETCHECK, IsInfiniteJumpEnabled() ? BST_CHECKED : BST_UNCHECKED, 0);
        }
        else if (kSettingsCheckboxStartId + i == kInertiaCheckboxId)
        {
            SendMessage(checkbox, BM_SETCHECK, IsInertiaEnabled() ? BST_CHECKED : BST_UNCHECKED, 0);
        }
        else if (kSettingsCheckboxStartId + i == kContactCheckboxId)
        {
            SendMessage(checkbox, BM_SETCHECK, IsContactEnabled() ? BST_CHECKED : BST_UNCHECKED, 0);
        }
    }

    CreateWindow(_T("BUTTON"),
                 _T("リセット"),
                 WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                 16,
                 270,
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

void DestroySettingsDialog()
{
    if (g_settingsDialog != NULL)
    {
        DestroyWindow(g_settingsDialog);
        g_settingsDialog = NULL;
    }
}
}
