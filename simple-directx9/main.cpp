#pragma comment( lib, "d3d9.lib" )
#if defined(DEBUG) || defined(_DEBUG)
#pragma comment( lib, "d3dx9d.lib" )
#else
#pragma comment( lib, "d3dx9.lib" )
#endif

#include <d3d9.h>
#include <d3dx9.h>
#include <tchar.h>
#include <cassert>
#include <crtdbg.h>
#include <set>
#include <string>
#include <vector>

#include "..\PhysicsLib\PhysicsLib.h"

#define SAFE_RELEASE(p) { if (p) { (p)->Release(); (p) = NULL; } }

const int WINDOW_SIZE_W = 1600;
const int WINDOW_SIZE_H = 900;
const float kPlayerSpeed = 6.0f;
const float kJumpVelocity = 2.0f;
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
D3DXVECTOR3 g_playerPosition(0.0f, 0.0f, 0.0f);
D3DXVECTOR3 g_playerMoveVector(0.0f, 0.0f, 0.0f);
bool g_isGrounded = true;
int g_movingPlatformId = -1;
int g_supportObjectId = -1;
std::set<int> g_collectedItemIds;
bool g_prevF1Pressed = false;

static void TextDraw(LPD3DXFONT pFont, TCHAR* text, int X, int Y);
static void InitD3D(HWND hWnd);
static void InitScene();
static void ResetPlayer();
static void UpdatePlayer();
static void SyncSceneFromPhysics();
static LPD3DXMESH CreateBoxMesh(float width, float height, float depth);
static LPD3DXMESH CreateSphereMesh(float radius);
static void SaveCollisionMesh(LPD3DXMESH mesh, const TCHAR* path);
static void DrawMesh(LPD3DXMESH mesh,
                     const D3DXVECTOR3& position,
                     const D3DXVECTOR3& scale,
                     const D3DXVECTOR3& rotation,
                     const D3DXCOLOR& color,
                     bool useTexture);
static void Cleanup();
static void Render();
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

    InitD3D(hWnd);
    PhysicsLib::PhysicsLib::Initialize();
    InitScene();
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
    d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_DEFAULT;

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

void ResetPlayer()
{
    g_playerPosition = kPlayerStartPosition;
    g_playerMoveVector = D3DXVECTOR3(0.0f, 0.0f, 0.0f);
    g_isGrounded = true;
    g_supportObjectId = -1;
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

    if (g_supportObjectId == g_movingPlatformId)
    {
        g_playerPosition += movingPlatformDelta;
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

    if (false)
    {
        if (g_isGrounded && isWindowActive && (GetAsyncKeyState('W') & 0x8000))
        {
            inputMove.z += 1.0f;
        }
        if (g_isGrounded && isWindowActive && (GetAsyncKeyState('S') & 0x8000))
        {
            inputMove.z -= 1.0f;
        }
        if (g_isGrounded && isWindowActive && (GetAsyncKeyState('A') & 0x8000))
        {
            inputMove.x -= 1.0f;
        }
        if (g_isGrounded && isWindowActive && (GetAsyncKeyState('D') & 0x8000))
        {
            inputMove.x += 1.0f;
        }
    }
    else
    {
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
    }

    if (inputMove.x != 0.0f || inputMove.z != 0.0f)
    {
        D3DXVec3Normalize(&inputMove, &inputMove);
        inputMove *= kPlayerSpeed;
    }

    g_playerMoveVector.x += inputMove.x;
    g_playerMoveVector.z += inputMove.z;

    if (isWindowActive && (GetAsyncKeyState(VK_SPACE) & 0x8000))
    {
        g_playerMoveVector.y = kJumpVelocity;
    }

    const float playerRadius = 0.5f;
    const D3DXVECTOR3 playerShapeOffset(0.0f, playerRadius, 0.0f);
    const D3DXVECTOR3 playerShapePosition = g_playerPosition + playerShapeOffset;
    D3DXVECTOR3 correctedPosition = playerShapePosition;
    D3DXVECTOR3 nextMoveVector = g_playerMoveVector;
    std::vector<int> passThroughIds;
    std::vector<int> solidIds;
    PhysicsLib::PhysicsLib::CheckCollide(playerShapePosition,
                                         g_playerMoveVector,
                                         PhysicsLib::PhysicsLib::ShapeType::Sphere,
                                         &correctedPosition,
                                         &nextMoveVector,
                                         &passThroughIds,
                                         &solidIds,
                                         playerRadius);

    if (nextMoveVector.y == 0.0f && correctedPosition.y <= playerShapePosition.y)
    {
        g_isGrounded = true;
        g_supportObjectId = solidIds.empty() ? -1 : solidIds.front();
    }
    else
    {
        g_isGrounded = false;
        g_supportObjectId = -1;
    }

    for (size_t i = 0; i < passThroughIds.size(); ++i)
    {
        g_collectedItemIds.insert(passThroughIds[i]);
    }

    g_playerPosition = correctedPosition - playerShapeOffset;
    g_playerMoveVector = nextMoveVector;
    SyncSceneFromPhysics();
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
    D3DXMATRIX matView;
    D3DXMATRIX matProj;
    D3DXMATRIX matWorldViewProj;

    D3DXMatrixScaling(&matScale, scale.x, scale.y, scale.z);
    D3DXMatrixRotationX(&matRotationX, rotation.x);
    D3DXMatrixRotationY(&matRotationY, rotation.y);
    D3DXMatrixRotationZ(&matRotationZ, rotation.z);
    D3DXMatrixTranslation(&matTranslation, position.x, position.y, position.z);

    matWorld = matScale * matRotationX * matRotationY * matRotationZ * matTranslation;

    D3DXVECTOR3 cameraPosition(0.0f, 7.0f, -16.0f);
    D3DXVECTOR3 cameraTarget(0.0f, 1.5f, 3.0f);
    D3DXVECTOR3 cameraUp(0.0f, 1.0f, 0.0f);
    D3DXMatrixLookAtLH(&matView, &cameraPosition, &cameraTarget, &cameraUp);

    D3DXMatrixPerspectiveFovLH(&matProj,
                               D3DXToRadian(45.0f),
                               (float)WINDOW_SIZE_W / WINDOW_SIZE_H,
                               0.1f,
                               1000.0f);

    matWorldViewProj = matWorld * matView * matProj;

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
    SAFE_RELEASE(g_pFont);
    SAFE_RELEASE(g_pd3dDevice);
    SAFE_RELEASE(g_pD3D);
}

void Render()
{
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

    TCHAR msg[256];
    _stprintf_s(msg,
                _T("WASD: move  SPACE: jump  F1: reset  Items: %d/5  Pos(%.2f, %.2f, %.2f)"),
                (int)g_collectedItemIds.size(),
                g_playerPosition.x,
                g_playerPosition.y,
                g_playerPosition.z);
    TextDraw(g_pFont, msg, 20, 20);

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
             g_playerPosition + D3DXVECTOR3(0.0f, 0.5f, 0.0f),
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

LRESULT WINAPI MsgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_DESTROY:
        PostQuitMessage(0);
        g_bClose = true;
        return 0;
    }

    return DefWindowProc(hWnd, msg, wParam, lParam);
}
