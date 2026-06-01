#pragma comment( lib, "d3d9.lib" )
#if defined(DEBUG) || defined(_DEBUG)
#pragma comment( lib, "d3dx9d.lib" )
#else
#pragma comment( lib, "d3dx9.lib" )
#endif
#pragma comment( lib, "winmm.lib" )

#include <d3d9.h>
#include <d3dx9.h>
#include <mmsystem.h>
#include <tchar.h>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <crtdbg.h>
#include <set>
#include <string>
#include <vector>

#include "..\PhysicsLib\PhysicsLib.h"

#define SAFE_RELEASE(p) { if (p) { (p)->Release(); (p) = NULL; } }

const int WINDOW_SIZE_W = 1600;
const int WINDOW_SIZE_H = 900;
const double kTargetFrameSeconds = 1.0 / 60.0;
const float kMinCameraDistance = 2.0f;
const float kMaxCameraDistance = 30.0f;
const float kCameraWheelZoomStep = 0.5f;
const float kPlayerTurnRadiansPerSecond = D3DX_PI * 3.0f;
const float kJumpVelocity = 7.0f;
const D3DXVECTOR3 kPlayerStartPosition(0.0f, 5.0f, 0.0f);

const int kCubeNumber = 1;

struct SceneObject
{
    LPD3DXMESH mesh;
    D3DXVECTOR3 position;
    D3DXVECTOR3 scale;
    D3DXVECTOR3 rotation;
    D3DXCOLOR color;
    bool useTexture;
    DWORD numMaterials;
};

LPDIRECT3D9 g_pD3D = NULL;
LPDIRECT3DDEVICE9 g_pd3dDevice = NULL;
LPD3DXFONT g_pFont = NULL;
LPD3DXFONT g_pFpsFont = NULL;
LPD3DXMESH g_pCubeMesh = NULL;
std::vector<D3DMATERIAL9> g_pMaterials;
std::vector<LPDIRECT3DTEXTURE9> g_pTextures;
DWORD g_dwNumMaterials = 0;
LPD3DXEFFECT g_pEffect = NULL;
bool g_bClose = false;
HWND g_mainWindow = NULL;
std::vector<LPD3DXMESH> g_ownedSceneMeshes;
std::vector<SceneObject> g_worldObjects;
std::vector<SceneObject> g_itemObjects;
std::set<size_t> g_collectedItemIds;
PhysicsLib::CharacterMover g_playerMover(kPlayerStartPosition);
PhysicsLib::CameraMover g_cameraMover;
float g_cameraYaw = 0.0f;
float g_cameraPitch = D3DXToRadian(18.0f);
float g_cameraDistance = 4.0f;
size_t g_movingPlatformIndex = 0;
bool g_movingPlatformForward = true;
bool g_prevSpacePressed = false;
float g_displayFps = 0.0f;
int g_fpsFrameCount = 0;
ULONGLONG g_fpsLastUpdateTick = 0;
bool g_timerPeriodEnabled = false;
D3DXMATRIX g_cameraView;
D3DXMATRIX g_cameraProjection;
float g_playerYaw = 0.0f;
bool g_isMouseCursorVisible = true;
bool g_prevEscPressed = false;
POINT g_lastMousePosition = { 0, 0 };

static void TextDraw(LPD3DXFONT pFont, TCHAR* text, int X, int Y);
static std::basic_string<TCHAR> ResolveAssetPath(const TCHAR* fileName);
static void InitD3D(HWND hWnd);
static void InitScene();
static void ResetPlayer();
static void UpdatePlayer();
static void UpdateCamera();
static LPD3DXMESH LoadSceneMeshFromX(const TCHAR* path, D3DXCOLOR* outColor = NULL, DWORD* outNumMaterials = NULL);
static void DrawMesh(LPD3DXMESH mesh,
                     const D3DXVECTOR3& position,
                     const D3DXVECTOR3& scale,
                     const D3DXVECTOR3& rotation,
                     const D3DXCOLOR& color,
                     bool useTexture,
                     DWORD numMaterials = 1);
static void Cleanup();
static void Render();
static void OnMouseMove(LPARAM lParam);
static void SetMouseCursorVisible(bool visible);
static float ClampFloat(float value, float minValue, float maxValue);
static float NormalizeAngle(float angle);
static float MoveAngleToward(float currentAngle, float targetAngle, float maxStep);
LRESULT WINAPI MsgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

extern int WINAPI _tWinMain(_In_ HINSTANCE hInstance,
                            _In_opt_ HINSTANCE hPrevInstance,
                            _In_ LPTSTR lpCmdLine,
                            _In_ int nCmdShow);

std::basic_string<TCHAR> ResolveAssetPath(const TCHAR* fileName)
{
    if (GetFileAttributes(fileName) != INVALID_FILE_ATTRIBUTES)
    {
        return fileName;
    }

    TCHAR modulePath[MAX_PATH] = {};
    GetModuleFileName(NULL, modulePath, MAX_PATH);
    TCHAR* lastSlash = _tcsrchr(modulePath, _T('\\'));
    if (lastSlash != nullptr)
    {
        *(lastSlash + 1) = _T('\0');
        std::basic_string<TCHAR> exeRelativePath = modulePath;
        exeRelativePath += fileName;
        if (GetFileAttributes(exeRelativePath.c_str()) != INVALID_FILE_ATTRIBUTES)
        {
            return exeRelativePath;
        }
    }

    std::basic_string<TCHAR> projectRelativePath = _T("simple-directx9\\");
    projectRelativePath += fileName;
    if (GetFileAttributes(projectRelativePath.c_str()) != INVALID_FILE_ATTRIBUTES)
    {
        return projectRelativePath;
    }

    return fileName;
}

int WINAPI _tWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPTSTR lpCmdLine,
                     _In_ int nCmdShow)
{
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);

    WNDCLASSEX wc { };
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = MsgProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = _T("Window1");

    ATOM atom = RegisterClassEx(&wc);
    assert(atom != 0);

    RECT rect;
    SetRect(&rect, 0, 0, WINDOW_SIZE_W, WINDOW_SIZE_H);
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    rect.right = rect.right - rect.left;
    rect.bottom = rect.bottom - rect.top;
    rect.top = 0;
    rect.left = 0;

    HWND hWnd = CreateWindow(_T("Window1"),
                             _T("Physics Sample"),
                             WS_OVERLAPPEDWINDOW,
                             CW_USEDEFAULT,
                             CW_USEDEFAULT,
                             rect.right,
                             rect.bottom,
                             NULL,
                             NULL,
                             wc.hInstance,
                             NULL);
    g_mainWindow = hWnd;

    g_timerPeriodEnabled = (timeBeginPeriod(1) == TIMERR_NOERROR);

    InitD3D(hWnd);
    PhysicsLib::PhysicsLib::Initialize();
    PhysicsLib::PhysicsLib::SetResetCallback(ResetPlayer);
    InitScene();
    PhysicsLib::PhysicsLib::ShowSettingsDialog(hWnd);
    ShowWindow(hWnd, SW_SHOWDEFAULT);
    UpdateWindow(hWnd);

    MSG msg;
    LARGE_INTEGER performanceFrequency;
    LARGE_INTEGER nextFrameCounter;
    QueryPerformanceFrequency(&performanceFrequency);
    QueryPerformanceCounter(&nextFrameCounter);

    while (true)
    {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else
        {
            LARGE_INTEGER currentCounter;
            QueryPerformanceCounter(&currentCounter);

            const LONGLONG remainingCounts = nextFrameCounter.QuadPart - currentCounter.QuadPart;
            if (remainingCounts > 0)
            {
                const double remainingSeconds =
                    static_cast<double>(remainingCounts) / static_cast<double>(performanceFrequency.QuadPart);
                if (remainingSeconds > 0.002)
                {
                    Sleep(static_cast<DWORD>((remainingSeconds - 0.001) * 1000.0));
                }
                else
                {
                    Sleep(0);
                }
                continue;
            }

            nextFrameCounter.QuadPart =
                currentCounter.QuadPart +
                static_cast<LONGLONG>(kTargetFrameSeconds * static_cast<double>(performanceFrequency.QuadPart));
            UpdatePlayer();
            Render();
        }

        if (g_bClose)
        {
            break;
        }
    }

    Cleanup();
    UnregisterClass(_T("Window1"), wc.hInstance);
    return 0;
}

void TextDraw(LPD3DXFONT pFont, TCHAR* text, int X, int Y)
{
    RECT rect = { X, Y, 0, 0 };
    HRESULT hResult = pFont->DrawText(NULL,
                                      text,
                                      -1,
                                      &rect,
                                      DT_LEFT | DT_NOCLIP,
                                      D3DCOLOR_ARGB(255, 0, 0, 0));
    assert((int)hResult >= 0);
}

void InitD3D(HWND hWnd)
{
    HRESULT hResult = E_FAIL;

    g_pD3D = Direct3DCreate9(D3D_SDK_VERSION);
    assert(g_pD3D != NULL);

    D3DPRESENT_PARAMETERS d3dpp;
    ZeroMemory(&d3dpp, sizeof(d3dpp));
    d3dpp.Windowed = TRUE;
    d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    d3dpp.BackBufferFormat = D3DFMT_UNKNOWN;
    d3dpp.BackBufferCount = 1;
    d3dpp.MultiSampleType = D3DMULTISAMPLE_NONE;
    d3dpp.EnableAutoDepthStencil = TRUE;
    d3dpp.AutoDepthStencilFormat = D3DFMT_D16;
    d3dpp.hDeviceWindow = hWnd;
    d3dpp.FullScreen_RefreshRateInHz = D3DPRESENT_RATE_DEFAULT;
    d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;

    hResult = g_pD3D->CreateDevice(D3DADAPTER_DEFAULT,
                                   D3DDEVTYPE_HAL,
                                   hWnd,
                                   D3DCREATE_HARDWARE_VERTEXPROCESSING,
                                   &d3dpp,
                                   &g_pd3dDevice);

    if (FAILED(hResult))
    {
        hResult = g_pD3D->CreateDevice(D3DADAPTER_DEFAULT,
                                       D3DDEVTYPE_HAL,
                                       hWnd,
                                       D3DCREATE_SOFTWARE_VERTEXPROCESSING,
                                       &d3dpp,
                                       &g_pd3dDevice);
        assert(hResult == S_OK);
    }

    hResult = D3DXCreateFont(g_pd3dDevice,
                             20,
                             0,
                             FW_HEAVY,
                             1,
                             FALSE,
                             SHIFTJIS_CHARSET,
                             OUT_TT_ONLY_PRECIS,
                             CLEARTYPE_NATURAL_QUALITY,
                             FF_DONTCARE,
                             _T("ＭＳ ゴシック"),
                             &g_pFont);
    assert(hResult == S_OK);

    hResult = D3DXCreateFont(g_pd3dDevice,
                             48,
                             0,
                             FW_BOLD,
                             1,
                             FALSE,
                             SHIFTJIS_CHARSET,
                             OUT_TT_ONLY_PRECIS,
                             CLEARTYPE_NATURAL_QUALITY,
                             FF_DONTCARE,
                             _T("ＭＳ ゴシック"),
                             &g_pFpsFont);
    assert(hResult == S_OK);

    LPD3DXBUFFER pD3DXMtrlBuffer = NULL;
    const std::basic_string<TCHAR> cubePath = ResolveAssetPath(_T("cube.x"));
    hResult = D3DXLoadMeshFromX(cubePath.c_str(),
                                D3DXMESH_SYSTEMMEM,
                                g_pd3dDevice,
                                NULL,
                                &pD3DXMtrlBuffer,
                                NULL,
                                &g_dwNumMaterials,
                                &g_pCubeMesh);
    assert(hResult == S_OK);

    D3DXMATERIAL* d3dxMaterials = (D3DXMATERIAL*)pD3DXMtrlBuffer->GetBufferPointer();
    g_pMaterials.resize(g_dwNumMaterials);
    g_pTextures.resize(g_dwNumMaterials);

    for (DWORD i = 0; i < g_dwNumMaterials; i++)
    {
        g_pMaterials[i] = d3dxMaterials[i].MatD3D;
        g_pMaterials[i].Ambient = g_pMaterials[i].Diffuse;
        g_pTextures[i] = NULL;

        if (d3dxMaterials[i].pTextureFilename != NULL)
        {
            hResult = D3DXCreateTextureFromFileA(g_pd3dDevice,
                                                 d3dxMaterials[i].pTextureFilename,
                                                 &g_pTextures[i]);
            assert(hResult == S_OK);
        }
    }

    hResult = pD3DXMtrlBuffer->Release();
    assert(hResult == S_OK);

    hResult = D3DXCreateEffectFromFile(g_pd3dDevice,
                                       _T("simple.fx"),
                                       NULL,
                                       NULL,
                                       D3DXSHADER_DEBUG,
                                       NULL,
                                       &g_pEffect,
                                       NULL);
    assert(hResult == S_OK);
}

void InitScene()
{
    g_worldObjects.clear();
    g_itemObjects.clear();
    g_collectedItemIds.clear();

    FILE* file = NULL;
    if (_tfopen_s(&file, _T("XFileListRender.csv"), _T("rt")) != 0 || file == NULL)
    {
        ResetPlayer();
        return;
    }

    TCHAR line[512];
    _fgetts(line, 512, file);

    while (_fgetts(line, 512, file) != NULL)
    {
        TCHAR* context = NULL;
        TCHAR* token = _tcstok_s(line, _T(",\n"), &context);
        if (token == NULL)
        {
            continue;
        }
        token = _tcstok_s(NULL, _T(",\n"), &context);
        const TCHAR* fileName = token;
        if (fileName == NULL)
        {
            continue;
        }

        token = _tcstok_s(NULL, _T(",\n"), &context);
        const float posX = token != NULL ? static_cast<float>(_tstof(token)) : 0.0f;
        token = _tcstok_s(NULL, _T(",\n"), &context);
        const float posY = token != NULL ? static_cast<float>(_tstof(token)) : 0.0f;
        token = _tcstok_s(NULL, _T(",\n"), &context);
        const float posZ = token != NULL ? static_cast<float>(_tstof(token)) : 0.0f;
        token = _tcstok_s(NULL, _T(",\n"), &context);
        const float rotX = token != NULL ? static_cast<float>(_tstof(token)) : 0.0f;
        token = _tcstok_s(NULL, _T(",\n"), &context);
        const float rotY = token != NULL ? static_cast<float>(_tstof(token)) : 0.0f;
        token = _tcstok_s(NULL, _T(",\n"), &context);
        const float rotZ = token != NULL ? static_cast<float>(_tstof(token)) : 0.0f;
        token = _tcstok_s(NULL, _T(",\n"), &context);
        const float scale = token != NULL ? static_cast<float>(_tstof(token)) : 1.0f;

        const D3DXVECTOR3 position(posX, posY, posZ);
        const D3DXVECTOR3 rotation(D3DXToRadian(rotX), D3DXToRadian(rotY), D3DXToRadian(rotZ));
        const D3DXVECTOR3 scaleVec(scale, scale, scale);

        D3DXCOLOR matColor(1.0f, 1.0f, 1.0f, 1.0f);
        DWORD numMaterials = 1;
        LPD3DXMESH mesh = LoadSceneMeshFromX(fileName, &matColor, &numMaterials);

        if (_tcsstr(fileName, _T("Instancing")) != NULL)
        {
            TCHAR csvFileName[512];
            _tcscpy_s(csvFileName, fileName);
            TCHAR* lastDot = _tcsrchr(csvFileName, _T('.'));
            if (lastDot != NULL)
            {
                _tcscpy_s(lastDot, 512 - static_cast<size_t>(lastDot - csvFileName), _T(".csv"));
            }

            std::basic_string<TCHAR> csvPath = ResolveAssetPath(csvFileName);
            FILE* instFile = NULL;
            int openResult = _tfopen_s(&instFile, csvPath.c_str(), _T("rt"));
            if (openResult == 0 && instFile != NULL)
            {
                TCHAR instLine[512];
                _fgetts(instLine, 512, instFile);

                while (_fgetts(instLine, 512, instFile) != NULL)
                {
                    if (instLine[0] == _T('#'))
                    {
                        continue;
                    }

                    TCHAR* ctx = NULL;
                    TCHAR* t = _tcstok_s(instLine, _T(",\n"), &ctx);
                    if (t == NULL)
                    {
                        continue;
                    }
                    float instX = static_cast<float>(_tstof(t));

                    t = _tcstok_s(NULL, _T(",\n"), &ctx);
                    if (t == NULL)
                    {
                        continue;
                    }
                    float instY = static_cast<float>(_tstof(t));

                    t = _tcstok_s(NULL, _T(",\n"), &ctx);
                    if (t == NULL)
                    {
                        continue;
                    }
                    float instZ = static_cast<float>(_tstof(t));

                    t = _tcstok_s(NULL, _T(",\n"), &ctx);
                    if (t == NULL)
                    {
                        continue;
                    }
                    float instRotY = static_cast<float>(_tstof(t));

                    t = _tcstok_s(NULL, _T(",\n"), &ctx);
                    if (t == NULL)
                    {
                        continue;
                    }
                    float instScale = static_cast<float>(_tstof(t));

                    D3DXVECTOR3 instPos(instX, instY, instZ);
                    D3DXVECTOR3 instRot(0.0f, D3DXToRadian(instRotY), 0.0f);
                    D3DXVECTOR3 instScl(instScale, instScale, instScale);
                    g_worldObjects.push_back({ mesh, instPos, instScl, instRot, matColor, false, numMaterials });
                }
                fclose(instFile);
            }
            else
            {
                g_worldObjects.push_back({ mesh, position, scaleVec, rotation, matColor, false, numMaterials });
            }
        }
        else if (_tcsstr(fileName, _T("item_sphere")) != NULL)
        {
            g_itemObjects.push_back({ mesh, position, scaleVec, rotation, matColor, false, numMaterials });
        }
        else
        {
            g_worldObjects.push_back({ mesh, position, scaleVec, rotation, matColor, false, numMaterials });
            if (_tcsstr(fileName, _T("moving_platform")) != NULL)
            {
                g_movingPlatformIndex = g_worldObjects.size() - 1;
            }
        }
    }

    fclose(file);

    ResetPlayer();
}

void ResetPlayer()
{
    PhysicsLib::CharacterMover::Settings settings = g_playerMover.GetSettings();
    settings.shapeType = PhysicsLib::PhysicsLib::ShapeType::Cylinder;
    settings.radius = 0.5f;
    settings.height = 1.0f;
    const float walkSpeed = PhysicsLib::PhysicsLib::GetWalkSpeed();
    settings.moveSpeed = walkSpeed;
    settings.groundAcceleration = walkSpeed * 2.0f;
    settings.airAcceleration = walkSpeed * 0.35f;
    settings.jumpVelocity = kJumpVelocity;
    settings.doubleJumpEnabled = false;
    settings.keepHorizontalVelocityOnJump = true;
    settings.groundDamping = 1.0f;
    settings.airDamping = 1.0f;
    g_playerMover.SetSettings(settings);
    g_playerMover.Reset(kPlayerStartPosition);
    g_playerYaw = 0.0f;
}

void UpdatePlayer()
{
    const bool isWindowActive = (GetForegroundWindow() == g_mainWindow);

    float targetSpeed = PhysicsLib::PhysicsLib::GetWalkSpeed();
    if (isWindowActive && ((GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0))
    {
        targetSpeed = PhysicsLib::PhysicsLib::GetDashSpeed();
    }
    PhysicsLib::CharacterMover::Settings speedSettings = g_playerMover.GetSettings();
    speedSettings.moveSpeed = targetSpeed;
    speedSettings.groundAcceleration = targetSpeed * 2.0f;
    speedSettings.airAcceleration = targetSpeed * 0.35f;
    g_playerMover.SetSettings(speedSettings);

    bool isEscPressed = isWindowActive && ((GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0);
    if (isEscPressed && !g_prevEscPressed)
    {
        SetMouseCursorVisible(!g_isMouseCursorVisible);
    }
    g_prevEscPressed = isEscPressed;

    PhysicsLib::PhysicsLib::Update();

    if (g_movingPlatformIndex < g_worldObjects.size())
    {
        const float kPlatformSpeed = 1.5f;
        const float kPlatformMinX = -4.0f;
        const float kPlatformMaxX = 4.0f;
        float& platformX = g_worldObjects[g_movingPlatformIndex].position.x;
        if (g_movingPlatformForward)
        {
            platformX += kPlatformSpeed * (1.0f / 60.0f);
            if (platformX >= kPlatformMaxX)
            {
                platformX = kPlatformMaxX;
                g_movingPlatformForward = false;
            }
        }
        else
        {
            platformX -= kPlatformSpeed * (1.0f / 60.0f);
            if (platformX <= kPlatformMinX)
            {
                platformX = kPlatformMinX;
                g_movingPlatformForward = true;
            }
        }
    }

    if (g_movingPlatformIndex < g_worldObjects.size())
    {
        PhysicsLib::PhysicsLib::UpdateCsvTransform(10,
            g_worldObjects[g_movingPlatformIndex].position,
            g_worldObjects[g_movingPlatformIndex].rotation,
            g_worldObjects[g_movingPlatformIndex].scale);
    }

    D3DXVECTOR3 inputMove(0.0f, 0.0f, 0.0f);
    D3DXVECTOR3 localInputMove(0.0f, 0.0f, 0.0f);

    const bool forwardPressed = isWindowActive && ((GetAsyncKeyState('W') & 0x8000) ||
                                                   (GetAsyncKeyState(VK_UP) & 0x8000));
    const bool backwardPressed = isWindowActive && ((GetAsyncKeyState('S') & 0x8000) ||
                                                    (GetAsyncKeyState(VK_DOWN) & 0x8000));
    const bool focusModeEnabled = PhysicsLib::PhysicsLib::IsFocusModeEnabled();

    if (forwardPressed)
    {
        localInputMove.z += 1.0f;
    }
    if (backwardPressed)
    {
        localInputMove.z -= 1.0f;
    }
    if (isWindowActive && ((GetAsyncKeyState('A') & 0x8000) ||
                           (GetAsyncKeyState(VK_LEFT) & 0x8000)))
    {
        localInputMove.x -= 1.0f;
    }
    if (isWindowActive && ((GetAsyncKeyState('D') & 0x8000) ||
                           (GetAsyncKeyState(VK_RIGHT) & 0x8000)))
    {
        localInputMove.x += 1.0f;
    }

    if (localInputMove.x != 0.0f || localInputMove.z != 0.0f)
    {
        const D3DXVECTOR3 cameraForward(-sinf(g_cameraYaw), 0.0f, cosf(g_cameraYaw));
        const D3DXVECTOR3 cameraRight(cosf(g_cameraYaw), 0.0f, sinf(g_cameraYaw));
        const D3DXVECTOR3 desiredMove = cameraRight * localInputMove.x + cameraForward * localInputMove.z;
        if (focusModeEnabled)
        {
            g_playerYaw = atan2f(cameraForward.x, cameraForward.z);
            inputMove = desiredMove;
        }
        else
        {
            const float targetPlayerYaw = atan2f(desiredMove.x, desiredMove.z);
            g_playerYaw = MoveAngleToward(g_playerYaw,
                                          targetPlayerYaw,
                                          kPlayerTurnRadiansPerSecond * static_cast<float>(kTargetFrameSeconds));
            inputMove = desiredMove;
        }
    }

    std::vector<int> passThroughIds;
    std::vector<int> solidIds;
    const bool isSpacePressed = isWindowActive && ((GetAsyncKeyState(VK_SPACE) & 0x8000) != 0);
    const bool jump = isSpacePressed && !g_prevSpacePressed;
    g_prevSpacePressed = isSpacePressed;
    g_playerMover.Update(inputMove, jump, &passThroughIds, &solidIds);

    const D3DXVECTOR3 playerPosition = g_playerMover.GetPosition();
    for (size_t i = 0; i < g_itemObjects.size(); ++i)
    {
        if (g_collectedItemIds.find(i) != g_collectedItemIds.end())
        {
            continue;
        }

        D3DXVECTOR3 diff = playerPosition - g_itemObjects[i].position;
        if (D3DXVec3Length(&diff) < 1.0f)
        {
            g_collectedItemIds.insert(i);
        }
    }
}

void UpdateCamera()
{
    const D3DXVECTOR3 playerPosition = g_playerMover.GetPosition();
    const D3DXVECTOR3 cameraTarget = playerPosition + D3DXVECTOR3(0.0f, 1.2f, 0.0f);
    const float horizontalDistance = g_cameraDistance * cosf(g_cameraPitch);
    const D3DXVECTOR3 offset(sinf(g_cameraYaw) * horizontalDistance,
                             sinf(g_cameraPitch) * g_cameraDistance,
                             -cosf(g_cameraYaw) * horizontalDistance);
    const D3DXVECTOR3 desiredCameraPosition = cameraTarget + offset;
    const D3DXVECTOR3 cameraPosition = g_cameraMover.ResolvePosition(cameraTarget, desiredCameraPosition);
    const D3DXVECTOR3 cameraUp(0.0f, 1.0f, 0.0f);

    D3DXMatrixLookAtLH(&g_cameraView, &cameraPosition, &cameraTarget, &cameraUp);
    const float aspect = (float)WINDOW_SIZE_W / WINDOW_SIZE_H;
    const float horizontalFov = D3DXToRadian(90.0f);
    const float verticalFov = 2.0f * atanf(tanf(horizontalFov * 0.5f) / aspect);
    D3DXMatrixPerspectiveFovLH(&g_cameraProjection,
                               verticalFov,
                               aspect,
                               0.1f,
                               1000.0f);
}

LPD3DXMESH LoadSceneMeshFromX(const TCHAR* path, D3DXCOLOR* outColor, DWORD* outNumMaterials)
{
    LPD3DXMESH mesh = NULL;
    LPD3DXBUFFER materialBuffer = NULL;
    DWORD numMaterials = 0;
    HRESULT hResult = D3DXLoadMeshFromX(path,
                                        D3DXMESH_SYSTEMMEM,
                                        g_pd3dDevice,
                                        NULL,
                                        &materialBuffer,
                                        NULL,
                                        &numMaterials,
                                        &mesh);
    assert(hResult == S_OK);
    if (outNumMaterials != NULL)
    {
        *outNumMaterials = numMaterials;
    }
    if (outColor != NULL && materialBuffer != NULL)
    {
        const D3DXMATERIAL* materials = static_cast<const D3DXMATERIAL*>(materialBuffer->GetBufferPointer());
        if (numMaterials > 0)
        {
            outColor->r = materials[0].MatD3D.Diffuse.r;
            outColor->g = materials[0].MatD3D.Diffuse.g;
            outColor->b = materials[0].MatD3D.Diffuse.b;
            outColor->a = 1.0f;
        }
    }
    if (materialBuffer != NULL)
    {
        materialBuffer->Release();
    }
    g_ownedSceneMeshes.push_back(mesh);
    return mesh;
}

void DrawMesh(LPD3DXMESH mesh,
              const D3DXVECTOR3& position,
              const D3DXVECTOR3& scale,
              const D3DXVECTOR3& rotation,
              const D3DXCOLOR& color,
              bool useTexture,
              DWORD numMaterials)
{
    HRESULT hResult = E_FAIL;

    D3DXMATRIX matScale;
    D3DXMATRIX matRotationX;
    D3DXMATRIX matRotationY;
    D3DXMATRIX matRotationZ;
    D3DXMATRIX matTranslation;
    D3DXMATRIX matWorld;
    D3DXMATRIX matWorldViewProj;

    D3DXMatrixScaling(&matScale, scale.x, scale.y, scale.z);
    D3DXMatrixRotationX(&matRotationX, rotation.x);
    D3DXMatrixRotationY(&matRotationY, rotation.y);
    D3DXMatrixRotationZ(&matRotationZ, rotation.z);
    D3DXMatrixTranslation(&matTranslation, position.x, position.y, position.z);

    matWorld = matScale * matRotationX * matRotationY * matRotationZ * matTranslation;

    matWorldViewProj = matWorld * g_cameraView * g_cameraProjection;

    hResult = g_pEffect->SetMatrix("g_matWorldViewProj", &matWorldViewProj);
    assert(hResult == S_OK);

    hResult = g_pEffect->SetVector("g_meshColor", (const D3DXVECTOR4*)&color);
    assert(hResult == S_OK);

    float useTextureValue = 0.0f;
    if (useTexture)
    {
        useTextureValue = 1.0f;
    }
    hResult = g_pEffect->SetFloat("g_useTexture", useTextureValue);
    assert(hResult == S_OK);

    if (mesh == g_pCubeMesh && useTexture)
    {
        for (DWORD i = 0; i < g_dwNumMaterials; i++)
        {
            hResult = g_pEffect->SetTexture("texture1", g_pTextures[i]);
            assert(hResult == S_OK);

            hResult = g_pEffect->CommitChanges();
            assert(hResult == S_OK);

            hResult = mesh->DrawSubset(i);
            assert(hResult == S_OK);
        }
    }
    else
    {
        hResult = g_pEffect->SetTexture("texture1", NULL);
        assert(hResult == S_OK);

        hResult = g_pEffect->CommitChanges();
        assert(hResult == S_OK);

        for (DWORD i = 0; i < numMaterials; i++)
        {
            hResult = mesh->DrawSubset(i);
            assert(hResult == S_OK);
        }
    }
}

void Cleanup()
{
    for (auto& texture : g_pTextures)
    {
        SAFE_RELEASE(texture);
    }

    PhysicsLib::PhysicsLib::Finalize();
    for (size_t i = 0; i < g_ownedSceneMeshes.size(); ++i)
    {
        SAFE_RELEASE(g_ownedSceneMeshes[i]);
    }
    g_ownedSceneMeshes.clear();
    SAFE_RELEASE(g_pCubeMesh);
    SAFE_RELEASE(g_pEffect);
    SAFE_RELEASE(g_pFpsFont);
    SAFE_RELEASE(g_pFont);
    SAFE_RELEASE(g_pd3dDevice);
    SAFE_RELEASE(g_pD3D);

    if (g_timerPeriodEnabled)
    {
        timeEndPeriod(1);
        g_timerPeriodEnabled = false;
    }
}

void Render()
{
    ++g_fpsFrameCount;
    const ULONGLONG currentTick = GetTickCount64();
    if (g_fpsLastUpdateTick == 0)
    {
        g_fpsLastUpdateTick = currentTick;
    }
    else
    {
        const ULONGLONG elapsedTick = currentTick - g_fpsLastUpdateTick;
        if (elapsedTick >= 1000)
        {
            g_displayFps = static_cast<float>(g_fpsFrameCount) * 1000.0f / static_cast<float>(elapsedTick);
            g_fpsFrameCount = 0;
            g_fpsLastUpdateTick = currentTick;
        }
    }

    HRESULT hResult = E_FAIL;

    hResult = g_pd3dDevice->Clear(0,
                                  NULL,
                                  D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
                                  D3DCOLOR_XRGB(170, 200, 235),
                                  1.0f,
                                  0);
    assert(hResult == S_OK);

    hResult = g_pd3dDevice->BeginScene();
    assert(hResult == S_OK);

    UpdateCamera();

    TCHAR fpsText[64];
    _stprintf_s(fpsText, _T("FPS %.1f"), g_displayFps);
    TextDraw(g_pFpsFont, fpsText, 20, 16);

    TCHAR msg[256];
    const D3DXVECTOR3 playerPosition = g_playerMover.GetPosition();
    const TCHAR* cursorText = _T("OFF");
    if (g_isMouseCursorVisible)
    {
        cursorText = _T("ON");
    }
    _stprintf_s(msg,
                _T("WASD: move  SHIFT: speed x3  SPACE: jump  ESC: cursor=%s  Items: %d/5  Pos(%.2f, %.2f, %.2f)"),
                cursorText,
                (int)g_collectedItemIds.size(),
                playerPosition.x,
                playerPosition.y,
                playerPosition.z);
    TextDraw(g_pFont, msg, 20, 72);

    TCHAR contactText[256];
    const D3DXVECTOR3 playerVelocity = g_playerMover.GetVelocity();
    const float playerSpeed = sqrtf(playerVelocity.x * playerVelocity.x +
                                    playerVelocity.z * playerVelocity.z);
    const TCHAR* groundText = _T("OFF");
    if (g_playerMover.IsGrounded())
    {
        groundText = _T("ON");
    }
    const TCHAR* wallText = _T("OFF");
    if (g_playerMover.IsTouchingWall())
    {
        wallText = _T("ON");
    }
    _stprintf_s(contactText,
                _T("Ground=%s  Wall=%s  Velocity(%.2f, %.2f, %.2f)  Speed=%.2f"),
                groundText,
                wallText,
                playerVelocity.x,
                playerVelocity.y,
                playerVelocity.z,
                playerSpeed);
    TextDraw(g_pFont, contactText, 20, 100);

    TCHAR collisionDebugText[256];
    const PhysicsLib::CharacterMover::DebugInfo debugInfo = g_playerMover.GetDebugInfo();
    _stprintf_s(collisionDebugText,
                _T("CollideChecks=%d  Hits=%d  SlidePasses=%d  HitDist=%.3f  NormalMove=%.3f  HitN(%.2f, %.2f, %.2f)  Slide(%.3f, %.3f, %.3f)"),
                debugInfo.collideCheckCount,
                debugInfo.hitCount,
                debugInfo.slideCount,
                debugInfo.lastHitDistance,
                debugInfo.lastNormalMove,
                debugInfo.lastHitNormal.x,
                debugInfo.lastHitNormal.y,
                debugInfo.lastHitNormal.z,
                debugInfo.lastSlideMove.x,
                debugInfo.lastSlideMove.y,
                debugInfo.lastSlideMove.z);
    TextDraw(g_pFont, collisionDebugText, 20, 128);

    hResult = g_pEffect->SetTechnique("Technique1");
    assert(hResult == S_OK);

    UINT numPass = 0;
    hResult = g_pEffect->Begin(&numPass, 0);
    assert(hResult == S_OK);

    hResult = g_pEffect->BeginPass(0);
    assert(hResult == S_OK);

    for (size_t i = 0; i < g_worldObjects.size(); i++)
    {
        DrawMesh(g_worldObjects[i].mesh,
                 g_worldObjects[i].position,
                 g_worldObjects[i].scale,
                 g_worldObjects[i].rotation,
                 g_worldObjects[i].color,
                 g_worldObjects[i].useTexture,
                 g_worldObjects[i].numMaterials);
    }

    for (size_t i = 0; i < g_itemObjects.size(); i++)
    {
        if (g_collectedItemIds.find(i) != g_collectedItemIds.end())
        {
            continue;
        }

        DrawMesh(g_itemObjects[i].mesh,
                 g_itemObjects[i].position,
                 g_itemObjects[i].scale,
                 g_itemObjects[i].rotation,
                 g_itemObjects[i].color,
                 g_itemObjects[i].useTexture,
                 g_itemObjects[i].numMaterials);
    }

    DrawMesh(g_pCubeMesh,
             g_playerMover.GetPosition(),
             D3DXVECTOR3(1.0f, 1.0f, 1.0f),
             D3DXVECTOR3(0.0f, g_playerYaw, 0.0f),
             D3DXCOLOR(1.0f, 1.0f, 1.0f, 1.0f),
             true,
             g_dwNumMaterials);

    hResult = g_pEffect->EndPass();
    assert(hResult == S_OK);

    hResult = g_pEffect->End();
    assert(hResult == S_OK);

    hResult = g_pd3dDevice->EndScene();
    assert(hResult == S_OK);

    hResult = g_pd3dDevice->Present(NULL, NULL, NULL, NULL);
    assert(hResult == S_OK);
}

void OnMouseMove(LPARAM lParam)
{
    const POINT currentPosition = { static_cast<short>(LOWORD(lParam)),
                                    static_cast<short>(HIWORD(lParam)) };
    if (!g_isMouseCursorVisible)
    {
        const LONG deltaX = currentPosition.x - g_lastMousePosition.x;
        const LONG deltaY = currentPosition.y - g_lastMousePosition.y;
        g_cameraYaw -= deltaX * 0.005f;
        g_cameraPitch += deltaY * 0.005f;
        g_cameraPitch = ClampFloat(g_cameraPitch, D3DXToRadian(-20.0f), D3DXToRadian(70.0f));
    }

    g_lastMousePosition = currentPosition;
}

void SetMouseCursorVisible(bool visible)
{
    g_isMouseCursorVisible = visible;
    BOOL showCursor = FALSE;
    if (visible)
    {
        showCursor = TRUE;
    }
    ShowCursor(showCursor);
    if (visible)
    {
        ReleaseCapture();
    }
    else
    {
        SetCapture(g_mainWindow);
        POINT cursorPosition;
        GetCursorPos(&cursorPosition);
        ScreenToClient(g_mainWindow, &cursorPosition);
        g_lastMousePosition = cursorPosition;
    }
}

float ClampFloat(float value, float minValue, float maxValue)
{
    if (value < minValue)
    {
        return minValue;
    }

    if (value > maxValue)
    {
        return maxValue;
    }

    return value;
}

float NormalizeAngle(float angle)
{
    while (angle > D3DX_PI)
    {
        angle -= D3DX_PI * 2.0f;
    }

    while (angle < -D3DX_PI)
    {
        angle += D3DX_PI * 2.0f;
    }

    return angle;
}

float MoveAngleToward(float currentAngle, float targetAngle, float maxStep)
{
    const float deltaAngle = NormalizeAngle(targetAngle - currentAngle);
    if (fabsf(deltaAngle) <= maxStep)
    {
        return NormalizeAngle(targetAngle);
    }

    if (deltaAngle > 0.0f)
    {
        return NormalizeAngle(currentAngle + maxStep);
    }

    return NormalizeAngle(currentAngle - maxStep);
}

LRESULT WINAPI MsgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_MOUSEMOVE:
        OnMouseMove(lParam);
        return 0;

    case WM_MOUSEWHEEL:
        g_cameraDistance -=
            static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / WHEEL_DELTA * kCameraWheelZoomStep;
        g_cameraDistance = ClampFloat(g_cameraDistance, kMinCameraDistance, kMaxCameraDistance);
        return 0;

    case WM_DESTROY:
        if (hWnd == g_mainWindow)
        {
            PostQuitMessage(0);
            g_bClose = true;
        }
        return 0;
    }

    return DefWindowProc(hWnd, msg, wParam, lParam);
}
