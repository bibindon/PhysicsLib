
https://github.com/user-attachments/assets/e9586ce5-25a7-495f-8f62-7a17666bc9b9

※スライドチェックをONにしないと、上り坂に裏からぶつかった時に、時々、地面を突き抜けてしまう。
動画では再現していないが。
 
 # PhysicsLib

DirectX 9 向けの簡易キャラクター移動・衝突判定ライブラリである。

このライブラリは、厳密な剛体物理エンジンではなく、ゲーム内のプレイヤー移動を扱いやすくすることを目的とする。Xファイルで読み込んだ地面、坂、壁、移動床などに対して、重力、ジャンプ、接面判定、壁摺り、接平面移動、接触判定をまとめて扱う。

## できること

- Xファイルを衝突オブジェクトとして読み込む
- 点、球、円柱、直方体のプレイヤー判定形状を使う
- D3DXIntersect による接面判定を行う
- 距離ベースの接触判定を行う
- 重力とジャンプを適用する
- 2段ジャンプ、多段ジャンプを切り替える
- 慣性あり、慣性なしの移動を切り替える
- 慣性の強さを 0.0～1.0 で調整する
- 壁摺りと追加のスライドチェックを切り替える
- 接平面に沿った移動を行う
- 空中移動のON/OFFを切り替える
- 移動する床にプレイヤーを追従させる
- 4分木による2D空間分割で接面判定候補を絞り込む
- カメラと注視点の間に障害物がある場合にカメラ位置を補正する
- CSVファイルから衝突オブジェクトを一括読み込みする
- 描画用オブジェクトをCSVから読み込む
- 移動オブジェクトの位置・速度を外部から更新する

## サンプル

`simple-directx9` プロジェクトにサンプルがある。

現在のサンプルで確認できる内容は以下である。

- `WASD` または方向キーで移動
- `Shift` を押しながら移動するとダッシュ
- `Space` でジャンプ
- `Esc` でマウスカーソル表示のON/OFF
- マウス移動でカメラ回転
- マウスホイールでカメラ距離変更
- 設定ダイアログによる各機能のON/OFFと数値設定
- 地面、でこぼこ地面、坂道、壁、交差した壁、大きな球、移動床、アイテム球
- プレイヤーの円柱判定
- `cube.png` によるプレイヤー立方体の向き表示

プレイヤー表示用の `cube.x` は、Y=0.0～1.0（足元が原点）の立方体である。XとZは-0.5～0.5。

### CSVファイル

シーンの構成は3つのCSVファイルで管理される。

| ファイル | 用途 |
|---------|------|
| `XFileListRender.csv` | 描画用オブジェクト（起動時に自動読み込み） |
| `XFileListPhysics.csv` | 衝突判定用オブジェクト（設定ダイアログの「ファイル読込」で手動読み込み） |

`XFileListRender.csv` のフォーマット:
```
ID,FileName,PosX,PosY,PosZ,RotX,RotY,RotZ,Scale
```

`XFileListPhysics.csv` のフォーマット:
```
ID,FileName,PosX,PosY,PosZ,RotX,RotY,RotZ,Scale,Type,Move
```

`Type` 列は `Collision`（衝突あり）または `NonCollision`（通過可能/アイテム）。
`Move` 列は `y` で `MovingSlide` として登録。

## 設定ダイアログ

サンプル起動時に、ライブラリ側の設定ダイアログが表示される。

主な項目は以下である。

- 2段ジャンプ
- 多段ジャンプ
- 重力
- スライド
- スライドチェック
- 高速化
- 慣性
- 接触判定
- 接面判定
- 接平面移動
- 空中移動
- 移動する床
- カメラ自動移動
- フォーカスモード
- 判定形状（Point / Sphere / Cylinder / Cuboid）
- 球の半径
- 円柱の半径 / 円柱の高さ
- 直方体の横 / 直方体の高さ / 直方体の奥行き
- 直方体の回転X / Y / Z
- 慣性の強さ (0.00～1.00)
- 歩き速度 / ダッシュ速度
- ファイル読込（CSVから衝突オブジェクトを読み込み）
- 慣性

フォーカスモードONでは、カメラが向いている方向を前として扱う。`S` を押すと、プレイヤーはカメラ前方を向いたまま後退する。

フォーカスモードOFFでは、移動方向はキー入力通りであり、プレイヤーの見た目の向きだけが徐々に移動方向へ追従する。

カメラ自動移動はデフォルトON。フォーカスモードはデフォルトOFF。

## 主なAPI

- `PhysicsLib::Initialize()`
- `PhysicsLib::Finalize()`
- `PhysicsLib::Update()`
- `PhysicsLib::ShowSettingsDialog()`
- `PhysicsLib::SetResetCallback()`
- `PhysicsLib::Load()`
- `PhysicsLib::SetTransform()`
- `PhysicsLib::SetVelocity()`
- `PhysicsLib::GetTransform()`
- `PhysicsLib::CheckCollide()`
- `PhysicsLib::CheckContact()`
- `PhysicsLib::ResolveCameraCollision()`
- `PhysicsLib::IsFocusModeEnabled()`
- `PhysicsLib::GetShapeType()`
- `PhysicsLib::GetRadius()`
- `PhysicsLib::GetCylinderRadius()` / `GetCylinderHeight()`
- `PhysicsLib::GetCuboidWidth()` / `GetCuboidHeight()` / `GetCuboidDepth()`
- `PhysicsLib::GetCuboidRotX()` / `GetCuboidRotY()` / `GetCuboidRotZ()`
- `PhysicsLib::GetWalkSpeed()` / `GetDashSpeed()`
- `PhysicsLib::LoadFromCsv()`
- `PhysicsLib::GetCsvFileName()` / `GetCsvObjectId()`
- `PhysicsLib::UpdateCsvTransform()`
- `CharacterMover::Update()`
- `CameraMover::ResolvePosition()`

## 衝突オブジェクト

`PhysicsLib::Load()` でXファイルを読み込み、以下の種類として登録する。

- `ObjectType::PassThrough`: 通過可能な接触判定用オブジェクト
- `ObjectType::Slide`: 静止している壁、地面、坂など
- `ObjectType::MovingSlide`: 速度を持つ移動床など

`PassThrough` はアイテム取得などの用途に使う。`Slide` と `MovingSlide` は接面判定、壁摺り、接地判定の対象である。

`UpdateCsvTransform()` を使うと、CSVのIDで指定した物理オブジェクトの位置・回転・スケールを外部から更新できる。ライブラリ側が前回位置との差分から速度を自動計算するため、移動体の同期に便利である。

## プレイヤー移動

`CharacterMover` は、入力方向、ジャンプ入力、重力、接面判定結果を使ってプレイヤー位置と速度を更新する。

プレイヤー側の判定形状は `ShapeType` で指定する。

- `ShapeType::Point`
- `ShapeType::Sphere`
- `ShapeType::Cylinder`
- `ShapeType::Cuboid`

現在のサンプルでは、プレイヤーに `Cylinder` を使っている。

`Cuboid` を選択した場合、直方体の8頂点を代表点としてレイキャストする。直方体の向きはプレイヤーの移動方向に自動追従し、設定ダイアログの回転値はオフセットとして加算される。

## カメラ

`CameraMover` は、注視点と希望カメラ位置を受け取り、カメラ自動移動がONの場合に障害物を避けた位置を返す。

カメラと注視点の間に物体がある場合は、障害物の手前までカメラを近づける。ただし、設定された最短距離より近づきすぎないようにする。

## ビルド

Visual Studio のMSBuildでビルドする。

このリポジトリでは以下のMSBuildを想定している。

```text
C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe
```

`Release|x64` のビルド例である。

```text
MSBuild.exe simple-directx9.sln /t:Build /p:Configuration=Release /p:Platform=x64
```

`simple-directx9` の `.x` ファイルと `.png` ファイルは、ビルド時に出力フォルダへコピーされる。

## 方針

- 障害物側はXファイルのメッシュを使い、D3DXIntersectで判定する
- プレイヤー側は点、球、円柱、直方体を代表点レイキャストで近似する
- Y方向に大量のオブジェクトが積み上がる前提ではないため、高速化はX/Z平面の4分木で行う
- ゲームのキャラクター移動として自然に扱えることを優先する
- 描画用Xファイルと衝突判定用Xファイルは分離する（マテリアル色をXファイルに埋め込み、CSVでは色指定不要）
- 衝突判定のメッシュ登録はCSV経由で行う

## 注意

- 厳密な汎用物理エンジンではない
- 球と円柱の接面判定は代表点による近似である
- 直方体の接面判定は8頂点レイキャストによる近似である
- 一部の古いDirectX Effect機能を使っているため、ビルド時に `FXC : warning X4717` が出る場合がある
