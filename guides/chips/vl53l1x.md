# VL53L1X — 940nm レーザ Time-of-Flight 測距センサ（最大 4 m）

> 光が往復する時間を測って距離を返すモジュール。**レジスタアドレスが 16bit** で、
> 同じカタログにある VL53L0X（8bit）とは**バス上の形が違う**。
> ST は完全なレジスタ表を公開しておらず、測距には ST 製ドライバの初期化データが要る。
>
> **確度**: STMicroelectronics の datasheet **DocID031281 Rev 2（2018-02）、全 35 ページ**に
> 基づく。`chips/vl53l1x.yaml` が記録している st.com の URL は 2026-08-15 時点で取得できず
> （TLS 層で自動取得が拒否される）、**M5Stack がミラーしている PDF**（ファイル名は
> オーダー品番の `VL53L1CXV0FY.PDF`）を取得して読んだ。カタログが記録している版は
> Rev 8（2024-08-09）で、**版が違う**。
>
> さらに重要な点として、**この datasheet にはレジスタ表が無い**。
> `profiles/vl53l1x.yaml` に載っている `0x0086` / `0x0087` / `0x0089` / `0x0096` は
> **ST の ULD ドライバから起こした推定**であって、datasheet に裏付けは無い。**未確認**。

## これは何か

940 nm の Class 1 レーザ（VCSEL）と SPAD（単一光子アバランシェダイオード）アレイ、
そしてマイコンとファームウェアを 4.9×2.5×1.56 mm のモジュールに収めた測距センサ。
「光を出して、返ってくるまでの時間を測る」ので、対象の色や反射率にあまり依存しない距離が出る。

できること:

- 最大 4 m までの測距。距離モードは short / medium / long の 3 つで、
  暗所での最大距離は順に 136 / 290 / 360 cm（datasheet p10, Table 4）。
- 測距周波数は最大 50 Hz。1 回の測距に掛ける時間（timing budget）を 20〜1000 ms で
  設定でき、長くするほど遠くまで届き繰り返し精度も上がる（p11）。
- **視野（FoV）をソフトで絞れる。** SPAD アレイ上の関心領域（ROI）を 4×4 から 16×16 まで
  位置・サイズとも指定でき、実効 FoV を 15〜27° の範囲で変えられる。
  複数の ROI を順に見ることで「多ゾーン」的な使い方もできる。
- 距離のしきい値（below / above / inside / outside）を設定し、条件を満たしたときだけ
  割り込みを上げられる（p9）。
- I2C アドレスをソフトで変更できる。XSHUT ピンで 1 個ずつ起こしてアドレスを振り分ければ、
  同じバスに複数個を並べられる。
- 返る値は距離 [mm] のほか、戻り光量（signal rate）、環境光量（ambient rate）、
  レンジステータス（p9）。

**できないこと・持っていないもの**:

- **電源投入しただけでは測距しない。** ST のドライバが行う初期化（レジスタへの大量の
  書き込み。俗に「初期化 blob」と呼ばれる）を流し込むまで、距離は出ない。
  レジスタを 2〜3 本叩けば動く類のセンサではない。
- **ST は完全なレジスタ表を公開していない。** 配布されるのは C の API / ULD（Ultra Lite
  Driver）で、datasheet には識別用の 3 本（`0x010F`/`0x0110`/`0x0111`）しか載っていない。
  他のレジスタ番号は、すべてドライバのソースから読み取ったもの。
- **画像もマップも出ない。** 出るのは ROI ごとの代表距離 1 個である。
- **強い環境光で急激に短くなる。** long モードは暗所で 360 cm 届くが、
  強い環境光下では **73 cm** まで落ちる（p10, Table 4）。short モードは環境光に強く、
  136 cm → 135 cm とほとんど変わらない。屋外で使うなら short 一択に近い。
- **透明・鏡面・吸収体は苦手。** ガラスや黒い布はレーザを返してくれない。
  測れなかった場合はレンジステータスがそう報告する（p9）。
- **カバーガラスを付けたら較正が要る。** RefSPAD とクロストークの較正をやり直す必要がある
  （p9）。較正値はホスト側に保存し、**起動のたびにドライバ経由で書き戻す**。
- 動作温度は -20〜85 ℃（M5Stack Unit ToF4M の製品ページは 0〜40 ℃ を謳う）。

**電気的な現実**:

- AVDD 2.6〜3.5 V（typ 2.8 V）。絶対最大 3.6 V。**5 V は掛けられない。**
  M5Stack Unit ToF4M の Grove 赤線は 5 V なので、ユニット基板側で降圧している。
- **I/O レベルは既定で 1.8 V である**（p23, Table 14 の注 4: "The default driver mode is
  1V8"）。2V8 モードはドライバが書き込む設定で切り替える。SDA/SCL/XSHUT/GPIO1 の
  絶対最大は 3.6 V。
- **XSHUT は必ずドライブする。** 浮かせるとリーク電流が流れる。ホスト側の状態が
  不定ならプルアップを入れる（推奨 10 kΩ）。XSHUT を High にしてよいのは AVDD が
  立ち上がった後だけ（p6, p23）。
- GPIO1（割り込み）はオープンドレイン。使わないなら未接続でよい。
- **ブート時間は最大 1.2 ms**（p12）。XSHUT を制御しない構成では、電源投入後に自動で
  ソフトウェアスタンバイに入る。

## 早見表

| 項目 | 値 |
|---|---|
| I2C アドレス | `0x29`（既定）。**ソフトで変更可**（`0x0001`） |
| 8bit 表記 | write `0x52` / read `0x53`（p19） |
| **レジスタアドレス幅** | **16bit、MSB first**（VL53L0X は 8bit。ここが最大の違い） |
| レジスタ幅 | 8bit。オートインクリメント有り |
| 電源（AVDD） | 2.6〜3.5 V（typ 2.8 V）。絶対最大 3.6 V |
| I/O レベル | 既定 1.8 V。2V8 モードはドライバ設定で切替（p23） |
| インタフェース | I2C。**Section 4 は最大 400 kbit/s**（表紙の Features は "up to 1 MHz" と食い違う） |
| 測距範囲 | 4〜400 cm |
| 距離モード | short / medium / long。暗所最大 136 / 290 / 360 cm |
| Timing budget | 20〜1000 ms。20 ms は short 専用、33 ms が全モード共通の下限、140 ms で 4 m |
| 測距周波数 | 最大 50 Hz |
| FoV | 27°（full）。ROI 指定で 15° まで絞れる |
| 光源 | 940 nm、Class 1 |
| 識別 | `0x010F` から 2 バイト読むと `0xEACC`（Model ID `0xEA` + Module Type `0xCC`） |
| ブート時間 | 最大 1.2 ms |
| 動作温度 | -20〜85 ℃ |
| ピン互換 | VL53L0X とピン互換。**プロトコルは互換ではない** |
| 搭載製品 | M5Stack Unit ToF4M (U172) — `0x29` |

## レジスタ・コマンド

### 16bit アドレスであること

これがこのチップを扱ううえで最初に理解すべき点である。**書き込みトランザクションは
「デバイスアドレス → インデックス上位バイト → インデックス下位バイト → データ…」**という
形になる（p19-20, Figure 13）。読み出しは、まずこの 2 バイトのインデックスを書いてから
repeated START で読む（p21, Figure 16）。

```
書き: S | 0x52 | INDEX[15:8] | INDEX[7:0] | DATA | ... | P
読み: S | 0x52 | INDEX[15:8] | INDEX[7:0] | P
      S | 0x53 | DATA | DATA | ... | P
```

インデックスは 1 バイト転送ごとに自動でインクリメントされる（p20）。

**同じカタログの [`vl53l0x`](../../chips/vl53l0x.yaml) は 8bit アドレスである。**
ピンは互換なので基板上は差し替えられるが、**バス上の形が違うので、ドライバは
そのままでは通用しない**。`profiles/vl53l0x.yaml` の `sysrange-start` が `0x00`、
`system-intr-clear` が `0x0B` のように 1 バイトで書かれているのに対し、
`profiles/vl53l1x.yaml` は `register_address_bits: 16` を宣言して `0x0086` のように
2 バイトで書いている。この違いは、キャプチャを見れば一目で分かる（書き込みトランザクションの
長さが 1 バイト違う）。

### datasheet に載っているレジスタ

**datasheet が示しているのはこの 3 本だけ**である（p22, Table 11）。

| インデックス | 名前 | ドライバ未ロード時の値 |
|---|---|---|
| `0x010F` | Model ID | `0xEA` |
| `0x0110` | Module Type | `0xCC` |
| `0x0111` | Mask Revision | `0x10` |

`0x010F` から 2 バイトまとめて読むと `0xEACC` になる。存在確認にはこれを使う。

### ドライバ由来のレジスタ（**未確認**）

以下は `profiles/vl53l1x.yaml` に入っているが、**datasheet には載っていない**。
ST の ULD ドライバのソースに現れる番号であって、ベンダーが仕様として保証したものではない。

| インデックス | 名前 | 意味（推定） |
|---|---|---|
| `0x0000` | soft reset | ソフトウェアリセット |
| `0x0001` | I2C slave address | 7bit アドレス（既定 `0x29`） |
| `0x0086` | system interrupt clear | 割り込みクリア |
| `0x0087` | system mode start | 測距の開始・停止 |
| `0x0089` | result range status | レンジステータス |
| `0x0096` | result final range mm | 距離 [mm]、16bit |

**16bit の結果値がビッグエンディアンなのは、アドレスの若い方が MSB という
datasheet p22 Table 12 の規約に従っているためと考えられる**（32bit レジスタの例として
「Address が MSB、Address+3 が LSB」と書かれている）。これは datasheet の一般規約から
の推論であって、`0x0096` について明示された記述ではない。

## 典型的な使い方

**存在確認だけなら（安全、初期化不要）**:

1. `0x010F` を 16bit インデックスとして書き、2 バイト読む。`0xEACC` なら VL53L1X。
2. `0x0001` を読めば現在の I2C アドレスが分かる。

本リポジトリの probe（`collection/sketches/vl53l1x__characterize/vl53l1x__characterize.ino`）が
やっているのはここまでである。**16bit アドレスであることをバス上に記録する**のが目的で、
測距は `deferred` に置かれている。

**測距する場合**:

1. ST の VL53L1X API / ULD ドライバを持ってくる。
2. `VL53L1X_SensorInit()` 相当を呼ぶ。ここで大量のレジスタ書き込み（初期化 blob）が流れる。
   **この手順を自前で再現するのは現実的でない。** ST は完全なレジスタ表を公開しておらず、
   ドライバのバイト列を写す以外の方法が無い。
3. 距離モードと timing budget を設定する。33 ms が全モード共通の下限、
   4 m 出したければ long モードで 140 ms（p11）。
4. 必要なら ROI を設定する。
5. 測距を開始する。連続モードでは inter-measurement period（測距と測距の間隔）も設定する。
6. 割り込みかポーリングでデータレディを待ち、距離・signal rate・ambient rate・
   レンジステータスを読む。
7. **割り込みをクリアする。** datasheet が "A clear interrupt is mandatory to allow the
   next ranging data to be updated" と明記している（p9）。**クリアし忘れると次のデータが
   更新されない。**
8. 較正値があれば、**起動のたびに**書き戻す（p9）。

**複数個を同じバスに置く場合**:

1. 全部の XSHUT を Low にして黙らせる。
2. 1 個だけ XSHUT を High にして起こす（AVDD が立っている状態で）。
3. `0x29` でアクセスして `0x0001` に新しいアドレスを書く。
4. 次の 1 個を起こす。以下繰り返し。

## 利用上の注意

- **VL53L0X 用のコードは動かない。** レジスタアドレスが 8bit と 16bit で違う。
  ピン互換なので基板を差し替えても通電はするが、**`0x29` に ACK は返るのに
  読める値がすべておかしい**、という症状になる。デバイスが応答しているだけに気づきにくい。
  見分け方は `0x010F` を 16bit インデックスで読んで `0xEACC` が返るかどうか。

- **`0x29` は本カタログ内で `vl53l0x` と `tcs34725`（カラーセンサ）と衝突する。**
  VL53L1X 側はソフトでアドレスを変えられるので、変更する側はこちらにする。
  ただし**変更は電源を切ると消える**（`0x0001` は不揮発ではない）。起動のたびに
  XSHUT を使った振り分けをやり直す必要がある。

- **`0x29` が 2 個いたら XSHUT で分ける以外に方法が無い。** 電源投入時は全員が `0x29` で
  応答するため、バス上で区別できない。XSHUT の配線を用意していないなら、
  複数個の運用は諦めることになる。

- **XSHUT を浮かせない。** リーク電流が流れる、と datasheet が明記している（p6）。
  10 kΩ でプルアップする。**AVDD が立つ前に XSHUT を High にしてはいけない**（p23 の注 2）。

- **割り込みのクリアは必須。** 「次の測距データを更新させるために割り込みのクリアが
  必要」と datasheet が書いている（p9）。ポーリングで距離だけ読み続けて割り込みを
  クリアしないと、**同じ値が返り続ける**。

- **I/O は既定で 1.8 V。** 3.3 V のホストと直結できるかはモジュール側の実装次第である。
  2V8 モードはドライバがロードする設定で切り替えるもので、ハードのジャンパではない（p23）。
  生の VL53L1X を扱うときは、レベル変換の要否を先に確認すること。

- **バス速度の記述が datasheet 内で食い違っている。** 表紙の Features は
  "I²C interface (up to 1 MHz)"、Section 4 は "maximum speed of 400 kbits/s"、
  Table 1 は "Up to 400 kHz (Fast mode)" と書いている。**400 kHz を上限として扱うのが安全**。

- **timing budget と距離はトレードオフ。** 20 ms は short モードでしか使えない。
  33 ms が全モード共通の下限。4 m 出すには long モードで 140 ms 必要で、
  そのぶん平均消費電力も上がる（p11）。「速くて遠い」設定は無い。

- **環境光で測距距離が激変する。** long モードは暗所 360 cm に対し強い環境光下で 73 cm
  （p10, Table 4）。屋外や窓際で使うなら、この差を見込んで距離モードを選ぶ。
  **暗所で確認した設定をそのまま屋外に持っていくと届かない。**

- **カバーガラスを付けたら較正が要る。** RefSPAD とクロストークの較正はカバーガラスを
  追加したときに必ずやり直す。オフセット較正はどんな場合でも必要（p9）。
  較正値はチップ内に残らないので、**ホストが保存して起動のたびに書き戻す**。
  これも ST のドライバ関数経由でしかできない。

- **測れなかったことを距離 0 で表現しない。** 対象が無い場合や信号が弱い場合は
  レンジステータスがそう報告する（p9）。ステータスを見ずに距離だけ使うと、
  「何も無い方向で 0 mm」という誤読が起きる。ステータスの値の意味は datasheet ではなく
  API ユーザマニュアル **UM2356** の側にある。

- **profile のレジスタ番号は仕様ではない。** `0x0086` / `0x0087` / `0x0089` / `0x0096` は
  ドライバから起こした推定である（`profiles/vl53l1x.yaml` の `evidence.source` も
  `inferred`）。これらに直接書き込んで測距させようとしても、初期化 blob 無しでは
  動かない。

## どこに何が書いてあるか

| 知りたいこと | 参照先 |
|---|---|
| **16bit インデックスの転送形式** | datasheet p19-21（Section 4、Figure 13〜16） |
| I2C のデバイスアドレス（`0x52`/`0x53`） | datasheet p19（Figure 12） |
| I2C のタイミング特性 | datasheet p21（Table 10） |
| 識別レジスタ（`0x010F`/`0x0110`/`0x0111`） | datasheet p22（Table 11） |
| マルチバイト値のバイト順の規約 | datasheet p22（Table 12） |
| 距離モードと最大距離（環境光の影響） | datasheet p10（Table 4） |
| Timing budget と距離・繰り返し精度 | datasheet p11（Section 2.5.2、Figure 6） |
| ROI としきい値割り込みの説明 | datasheet p9（Section 2.4） |
| 較正が必要な場面と 3 種類の較正 | datasheet p9（Section 2.3） |
| 電源電圧・I/O レベル・1V8/2V8 | datasheet p23（Table 13、Table 14） |
| XSHUT / GPIO1 の扱いとプルアップ値 | datasheet p6（Section 1.4 の Note） |
| ブートシーケンスとブート時間 | datasheet p12（Section 2.6.1） |
| **レジスタ番号・API 関数・レンジステータスの意味** | **VL53L1X API user manual (UM2356)。本ガイドでは読んでいない** |
| ドライバ実装 | [M5Unit-ToF4M](https://github.com/m5stack/M5Unit-ToF4M)（M5Stack 製品ページからのリンク。**本ガイドでは読んでいない**） |
| 機械可読なレジスタマップ | [`profiles/vl53l1x.yaml`](https://github.com/tanakamasayuki/I2CDeviceDB/blob/38f1d82/profiles/vl53l1x.yaml)（`evidence.source: inferred`） |
| VL53L0X との違い | [`profiles/vl53l0x.yaml`](https://github.com/tanakamasayuki/I2CDeviceDB/blob/38f1d82/profiles/vl53l0x.yaml)（8bit アドレス）、[`chips/vl53l0x.yaml`](../../chips/vl53l0x.yaml) |
| アドレスと搭載製品 | [`chips/vl53l1x.yaml`](../../chips/vl53l1x.yaml)、[`products/m5stack-u172.yaml`](../../products/m5stack-u172.yaml) |
| 本リポジトリの probe が何を触るか | [`scenarios/vl53l1x/p0.yaml`](https://github.com/tanakamasayuki/I2CDeviceDB/blob/38f1d82/scenarios/vl53l1x/p0.yaml)、`collection/sketches/vl53l1x__characterize/vl53l1x__characterize.ino` |
| ミラーされた datasheet（Rev 2） | [M5Stack OSS の VL53L1CXV0FY.PDF](https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/products/unit/Unit-ToF4M/VL53L1CXV0FY.PDF) |
| ユニットの仕様・測距範囲 | [M5Stack Unit ToF4M 製品ページ](https://docs.m5stack.com/en/unit/Unit-ToF4M) |
| 実測（あれば） | `observations/vl53l1x/`（現時点で存在しない） |

## 出典と確度

- **一次資料**: STMicroelectronics *VL53L1X — A new generation, long distance ranging
  Time-of-Flight sensor*、**DocID031281 Rev 2（2018-02）、全 35 ページ**。
- `chips/vl53l1x.yaml` が記録している `https://www.st.com/resource/en/datasheet/vl53l1x.pdf`
  は **2026-08-15 時点で取得できなかった**（`tools/datasheet_text.py` も curl も TLS 層で
  切断される）。代わりに M5Stack Unit ToF4M の製品ページからたどれる
  `https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/products/unit/Unit-ToF4M/VL53L1CXV0FY.PDF`
  を取得し、pypdf でテキスト化して読んだ。本文中のページ番号はこの Rev 2 のもの。
- **版の違い**: カタログの記録は Rev 8（2024-08-09）。**Rev 2 → Rev 8 の変更点は
  確認していない。** 距離性能の数値や推奨条件が更新されている可能性がある。**未確認**。
- **16bit インデックスであること**、デバイスアドレス、転送形式、識別レジスタの値、
  距離モード、timing budget、電源・I/O レベル、XSHUT の扱いは、すべて上記 datasheet の記述。
- **`0x0000` / `0x0001` / `0x0086` / `0x0087` / `0x0089` / `0x0096` は datasheet に載っていない。**
  `profiles/vl53l1x.yaml` がドライバから起こした**推定**であり、本ガイドもそう扱っている。
  番号・意味・バイト順のいずれも **未確認**。
- **UM2356（VL53L1X API user manual）は読んでいない。** datasheet が「詳細は UM2356 を
  見よ」としている箇所（レジスタ番号、API 関数、較正手順の詳細、レンジステータスの
  値の意味）は、したがって本ガイドでは埋まっていない。
- **M5Unit-ToF4M ライブラリのソースも読んでいない。** 初期化 blob の中身や、
  M5Stack がどの API を使っているかは確認していない。
- M5Stack Unit ToF4M が 5 V を降圧している、という記述は、チップの AVDD 上限（3.5 V）と
  Grove の 5 V が両立しないことからの**推論**である。ユニットの回路図で実際の
  レギュレータを確認してはいない。**未確認**。
- `0x29` の衝突（`vl53l0x`、`tcs34725`）は `chips/*.yaml` のアドレス欄を突き合わせて確認した。
- **未確認**: 実機のキャプチャが無い。`0x010F` が `0xEACC` を返すか、
  16bit インデックスの転送が想定どおりバス上に現れるか、400 kHz で動くかは確かめていない。
  `scenarios/vl53l1x/p0.yaml` はまさにこの「同じセンサ系列で転送形が違う」ことを
  記録するための計画だが、まだ走っていない。
- **未調査**: 測距そのもの（初期化 blob、単発測距、連続測距、ROI 設定）は
  `scenarios/vl53l1x/p0.yaml` で `deferred` に置かれており、一切触れていない。
