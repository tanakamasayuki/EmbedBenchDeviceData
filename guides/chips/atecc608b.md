# ATECC608B — ECC P-256 / AES / SHA を持つセキュアエレメント

> 秘密鍵を外に出さないまま署名・鍵共有・暗号化をやってくれる暗号コプロセッサ。**レジスタマップは
> 無く**、ワードアドレス 1 バイトの後ろにコマンドパケットを流し込み、実行が終わるのを待って
> 結果パケットを読む、という独特のプロトコルで動く。**眠っているときは I2C アドレスに ACK すら返さない。**
>
> **確度**: 一般品の公開資料は
> [Summary Data Sheet DS40002239A](https://ww1.microchip.com/downloads/en/DeviceDoc/ATECC608B-CryptoAuthentication-Device-Summary-Data-Sheet-DS40002239A.pdf)（2020）
> だけで、**完全版は NDA 扱い**（summary の表紙に明記されている）。コマンド体系・ワードアドレス・
> パケット形式・エラーコードは、事前プロビジョニング品の完全版
> [ATECC608B-TNGTLS Complete Data Sheet DS40002250A](https://ww1.microchip.com/downloads/en/DeviceDoc/ATECC608B-TNGTLS-CryptoAuthentication-Data-Sheet-DS40002250A.pdf)（2020）
> から取った。M5Stack Unit ID が載せているのはこの TNGTLS 品なので、実用上はそちらが一次資料になる。
> **ライブラリ実装から補った箇所は無い。** 実測での確認はしていない。

## これは何か

Microchip CryptoAuthentication ファミリの暗号 IC。8 ピン（UDFN / SOIC）または 3 ピンで、
中身は暗号アクセラレータ + 16 スロットの EEPROM + 真性乱数生成器 + 固有シリアル番号。
ホストの MCU から見ると「鍵を預けておいて、計算だけ頼む相手」である。

できること:

- **ECDSA 署名・検証**（NIST P-256）、**ECDH 鍵共有**。秘密鍵はチップ内で生成でき、外に出ない。
- **AES-128 ECB** の暗号化・復号、GCM 用の Galois Field Multiply。
- **SHA-256 / HMAC(SHA-256)**、TLS 1.2 の PRF と HKDF による鍵導出。
- 16 スロットの EEPROM に鍵・証明書・データを保管し、スロットごとにアクセスポリシーを設定する。
- NIST SP 800-90A/B/C 準拠の**乱数生成**。
- 72bit の固有シリアル番号、高耐久の単調カウンタ 2 個。
- **I2C アドレスを 1 回だけ変更できる**（`UpdateExtra` コマンドで Configuration zone の 85 バイト目を書く）。

できないこと・持っていないもの:

- **秘密鍵を読み出せない。** それが存在理由なので、「鍵をバックアップする」用途には使えない。
- **RSA は無い。** 対応するのは P-256 の ECC と AES-128 だけ。任意の曲線・任意の鍵長は扱えない。
- **汎用ストレージではない。** スロットは 36〜72 バイト程度の単位で、合計でも数百バイト。
- **レジスタが無い。** ステータス専用のレジスタすら無く、ステータス・エラー・実行結果はすべて
  同じ出力 FIFO を共有する（datasheet §4.3, p.19）。
- **割り込み線が無い。** 実行完了はポーリング（ビジー中は NACK が返る）で知る。
- **TNGTLS 品では設定を変えられない。** Configuration zone はロック済みで出荷される。
  変更できるのは I2C アドレスを含むごく一部のバイトだけ。鍵も証明書も工場で書き込まれている。
- **Single-Wire Interface (SWI) 品と I2C 品は別。** 型番で決まる。M5Stack Unit ID は I2C 品。

## 早見表

| 項目 | 値 |
|---|---|
| I2C アドレス | **TNGTLS 品は `0x35`**（7bit）。一般品の既定は `0x60`。両者は違うので注意 |
| アドレス変更 | `UpdateExtra` で Configuration zone byte 85 に書く。**非ゼロを書けるのは 1 回だけ** |
| 電源 | 2.0V–5.5V（VCC）。I/O レベルは 1.8V–5.5V |
| 消費電流 | 非 ECC コマンド実行中 2mA typ / ECC コマンド実行中 最大 14mA（クロック分周 0x0）。Idle 800µA typ、Sleep 30nA typ（150nA max, VCC≤3.6V, TA≤+55°C） |
| インタフェース | I2C、最大 **1MHz**。**Hs モード（3.4MHz）は無い**。外部プルアップ必須（SDA はオープンドレイン、SCL は入力のみ） |
| ウェイク | SDA を **tWLO = 60µs 以上** Low に保って立ち上げる。その後 **tWHI = 1500µs** 待つ |
| ウォッチドッグ | **0.7〜1.7 秒**（typ 1.3s）でウェイクから強制スリープ |
| 電源投入遅延 | tPU = 100µs 以上（Self Test 有効時はさらに長い。tWHIST = 20ms） |
| 中身 | ECC P-256 アクセラレータ + AES-128 + SHA-256 + TRNG + 16 スロット EEPROM + 72bit シリアル |
| EEPROM 耐久 | 書き込み 400,000 回以上（+85°C）、データ保持 10 年以上（+55°C） |
| 割り込み | 無し（ビジー中は NACK、ポーリングで待つ） |
| バイト順 | パケット内の複数バイトパラメータは **little-endian（LSB first）で送る**。CRC も LSB first |
| 搭載製品 | [Unit ID (U124)](../../products/m5stack-u124.yaml) — ATECC608B-TNGTLS を `0x35` で使用 |

## コマンド

`profiles/atecc608b.yaml` が持っているのは**ワードアドレス**の 4 値だけで、これは
「I2C 書き込みの 2 バイト目が何を意味するか」を決めるものである。

### ワードアドレス（書き込みの 2 バイト目）

| key | 値 | 意味 |
|---|---|---|
| `word-reset` | `0x00` | アドレスカウンタをリセット。次の読み書きが I/O バッファの先頭から始まる |
| `word-sleep` | `0x01` | 低消費電力の Sleep へ。**揮発性の状態がすべて消える**。次の Wake まで I/O を無視 |
| `word-idle` | `0x02` | Idle へ。TempKey / MessageDigestBuffer / Alternate Key は保持される |
| `word-command` | `0x03` | 続くバイトをコマンド入力バッファに書く。**通常の操作はこれ** |
| （予約） | `0x04`–`0xFF` | 送ってはいけない |

### コマンドグループ（ワードアドレス `0x03` の後ろに続く形）

```
count(1) | opcode(1) | param1(1) | param2(2) | data(0..) | CRC16(2)
```

- **count** は自分自身を含むグループ全体のバイト数（= パケットバイト数 + 2 + 1）。
  最小 4、最大 155。範囲外は I/O エラーになる。
- **CRC** は count とパケットの全バイトに対する CRC-16。多項式 `0x8005`、初期値 0。
  **先に送るのが下位バイト**（LSB first）。
- 入力バッファは FIFO なので、1 グループを複数回の I2C 書き込みに分けて送ってよい。
- **最後に STOP を送らないと実行が始まらない。** STOP を忘れると同期が壊れる。

### 応答グループ

```
count(1) | status/error または出力データ | CRC16(2)
```

count が 4 のときは 1 バイトのステータスだけが返る（datasheet §4.3, Table 4-3, p.19）:

| 値 | 意味 |
|---|---|
| `0x00` | 成功 |
| `0x01` | CheckMac / Verify の不一致 |
| `0x03` | Parse error（長さ・opcode・パラメータが不正） |
| `0x05` | ECC Fault（再送すれば成功することがある） |
| `0x07` | Self Test error |
| `0x08` | 乱数生成器の Health Test error |
| `0x0F` | Execution error（現在の状態では実行できない） |
| `0x11` | **Wake を正しく受け取った直後の状態**。ウェイク成功の確認に使える |
| `0xEE` | ウォッチドッグ満了間近。Idle か Sleep に入って再開する |
| `0xFF` | CRC / 通信エラー。再送してよい |

### 主なコマンドの opcode（TNGTLS datasheet Table 5-1, p.25–26）

| コマンド | opcode | 用途 |
|---|---|---|
| `Read` | `0x02` | ゾーンから 4 または 32 バイト読む |
| `MAC` | `0x08` | 鍵と内部データから SHA-256 ダイジェストを計算 |
| `GenDig` | `0x15` | データダイジェスト生成 |
| `Nonce` | `0x16` | 乱数 / 固定値から TempKey を作る |
| `Lock` | `0x17` | ゾーン・スロットをロック（不可逆） |
| `Random` | `0x1B` | 乱数を返す |
| `Counter` | `0x24` | 単調カウンタの読み出し / 増加 |
| `CheckMac` | `0x28` | 他機が計算した MAC を検証 |
| `Info` | `0x30` | リビジョン・鍵の有効性・デバイス状態を返す |
| `GenKey` | `0x40` | ECC 鍵ペア生成 / 公開鍵計算 |
| `Sign` | `0x41` | ECDSA 署名 |
| `ECDH` | `0x43` | ECDH プリマスタシークレット生成 |
| `Verify` | `0x45` | ECDSA 検証 |
| `SHA` | `0x47` | SHA-256 / HMAC ダイジェスト |
| `AES` | `0x51` | AES-ECB 暗号化 / 復号、GF 乗算 |
| `KDF` | `0x56` | PRF / HKDF 鍵導出 |
| `SelfTest` | `0x77` | 内部演算要素の自己診断 |
| `Write` | `0x12` | ゾーンへ 4 または 32 バイト書き込み |
| `UpdateExtra` | `0x20` | Configuration zone の 84 / 85 バイト目を更新（**I2C アドレス変更はこれ**） |

## 典型的な使い方

1. **ウェイクする。** SDA を 60µs 以上 Low に保ってから立ち上げる。
   実装上は **100kHz で `0x00` を 1 バイト送る**のが定番（datasheet §7.1.1, p.57 の Tip）。
   この転送は ACK されないので、ライブラリがエラーを返しても正常である。
2. **tWHI = 1500µs 待つ。** Self Test が有効な個体では 20ms 待つ。
3. **4 バイト読む。** `04 11 33 43`（count=4, status=`0x11`, CRC）が返れば
   「Wake を受け取り、まだコマンドを受けていない」状態である。**ここが疎通確認になる。**
4. **コマンドを送る。** `START` → アドレス+W → ワードアドレス `0x03` →
   `count, opcode, param1, param2(2), data..., CRC(LSB), CRC(MSB)` → `STOP`。
5. **実行完了を待つ。** ビジー中はアドレスに NACK が返る。読み出しを試して ACK が返るまで
   ポーリングするか、コマンドごとの最大実行時間を待つ。実行時間はコマンドによって桁が違う
   （ECC 系が最も長い）。
6. **応答を読む。** 先頭バイトが count。その数だけ読んで CRC を検証する。
7. **使い終わったら Sleep か Idle を送る。** ワードアドレス `0x01`（Sleep）または `0x02`（Idle）に
   続けて STOP。**これを怠るとウォッチドッグ（最短 0.7 秒）で勝手にスリープする。**

## 利用上の注意

- **スリープ中は I2C scan に映らない。** アドレスバイトに ACK を返さないので、
  素朴な「0x08〜0x77 を舐めて ACK を数える」スキャンでは**存在しないように見える**。
  ウェイクしてからスキャンする必要がある。本リポジトリの
  [`scenarios/atecc608b/p0.yaml`](https://github.com/tanakamasayuki/I2CDeviceDB/blob/38f1d82/scenarios/atecc608b/p0.yaml) が
  `presence-before-wake` と `presence-after-wake` を別の出力として持っているのはこのためである。
- **ビジー中も NACK。** 「応答しない」の原因が「眠っている」「実行中」「そもそも居ない」の
  3 通りあり、区別するには手順（§7.2.2 の再同期手順, p.60）を踏む必要がある。
- **ウォッチドッグは最短 0.7 秒。** ウェイクから 0.7〜1.7 秒で強制的にスリープに落ち、
  TempKey などの揮発性状態が消える。複数コマンドにまたがる手順（Nonce → GenDig → Sign など）は
  この時間内に終わらせるか、途中で Idle を挟んでタイマをリセットする。
  `0xEE` が返ってきたらこれが起きかけている。
- **複数バイトのパラメータは little-endian で送る。** datasheet のパラメータ表は
  **big-endian（MSB first）で書かれているが、デバイスが期待するのは little-endian** と
  §5 の冒頭（p.25）に明記されている。表をそのまま信じてバイトを並べると parse error になる。
- **CRC も LSB first。** 多項式 `0x8005`、初期値 0、count を含む全バイトが対象。
  「先に送るバイトが CRC の下位」という点を間違えると常に `0xFF` が返る。
- **STOP を忘れると実行が始まらない。** コマンドの最後には必ず STOP を打つ。
  忘れると同期が失われ、再同期手順が必要になる。
- **遅いバスで他のデバイスと話すと勝手に起きる。** 約 300kHz 未満で通信していると、
  データパターンが Wake 条件（SDA が長く Low）に見えて ATECC608B が起きてしまう。
  自分宛でないので応答はしないが、起きたままウォッチドッグを消費する。
  **低速バスで他デバイスと通信したあとは Sleep か Idle を投げておく**（§7.1.1, p.57）。
- **アドレス変更は 1 回きり。** `UpdateExtra` で Configuration zone byte 85 に非ゼロを書くと、
  以後そのアドレスになる。**取り消せない。** 変更する必要がないなら、
  この場所には既定アドレスを書いておけ、と datasheet が指示している。
- **TNGTLS の既定アドレスは一般品と違う。** TNGTLS は `0x35`、一般の ATECC608B は `0x60`。
  ATECC508A / ATECC608A 向けのコード例をそのまま持ってくるとアドレスが合わない。
  本リポジトリの [`chips/atecc608b.yaml`](../../chips/atecc608b.yaml) は M5Stack 実装に合わせて
  `0x35` を記録している。
- **1MHz まで対応だが Hs モードは無い。** ADS111x のような 3.4MHz は使えない。
  一方でウェイクパルスは低速でないと作れないので、**ウェイクは 100kHz、以後は高速**という
  切り替えが必要になることが多い。
- **`Lock` は不可逆。** ゾーンやスロットをロックすると戻せない。TNGTLS 品は出荷時点で
  すでにロックされているので、通常のアプリケーションから触る理由は無い。
- **鍵と証明書は個体ごとに違う。** TNGTLS はスロット 0 に固有の秘密鍵、スロット 10 に
  デバイス証明書（圧縮形式）、スロット 11/12 に署名者の公開鍵・証明書を持って出荷される。
  **他機の値を前提にしたテストは意味を持たない。**

## どこに何が書いてあるか

一般品の公開資料は summary のみで、**コマンド仕様は載っていない**。
実質的な一次資料は TNGTLS 版の完全版 datasheet である。
以下のページ番号は PDF のページ番号で、印刷ページ番号と一致する。

### Summary Data Sheet DS40002239A（全 28 ページ、電気的特性と外形のみ）

| 知りたいこと | 参照先 |
|---|---|
| 機能一覧・電源範囲（2.0–5.5V）・I/O レベル（1.8–5.5V）・1MHz I2C | Features, p.1 |
| **完全版が NDA であること** | p.1 右下の注記 |
| 暗号機能の概要（ECDSA / ECDH / AES / SHA / TRNG） | §1.2 Device Features, p.4 / §1.3 Cryptographic Operation, p.5 |
| 絶対最大定格 | §2.1 Absolute Maximum Ratings, p.6 |
| **tPU / tWLO / tWHI / tWATCHDOG のタイミング規格値** | §2.3 AC Parameters / Table 2-2, p.7 |
| **I2C タイミング規格値（fSCL 最大 1MHz）** | §2.3.2 AC Parameters: I2C Interface / Table 2-4, p.9 |
| **電源電圧・消費電流（Active / Idle / Sleep）** | §2.4 DC Parameters / Table 2-5, p.10 |
| VIH / VIL（Sleep・Idle 時は VCC 依存） | §2.4.1 VIH and VIL Specifications, p.10–11 |
| ATECC608A / 508A / SHA204A との互換性 | §3 Compatibility, p.12–13 |
| パッケージ刻印 | §4 Package Marking Information, p.14 |
| 外形寸法 | §5 Package Drawings, p.15 |

### ATECC608B-TNGTLS Complete Data Sheet DS40002250A（全 80 ページ）

| 知りたいこと | 参照先 |
|---|---|
| ピン配置 | §1 Pin Configuration and Pinouts, p.5 |
| **既定 I2C アドレスが `0x35` であること、変更が 1 回きりであること** | §2.1 ATECC608B-TNGTLS Configuration Zone, p.7 |
| スロットの用途一覧（鍵・証明書・MAC アドレス） | §2.2.3 Slot Configuration Summary, p.11 |
| スロットごとのアクセスポリシー詳細 | §2.2.4 Detailed Slot Access Policies, p.12–14 |
| OTP ゾーン | §2.3 EEPROM One Time Programmable (OTP) Zone, p.15 |
| SRAM（TempKey など揮発性領域） | §3 Static RAM (SRAM) Memory, p.17 |
| **コマンドグループの形式（count / packet / CRC-16）** | §4.1 I/O Transactions / Table 4-1, p.18 |
| **コマンドパケットの形式（opcode / param1 / param2 / data）** | §4.2 Command Packets / Table 4-2, p.18 |
| **ステータス / エラーコード一覧** | §4.3 Status/Error Codes / Table 4-3, p.19 |
| ゾーンのアドレス指定方法 | §4.4 Address Encoding, p.20–21 |
| 鍵・署名・証明書のフォーマット | §4.5 Formatting of Keys, Signatures and Certificates, p.22–24 |
| **opcode 一覧**、**パラメータが little-endian であること** | §5 Device Commands / Table 5-1, p.25–26 |
| `Info` / `Random` / `Read` の詳細 | §5.1.2, p.27 / §5.1.5, p.30 / §5.1.6, p.31 |
| **`UpdateExtra`（アドレス変更に使う）** | §5.1.9 UpdateExtra Command, p.34 |
| `GenKey` / `Sign` / `Verify` / `ECDH` | §5.2, p.35–43 |
| `AES` / `KDF` / `MAC` / `GenDig` | §5.3, p.44–53 |
| 開発ツール・CryptoAuthLib | §6.2 Development Tools, p.54–55 |
| **ウェイク条件（tWLO / tWHI）と低速バスでの誤ウェイク** | §7.1.1 Device is Asleep, p.57 |
| **ワードアドレスの値と意味** | §7.2.1 Word Address Values / Table 7-2, p.59–60 |
| **同期が壊れたときの復旧手順** | §7.2.2 I2C Synchronization, p.60 |
| Sleep / Idle シーケンス | §7.3, §7.4, p.60–61 |
| **読み出し時の振る舞い（ビジー・スリープ時は NACK）** | §7.5 I2C Transmission from the ATECC608B-TNGTLS, p.61 |
| EEPROM 耐久・保持年数 | §8.2 Reliability / Table 8-1, p.62 |
| **tWLO / tWHI / tWATCHDOG の規格値** | §8.3 AC Parameters / Table 8-2, p.63 |
| I2C タイミング規格値 | §8.3.1 AC Parameters: I2C Interface / Table 8-3, p.63–64 |

### 本リポジトリ

| 知りたいこと | 参照先 |
|---|---|
| 機械可読なワードアドレス定義 | [`profiles/atecc608b.yaml`](https://github.com/tanakamasayuki/I2CDeviceDB/blob/38f1d82/profiles/atecc608b.yaml) |
| アドレス・datasheet の版・TNGTLS の注記 | [`chips/atecc608b.yaml`](../../chips/atecc608b.yaml) |
| 搭載製品 | [`products/m5stack-u124.yaml`](../../products/m5stack-u124.yaml) |
| probe が何をして何を避けているか | [`scenarios/atecc608b/p0.yaml`](https://github.com/tanakamasayuki/I2CDeviceDB/blob/38f1d82/scenarios/atecc608b/p0.yaml) |
| 実測 | 未取得（`observations/atecc608b/` は存在しない） |

## 出典と確度

- ワードアドレス・パケット形式・エラーコード・ウェイク手順・タイミング: すべて
  Microchip の TNGTLS 完全版 datasheet DS40002250A（2020）。
- 電源範囲・消費電流・I2C タイミング・機能一覧: Summary Data Sheet DS40002239A（2020）。
  両方とも [`chips/atecc608b.yaml`](../../chips/atecc608b.yaml) の `datasheets:` に記録されている版と一致する。
- [`profiles/atecc608b.yaml`](https://github.com/tanakamasayuki/I2CDeviceDB/blob/38f1d82/profiles/atecc608b.yaml) は `evidence.source: datasheet` /
  `confidence: tentative`。`commands:` に並んでいる 4 つは
  「コマンド」ではなく**ワードアドレス**である点に注意（`desc` にもそう書かれている）。
  実際の暗号コマンドの opcode は profile に入っていない。
- **未確認**: 実機キャプチャが無い。ウェイクパルスが実際に何 µs 必要か、
  `0x11` がどのタイミングで返るか、ビジー中の NACK の実際の長さ、400kHz や 1MHz での動作は
  確かめられていない。
- **未調査**: [`scenarios/atecc608b/p0.yaml`](https://github.com/tanakamasayuki/I2CDeviceDB/blob/38f1d82/scenarios/atecc608b/p0.yaml) は
  `safety: reversible-write` で、device-detection / wake / status-read / sleep しか行わない。
  `info-command` / `random-command` / `config-zone-read` / `any-zone-write` / `lock-operations` は
  すべて `deferred`。**ロック操作は不可逆なので意図的に触っていない。**
- **datasheet が答えていないこと**:
  - 一般品 ATECC608B のコマンド仕様は公開されていない（NDA）。本ガイドの記述は
    TNGTLS 版から取ったもので、一般品でも同じである保証は文書上は無い
    （summary は「ATECC608A の全機能・全コマンドが利用できる」とだけ述べている）。
  - コマンドごとの最大実行時間（tEXEC）の表は、参照した 2 文書のどちらにも含まれていない。
    ポーリングで待つか、CryptoAuthLib の値に頼ることになる。
  - M5Stack Unit ID が工場で何をプロビジョニングしているか（どの証明書チェーンか）は
    製品ページ側の情報。
