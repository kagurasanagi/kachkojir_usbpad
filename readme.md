# CH32X035G8U6 USB-SPI ブリッジファームウェア仕様書 (v1.0)

## 1. プロジェクト概要
本デバイスは、市販のUSB HIDゲームパッドを接続し、その入力を解析して本体（RP2350）へSPI通信で高速転送する「入力ブリッジ」として動作する。
CH32X035G8U6のUSB Host機能およびSPI Slave機能を活用し、極低遅延な操作環境を実現する。

## 2. ハードウェア仕様 (QFN28パッケージ)

### 2.1 主要ピンアサイン (G8U6)
※データシート（v1.7 / QFN28）に基づく正しいピン配置。

| 機能 | ピン番号 | ピン名 | 接続先 / 備考 |
| :--- | :---: | :--- | :--- |
| **USB D+** | 23 | **PA12 (UDP)** | USBパッド D+ |
| **USB D-** | 22 | **PA11 (UDM)** | USBパッド D- |
| **SPI NSS** | 9 | **PA4 (CS)** | RP2350 CS (物理同期) |
| **SPI SCK** | 10 | **PA5 (SCK)** | RP2350 SCK |
| **SPI MISO** | 11 | **PA6 (MISO)** | RP2350 MISO (データ出力) |
| **Debug DIO**| 25 | **PC18 (DIO)** | WCH-LinkE SWDIO |
| **Debug CLK**| 24 | **PC19 (DCK)** | WCH-LinkE SWCLK |
| **LED**      | 5  | **PA0**        | 動作確認用LED |
| **電源 VDD** | 2  | **VDD**        | 3.3V 入力 |
| **電源 GND** | 0  | **GND**        | 中心パッド (Exposed Pad) |

### 2.2 システム要件
* **動作周波数:** 48MHz (内部HSI使用)。
* **メモリ:** 20KB SRAM / 62KB Flash。
* **USB:** USB 2.0 Full-Speed Host Controller。

## 3. ソフトウェア実装仕様

### 3.1 USB HID Host ロジック
* **Enumeration:** 接続されたHIDゲームパッドを認識し、レポーティングを開始する。
* **Data Extraction:** HIDレポートから各ボタン（ABXY、十字、トリガー等）の状態を抽出する。
* **Buffer:** 抽出した最新の入力状態を、常にSPI送信バッファ（3バイト）に書き込んでおく。

### 3.2 SPI スレーブ通信 (対 RP2350)
* **転送形式:** 24ビット (3バイト) 固定長。
* **同期方式:** NSS（PA4）の立ち下がりでパケット開始、立ち上がりで終了。
* **ヘッダ/チェックサム:** 無し。
* **MISO挙動:** ホスト（RP2350）がクロックを送った瞬間にバッファ内の最新値をビット送りする。

### 3.3 非対称デバウンス処理
USBパッドからの生データに対し、以下の処理を適用して反映する。
1. **プレス（押し下げ）:** `0`から`1`の変化検知時、即座にSPIバッファを`1`にする。
2. **リリース（離上）:** `1`から`0`の変化検知後、5ms〜8msの間は物理入力を無視（ブランキング）し、バッファ上は`0`を維持する。

## 4. SPIデータフォーマット (24bit)

| Byte | Bit 7 | Bit 6 | Bit 5 | Bit 4 | Bit 3 | Bit 2 | Bit 1 | Bit 0 |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **0** | A | B | X | Y | L1 | L2 | R1 | R2 |
| **1** | Start | Select| L3 | R3 | 上 | 下 | 左 | 右 |
| **2** | L上 | L下 | L左 | L右 | R上 | R下 | R左 | R右 |

## 5. 開発ワークフロー (Antigravity)
1. **コンパイル:** VS Code + WCH-RISC-V 拡張 もしくは `make` を使用。
2. **書き込み:** WCH-LinkE経由でSDIインターフェースを使用。
3. **デバッグ:** 動作ログはUART1 (PB10) または WCH-LinkE のデバッグビューアで確認。

## 6. ビルド環境 / 必要ツール
本プロジェクトをビルドするには以下の環境が必要です。

* **Toolchain:** [xPack RISC-V Embedded GCC 15.2.0-1](https://github.com/xpack-dev-tools/riscv-none-elf-gcc-xpack) 以降
* **Build Tool:** GNU Make
* **Git Submodule:** 外部ライブラリを `SDK/` フォルダに含んでいます。
  ```bash
  git submodule update --init --recursive
  ```
* **Hardware:** CH32X035G8U6, WCH-LinkE (書き込み用)
* **書き込みツール:** [WCH-LinkUtility](https://www.wch.cn/downloads/wch-linkutility_zip.html)