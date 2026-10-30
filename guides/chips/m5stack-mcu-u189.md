# Unit TimerPWR (U189) — タイマー付き電源ユニット

> リチウムバッテリを充電しながら昇圧 5V を Grove ポートに出し、「あと何秒で切る / あと何秒で
> 入れる」をタイマーで制御する電源そのもの。ホストから見えるのは電源 IC ではなく、
> それを束ねる STM32 のファームウェアである。
>
> **確度**: M5Stack が `U189_UnitTimerPwr_I2C_Protocol_V1` という PDF を公開しており、
> レジスタ表はそこと `M5Unit-TimerPWR` ライブラリのソースの両方に存在する。本ガイドは
> 両方を突き合わせて書いた。ただし `profiles/m5stack-mcu-u189.yaml` の evidence は
> `inferred` / `tentative` のままで、**実測では未確認**。転送長・アクセス方向・バイト順は
> ライブラリのメソッド本体から起こした推定である。

## これは何か

**電源ユニットである。センサではない。** 3.7V のリチウムポリマー電池を USB Type-C から
充電し、DC-DC で 5V に昇圧して Grove ポートに出す。その出力を、内蔵の STM32G031G8U6 に
載った M5Stack ファームウェアが時間で入り切りする。0.66 インチ 64x48 の OLED と側面ボタン
2 個が付いていて、単体でも状態が見える。

I2C の相手はこの STM32 であって、充電 IC でも電流計でもない。**電流・電圧を実際に測っている
INA3221 は STM32 の内側の I2C バスにいて、Grove ポートからは見えない。** 読めるのは STM32 が
中継したレジスタの値である。したがってレジスタマップはこの製品固有のもので、INA3221 の
datasheet を読んでも役に立たない。

できること:

- Grove の 5V 出力を on/off できる。
- 通電時間（power on time）と停止時間（power off time）を「時・分・秒」で設定できる。
  単発の sleep と、on/off を繰り返すサイクル動作の両方がある。
- 起床条件と停止条件のトリガ源（タイマー / ボタン / I2C コマンド）を選べる。
- USB 入力・Grove 出力・バッテリの、電圧と電流を合わせて 6 系統読める。
- 充電中かどうかを読める。
- OLED のバックライトを消せる。
- 設定を内部 flash に保存できる。I2C アドレスも変更できる。

できないこと・持っていないもの:

- **USB だけでは出力できない。** ベンダーの製品ページに明記がある —
  「Unit TimerPWR must be connected to a lithium battery during use. Its USB interface is
  only used to charge the battery and cannot directly supply output power.」
  電池を繋がずに USB だけ挿しても、Grove には 5V が出ない。
- **壁時計としての RTC ではない。** 「毎日 7 時に入れる」はできない。日付も時刻も持たず、
  持っているのは「あと何時間何分何秒」という相対タイマーだけである。日時レジスタは無い。
- **残量計ではない。** 電圧と電流の瞬時値は返すが、クーロンカウントも残量 % も返さない。
  必要ならホスト側で積分する。
- 出力電圧は変えられない。昇圧回路は 5V 固定。
- 割り込み線は無い。**変化を知るにはポーリングするしかない。**
- Grove ポートは 1 系統しかない。**5V 出力と I2C が同じコネクタを通る。**
  分岐したい場合はハブが要る。

## 早見表

| 項目 | 値 |
|---|---|
| I2C アドレス | `0x56`（既定） |
| アドレス変更 | レジスタ `0xFF` に書き込み。**書いた直後から新アドレスで応答する** |
| 入力 | 5V DC（USB Type-C）。バッテリ充電専用 |
| 出力 | 昇圧 5V。1400mAh 電池で 4.96V@800mA、110mAh 電池で 5.03V@400mA（製品ページ） |
| バッテリ | 3.7V リチウムポリマー（別体）。充電 IC LGS4056H、充電電流 330mA @5V |
| インタフェース | Grove HY2.0-4P × 1。5V 出力と I2C を同じポートで兼ねる |
| I2C 速度 | ライブラリの既定は 400kHz（未実測。上限は firmware 依存） |
| 中身 | STM32G031G8U6 + INA3221（内部バス）+ LGS4056H + 0.66" OLED 64x48 |
| ボタン | 2 個（A / B）。**アクティブ Low** |
| 割り込み | 無し（ポーリング） |
| バイト順 | 32bit の測定値はリトルエンディアン（LSB first） |
| 待機電流 | 36.67 µA @4.2V／動作温度 0〜40 °C |

## レジスタ

| アドレス | 長さ | R/W | 意味 |
|---|---|---|---|
| `0x00` | 1 | R/W | Grove 5V 出力。`0`=off、`1`=on。**書くと負荷の電源が変わる** |
| `0x01` | 1 | R/W | OLED バックライト。`0`=off、`1`=on |
| `0x10` | 2 | R | ボタン。byte0 = A、byte1 = B。**`0` が押下、`1` が非押下** |
| `0x20` | 1 | R/W | 起床トリガのビットマップ。bit0 = タイマー、bit1 = ボタン |
| `0x21` | 1 | R/W | 停止トリガのビットマップ。bit0 = タイマー、bit1 = ボタン、bit2 = I2C |
| `0x30` | 3 | R/W | 通電時間。byte0 = 時（0-255）、byte1 = 分（0-59）、byte2 = 秒（0-59） |
| `0x40` | 1 | W | `1` を書くと即座に sleep に入る |
| `0x41` | 1 | R/W | サイクル動作。`0`=停止、`1`=開始 |
| `0x60` | 4 | R | USB 入力電圧。符号付き 32bit、LSB first、単位は **1/100 V** |
| `0x64` | 4 | R | USB 入力電流。符号付き 32bit、LSB first、単位は **1/100 mA** |
| `0x68` | 4 | R | Grove 出力電圧。符号付き 32bit、LSB first、単位は **1/100 V** |
| `0x6C` | 4 | R | Grove 出力電流。符号付き 32bit、LSB first、単位は **1/100 mA** |
| `0x70` | 4 | R | バッテリ電圧。符号付き 32bit、LSB first、単位は **1/100 V** |
| `0x74` | 4 | R | バッテリ電流。符号付き 32bit、LSB first、単位は **1/100 mA** |
| `0x80` | 3 | R/W | 停止時間。byte0 = 時（0-255）、byte1 = 分（0-59）、byte2 = 秒（0-59） |
| `0x90` | 1 | R | 充電状態。`1`=充電中、`0`=非充電 |
| `0xF0` | 1 | W | `1` を書くと現在の設定を内部 flash に保存する |
| `0xFC` | 1 | R | ブートローダのバージョン（ライブラリのみ。PDF には記載が無い） |
| `0xFE` | 1 | R | ファームウェアのバージョン |
| `0xFF` | 1 | R/W | I2C アドレス |

**測定値の並び**: `0x60` から `0x77` までは連続した 24 バイトの領域で、ヘッダは全体を
`VOLTAGE_AND_CURRENT` という 1 つのレジスタとして定義している。ライブラリはそのベースに
0, 4, 8, 12, 16, 20 を足して 4 バイトずつ読むので、上の表では 6 本の 32bit レジスタとして
分けてある。PDF の式は `byte0 + byte1*256 + byte2*65536 + byte3*16777216`、つまり LSB first
であることと、「The unit of voltage is V. The unit of current is mA. The true value needs to
be divided by 100.」を明記している。

**転送の形**: 書き込みは「ポインタバイト + ペイロード」を 1 トランザクションで送る。
読み出しはポインタバイトを書いて **repeated START**（`endTransmission(false)`）を打ち、
続けて必要な長さを要求する。

## 典型的な使い方

1. `0xFE` を読んでファームウェアのバージョンを確認する（疎通確認も兼ねる）。
2. 測るだけなら以降は読み出しだけで済む。`0x90` で充電中かを見て、`0x60`〜`0x74` を
   4 バイトずつ読み、100 で割って V / mA にする。
3. タイマー動作を仕込むときは順番がある。
   1. `0x20` と `0x21` で起床・停止のトリガ源を立てる。両方ともビットマップなので、
      **読んで、該当ビットを立てて、書き戻す**（ライブラリの setter は全部これをやる）。
   2. `0x30` に通電時間、`0x80` に停止時間を「時・分・秒」の 3 バイトで書く。
   3. 繰り返し動作させるなら `0x41` に `1`。1 回だけ落とすなら `0x40` に `1`。
   4. 電源が切れても残したい設定なら、最後に `0xF0` に `1` を書いて flash に保存する。
4. ボタンは `0x10` を 2 バイト読む。`0` が押下。

## 利用上の注意

- **`0x00` に `0` を書くと Grove の 5V 出力が止まる。** このユニットの Grove ポートは
  1 つしかなく、そこに 5V 出力と I2C が同居している。ホストがそのポートから給電されて
  いるなら、`0x00` に `0` を書くことは**自分の電源を落とすこと**である。読み書きの実験を
  するなら、ホストは別電源で動かす。
- **`0x40` に `1` を書くと即座に sleep に入り、バスが消える。** ライブラリの `sleep_once`
  例も、これをセッションの最後の操作として呼んでいる。以後は `0x20` で設定した起床条件
  （タイマーかボタン）が成立するまで応答が返らない。`0x30`（通電時間）に 0 以外を入れて
  から `0x40` を書くと「その時間だけ待ってから落ちる」挙動になる。
- **この 2 つがあるので、本リポジトリの probe は read-only である。**
  `scenarios/m5stack-mcu-u189/p0.yaml` は `safety: read-only` を宣言し、読めるレジスタを
  舐めるだけで 1 バイトも書かない。`write-sleep-command` と `write-save-to-flash`、および
  書き込み挙動全般は `deferred` に置いてある。**つまり「書いたらどうなるか」は本ガイドでは
  実測で裏付けられていない。**
- **設定は `0xF0` を書くまで揮発する。** トリガと時間を書いただけでは電源断で消える。
  ただし `0xF0` は flash 書き込みなので、ループの中で毎回叩いてよいものではない。
- **`0x20` / `0x21` は read-modify-write が前提。** ライブラリの setter はどれも、
  対象レジスタを 1 バイト読み、ビットを立て（または落とし）、丸ごと書き戻す。バスの
  キャプチャを見ると書き込みの直前に必ず同じレジスタの読み出しが出るのはそのためで、
  デバイス側の仕様ではない。自分で書くときも、他のビットを消さないよう同じことをする。
- **ボタンはアクティブ Low。** `0x10` の値をそのまま「押されている」と解釈すると意味が
  逆になる。PDF にも `0: press; 1: no press` と明記されている。
- **測定値は 100 で割る。** 符号付き 32bit LSB first。100 で割ると電圧は V、電流は mA に
  なる。符号付きなのは充電と放電で向きが変わるためと思われるが、**どちらが正かは未確認**。
- **`0x21` の bit2（I2C sleep トリガ）は PDF に載っていない。** PDF は `0x20` `0x21` の
  どちらも bit0 = TIMER、bit1 = BUTTON までしか書いていない。bit2 の存在はライブラリの
  `set_i2c_sleep_trigger()` だけが根拠で、**未確認**。
- **`0xFC`（ブートローダのバージョン）も PDF に載っていない。** ライブラリのみが持つ。
- **アドレス変更は即時反映。** `0xFF` に書いた次のトランザクションはもう新アドレス宛に
  出さないと応答しない。書き換えた値を忘れると `0x56` を探しても見つからないユニットが
  手元に残る。復旧は scan で全アドレスを舐めることになる。
- **電池を繋がずに使わない。** USB 単独では出力が出ない。「電源を入れたのに Grove から
  5V が来ない」の原因はたいていこれである。

## どこに何が書いてあるか

| 知りたいこと | 参照先 |
|---|---|
| レジスタ表（ベンダー公式） | [U189 I2C Protocol V1 PDF](https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/products/unit/Unit-TimerPWR/U189_UnitTimerPwr_I2C_Protocol_V1_20241018.pdf)（2024-03-11、firmware V1 向け。1 行 16 バイトの表で、行頭の `0x40` `0xF0` は行の先頭アドレス） |
| 製品仕様・出力能力・購入 | [M5Stack 製品ページ](https://docs.m5stack.com/en/unit/Unit-TimerPWR) |
| レジスタ定数の定義 | [`collection/vendor/M5Unit-TimerPWR/src/m5_unit_timerpwr.hpp`](https://github.com/tanakamasayuki/I2CDeviceDB/blob/38f1d82/collection/vendor/M5Unit-TimerPWR/src/m5_unit_timerpwr.hpp) |
| 転送長・アクセス方向・バイト順・read-modify-write | [`collection/vendor/M5Unit-TimerPWR/src/m5_unit_timerpwr.cpp`](https://github.com/tanakamasayuki/I2CDeviceDB/blob/38f1d82/collection/vendor/M5Unit-TimerPWR/src/m5_unit_timerpwr.cpp) |
| ライブラリの取得元と commit 固定 | [`collection/vendor_libraries.toml`](https://github.com/tanakamasayuki/I2CDeviceDB/blob/38f1d82/collection/vendor_libraries.toml) の `["M5Unit-TimerPWR"]` |
| 機械可読なレジスタマップ | [`profiles/m5stack-mcu-u189.yaml`](https://github.com/tanakamasayuki/I2CDeviceDB/blob/38f1d82/profiles/m5stack-mcu-u189.yaml) |
| ユニットの構成・搭載 IC | [`chips/m5stack-mcu-u189.yaml`](../../chips/m5stack-mcu-u189.yaml) |
| 製品（SKU・ベンダー URL） | [`products/m5stack-u189.yaml`](../../products/m5stack-u189.yaml) |
| 測定計画と安全性の宣言 | [`scenarios/m5stack-mcu-u189/p0.yaml`](https://github.com/tanakamasayuki/I2CDeviceDB/blob/38f1d82/scenarios/m5stack-mcu-u189/p0.yaml) |
| read-only probe のソース | [`collection/sketches/m5stack-mcu-u189__characterize/`](https://github.com/tanakamasayuki/I2CDeviceDB/tree/38f1d82/collection/sketches/m5stack-mcu-u189__characterize/) |
| 実測データ | [`observations/`](https://github.com/tanakamasayuki/I2CDeviceDB/tree/38f1d82/observations/) — 本ユニットのディレクトリはまだ無い |

## 出典と確度

- レジスタのアドレスと意味: ベンダーの Protocol PDF と `M5Unit-TimerPWR` ライブラリの
  ソース（`collection/vendor/M5Unit-TimerPWR` に commit `94e0089` で固定）。両者は
  一致する。ただし `0xFC` と `0x21` bit2 はライブラリにしか無い。
- 転送長・アクセス方向・バイト順・read-modify-write の必要性: 同ライブラリのメソッド
  本体から起こした**推定**。`profiles/` の evidence は `inferred` / `tentative`。
- ボタンのアクティブ Low、測定値の 1/100 スケールと LSB first: PDF に明記あり。
- **未確認**: 実機のキャプチャがまだ無い。レジスタが本当に表のとおり応答するか、
  未使用アドレスがどう振る舞うか、400kHz で動くかは確かめられていない。
- **未調査**: 書き込み側の挙動全般（5V 出力の切り替え、sleep、flash 保存、アドレス変更）は
  probe が read-only のため触れていない。電流値の符号の向きも確かめていない。
