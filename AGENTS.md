# AGENTS.md

## Build

```
C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe simple-directx9.sln /t:Build /p:Configuration=Release /p:Platform=x64
```

- Use **x64** only. x86/Win32 is present but not the primary target.
- Toolset: **v145** (VS 2022), C++20, Level3 warnings, conformance mode on, multiprocessor compilation.
- NuGet restore is required first. Package: `Microsoft.DXSDK.D3DX 9.29.952.8` (directly under `packages/`).
- `.x` and `.png` files in `simple-directx9/` are `CopyToOutputDirectory=PreserveNewest`.
- `.fx` shaders compile via FXC as Effect shader model 2.0. Warning `X4717` from FXC is expected (legacy DirectX Effects).

### MSBuild environment quirk (critical)
If both `Path` and `PATH` environment variables exist when running MSBuild, you will get **MSB6001**. Before building, deduplicate them into `Path` only:

```powershell
if (Test-Path env:PATH) { $env:Path = $env:PATH; Remove-Item env:PATH }
```

## Solution structure

| Project | Type | Purpose |
|---------|------|---------|
| `PhysicsLib` | Static library (`PhysicsLib.vcxproj`) | Character movement & collision library |
| `simple-directx9` | Windows app (`simple-directx9.vcxproj`) | Demo/sample, depends on PhysicsLib |

`simple-directx9` references PhysicsLib as a project reference with `ReferenceOutputAssembly=false` (links source objects).

## Encoding & style

- **Line endings**: CRLF everywhere.
- **UTF-8 with BOM**: `.cpp`, `.h`, `.vcxproj`, `.sln`, `.csv`, `.fx`, `.config`
- **UTF-8 without BOM**: `.fx` files and `.X` files (DirectX `.X` mesh files)
- **No ternary operator** (`?:`) anywhere.
- **DirectX coordinate system**: Y-axis is **up** (not depth). 1 unit = 1 meter.
- Naming: PascalCase types, camelCase locals, `k` prefix for constants, `g_` prefix for file-static globals.

## API & architecture

- `PhysicsLib::PhysicsLib` — static class. `Initialize()` requires a `LPDIRECT3DDEVICE9`. All methods are static.
- `CharacterMover` — per-instance player state (position, velocity, grounded, jump counts, dash).
- `CameraMover` — resolves camera position around obstacles.
- Collision uses `D3DXIntersect` on X-file meshes. Player shape (Point/Sphere/Cylinder/Cuboid) is approximated via representative-point raycasts.
- 2D quadtree (X/Z plane) accelerates mesh selection. Max depth 5, 4 objects per node.
- Settings dialog (`ShowSettingsDialog`) is built into the library, not the sample.

### Object types
`PhysicsLib::Load(path, type, friction)` returns an int handle. Types: `PassThrough` (contact-only), `Slide` (static collision), `MovingSlide` (velocity-driven).

### CSV scene files
- `XFileListRender.csv` — auto-loaded at startup (ID, FileName, Pos, Rot, Scale)
- `XFileListPhysics.csv` — manual-load via settings dialog (adds Type=Collision/NonCollision, Move=y/n columns)
- `XFileListMove.csv` — moving platform waypoints (ID, RenderID, PhysicsID, Pos, Rot, Scale, Start, End, Duration)

### Player shape (cube.x)
`cube.x` is the player model: Y=0.0~1.0 (feet at origin), X/Z=-0.5~0.5. The sample draws it, but collision uses `CharacterMover`'s shape (default: Cylinder).

## Key gotchas

- `CheckCollide` slide-check must be ON to prevent tunneling through terrain from behind slopes (see README warning).
- `Cuboid` shape uses 8 corner rays; orientation auto-follows movement direction with dialog rotation as an offset.
- `d3dx9.lib` vs `d3dx9d.lib` is selected at compile time via `#if defined(DEBUG) || defined(_DEBUG)`.
- Scene .x files embed material colors; CSV has no color column for render objects.
- `UpdateCsvTransform()` computes velocity from position delta between calls — useful for syncing external moving objects.
