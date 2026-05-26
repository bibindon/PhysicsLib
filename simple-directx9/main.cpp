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
#include <crtdbg.h>
#include <set>
#include <string>
#include <vector>

#include "..\PhysicsLib\PhysicsLib.h"

#define SAFE_RELEASE(p) { if (p) { (p)->Release(); (p) = NULL; } }

const int IDC_DOUBLE_JUMP_CHECKBOX = 1001;
const int WINDOW_SIZE_W = 1600;
const int WINDOW_SIZE_H = 900;
const float kPlayerSpeed = 18.0f;
const float kJumpVelocity = 7.0f;
const D3DXVECTOR3 kPlayerStartPosition(0.0f, 5.0f, 0.0f);

struct SceneObject
{
    LPD3DXMESH mesh;
    int collisionId;
    D3DXVECTOR3 position;
    D3DXVECTOR3 scale;
    D3DXVECTOR3 rotation;
    D3DXCOLOR color;
    bool useTexture;
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
HWND g_settingsWindow = NULL;
HWND g_doubleJumpCheckBox = NULL;
std::vector<LPD3DXMESH> g_ownedSceneMeshes;
std::vector<SceneObject> g_worldObjects;
std::vector<SceneObject> g_itemObjects;
PhysicsLib::CharacterMover g_playerMover(kPlayerStartPosition);
int g_movingPlatformId = -1;
std::set<int> g_collectedItemIds;
bool g_prevF1Pressed = false;
bool g_prevF2Pressed = false;
bool g_prevF3Pressed = false;
bool g_prevF4Pressed = false;
bool g_prevSpacePressed = false;
float g_displayFps = 0.0f;
int g_fpsFrameCount = 0;
ULONGLONG g_fpsLastUpdateTick = 0;
bool g_timerPeriodEnabled = false;
D3DXMATRIX g_cameraView;
D3DXMATRIX g_cameraProjection;
float g_cameraYaw = 0.0f;
float g_cameraPitch = D3DXToRadian(18.0f);
float g_cameraDistance = 12.0f;
bool g_isMouseCursorVisible = true;
bool g_prevEscPressed = false;
POINT g_lastMousePosition = { 0, 0 };

static void TextDraw(LPD3DXFONT pFont, TCHAR* text, int X, int Y);
static void InitD3D(HWND hWnd);
static void InitScene();
static void InitSettingsDialog(HWND ownerWindow);
static void ResetPlayer();
static void UpdatePlayer();
static void UpdateCamera();
static void SyncSceneFromPhysics();
static LPD3DXMESH CreateBoxMesh(float width, float height, float depth);
static LPD3DXMESH CreateSphereMesh(float radius);
static LPD3DXMESH LoadSceneMeshFromX(const TCHAR* path);
static void SaveCollisionMesh(LPD3DXMESH mesh, const TCHAR* path);
static void DrawMesh(LPD3DXMESH mesh,
                     const D3DXVECTOR3& position,
                     const D3DXVECTOR3& scale,
                     const D3DXVECTOR3& rotation,
                     const D3DXCOLOR& color,
                     bool useTexture);
static void Cleanup();
static void Render();
static void OnMouseMove(LPARAM lParam);
static void SetMouseCursorVisible(bool visible);
static void SetDoubleJumpEnabled(bool enabled);
static void SyncSettingsDialog();
static float ClampFloat(float value, float minValue, float maxValue);
LRESULT WINAPI MsgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

extern int WINAPI _tWinMain(_In_ HINSTANCE hInstance,
                            _In_opt_ HINSTANCE hPrevInstance,
                            _In_ LPTSTR lpCmdLine,
                            _In_ int nCmdShow);

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
    PhysicsLib::PhysicsLib::SetIntersectMultithreadEnabled(false);
    InitScene();
    InitSettingsDialog(hWnd);
    ShowWindow(hWnd, SW_SHOWDEFAULT);
    UpdateWindow(hWnd);

    MSG msg;

    while (true)
    {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            DispatchMessage(&msg);
        }
        else
        {
            Sleep(16);
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
    hResult = D3DXLoadMeshFromX(_T("cube.x"),
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
    for (size_t i = 0; i < g_ownedSceneMeshes.size(); ++i)
    {
        SAFE_RELEASE(g_ownedSceneMeshes[i]);
    }
    g_ownedSceneMeshes.clear();

    LPD3DXMESH groundMesh = CreateBoxMesh(24.0f, 1.0f, 24.0f);
    SaveCollisionMesh(groundMesh, _T("collision_ground.x"));
    const int groundId = PhysicsLib::PhysicsLib::Load(_T("collision_ground.x"),
                                                      PhysicsLib::PhysicsLib::ObjectType::Slide,
                                                      0.8f);
    PhysicsLib::PhysicsLib::SetTransform(groundId,
                                         D3DXVECTOR3(0.0f, -0.5f, 0.0f),
                                         D3DXVECTOR3(0.0f, 0.0f, 0.0f),
                                         D3DXVECTOR3(1.0f, 1.0f, 1.0f));
    g_worldObjects.push_back({ groundMesh, groundId, D3DXVECTOR3(0.0f, -0.5f, 0.0f), D3DXVECTOR3(1.0f, 1.0f, 1.0f), D3DXVECTOR3(0.0f, 0.0f, 0.0f), D3DXCOLOR(0.35f, 0.70f, 0.35f, 1.0f), false });

    LPD3DXMESH slopeMesh = CreateBoxMesh(6.0f, 0.8f, 4.0f);
    SaveCollisionMesh(slopeMesh, _T("collision_slope.x"));
    const int slopeId = PhysicsLib::PhysicsLib::Load(_T("collision_slope.x"),
                                                     PhysicsLib::PhysicsLib::ObjectType::Slide,
                                                     0.7f);
    PhysicsLib::PhysicsLib::SetTransform(slopeId,
                                         D3DXVECTOR3(4.0f, 0.75f, 0.0f),
                                         D3DXVECTOR3(0.0f, 0.0f, -D3DX_PI / 7.0f),
                                         D3DXVECTOR3(1.0f, 1.0f, 1.0f));
    g_worldObjects.push_back({ slopeMesh, slopeId, D3DXVECTOR3(4.0f, 0.75f, 0.0f), D3DXVECTOR3(1.0f, 1.0f, 1.0f), D3DXVECTOR3(0.0f, 0.0f, -D3DX_PI / 7.0f), D3DXCOLOR(0.76f, 0.62f, 0.36f, 1.0f), false });

    LPD3DXMESH wallMesh = CreateBoxMesh(1.0f, 3.0f, 8.0f);
    SaveCollisionMesh(wallMesh, _T("collision_wall.x"));
    const int wallId = PhysicsLib::PhysicsLib::Load(_T("collision_wall.x"),
                                                    PhysicsLib::PhysicsLib::ObjectType::Slide,
                                                    0.4f);
    PhysicsLib::PhysicsLib::SetTransform(wallId,
                                         D3DXVECTOR3(-6.0f, 1.5f, 0.0f),
                                         D3DXVECTOR3(0.0f, D3DXToRadian(18.0f), 0.0f),
                                         D3DXVECTOR3(1.0f, 1.0f, 1.0f));
    g_worldObjects.push_back({ wallMesh, wallId, D3DXVECTOR3(-6.0f, 1.5f, 0.0f), D3DXVECTOR3(1.0f, 1.0f, 1.0f), D3DXVECTOR3(0.0f, D3DXToRadian(18.0f), 0.0f), D3DXCOLOR(0.55f, 0.58f, 0.65f, 1.0f), false });

    LPD3DXMESH bigSphereMesh = CreateSphereMesh(2.0f);
    SaveCollisionMesh(bigSphereMesh, _T("collision_big_sphere.x"));
    const int bigSphereId = PhysicsLib::PhysicsLib::Load(_T("collision_big_sphere.x"),
                                                         PhysicsLib::PhysicsLib::ObjectType::Slide,
                                                         0.5f);
    PhysicsLib::PhysicsLib::SetTransform(bigSphereId,
                                         D3DXVECTOR3(6.5f, 2.0f, 4.5f),
                                         D3DXVECTOR3(0.0f, 0.0f, 0.0f),
                                         D3DXVECTOR3(1.0f, 1.0f, 1.0f));
    g_worldObjects.push_back({ bigSphereMesh, bigSphereId, D3DXVECTOR3(6.5f, 2.0f, 4.5f), D3DXVECTOR3(1.0f, 1.0f, 1.0f), D3DXVECTOR3(0.0f, 0.0f, 0.0f), D3DXCOLOR(0.75f, 0.45f, 0.30f, 1.0f), false });

    LPD3DXMESH movingPlatformMesh = CreateBoxMesh(3.0f, 0.4f, 3.0f);
    SaveCollisionMesh(movingPlatformMesh, _T("collision_moving_platform.x"));
    g_movingPlatformId = PhysicsLib::PhysicsLib::Load(_T("collision_moving_platform.x"),
                                                      PhysicsLib::PhysicsLib::ObjectType::MovingSlide,
                                                      0.9f);
    PhysicsLib::PhysicsLib::SetTransform(g_movingPlatformId,
                                         D3DXVECTOR3(0.0f, 2.5f, 7.0f),
                                         D3DXVECTOR3(0.0f, 0.0f, 0.0f),
                                         D3DXVECTOR3(1.0f, 1.0f, 1.0f));
    PhysicsLib::PhysicsLib::SetVelocity(g_movingPlatformId, D3DXVECTOR3(1.5f, 0.0f, 0.0f));
    g_worldObjects.push_back({ movingPlatformMesh, g_movingPlatformId, D3DXVECTOR3(0.0f, 2.5f, 7.0f), D3DXVECTOR3(1.0f, 1.0f, 1.0f), D3DXVECTOR3(0.0f, 0.0f, 0.0f), D3DXCOLOR(0.25f, 0.72f, 0.78f, 1.0f), false });

    LPD3DXMESH manyEdgesMesh = LoadSceneMeshFromX(_T("cubeManyEdges.x"));
    const float manyEdgesSpacingX = 7.2f;
    const float manyEdgesSpacingZ = 7.2f;
    const D3DXVECTOR3 manyEdgesBase(-7.2f, 0.75f, -5.4f);
    for (int row = 0; row < 10; ++row)
    {
        for (int col = 0; col < 5; ++col)
        {
            const D3DXVECTOR3 objectPosition(manyEdgesBase.x + col * manyEdgesSpacingX,
                                             manyEdgesBase.y,
                                             manyEdgesBase.z + row * manyEdgesSpacingZ);
            const D3DXVECTOR3 objectRotation(0.0f,
                                             (row * 5 + col) * (D3DX_PI / 10.0f),
                                             0.0f);
            const D3DXVECTOR3 objectScale(0.75f, 0.75f, 0.75f);
            const D3DXCOLOR objectColor(0.30f + 0.12f * col,
                                        0.35f + 0.10f * row,
                                        0.85f - 0.10f * row,
                                        1.0f);
            const int objectId = PhysicsLib::PhysicsLib::Load(_T("cubeManyEdges.x"),
                                                              PhysicsLib::PhysicsLib::ObjectType::Slide,
                                                              0.6f);
            PhysicsLib::PhysicsLib::SetTransform(objectId,
                                                 objectPosition,
                                                 objectRotation,
                                                 objectScale);
            g_worldObjects.push_back({ manyEdgesMesh, objectId, objectPosition, objectScale, objectRotation, objectColor, false });
        }
    }

    const D3DXVECTOR3 itemPositions[] =
    {
        D3DXVECTOR3(-2.0f, 0.5f, 2.0f),
        D3DXVECTOR3(0.5f, 0.5f, 3.5f),
        D3DXVECTOR3(3.0f, 0.5f, -1.0f),
        D3DXVECTOR3(5.0f, 0.8f, 1.5f),
        D3DXVECTOR3(7.0f, 0.5f, -3.5f),
    };
    const D3DXCOLOR itemColors[] =
    {
        D3DXCOLOR(0.95f, 0.86f, 0.30f, 1.0f),
        D3DXCOLOR(0.90f, 0.30f, 0.40f, 1.0f),
        D3DXCOLOR(0.30f, 0.75f, 0.95f, 1.0f),
        D3DXCOLOR(0.55f, 0.90f, 0.50f, 1.0f),
        D3DXCOLOR(0.75f, 0.55f, 0.95f, 1.0f),
    };

    LPD3DXMESH itemSphereMesh = CreateSphereMesh(0.25f);
    SaveCollisionMesh(itemSphereMesh, _T("collision_item_sphere.x"));

    for (int i = 0; i < 5; ++i)
    {
        const int itemId = PhysicsLib::PhysicsLib::Load(_T("collision_item_sphere.x"),
                                                        PhysicsLib::PhysicsLib::ObjectType::PassThrough,
                                                        0.0f);
        PhysicsLib::PhysicsLib::SetTransform(itemId,
                                             itemPositions[i],
                                             D3DXVECTOR3(0.0f, 0.0f, 0.0f),
                                             D3DXVECTOR3(1.0f, 1.0f, 1.0f));
        g_itemObjects.push_back({ itemSphereMesh, itemId, itemPositions[i], D3DXVECTOR3(1.0f, 1.0f, 1.0f), D3DXVECTOR3(0.0f, 0.0f, 0.0f), itemColors[i], false });
    }

    ResetPlayer();
    SyncSceneFromPhysics();
}

void InitSettingsDialog(HWND ownerWindow)
{
    g_settingsWindow = CreateWindowEx(WS_EX_TOOLWINDOW,
                                      _T("Window1"),
                                      _T("Settings"),
                                      WS_CAPTION | WS_BORDER | WS_VISIBLE,
                                      40,
                                      40,
                                      240,
                                      90,
                                      ownerWindow,
                                      NULL,
                                      GetModuleHandle(NULL),
                                      NULL);
    assert(g_settingsWindow != NULL);

    g_doubleJumpCheckBox = CreateWindow(_T("BUTTON"),
                                        _T("Enable double jump"),
                                        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                        16,
                                        18,
                                        190,
                                        28,
                                        g_settingsWindow,
                                        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_DOUBLE_JUMP_CHECKBOX)),
                                        GetModuleHandle(NULL),
                                        NULL);
    assert(g_doubleJumpCheckBox != NULL);
    SyncSettingsDialog();
}

void ResetPlayer()
{
    PhysicsLib::CharacterMover::Settings settings = g_playerMover.GetSettings();
    settings.shapeType = PhysicsLib::PhysicsLib::ShapeType::Sphere;
    settings.shapeOffset = D3DXVECTOR3(0.0f, 0.5f, 0.0f);
    settings.radius = 0.5f;
    settings.height = 0.0f;
    settings.moveSpeed = kPlayerSpeed;
    settings.groundAcceleration = kPlayerSpeed;
    settings.airAcceleration = kPlayerSpeed * 0.35f;
    settings.jumpVelocity = kJumpVelocity;
    settings.doubleJumpEnabled = false;
    settings.keepHorizontalVelocityOnJump = true;
    settings.groundDamping = 1.0f;
    settings.airDamping = 1.0f;
    g_playerMover.SetSettings(settings);
    g_playerMover.Reset(kPlayerStartPosition);
    SyncSettingsDialog();
}

void UpdatePlayer()
{
    const bool isWindowActive = (GetForegroundWindow() == g_mainWindow);
    bool isF1Pressed = isWindowActive && ((GetAsyncKeyState(VK_F1) & 0x8000) != 0);
    if (isF1Pressed && !g_prevF1Pressed)
    {
        ResetPlayer();
    }
    g_prevF1Pressed = isF1Pressed;

    bool isF2Pressed = isWindowActive && ((GetAsyncKeyState(VK_F2) & 0x8000) != 0);
    if (isF2Pressed && !g_prevF2Pressed)
    {
        const bool nextEnabled = !PhysicsLib::PhysicsLib::IsIntersectMultithreadEnabled();
        PhysicsLib::PhysicsLib::SetIntersectMultithreadEnabled(nextEnabled);
    }
    g_prevF2Pressed = isF2Pressed;

    bool isF3Pressed = isWindowActive && ((GetAsyncKeyState(VK_F3) & 0x8000) != 0);
    if (isF3Pressed && !g_prevF3Pressed)
    {
        PhysicsLib::CharacterMover::Settings settings = g_playerMover.GetSettings();
        settings.airControlEnabled = !settings.airControlEnabled;
        g_playerMover.SetSettings(settings);
    }
    g_prevF3Pressed = isF3Pressed;

    bool isF4Pressed = isWindowActive && ((GetAsyncKeyState(VK_F4) & 0x8000) != 0);
    if (isF4Pressed && !g_prevF4Pressed)
    {
        PhysicsLib::CharacterMover::Settings settings = g_playerMover.GetSettings();
        SetDoubleJumpEnabled(!settings.doubleJumpEnabled);
    }
    g_prevF4Pressed = isF4Pressed;

    bool isEscPressed = isWindowActive && ((GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0);
    if (isEscPressed && !g_prevEscPressed)
    {
        SetMouseCursorVisible(!g_isMouseCursorVisible);
    }
    g_prevEscPressed = isEscPressed;

    D3DXVECTOR3 movingPlatformDelta(0.0f, 0.0f, 0.0f);
    if (g_movingPlatformId >= 0)
    {
        const PhysicsLib::PhysicsLib::Transform beforeUpdate =
            PhysicsLib::PhysicsLib::GetTransform(g_movingPlatformId);
        PhysicsLib::PhysicsLib::Update();
        const PhysicsLib::PhysicsLib::Transform afterUpdate =
            PhysicsLib::PhysicsLib::GetTransform(g_movingPlatformId);
        movingPlatformDelta = afterUpdate.position - beforeUpdate.position;
    }
    else
    {
        PhysicsLib::PhysicsLib::Update();
    }

    if (g_playerMover.GetSupportObjectId() == g_movingPlatformId)
    {
        g_playerMover.SetPosition(g_playerMover.GetPosition() + movingPlatformDelta);
    }

    if (g_movingPlatformId >= 0)
    {
        PhysicsLib::PhysicsLib::Transform platformTransform = PhysicsLib::PhysicsLib::GetTransform(g_movingPlatformId);
        if (platformTransform.position.x > 4.0f)
        {
            PhysicsLib::PhysicsLib::SetVelocity(g_movingPlatformId, D3DXVECTOR3(-1.5f, 0.0f, 0.0f));
        }
        else if (platformTransform.position.x < -4.0f)
        {
            PhysicsLib::PhysicsLib::SetVelocity(g_movingPlatformId, D3DXVECTOR3(1.5f, 0.0f, 0.0f));
        }
    }

    D3DXVECTOR3 inputMove(0.0f, 0.0f, 0.0f);

    if (isWindowActive && (GetAsyncKeyState('W') & 0x8000))
    {
        inputMove.z += 1.0f;
    }
    if (isWindowActive && (GetAsyncKeyState('S') & 0x8000))
    {
        inputMove.z -= 1.0f;
    }
    if (isWindowActive && (GetAsyncKeyState('A') & 0x8000))
    {
        inputMove.x -= 1.0f;
    }
    if (isWindowActive && (GetAsyncKeyState('D') & 0x8000))
    {
        inputMove.x += 1.0f;
    }

    if (inputMove.x != 0.0f || inputMove.z != 0.0f)
    {
        const D3DXVECTOR3 cameraForward(-sinf(g_cameraYaw), 0.0f, cosf(g_cameraYaw));
        const D3DXVECTOR3 cameraRight(cosf(g_cameraYaw), 0.0f, sinf(g_cameraYaw));
        inputMove = cameraRight * inputMove.x + cameraForward * inputMove.z;
    }

    std::vector<int> passThroughIds;
    std::vector<int> solidIds;
    const bool isSpacePressed = isWindowActive && ((GetAsyncKeyState(VK_SPACE) & 0x8000) != 0);
    const bool jump = isSpacePressed && !g_prevSpacePressed;
    g_prevSpacePressed = isSpacePressed;
    g_playerMover.Update(inputMove, jump, &passThroughIds, &solidIds);

    for (size_t i = 0; i < passThroughIds.size(); ++i)
    {
        g_collectedItemIds.insert(passThroughIds[i]);
    }

    SyncSceneFromPhysics();
}

void UpdateCamera()
{
    const D3DXVECTOR3 playerPosition = g_playerMover.GetPosition();
    const D3DXVECTOR3 cameraTarget = playerPosition + D3DXVECTOR3(0.0f, 1.2f, 0.0f);
    const float horizontalDistance = g_cameraDistance * cosf(g_cameraPitch);
    const D3DXVECTOR3 offset(sinf(g_cameraYaw) * horizontalDistance,
                             sinf(g_cameraPitch) * g_cameraDistance,
                             -cosf(g_cameraYaw) * horizontalDistance);
    const D3DXVECTOR3 cameraPosition = cameraTarget + offset;
    const D3DXVECTOR3 cameraUp(0.0f, 1.0f, 0.0f);

    D3DXMatrixLookAtLH(&g_cameraView, &cameraPosition, &cameraTarget, &cameraUp);
    D3DXMatrixPerspectiveFovLH(&g_cameraProjection,
                               D3DXToRadian(45.0f),
                               (float)WINDOW_SIZE_W / WINDOW_SIZE_H,
                               0.1f,
                               1000.0f);
}

void SyncSceneFromPhysics()
{
    for (size_t i = 0; i < g_worldObjects.size(); ++i)
    {
        if (g_worldObjects[i].collisionId < 0)
        {
            continue;
        }

        PhysicsLib::PhysicsLib::Transform transform =
            PhysicsLib::PhysicsLib::GetTransform(g_worldObjects[i].collisionId);
        g_worldObjects[i].position = transform.position;
        g_worldObjects[i].rotation = transform.rotation;
        g_worldObjects[i].scale = transform.scale;
    }

    for (size_t i = 0; i < g_itemObjects.size(); ++i)
    {
        if (g_itemObjects[i].collisionId < 0)
        {
            continue;
        }

        PhysicsLib::PhysicsLib::Transform transform =
            PhysicsLib::PhysicsLib::GetTransform(g_itemObjects[i].collisionId);
        g_itemObjects[i].position = transform.position;
        g_itemObjects[i].rotation = transform.rotation;
        g_itemObjects[i].scale = transform.scale;
    }
}

LPD3DXMESH CreateBoxMesh(float width, float height, float depth)
{
    LPD3DXMESH mesh = NULL;
    HRESULT hResult = D3DXCreateBox(g_pd3dDevice, width, height, depth, &mesh, NULL);
    assert(hResult == S_OK);
    g_ownedSceneMeshes.push_back(mesh);
    return mesh;
}

LPD3DXMESH CreateSphereMesh(float radius)
{
    LPD3DXMESH mesh = NULL;
    HRESULT hResult = D3DXCreateSphere(g_pd3dDevice, radius, 24, 24, &mesh, NULL);
    assert(hResult == S_OK);
    g_ownedSceneMeshes.push_back(mesh);
    return mesh;
}

LPD3DXMESH LoadSceneMeshFromX(const TCHAR* path)
{
    LPD3DXMESH mesh = NULL;
    HRESULT hResult = D3DXLoadMeshFromX(path,
                                        D3DXMESH_SYSTEMMEM,
                                        g_pd3dDevice,
                                        NULL,
                                        NULL,
                                        NULL,
                                        NULL,
                                        &mesh);
    assert(hResult == S_OK);
    g_ownedSceneMeshes.push_back(mesh);
    return mesh;
}

void SaveCollisionMesh(LPD3DXMESH mesh, const TCHAR* path)
{
    std::vector<DWORD> adjacency(mesh->GetNumFaces() * 3);
    HRESULT hResult = mesh->GenerateAdjacency(0.0f, adjacency.data());
    assert(hResult == S_OK);

    hResult = D3DXSaveMeshToX(path,
                              mesh,
                              adjacency.data(),
                              NULL,
                              NULL,
                              0,
                              D3DXF_FILEFORMAT_TEXT);
    assert(hResult == S_OK);
}

void DrawMesh(LPD3DXMESH mesh,
              const D3DXVECTOR3& position,
              const D3DXVECTOR3& scale,
              const D3DXVECTOR3& rotation,
              const D3DXCOLOR& color,
              bool useTexture)
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

    hResult = g_pEffect->SetFloat("g_useTexture", useTexture ? 1.0f : 0.0f);
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

        hResult = mesh->DrawSubset(0);
        assert(hResult == S_OK);
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
    const PhysicsLib::CharacterMover::Settings moverSettings = g_playerMover.GetSettings();
    const bool airControlEnabled = moverSettings.airControlEnabled;
    const bool doubleJumpEnabled = moverSettings.doubleJumpEnabled;
    _stprintf_s(msg,
                _T("WASD: move  SPACE: jump  ESC: cursor=%s  F1: reset  F2: D3DXIntersect MT=%s  F3: AirControl=%s  F4: DoubleJump=%s  Items: %d/5  Pos(%.2f, %.2f, %.2f)"),
                g_isMouseCursorVisible ? _T("ON") : _T("OFF"),
                PhysicsLib::PhysicsLib::IsIntersectMultithreadEnabled() ? _T("ON") : _T("OFF"),
                airControlEnabled ? _T("ON") : _T("OFF"),
                doubleJumpEnabled ? _T("ON") : _T("OFF"),
                (int)g_collectedItemIds.size(),
                playerPosition.x,
                playerPosition.y,
                playerPosition.z);
    TextDraw(g_pFont, msg, 20, 72);

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
                 g_worldObjects[i].useTexture);
    }

    for (size_t i = 0; i < g_itemObjects.size(); i++)
    {
        if (g_collectedItemIds.find(g_itemObjects[i].collisionId) != g_collectedItemIds.end())
        {
            continue;
        }

        DrawMesh(g_itemObjects[i].mesh,
                 g_itemObjects[i].position,
                 g_itemObjects[i].scale,
                 g_itemObjects[i].rotation,
                 g_itemObjects[i].color,
                 g_itemObjects[i].useTexture);
    }

    DrawMesh(g_pCubeMesh,
             g_playerMover.GetPosition() + D3DXVECTOR3(0.0f, 0.5f, 0.0f),
             D3DXVECTOR3(1.0f, 1.0f, 1.0f),
             D3DXVECTOR3(0.0f, 0.0f, 0.0f),
             D3DXCOLOR(1.0f, 1.0f, 1.0f, 1.0f),
             true);

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
    ShowCursor(visible ? TRUE : FALSE);
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

void SetDoubleJumpEnabled(bool enabled)
{
    PhysicsLib::CharacterMover::Settings settings = g_playerMover.GetSettings();
    settings.doubleJumpEnabled = enabled;
    g_playerMover.SetSettings(settings);
    SyncSettingsDialog();
}

void SyncSettingsDialog()
{
    if (g_doubleJumpCheckBox == NULL)
    {
        return;
    }

    PhysicsLib::CharacterMover::Settings settings = g_playerMover.GetSettings();
    WPARAM checkState = BST_UNCHECKED;
    if (settings.doubleJumpEnabled)
    {
        checkState = BST_CHECKED;
    }

    SendMessage(g_doubleJumpCheckBox, BM_SETCHECK, checkState, 0);
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

LRESULT WINAPI MsgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_DOUBLE_JUMP_CHECKBOX && HIWORD(wParam) == BN_CLICKED)
        {
            const LRESULT checkState = SendMessage(g_doubleJumpCheckBox, BM_GETCHECK, 0, 0);
            SetDoubleJumpEnabled(checkState == BST_CHECKED);
            return 0;
        }
        break;

    case WM_MOUSEMOVE:
        OnMouseMove(lParam);
        return 0;

    case WM_MOUSEWHEEL:
        g_cameraDistance -= static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / WHEEL_DELTA;
        g_cameraDistance = ClampFloat(g_cameraDistance, 4.0f, 30.0f);
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
