# Logic Table Tennis

## ハード構成
- マイコン: Seeed Studio XIAO ESP32C3
- ディスプレイ: SSD1306 OLED 128x64 (I2C, `0x3C`)
- 入力: 4ボタン（`UP / DOWN / LEFT / RIGHT`）
- 音: 圧電ブザー
- 演出: NeoPixel LED 3個

### ピン割り当て（`config.h`）
- OLED SDA: `GPIO6` (D4)
- OLED SCL: `GPIO7` (D5)
- BUTTON_UP: `GPIO20` (D7)
- BUTTON_DOWN: `GPIO8` (D8)
- BUTTON_LEFT: `GPIO9` (D9)
- BUTTON_RIGHT: `GPIO10` (D10)
- BUZZER: `GPIO5` (D3)
- NeoPixel DIN: `GPIO2` (D0)

## ビルド方法

### 実機（XIAO ESP32C3）
前提:
- Arduino CLI をインストール済み
- ボードパッケージ `esp32:esp32` を導入済み
- 以下ライブラリを導入済み
  - `Adafruit GFX Library`
  - `Adafruit SSD1306`
  - `Adafruit NeoPixel`

ビルド:
```powershell
arduino-cli compile --fqbn esp32:esp32:XIAO_ESP32C3 .
```

書き込み（ポートは環境に合わせて変更）:
```powershell
arduino-cli upload -p COM3 --fqbn esp32:esp32:XIAO_ESP32C3 .
```

シリアルモニタ:
```powershell
arduino-cli monitor -p COM3 -c baudrate=115200
```

### Wokwi
このリポジトリには以下が含まれています。
- 回路図: `diagram.json`
- Wokwi設定: `wokwi.toml`
- ビルド成果物は `.arduino-build/` を使う

手順:
1. Wokwiでこのプロジェクトを開く
2. `diagram.json` と `wokwi.toml` を読み込む
3. 再生するとシミュレーション開始

補足:
- `wokwi.toml` は `firmware = ".arduino-build/logic-table-tennis.ino.bin"` を参照します。
- 必要に応じて先にローカルでビルドし、`.arduino-build` 配下の成果物を更新してください。
- `build/` は使わず、`.arduino-build/` を更新してください。

## ゲームルールの概要
- 盤面は `6x6`。
- プレイヤーとCPUが交互にカードを出してボールを打ち返す。
- カードは「方向（直進/斜め）」と「移動マス数（1〜5）」を持つ。
- ボールが盤外に出る、相手陣に届かない、30秒以内に選択できない、手札が尽きる等で失点。
- 1ゲームごとに勝敗が決まり、先に3ゲーム先取でマッチ勝利。

## 操作方法
- タイトル画面: いずれかのボタンで開始
- サーブ位置選択（`SERVE POS`）
  - `LEFT / RIGHT`: サーブ位置移動
  - `UP`: サーブ位置決定
- カード選択（`SERVE CARD` / `1P`）
  - `LEFT / RIGHT`: 手札カーソル移動
  - `UP`: カード決定
- マッチ終了画面（`MATCH OVER`）
  - `UP`: タイトルへ戻って再戦

## 補足
- 『ロジタク』というボードゲームのルールをもとにしています。
