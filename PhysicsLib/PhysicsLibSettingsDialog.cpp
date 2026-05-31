#include "PhysicsLib.h"
#include "PhysicsLibInternal.h"

#include <windows.h>
#include <commdlg.h>

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
const int kOptimizationCheckboxId = kSettingsCheckboxStartId + 4;
const int kInertiaCheckboxId = kSettingsCheckboxStartId + 5;
const int kContactCheckboxId = kSettingsCheckboxStartId + 6;
const int kSurfaceContactCheckboxId = kSettingsCheckboxStartId + 7;
const int kTangentMoveCheckboxId = kSettingsCheckboxStartId + 8;
const int kAirMoveCheckboxId = kSettingsCheckboxStartId + 9;
const int kMovingFloorCheckboxId = kSettingsCheckboxStartId + 10;
const int kSlideCheckCheckboxId = kSettingsCheckboxStartId + 11;
const int kCameraAutoMoveCheckboxId = kSettingsCheckboxStartId + 12;
const int kFocusModeCheckboxId = kSettingsCheckboxStartId + 13;
const int kSettingsResetButtonId = 4200;
const int kFileLoadButtonId = 4210;
const int kShapeTypeComboBoxId = 4300;
const int kRadiusEditBoxId = 4400;
const int kCylinderRadiusEditBoxId = 4401;
const int kCylinderHeightEditBoxId = 4402;
const int kCuboidWidthEditBoxId = 4403;
const int kCuboidHeightEditBoxId = 4404;
const int kCuboidDepthEditBoxId = 4405;
const int kCuboidRotXEditBoxId = 4406;
const int kCuboidRotYEditBoxId = 4407;
const int kCuboidRotZEditBoxId = 4408;

const TCHAR* kSettingsCheckboxLabels[] =
{
    _T("2段ジャンプ"),
    _T("多段ジャンプ"),
    _T("重力"),
    _T("スライド"),
    _T("高速化"),
    _T("慣性"),
    _T("接触判定"),
    _T("接面判定"),
    _T("接平面移動"),
    _T("空中移動"),
    _T("移動する床"),
    _T("スライドチェック"),
    _T("カメラ自動移動"),
    _T("フォーカスモード"),
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

        if (LOWORD(wParam) == kFileLoadButtonId && HIWORD(wParam) == BN_CLICKED)
        {
            TCHAR filePath[MAX_PATH] = _T("");
            OPENFILENAME openFileName = {};
            openFileName.lStructSize = sizeof(OPENFILENAME);
            openFileName.hwndOwner = window;
            openFileName.lpstrFilter = _T("CSV Files (*.csv)\0*.csv\0All Files (*.*)\0*.*\0");
            openFileName.lpstrFile = filePath;
            openFileName.nMaxFile = MAX_PATH;
            openFileName.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
            if (GetOpenFileName(&openFileName))
            {
                PhysicsLib::LoadFromCsv(filePath);

                TCHAR movePath[MAX_PATH];
                _tcscpy_s(movePath, filePath);
                TCHAR* lastSlash = _tcsrchr(movePath, _T('\\'));
                if (lastSlash != NULL)
                {
                    *(lastSlash + 1) = _T('\0');
                }
                _tcscat_s(movePath, _T("XFileListMove.csv"));
                PhysicsLib::LoadMoveFromCsv(movePath);
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

        if (LOWORD(wParam) == kSlideCheckCheckboxId && HIWORD(wParam) == BN_CLICKED)
        {
            const LRESULT checkState = SendMessage(reinterpret_cast<HWND>(lParam), BM_GETCHECK, 0, 0);
            SettingsState::SetSlideCheckEnabled(checkState == BST_CHECKED);
            return 0;
        }

        if (LOWORD(wParam) == kOptimizationCheckboxId && HIWORD(wParam) == BN_CLICKED)
        {
            const LRESULT checkState = SendMessage(reinterpret_cast<HWND>(lParam), BM_GETCHECK, 0, 0);
            SettingsState::SetOptimizationEnabled(checkState == BST_CHECKED);
            return 0;
        }

        if (LOWORD(wParam) == kTangentMoveCheckboxId && HIWORD(wParam) == BN_CLICKED)
        {
            const LRESULT checkState = SendMessage(reinterpret_cast<HWND>(lParam), BM_GETCHECK, 0, 0);
            SettingsState::SetTangentMoveEnabled(checkState == BST_CHECKED);
            return 0;
        }

        if (LOWORD(wParam) == kAirMoveCheckboxId && HIWORD(wParam) == BN_CLICKED)
        {
            const LRESULT checkState = SendMessage(reinterpret_cast<HWND>(lParam), BM_GETCHECK, 0, 0);
            SettingsState::SetAirMoveEnabled(checkState == BST_CHECKED);
            return 0;
        }

        if (LOWORD(wParam) == kMovingFloorCheckboxId && HIWORD(wParam) == BN_CLICKED)
        {
            const LRESULT checkState = SendMessage(reinterpret_cast<HWND>(lParam), BM_GETCHECK, 0, 0);
            SettingsState::SetMovingFloorEnabled(checkState == BST_CHECKED);
            return 0;
        }

        if (LOWORD(wParam) == kCameraAutoMoveCheckboxId && HIWORD(wParam) == BN_CLICKED)
        {
            const LRESULT checkState = SendMessage(reinterpret_cast<HWND>(lParam), BM_GETCHECK, 0, 0);
            SettingsState::SetCameraAutoMoveEnabled(checkState == BST_CHECKED);
            return 0;
        }

        if (LOWORD(wParam) == kFocusModeCheckboxId && HIWORD(wParam) == BN_CLICKED)
        {
            const LRESULT checkState = SendMessage(reinterpret_cast<HWND>(lParam), BM_GETCHECK, 0, 0);
            SettingsState::SetFocusModeEnabled(checkState == BST_CHECKED);
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

        if (LOWORD(wParam) == kShapeTypeComboBoxId && HIWORD(wParam) == CBN_SELCHANGE)
        {
            const LRESULT selectedIndex = SendMessage(reinterpret_cast<HWND>(lParam), CB_GETCURSEL, 0, 0);
            if (selectedIndex >= 0 && selectedIndex <= 3)
            {
                SettingsState::SetShapeType(static_cast<PhysicsLib::ShapeType>(selectedIndex));
            }
            return 0;
        }

        if (LOWORD(wParam) == kRadiusEditBoxId && HIWORD(wParam) == EN_CHANGE)
        {
            TCHAR buffer[32];
            GetWindowText(reinterpret_cast<HWND>(lParam), buffer, 32);
            const float value = static_cast<float>(_tstof(buffer));
            if (value > 0.0f)
            {
                SettingsState::SetRadius(value);
            }
            return 0;
        }

        if (LOWORD(wParam) == kCylinderRadiusEditBoxId && HIWORD(wParam) == EN_CHANGE)
        {
            TCHAR buffer[32];
            GetWindowText(reinterpret_cast<HWND>(lParam), buffer, 32);
            const float value = static_cast<float>(_tstof(buffer));
            if (value > 0.0f)
            {
                SettingsState::SetCylinderRadius(value);
            }
            return 0;
        }

        if (LOWORD(wParam) == kCylinderHeightEditBoxId && HIWORD(wParam) == EN_CHANGE)
        {
            TCHAR buffer[32];
            GetWindowText(reinterpret_cast<HWND>(lParam), buffer, 32);
            const float value = static_cast<float>(_tstof(buffer));
            if (value > 0.0f)
            {
                SettingsState::SetCylinderHeight(value);
            }
            return 0;
        }

        if (LOWORD(wParam) == kCuboidWidthEditBoxId && HIWORD(wParam) == EN_CHANGE)
        {
            TCHAR buffer[32];
            GetWindowText(reinterpret_cast<HWND>(lParam), buffer, 32);
            const float value = static_cast<float>(_tstof(buffer));
            if (value > 0.0f)
            {
                SettingsState::SetCuboidWidth(value);
            }
            return 0;
        }

        if (LOWORD(wParam) == kCuboidHeightEditBoxId && HIWORD(wParam) == EN_CHANGE)
        {
            TCHAR buffer[32];
            GetWindowText(reinterpret_cast<HWND>(lParam), buffer, 32);
            const float value = static_cast<float>(_tstof(buffer));
            if (value > 0.0f)
            {
                SettingsState::SetCuboidHeight(value);
            }
            return 0;
        }

        if (LOWORD(wParam) == kCuboidDepthEditBoxId && HIWORD(wParam) == EN_CHANGE)
        {
            TCHAR buffer[32];
            GetWindowText(reinterpret_cast<HWND>(lParam), buffer, 32);
            const float value = static_cast<float>(_tstof(buffer));
            if (value > 0.0f)
            {
                SettingsState::SetCuboidDepth(value);
            }
            return 0;
        }

        if (LOWORD(wParam) == kCuboidRotXEditBoxId && HIWORD(wParam) == EN_CHANGE)
        {
            TCHAR buffer[32];
            GetWindowText(reinterpret_cast<HWND>(lParam), buffer, 32);
            SettingsState::SetCuboidRotX(static_cast<float>(_tstof(buffer)));
            return 0;
        }

        if (LOWORD(wParam) == kCuboidRotYEditBoxId && HIWORD(wParam) == EN_CHANGE)
        {
            TCHAR buffer[32];
            GetWindowText(reinterpret_cast<HWND>(lParam), buffer, 32);
            SettingsState::SetCuboidRotY(static_cast<float>(_tstof(buffer)));
            return 0;
        }

        if (LOWORD(wParam) == kCuboidRotZEditBoxId && HIWORD(wParam) == EN_CHANGE)
        {
            TCHAR buffer[32];
            GetWindowText(reinterpret_cast<HWND>(lParam), buffer, 32);
            SettingsState::SetCuboidRotZ(static_cast<float>(_tstof(buffer)));
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

    g_settingsDialog = CreateWindowEx(WS_EX_TOOLWINDOW | WS_EX_CONTROLPARENT,
                                       className,
                                       _T("PhysicsLib Settings"),
                                       WS_CAPTION | WS_BORDER | WS_VISIBLE,
                                       40,
                                       40,
                                       340,
                                       830,
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
        else if (kSettingsCheckboxStartId + i == kSlideCheckCheckboxId)
        {
            LRESULT checkState = BST_UNCHECKED;
            if (SettingsState::IsSlideCheckEnabled())
            {
                checkState = BST_CHECKED;
            }
            SendMessage(checkbox, BM_SETCHECK, checkState, 0);
        }
        else if (kSettingsCheckboxStartId + i == kOptimizationCheckboxId)
        {
            LRESULT checkState = BST_UNCHECKED;
            if (SettingsState::IsOptimizationEnabled())
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
        else if (kSettingsCheckboxStartId + i == kAirMoveCheckboxId)
        {
            LRESULT checkState = BST_UNCHECKED;
            if (SettingsState::IsAirMoveEnabled())
            {
                checkState = BST_CHECKED;
            }
            SendMessage(checkbox, BM_SETCHECK, checkState, 0);
        }
        else if (kSettingsCheckboxStartId + i == kMovingFloorCheckboxId)
        {
            LRESULT checkState = BST_UNCHECKED;
            if (SettingsState::IsMovingFloorEnabled())
            {
                checkState = BST_CHECKED;
            }
            SendMessage(checkbox, BM_SETCHECK, checkState, 0);
        }
        else if (kSettingsCheckboxStartId + i == kCameraAutoMoveCheckboxId)
        {
            LRESULT checkState = BST_UNCHECKED;
            if (SettingsState::IsCameraAutoMoveEnabled())
            {
                checkState = BST_CHECKED;
            }
            SendMessage(checkbox, BM_SETCHECK, checkState, 0);
        }
        else if (kSettingsCheckboxStartId + i == kFocusModeCheckboxId)
        {
            LRESULT checkState = BST_UNCHECKED;
            if (SettingsState::IsFocusModeEnabled())
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

    CreateWindow(_T("STATIC"),
                 _T("判定形状:"),
                 WS_CHILD | WS_VISIBLE,
                 16,
                 450,
                 130,
                 24,
                 g_settingsDialog,
                 NULL,
                 instance,
                 NULL);

    HWND comboBox = CreateWindow(_T("COMBOBOX"),
                                 NULL,
                                 WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                                  150,
                                 450,
                                 130,
                                 200,
                                 g_settingsDialog,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(kShapeTypeComboBoxId)),
                                 instance,
                                 NULL);
    if (comboBox != NULL)
    {
        SendMessage(comboBox, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(_T("Point")));
        SendMessage(comboBox, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(_T("Sphere")));
        SendMessage(comboBox, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(_T("Cylinder")));
        SendMessage(comboBox, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(_T("Cuboid")));
        SendMessage(comboBox, CB_SETCURSEL, static_cast<WPARAM>(SettingsState::GetShapeType()), 0);
    }

    CreateWindow(_T("STATIC"),
                 _T("球の半径:"),
                 WS_CHILD | WS_VISIBLE,
                 16,
                 480,
                 130,
                 24,
                 g_settingsDialog,
                 NULL,
                 instance,
                 NULL);

    TCHAR radiusText[32];
    _stprintf_s(radiusText, _T("%.1f"), SettingsState::GetRadius());
    CreateWindow(_T("EDIT"),
                 radiusText,
                  WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT | WS_TABSTOP,
                  150,
                  480,
                  80,
                  24,
                  g_settingsDialog,
                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(kRadiusEditBoxId)),
                 instance,
                 NULL);

    CreateWindow(_T("STATIC"),
                 _T("円柱の半径:"),
                 WS_CHILD | WS_VISIBLE,
                 16,
                 510,
                 130,
                 24,
                 g_settingsDialog,
                 NULL,
                 instance,
                 NULL);

    TCHAR cylinderRadiusText[32];
    _stprintf_s(cylinderRadiusText, _T("%.1f"), SettingsState::GetCylinderRadius());
    CreateWindow(_T("EDIT"),
                 cylinderRadiusText,
                  WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT | WS_TABSTOP,
                  150,
                  510,
                  80,
                  24,
                  g_settingsDialog,
                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCylinderRadiusEditBoxId)),
                 instance,
                 NULL);

    CreateWindow(_T("STATIC"),
                 _T("円柱の高さ:"),
                 WS_CHILD | WS_VISIBLE,
                 16,
                 540,
                 130,
                 24,
                 g_settingsDialog,
                 NULL,
                 instance,
                 NULL);

    TCHAR cylinderHeightText[32];
    _stprintf_s(cylinderHeightText, _T("%.1f"), SettingsState::GetCylinderHeight());
    CreateWindow(_T("EDIT"),
                 cylinderHeightText,
                  WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT | WS_TABSTOP,
                  150,
                  540,
                  80,
                  24,
                  g_settingsDialog,
                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCylinderHeightEditBoxId)),
                 instance,
                  NULL);

    CreateWindow(_T("STATIC"),
                 _T("直方体の横:"),
                 WS_CHILD | WS_VISIBLE,
                 16,
                 570,
                 130,
                 24,
                 g_settingsDialog,
                 NULL,
                 instance,
                 NULL);

    TCHAR cuboidWidthText[32];
    _stprintf_s(cuboidWidthText, _T("%.1f"), SettingsState::GetCuboidWidth());
    CreateWindow(_T("EDIT"),
                 cuboidWidthText,
                 WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT | WS_TABSTOP,
                 150,
                 570,
                 80,
                 24,
                 g_settingsDialog,
                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCuboidWidthEditBoxId)),
                 instance,
                 NULL);

    CreateWindow(_T("STATIC"),
                 _T("直方体の高さ:"),
                 WS_CHILD | WS_VISIBLE,
                 16,
                 600,
                 130,
                 24,
                 g_settingsDialog,
                 NULL,
                 instance,
                 NULL);

    TCHAR cuboidHeightText[32];
    _stprintf_s(cuboidHeightText, _T("%.1f"), SettingsState::GetCuboidHeight());
    CreateWindow(_T("EDIT"),
                 cuboidHeightText,
                 WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT | WS_TABSTOP,
                 150,
                 600,
                 80,
                 24,
                 g_settingsDialog,
                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCuboidHeightEditBoxId)),
                 instance,
                 NULL);

    CreateWindow(_T("STATIC"),
                 _T("直方体の奥行き:"),
                 WS_CHILD | WS_VISIBLE,
                 16,
                 630,
                 130,
                 24,
                 g_settingsDialog,
                 NULL,
                 instance,
                 NULL);

    TCHAR cuboidDepthText[32];
    _stprintf_s(cuboidDepthText, _T("%.1f"), SettingsState::GetCuboidDepth());
    CreateWindow(_T("EDIT"),
                 cuboidDepthText,
                 WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT | WS_TABSTOP,
                 150,
                 630,
                 80,
                 24,
                 g_settingsDialog,
                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCuboidDepthEditBoxId)),
                 instance,
                 NULL);

    CreateWindow(_T("STATIC"),
                 _T("直方体の回転X:"),
                 WS_CHILD | WS_VISIBLE,
                 16,
                 660,
                 130,
                 24,
                 g_settingsDialog,
                 NULL,
                 instance,
                 NULL);

    TCHAR cuboidRotXText[32];
    _stprintf_s(cuboidRotXText, _T("%.1f"), SettingsState::GetCuboidRotX());
    CreateWindow(_T("EDIT"),
                 cuboidRotXText,
                 WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT | WS_TABSTOP,
                 150,
                 660,
                 80,
                 24,
                 g_settingsDialog,
                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCuboidRotXEditBoxId)),
                 instance,
                 NULL);

    CreateWindow(_T("STATIC"),
                 _T("直方体の回転Y:"),
                 WS_CHILD | WS_VISIBLE,
                 16,
                 690,
                 130,
                 24,
                 g_settingsDialog,
                 NULL,
                 instance,
                 NULL);

    TCHAR cuboidRotYText[32];
    _stprintf_s(cuboidRotYText, _T("%.1f"), SettingsState::GetCuboidRotY());
    CreateWindow(_T("EDIT"),
                 cuboidRotYText,
                 WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT | WS_TABSTOP,
                 150,
                 690,
                 80,
                 24,
                 g_settingsDialog,
                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCuboidRotYEditBoxId)),
                 instance,
                 NULL);

    CreateWindow(_T("STATIC"),
                 _T("直方体の回転Z:"),
                 WS_CHILD | WS_VISIBLE,
                 16,
                 720,
                 130,
                 24,
                 g_settingsDialog,
                 NULL,
                 instance,
                 NULL);

    TCHAR cuboidRotZText[32];
    _stprintf_s(cuboidRotZText, _T("%.1f"), SettingsState::GetCuboidRotZ());
    CreateWindow(_T("EDIT"),
                 cuboidRotZText,
                 WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT | WS_TABSTOP,
                 150,
                 720,
                 80,
                 24,
                 g_settingsDialog,
                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCuboidRotZEditBoxId)),
                 instance,
                 NULL);

    CreateWindow(_T("BUTTON"),
                 _T("リセット"),
                 WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                 16,
                 770,
                 130,
                 32,
                 g_settingsDialog,
                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSettingsResetButtonId)),
                 instance,
                 NULL);

    CreateWindow(_T("BUTTON"),
                 _T("ファイル読込"),
                 WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                 156,
                 770,
                 130,
                 32,
                 g_settingsDialog,
                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFileLoadButtonId)),
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
