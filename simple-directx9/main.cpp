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
#include <vector>

#define SAFE_RELEASE(p) { if (p) { (p)->Release(); (p) = NULL; } }

const int WINDOW_SIZE_W = 1600;
const int WINDOW_SIZE_H = 900;
const float kPlayerSpeed = 0.18f;
const float kJumpVelocity = 0.10f;
const float kGravityPerFrame = 9.8f / 3600.0f;
const D3DXVECTOR3 kPlayerStartPosition(0.0f, 0.0f, 0.0f);
const size_t kMovingPlatformIndex = 4;

struct SceneObject
{
    LPD3DXMESH mesh;
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
LPD3DXMESH g_pSphereMesh = NULL;
LPD3DXMESH g_pBoxMesh = NULL;
std::vector<D3DMATERIAL9> g_pMaterials;
std::vector<LPDIRECT3DTEXTURE9> g_pTextures;
DWORD g_dwNumMaterials = 0;
LPD3DXEFFECT g_pEffect = NULL;
bool g_bClose = false;
std::vector<SceneObject> g_worldObjects;
std::vector<SceneObject> g_itemObjects;
D3DXVECTOR3 g_playerPosition(0.0f, 0.0f, 0.0f);
float g_playerVelocityY = 0.0f;
bool g_isGrounded = true;
float g_movingPlatformPhase = 0.0f;
bool g_prevF1Pressed = false;

static void TextDraw(LPD3DXFONT pFont, TCHAR* text, int X, int Y);
static void InitD3D(HWND hWnd);
static void InitScene();
static void ResetPlayer();
static void UpdatePlayer();
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

    InitD3D(hWnd);
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

    hResult = D3DXCreateSphere(g_pd3dDevice, 0.5f, 24, 24, &g_pSphereMesh, NULL);
    assert(hResult == S_OK);

    hResult = D3DXCreateBox(g_pd3dDevice, 1.0f, 1.0f, 1.0f, &g_pBoxMesh, NULL);
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
    g_movingPlatformPhase = 0.0f;

    g_worldObjects.push_back({ g_pBoxMesh,
                               D3DXVECTOR3(0.0f, -0.5f, 0.0f),
                               D3DXVECTOR3(24.0f, 1.0f, 24.0f),
                               D3DXVECTOR3(0.0f, 0.0f, 0.0f),
                               D3DXCOLOR(0.35f, 0.70f, 0.35f, 1.0f),
                               false });

    g_worldObjects.push_back({ g_pBoxMesh,
                               D3DXVECTOR3(4.0f, 0.75f, 0.0f),
                               D3DXVECTOR3(6.0f, 0.8f, 4.0f),
                               D3DXVECTOR3(0.0f, 0.0f, -D3DX_PI / 7.0f),
                               D3DXCOLOR(0.76f, 0.62f, 0.36f, 1.0f),
                               false });

    g_worldObjects.push_back({ g_pBoxMesh,
                               D3DXVECTOR3(-6.0f, 1.5f, 0.0f),
                               D3DXVECTOR3(1.0f, 3.0f, 8.0f),
                               D3DXVECTOR3(0.0f, 0.0f, 0.0f),
                               D3DXCOLOR(0.55f, 0.58f, 0.65f, 1.0f),
                               false });

    g_worldObjects.push_back({ g_pSphereMesh,
                               D3DXVECTOR3(6.5f, 2.0f, 4.5f),
                               D3DXVECTOR3(4.0f, 4.0f, 4.0f),
                               D3DXVECTOR3(0.0f, 0.0f, 0.0f),
                               D3DXCOLOR(0.75f, 0.45f, 0.30f, 1.0f),
                               false });

    g_worldObjects.push_back({ g_pBoxMesh,
                               D3DXVECTOR3(0.0f, 2.5f, 7.0f),
                               D3DXVECTOR3(3.0f, 0.4f, 3.0f),
                               D3DXVECTOR3(0.0f, 0.0f, 0.0f),
                               D3DXCOLOR(0.25f, 0.72f, 0.78f, 1.0f),
                               false });

    g_itemObjects.push_back({ g_pSphereMesh,
                              D3DXVECTOR3(-2.0f, 0.5f, 2.0f),
                              D3DXVECTOR3(0.5f, 0.5f, 0.5f),
                              D3DXVECTOR3(0.0f, 0.0f, 0.0f),
                              D3DXCOLOR(0.95f, 0.86f, 0.30f, 1.0f),
                              false });
    g_itemObjects.push_back({ g_pSphereMesh,
                              D3DXVECTOR3(0.5f, 0.5f, 3.5f),
                              D3DXVECTOR3(0.5f, 0.5f, 0.5f),
                              D3DXVECTOR3(0.0f, 0.0f, 0.0f),
                              D3DXCOLOR(0.90f, 0.30f, 0.40f, 1.0f),
                              false });
    g_itemObjects.push_back({ g_pSphereMesh,
                              D3DXVECTOR3(3.0f, 0.5f, -1.0f),
                              D3DXVECTOR3(0.5f, 0.5f, 0.5f),
                              D3DXVECTOR3(0.0f, 0.0f, 0.0f),
                              D3DXCOLOR(0.30f, 0.75f, 0.95f, 1.0f),
                              false });
    g_itemObjects.push_back({ g_pSphereMesh,
                              D3DXVECTOR3(5.0f, 0.8f, 1.5f),
                              D3DXVECTOR3(0.5f, 0.5f, 0.5f),
                              D3DXVECTOR3(0.0f, 0.0f, 0.0f),
                              D3DXCOLOR(0.55f, 0.90f, 0.50f, 1.0f),
                              false });
    g_itemObjects.push_back({ g_pSphereMesh,
                              D3DXVECTOR3(7.0f, 0.5f, -3.5f),
                              D3DXVECTOR3(0.5f, 0.5f, 0.5f),
                              D3DXVECTOR3(0.0f, 0.0f, 0.0f),
                              D3DXCOLOR(0.75f, 0.55f, 0.95f, 1.0f),
                              false });

    ResetPlayer();
}

void ResetPlayer()
{
    g_playerPosition = kPlayerStartPosition;
    g_playerVelocityY = 0.0f;
    g_isGrounded = true;
}

void UpdatePlayer()
{
    g_movingPlatformPhase += 0.03f;
    if (g_worldObjects.size() > kMovingPlatformIndex)
    {
        g_worldObjects[kMovingPlatformIndex].position.x = sinf(g_movingPlatformPhase) * 4.0f;
        g_worldObjects[kMovingPlatformIndex].position.y = 2.5f;
        g_worldObjects[kMovingPlatformIndex].position.z = 7.0f;
    }

    bool isF1Pressed = (GetAsyncKeyState(VK_F1) & 0x8000) != 0;
    if (isF1Pressed && !g_prevF1Pressed)
    {
        ResetPlayer();
    }
    g_prevF1Pressed = isF1Pressed;

    D3DXVECTOR3 move(0.0f, 0.0f, 0.0f);

    if (GetAsyncKeyState('W') & 0x8000)
    {
        move.z += 1.0f;
    }
    if (GetAsyncKeyState('S') & 0x8000)
    {
        move.z -= 1.0f;
    }
    if (GetAsyncKeyState('A') & 0x8000)
    {
        move.x -= 1.0f;
    }
    if (GetAsyncKeyState('D') & 0x8000)
    {
        move.x += 1.0f;
    }

    if (move.x != 0.0f || move.z != 0.0f)
    {
        D3DXVec3Normalize(&move, &move);
        move *= kPlayerSpeed;
    }

    if ((GetAsyncKeyState(VK_SPACE) & 0x8000) && g_isGrounded)
    {
        g_playerVelocityY = kJumpVelocity;
        g_isGrounded = false;
    }

    g_playerVelocityY -= kGravityPerFrame;
    g_playerPosition += move;
    g_playerPosition.y += g_playerVelocityY;

    if (g_playerPosition.y <= 0.0f)
    {
        g_playerPosition.y = 0.0f;
        g_playerVelocityY = 0.0f;
        g_isGrounded = true;
    }
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

    D3DXVECTOR3 cameraPosition(0.0f, 14.0f, -22.0f);
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

    SAFE_RELEASE(g_pBoxMesh);
    SAFE_RELEASE(g_pSphereMesh);
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
                _T("WASD: move  SPACE: jump  F1: reset  Player Pos(%.2f, %.2f, %.2f)"),
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
        DrawMesh(g_itemObjects[i].mesh,
                 g_itemObjects[i].position,
                 g_itemObjects[i].scale,
                 g_itemObjects[i].rotation,
                 g_itemObjects[i].color,
                 g_itemObjects[i].useTexture);
    }

    DrawMesh(g_pCubeMesh,
             g_playerPosition + D3DXVECTOR3(0.0f, 0.5f, 0.0f),
             D3DXVECTOR3(0.8f, 1.0f, 0.8f),
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
