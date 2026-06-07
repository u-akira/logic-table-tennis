# Arduino Skill

対象ボード:

* ESP32-C3 (Seeed XIAO ESP32C3)

ビルドと書き込み:

```powershell
./scripts/arduino-upload.ps1
```

ポート指定:

```powershell
./scripts/arduino-upload.ps1 -Port COM5
```

シリアルモニタ:

```powershell
./scripts/serial-monitor.ps1
```

ルール:

* コード変更後は必ず build → upload を実行する
* compile が失敗した場合は upload しない
* upload 前にポート番号を確認する
* シリアル出力確認時は baudrate=115200 を使用する
* FQBN は `esp32:esp32:XIAO_ESP32C3` を使用する
