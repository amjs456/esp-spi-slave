# esp-spi-slave

ESP-IDF の SPI Slave ドライバーを使用し、ESP32 を SPI スレーブとして動作させるサンプルです。

対となる [`esp-spi-master`](https://github.com/amjs456/esp-spi-master) と組み合わせることで、2台の ESP32 間で UART から入力した文字列を全二重 SPI 通信できます。通常の MOSI、MISO、SCLK、CS に加えて、通信開始を相手へ知らせる2本の IRQ（ハンドシェイク）信号を使用します。

## 主な機能

- ESP32 の `SPI2_HOST` を SPI スレーブとして使用
- SPI mode 0、64バイト固定長の全二重転送
- DMA 対応の送受信バッファを3組用意
- UART0 の標準入力から送信文字列を受け付け
- 受信した文字列を UART0 の標準出力へ表示
- 2本の GPIO IRQ により、マスターとスレーブのどちらからでも転送を開始可能
- GPIO番号、キュー数、バッファサイズを `menuconfig` に表示

## 必要なもの

- ESP32 開発ボード 2台
- ESP-IDF が利用できる開発環境
- USBケーブル 2本
- ESP32間を接続するジャンパーワイヤー
- 対となる [`esp-spi-master`](https://github.com/amjs456/esp-spi-master)

このプロジェクトは ESP32 をターゲットとして作成されています。信号レベルは 3.3 V とし、2台の GND を必ず共通にしてください。

## 配線

デフォルト設定では、次のように接続します。

| 信号 | マスター側 GPIO | スレーブ側 GPIO | 方向 |
| --- | ---: | ---: | --- |
| MOSI | 13 | 12 | Master → Slave |
| MISO | 12 | 13 | Slave → Master |
| SCLK | 14 | 14 | Master → Slave |
| CS | 15 | 15 | Master → Slave |
| Master → Slave IRQ | 4 | 4 | Master → Slave |
| Slave → Master IRQ | 2 | 2 | Slave → Master |
| GND | GND | GND | 共通GND |

IRQ信号は通常 High で、通信要求時に Low へ変化します。入力側は立ち下がりエッジを検出します。外付けプルアップ／プルダウンは使用せず、各ESP32の出力によってレベルを決めます。

> [!CAUTION]
> GPIO12 は ESP32 のストラッピングピンです。リセット中に接続先が不適切なレベルで駆動すると起動に影響する場合があります。書き込みや起動に失敗する場合は、相手側を先にリセット状態にする、起動時に信号を駆動しない、または使用GPIOを変更してください。

## 通信仕様

| 項目 | 設定 |
| --- | --- |
| SPIホスト | `SPI2_HOST` |
| SPIモード | mode 0（CPOL=0、CPHA=0） |
| クロック | マスター側で 1 MHz |
| 転送方式 | 全二重 |
| 1回の転送長 | 64バイト（512ビット） |
| ビット順 | ESP-IDF のデフォルト（MSB first） |
| CS | Active Low |

一方が UART から文字列を受け取ると、IRQ線を使って相手側へ転送要求を通知します。相手側に送信データがない場合は、ゼロで始まるダミーバッファをキューへ投入します。双方が SPI トランザクションを準備した後、マスターがクロックを出力して64バイトを同時に送受信します。転送後、各ボードは受信バッファを文字列として UART へ出力します。

## セットアップ

### 1. スレーブ側

ESP-IDF のターミナルで本リポジトリへ移動し、ターゲットを設定します。

```sh
idf.py set-target esp32
```

必要に応じて設定を変更します。

```sh
idf.py menuconfig
```

`ESP-SPI-SLAVE settings` メニューで、SPIおよびIRQに使用するGPIOを設定できます。

次にビルドし、スレーブ用ESP32へ書き込みます。`PORT` は環境に合わせて置き換えてください。

```sh
idf.py build
idf.py -p PORT flash monitor
```

### 2. マスター側

別のディレクトリへマスター用リポジトリを取得し、もう1台のESP32へ書き込みます。

```sh
git clone https://github.com/amjs456/esp-spi-master.git
cd esp-spi-master
idf.py set-target esp32
idf.py build
idf.py -p PORT flash monitor
```

両方のシリアルモニターを同時に開く場合は、異なるポートを指定してください。

## 使い方

1. 配線を確認して両方のESP32を起動します。
2. マスター側またはスレーブ側のシリアルモニターで文字列を入力し、Enterを押します。
3. 入力した側の文字列が SPI で送信され、相手側のシリアルモニターへ表示されます。
4. 双方が送信データを持つ場合は、同じ全二重トランザクションで互いの文字列を交換します。

入力できる文字列は終端の `\0` を含めて最大64バイトです。`fgets()` が一度に読み込むのは最大63文字で、末尾の改行コードは送信前に除去されます。

## 設定項目

`main/Kconfig.projbuild` では次の項目を定義しています。

| 設定 | デフォルト | 内容 |
| --- | ---: | --- |
| `CONFIG_SPI_MOSI_GPIO` | 12 | スレーブがMOSIを受信するGPIO |
| `CONFIG_SPI_MISO_GPIO` | 13 | スレーブがMISOを送信するGPIO |
| `CONFIG_SPI_SCLK_GPIO` | 14 | SPIクロック入力GPIO |
| `CONFIG_SPI_CS_GPIO` | 15 | チップセレクト入力GPIO |
| `CONFIG_MASTER_TO_SLAVE_IRQ_GPIO` | 4 | マスターからの通信要求入力GPIO |
| `CONFIG_SLAVE_TO_MASTER_IRQ_GPIO` | 2 | マスターへの通信要求出力GPIO |
| `CONFIG_QUEUE_SIZE` | 3 | DMAトランザクションバッファ数 |
| `CONFIG_BUF_SIZE` | 64 | 1トランザクションのバッファサイズ |

GPIOを変更する場合は、対となるマスター側の設定と配線も一致させてください。

> [!NOTE]
> 現在の `main/esp-spi-slave.c` は `CONFIG_QUEUE_SIZE` を3、`CONFIG_BUF_SIZE` を64としてソース内でも定義しています。このため、`menuconfig` でキュー数またはバッファサイズを変更しても実装には反映されません。現状はデフォルト値のまま使用してください。

## プログラム構成

- `uart_vfs_init()` — UART0 を標準入出力として初期化
- `irq_gpio_init()` — 2本のIRQ GPIOと割り込みハンドラーを初期化
- `dma_buf_init()` — DMA送受信バッファとSPIトランザクションを初期化
- `stdin_read_task()` — UART入力を受け取り、送信キューへ格納
- `create_dummy_task()` — 相手から要求された場合にダミーデータを送信キューへ格納
- `transaction_task()` — SPIトランザクションの登録、IRQ制御、受信データ表示を実行

## 実装上の注意

- バイナリプロトコルではなく、NULL終端された文字列の交換を想定しています。
- 受信データは `printf("%s")` で表示するため、64バイト以内に `\0` が必要です。
- SPI APIとGPIO APIの戻り値を一部確認していないため、配線や設定に問題があるとタスクが待機したままになる場合があります。
- IRQ信号にタイムアウト処理はなく、相手側が未起動または応答不能の場合は転送完了を待ち続けます。
- マスター側とバッファサイズ、SPIモード、GPIO割り当てを必ず揃えてください。

## ディレクトリ構成

```text
.
├── CMakeLists.txt
├── README.md
└── main
    ├── CMakeLists.txt
    ├── Kconfig.projbuild
    └── esp-spi-slave.c
```
