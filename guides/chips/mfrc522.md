# MFRC522 — 13.56MHz RFID/NFC リーダ IC（ISO/IEC 14443 A / MIFARE）

> 13.56MHz の電波を出してカードに給電し、ISO/IEC 14443 A のフレーミングと誤り検出まで
> ハードウェアで面倒を見るリーダ IC。I2C の相手としては「64 バイトの FIFO と 64 本の
> レジスタを持ったモデム」であって、カードの中身を解釈するのはホスト側の仕事である。
>
> **確度**: NXP の datasheet（rev 3.9, 2016-04-27, 95 ページ）に基づく。**実測では未確認**。
> 本カタログの probe はアンテナを ON にしないので、RF が絡む挙動は一切見ていない。

## これは何か

**これはセンサではなく送信機である。** TxControlReg の Tx1RFEn / Tx2RFEn を 1 にした瞬間、
TX1/TX2 ピンからアンテナに 13.56MHz の搬送波が出る。近くのカードは給電されて起き、
近くの他の NFC リーダは干渉を受ける。レジスタを 1 バイト書くことが電波を出すことに
直結する、という点で普通の I2C デバイスとは性格が違う。

チップの中身は 3 層になっている:

- **アナログ部**: TX1/TX2 のドライバと RX の復調器。アンテナと整合回路は外付け。
- **コンタクトレス UART**: ISO/IEC 14443 A のフレーミング、パリティ、CRC、ビット衝突検出。
  MIFARE Classic (MF1xxS20/S50/S70) の CRYPTO1 認証も内蔵する。
- **ホストインタフェース + 64 バイト FIFO**: SPI / UART / I2C のどれかで、レジスタと FIFO を叩く。

できること:

- ISO/IEC 14443 A / MIFARE / NTAG のカードとの読み書き。転送速度は 106〜848 kBd。
- CRC コプロセッサ（ホストで CRC を計算しなくてよい）。
- MIFARE Classic の認証（MFAuthent コマンド）。
- 割り込み出力（IRQ ピン）、プログラマブルタイマ、自己診断。
- ソフトリセットとソフトパワーダウン。

できないこと・持っていないもの:

- **ISO/IEC 14443 Type B、FeliCa、ISO/IEC 15693 は扱えない。** 対応は Type A / MIFARE / NTAG だけ。
  「NFC タグが読めない」という相談の多くはこれ。
- **アンテナは内蔵していない。** TX1/TX2 と RX に整合回路とコイルを外付けする必要があり、
  読める距離（typ 50mm 程度）はその設計次第。モジュール品を使う場合、アンテナの出来は
  チップではなく基板の話である。
- **カードの上位プロトコルは解釈しない。** アンチコリジョン手順、SELECT、認証、ブロック読み書きの
  手順はホスト側が組む。チップがやるのは 1 フレームの送受信までである。
- **I2C アドレスをソフトで変更できない。** アドレスはリセット解除の瞬間にピンからラッチされ、
  以降ピンを動かしても反映されない。
- **ホストインタフェースの種類もソフトで選べない。** 電源投入 / ハードリセット後にピンの
  論理レベルの組み合わせを見て SPI / UART / I2C を自動判別する（Table 5）。

電気的な現実:

- VDDA / VDDD / TVDD は 2.5 〜 3.6V（typ 3.3V）で、**3 つとも同じ電圧でなければならない**。
  3V を下回ると読み取り距離が落ちる。PVDD（ホスト I/O 側）は 1.6 〜 3.6V で、VDDD 以下。
- 27.12MHz の水晶が要る。発振安定後さらに 1024/27µs ≈ 37.74µs の内部遅延を経てから
  レジスタにアクセスできる。
- ハードリセット（NRSTPD）は 100ns 以上 Low。10ns 未満のパルスはスパイクフィルタで捨てられる。
- I2C は Fast mode 400kBd、High-speed mode 3400kBd まで対応。プルアップは外付け。
- I2C アドレスは EA ピンで決まる。**EA = Low なら上位 4bit は NXP 固定の `0101` で、
  ADR_2..ADR_0 の 3 本だけが自由** → `0x28` 〜 `0x2F`。EA = High なら ADR_5..ADR_0 の 6 本で
  全部指定でき、ADR_6 は常に 0。M5Stack Unit RFID (U031) は `0x28`。
- 割り込みは IRQ ピン。極性と push-pull / open-drain はレジスタで選べる。

## 早見表

| 項目 | 値 |
|---|---|
| I2C アドレス | `0x28`（EA = Low、ADR_2..0 = 000 のとき）。M5Stack Unit RFID (U031) も `0x28` |
| アドレスの決まり方 | EA = Low: `0101` + ADR_2..0 → `0x28`〜`0x2F` / EA = High: ADR_5..0 で 7bit 全部 |
| アドレスのラッチ | **リセット解除直後にピンから 1 回だけ読む。以後の変化は無視** |
| ホストインタフェース | SPI / UART / I2C をピンの組み合わせで自動判別（ソフトでは選べない） |
| 電源 | VDDA = VDDD = TVDD、2.5〜3.6V（typ 3.3V）。PVDD 1.6〜3.6V かつ ≤ VDDD |
| クロック | 27.12MHz 水晶（外付け） |
| I2C 速度 | Fast 400kBd / High-speed 3400kBd |
| レジスタ空間 | `0x00`〜`0x3F`（16 本 × 4 ページ）、8bit |
| FIFO | 64 バイト（送受信共用） |
| RF | 13.56MHz。TX1 / TX2 の 2 ドライバ。アンテナは外付け |
| 読み取り距離 | typ 50mm（アンテナ設計依存） |
| 割り込み | IRQ ピン（極性・駆動形式はレジスタで設定） |
| バイト順 | MSB first |
| 素性の確認 | VersionReg `0x37` が `0x91`（v1.0）または `0x92`（v2.0） |

## レジスタ

レジスタは 64 本（`0x00`〜`0x3F`）。[`profiles/mfrc522.yaml`](https://github.com/tanakamasayuki/I2CDeviceDB/blob/38f1d82/profiles/mfrc522.yaml) には
最低限使うものだけを収めてある。

| アドレス | 名前 | リセット値 | 意味 |
|---|---|---|---|
| `0x01` | CommandReg | `0x20` | コマンドの起動・停止。bit4 = PowerDown、bit3:0 = コマンドコード |
| `0x04` | ComIrqReg | `0x14` | 割り込み要求ビット。bit7 = Set1（書き込み時のマスク指定） |
| `0x09` | FIFODataReg | — | FIFO の入出力。ここを連続で叩いて FIFO を読み書きする |
| `0x0A` | FIFOLevelReg | `0x00` | FIFO に入っているバイト数。bit7 = FlushBuffer |
| `0x0D` | BitFramingReg | `0x00` | ビット単位のフレーミングと StartSend |
| `0x11` | ModeReg | `0x3F` | 送受信の全般設定（CRC プリセットなど） |
| `0x14` | TxControlReg | `0x80` | **アンテナドライバ制御。bit1 = Tx2RFEn、bit0 = Tx1RFEn** |
| `0x37` | VersionReg | — | チップ種別とバージョン。上位 4bit の `9` が MFRC522 |

**転送の形（ここが普通の I2C デバイスと違う）**:

書き込みは「デバイスアドレス + レジスタアドレス(1) + データ(n)」。ただし
**n バイトのデータはすべて同じレジスタアドレスに書かれる。アドレスの自動インクリメントは無い。**
これは FIFO に 64 バイトを一気に流し込むための設計である。

読み出しは 2 段階。まずデータバイト無しで「デバイスアドレス + レジスタアドレス」を書き、
続いてデバイスアドレス + R で読む。**こちらも n バイト読むと同じレジスタが n 回返る。**
連続するレジスタをまとめて読むことはできず、1 本ずつ手順を踏む必要がある。
（直前のアクセスが同じレジスタなら、レジスタアドレスの書き込みは省ける。）

主なコマンド（CommandReg の bit3:0 に書く。Table 149, p70）:

| コード | コマンド | 意味 |
|---|---|---|
| `0000` | Idle | 何もしない / 実行中のコマンドを止める |
| `0001` | Mem | FIFO と内部 25 バイトバッファの間で転送 |
| `0011` | CalcCRC | CRC コプロセッサを回す |
| `0100` | Transmit | FIFO の内容を送信 |
| `1000` | Receive | 受信 |
| `1100` | Transceive | 送信して応答を受信（カード操作の主力） |
| `1110` | MFAuthent | MIFARE Classic の認証 |
| `1111` | SoftReset | 全レジスタをリセット値に戻す。自動で終了する |

## 典型的な使い方

1. **まず VersionReg (`0x37`) を読む。** `0x91` か `0x92` が返れば NXP の MFRC522 である。
   通信が成立しているかの確認も兼ねる。ここが `0x00` や `0xFF` なら配線かアドレスの問題。
2. SoftReset（CommandReg に `0x0F`）を送る。全レジスタがリセット値に戻る。
3. タイマ、ModeReg、TxASKReg などを目的の設定に書き換える。
4. **アンテナを ON にする**（TxControlReg の bit1:0 を 1 にする）。ここで電波が出る。
5. カードを探す: BitFramingReg でビット長を指定し、FIFO に REQA (`0x26`) を入れて
   Transceive を起動、ComIrqReg を見て完了を待ち、FIFO から応答を読む。
6. 以降、アンチコリジョン → SELECT → 認証 → ブロック読み書き、と ISO/IEC 14443 A の
   手順を踏む。これらはすべてホスト側のロジックである。
7. 使い終わったらアンテナを OFF にする（TxControlReg の bit1:0 を 0）。

## 利用上の注意

- **アンテナを ON にすると 13.56MHz を放射する。** 「試しに全レジスタに書いてみる」を
  やってはいけないデバイスである。TxControlReg (`0x14`) の下位 2bit がその引き金で、
  リセット値 `0x80` では OFF になっている。本カタログの probe が `antenna-enable` を
  deferred にしているのはこのため（[`scenarios/mfrc522/p0.yaml`](https://github.com/tanakamasayuki/I2CDeviceDB/blob/38f1d82/scenarios/mfrc522/p0.yaml)）。
  近くに別の NFC リーダや交通系カードがある環境では、意図しない給電・干渉になる。
- **VersionReg が本物と互換品を見分ける唯一の手掛かりである。** `0x91` = v1.0、`0x92` = v2.0。
  上位 4bit の `9` が「MFRC522 というチップ種別」を表す。同じ `0x28` に居て同じレジスタ表で
  動くのに VersionReg が違う値を返すなら、それは NXP の MFRC522 ではない。
  **本カタログの [ws1850s](../../chips/ws1850s.yaml) がまさにその例**で、WiseSun 製の
  レジスタ互換品として M5Stack Unit RFID2 に載っている。MFRC522 用のドライバがそのまま動くので
  混同されやすいが、別のチップであり、VersionReg の値も NXP のものとは一致しない
  （WS1850S の datasheet は VersionReg の値を公開していない）。ドライバが
  「バージョンが 0x91/0x92 でなければエラー」と書いてあると RFID2 で弾かれる。
- **レジスタアドレスは自動インクリメントしない。** 「`0x00` から 64 バイト連続で読んでレジスタを
  ダンプする」ができない。他の I2C デバイスの感覚で書いたコードは、同じレジスタを 64 回
  読んだ結果を掴むことになる。ダンプしたければ 1 本ずつ read を繰り返す。
- **ComIrqReg は read-to-clear ではない。** ビットは「ソフトウェアが書いてクリアする」方式で、
  bit7 の Set1 を 0 にして、消したいビットに 1 を立てた値を書く。読んだだけでは消えないので、
  クリアを忘れると次の Transceive の完了判定が即座に真になる。
- **HiAlertIRq / LoAlertIRq は Set1 でしか消えない。** 元の HiAlert / LoAlert（Status1Reg）が
  下がっても、IRq 側は保持され続ける。
- **PowerDown ビットは即座には落ちない。** CommandReg の PowerDown を 0 に戻してから
  ソフトパワーダウンを抜けるまで 1024 クロックかかる。ビットが自分で 0 になるのを待つ。
  また SoftReset の実行中は PowerDown をセットできない。
- **リセット直後は水晶が安定するまでアクセスできない。** データシートは
  「アドレス 0 への読み出しを、正しい内容が返るまで繰り返せ」という確認方法を挙げている（8.6.2）。
  電源投入直後に決め打ちの delay だけで書き始める実装は、水晶次第で不安定になる。
- **I2C アドレスとホストインタフェースはリセット時のピンで決まる。** 動作中に ADR ピンを
  動かしても無効。逆に、リセットのタイミングでピンが浮いていると、想定と違うアドレス、
  最悪は違うインタフェース（SPI/UART）で立ち上がって I2C に一切応答しなくなる。
- **電源 3 系統（VDDA / VDDD / TVDD）は同電圧が必須。** 別々の電圧を掛けると仕様外。また
  3V 未満だと読み取り距離が落ちる。5V 系のホストに繋ぐときは PVDD 側だけがホストの
  I/O 電圧に追従する構成であり、SDA/SCL を 5V で振ってよいわけではない。
- **SoftReset は SerialSpeedReg も初期化する。** UART で使っている場合は 9.6kBd に戻る。
  I2C では実害は無いが、リセット後は全レジスタがリセット値であることを忘れない。
- **400kHz は仕様内**（Fast mode 400kBd 対応）だが、本カタログでは未実測。

## どこに何が書いてあるか

一次資料は NXP MFRC522 datasheet rev 3.9（2016-04-27）。PDF のページ番号と印刷ページ番号
（フッタの「N of 95」）は一致している。

| 知りたいこと | 参照先 |
|---|---|
| I2C アドレスの決まり方（EA ピン） | 8.1.4.5 7-Bit addressing（p19） |
| インタフェースの自動判別（ピン組み合わせ） | 8.1.1、Table 5（p9） |
| レジスタ書き込みのフレーム形式 | 8.1.4.6 Register write access（p19） |
| レジスタ読み出しの 2 段階手順 | 8.1.4.7 Register read access、Fig 17（p20） |
| High-speed モード | 8.1.4.8〜8.1.4.12（p21-23） |
| レジスタ一覧（アドレスと参照先ページ） | Table 20 MFRC522 register overview（p36-37） |
| レジスタビットの access 表記（R/W, D, R, W, RFT） | 9.1 Table 19（p35） |
| CommandReg / ComIrqReg のビット定義 | 9.3.1.2、9.3.1.5、Table 23〜Table 30（p38-40） |
| **TxControlReg（アンテナ ON/OFF）** | 9.3.2.5、Table 61 / Table 62（p50） |
| **VersionReg（0x91 / 0x92）** | 9.3.4.8、Table 131 / Table 132（p66） |
| コマンド一覧（コードと意味） | 10.3 Table 149 Command overview（p70） |
| 各コマンドの詳細（Transceive, MFAuthent, SoftReset） | 10.3.1.1〜10.3.1.10（p71-73） |
| FIFO の使い方・水位割り込み | 8.3 FIFO buffer（p29） |
| 割り込み源の一覧 | 8.4、Table 18（p30-31） |
| タイマユニット | 8.5（p31-32） |
| ソフトパワーダウン / 送信部パワーダウン | 8.6.2 / 8.6.3（p33） |
| リセット時間・発振立ち上がり時間 | 8.8（p34） |
| 電源電圧の条件 | 12. Recommended operating conditions、Table 151（p74） |
| アンテナ整合回路の例 | 15. Application information（p81） |
| 機械可読なレジスタマップ | [`profiles/mfrc522.yaml`](https://github.com/tanakamasayuki/I2CDeviceDB/blob/38f1d82/profiles/mfrc522.yaml) |
| アドレスと datasheet のリビジョン | [`chips/mfrc522.yaml`](../../chips/mfrc522.yaml) |
| 搭載製品（M5Stack Unit RFID） | [`products/m5stack-u031.yaml`](../../products/m5stack-u031.yaml) |
| 互換品（WS1850S、Unit RFID2） | [`chips/ws1850s.yaml`](../../chips/ws1850s.yaml) |
| probe が何をするか / 何を避けているか | [`scenarios/mfrc522/p0.yaml`](https://github.com/tanakamasayuki/I2CDeviceDB/blob/38f1d82/scenarios/mfrc522/p0.yaml) |

## 出典と確度

- レジスタアドレス、リセット値、ビット定義、コマンドコード、I2C のフレーム形式、
  電源条件: すべて NXP datasheet rev 3.9 由来。
- **未確認**: 実機のキャプチャが無い。`0x28` で応答するか、VersionReg が実際に `0x91`/`0x92` を
  返すか、リセット値が表のとおりかは確かめられていない。
- **未調査**: probe は presence / VersionReg / リセット直後のレジスタ値までしか見ない。
  アンテナ ON、FIFO の書き読み、カードとのトランザクション、SoftReset は deferred。
  400kHz での動作も未実測。
- WS1850S が VersionReg に何を返すかは、WS1850S 側の datasheet（9 ページ、中国語）に
  記載が無く、**本カタログとしては未確認**である。
