# PhysicsLib

DirectX 9 向けの衝突判定ライブラリです。

移動ベクトルを受け取って、

- 重力を適用する
- 壁摺りを行う
- 接触相手を返す
- 次フレーム用の移動ベクトルを返す

ところまでをまとめて扱います。

## できること

- アイテムに触れたことを検出する
- 坂道を上る
- 壁に沿って滑る
- 動く床に乗る
- 点、球、円柱の移動体を扱う

## サンプル

`simple-directx9` プロジェクトにサンプルがあります。

現在のサンプルでは、以下を確認できます。

- `WASD` で移動
- `Space` でジャンプ
- `F1` で位置リセット
- 地面、坂道、壁、大きな球体、動く床
- 複数のアイテム球

動画:

https://github.com/user-attachments/assets/292badbc-a9be-48cc-9c39-0369838ffa25

## 主な API

- `PhysicsLib::Initialize()`
- `PhysicsLib::Finalize()`
- `PhysicsLib::Update()`
- `PhysicsLib::Load()`
- `PhysicsLib::SetTransform()`
- `PhysicsLib::SetVelocity()`
- `PhysicsLib::GetTransform()`
- `PhysicsLib::CheckCollide()`

`CheckCollide()` は現在位置と移動ベクトルを受け取り、補正後の位置と次フレーム用の移動ベクトルを返します。

## 現状の方針

- 障害物側は `D3DXIntersect` を使ってメッシュ形状ベースで判定します
- 移動体側の形状は `ShapeType` で指定します
- 重力はライブラリ側で適用します
- 水平方向の減衰もライブラリ側で適用します

## メモ

- 現状は「自然に動けるキャラクター移動」を優先した実装です
- 厳密な剛体物理や汎用物理エンジンではありません

## TODO

- ジャンプ中に `XZ` 軸方向の移動ができないモードの時、坂を上れなくなる
- D3DXIntersect関数を複数スレッドで呼ぶことができるが内部でロックしているのか、速度は変わらない。
- 4分木空間分割、8分木空間分割でD3DXIntersect関数の実行回数を減らす対策が必要。
- もしくはD3DXIntersect関数のような交差判定関数を自作する。

