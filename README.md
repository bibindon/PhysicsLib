

https://github.com/user-attachments/assets/e9586ce5-25a7-495f-8f62-7a17666bc9b9

※スライドチェックをONにしないと、上り坂に裏からぶつかった時に、時々、地面を突き抜けてしまう。
動画では再現していないが。
 
 # PhysicsLib

DirectX 9 向けの簡易キャラクター移動・衝突判定ライブラリである。

このライブラリは、厳密な剛体物理エンジンではなく、ゲーム内のプレイヤー移動を扱いやすくすることを目的とする。Xファイルで読み込んだ地面、坂、壁、移動床などに対して、重力、ジャンプ、接面判定、壁摺り、接平面移動、接触判定をまとめて扱う。

## できること

- Xファイルを衝突オブジェクトとして読み込む
- 点、球、円柱のプレイヤー判定形状を使う
- D3DXIntersect による接面判定を行う
- 距離ベースの接触判定を行う
- 重力とジャンプを適用する
- 2段ジャンプ、多段ジャンプを切り替える
- 慣性あり、慣性なしの移動を切り替える
- 壁摺りと追加のスライドチェックを切り替える
- 接平面に沿った移動を行う
- 空中移動のON/OFFを切り替える
- 移動する床にプレイヤーを追従させる
- 4分木による2D空間分割で接面判定候補を絞り込む
- カメラと注視点の間に障害物がある場合にカメラ位置を補正する

## サンプル

`simple-directx9` プロジェクトにサンプルがある。

現在のサンプルで確認できる内容は以下である。

- `WASD` または方向キーで移動
- `Shift` を押しながら移動すると移動速度3倍
- `Space` でジャンプ
- `Esc` でマウスカーソル表示のON/OFF
- マウス移動でカメラ回転
- マウスホイールでカメラ距離変更
- 設定ダイアログによる各機能のON/OFF
- 地面、坂道、複数の角度の坂、壁、交差した壁、大きな球、移動床、アイテム球
- プレイヤーの円柱判定
- `cube.png` によるプレイヤー立方体の向き表示

プレイヤー表示用の `cube.png` は、Blender由来のモデルを意識して `-Z` 側を `FRONT` としている。

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

フォーカスモードONでは、カメラが向いている方向を前として扱う。`S` を押すと、プレイヤーはカメラ前方を向いたまま後退する。

フォーカスモードOFFでは、移動方向はキー入力通りであり、プレイヤーの見た目の向きだけが徐々に移動方向へ追従する。

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
- `CharacterMover::Update()`
- `CameraMover::ResolvePosition()`

## 衝突オブジェクト

`PhysicsLib::Load()` でXファイルを読み込み、以下の種類として登録する。

- `ObjectType::PassThrough`: 通過可能な接触判定用オブジェクト
- `ObjectType::Slide`: 静止している壁、地面、坂など
- `ObjectType::MovingSlide`: 速度を持つ移動床など

`PassThrough` はアイテム取得などの用途に使う。`Slide` と `MovingSlide` は接面判定、壁摺り、接地判定の対象である。

## プレイヤー移動

`CharacterMover` は、入力方向、ジャンプ入力、重力、接面判定結果を使ってプレイヤー位置と速度を更新する。

プレイヤー側の判定形状は `ShapeType` で指定する。

- `ShapeType::Point`
- `ShapeType::Sphere`
- `ShapeType::Cylinder`

現在のサンプルでは、プレイヤーに `Cylinder` を使っている。

## カメラ

`CameraMover` は、注視点と希望カメラ位置を受け取り、カメラ自動移動がONの場合に障害物を避けた位置を返す。

カメラと注視点の間に物体がある場合は、障害物の手前までカメラを近づける。ただし、設定された最短距離より近づきすぎないようにする。

## ビルド

Visual Studio のMSBuildでビルドする。

このリポジトリでは以下のMSBuildを想定している。

```text
C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe
```

`Debug|x64` のビルド例である。

```text
MSBuild.exe simple-directx9.sln /t:Build /p:Configuration=Debug /p:Platform=x64
```

`simple-directx9` の `cube.x` と `cube.png` は、ビルド時に出力フォルダへコピーされる。

## 方針

- 障害物側はXファイルのメッシュを使い、D3DXIntersectで判定する
- プレイヤー側は点、球、円柱を代表点レイキャストで近似する
- Y方向に大量のオブジェクトが積み上がる前提ではないため、高速化はX/Z平面の4分木で行う
- ゲームのキャラクター移動として自然に扱えることを優先する

## 注意

- 厳密な汎用物理エンジンではない
- 球と円柱の接面判定は代表点による近似である
- 一部の古いDirectX Effect機能を使っているため、ビルド時に `FXC : warning X4717` が出る場合がある

衝突判定のタイプで直方体を選べるが全くまともに動作しない。
