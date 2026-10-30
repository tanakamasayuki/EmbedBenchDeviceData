# Unit ByteSwitch (U191) — 8 連トグルスイッチ入力ユニット

> 8 個のトグルスイッチと 9 個の RGB LED を、I2C のレジスタとして読み書きできるように
> したユニット。ホストから見えるのはスイッチでも LED でもなく、STM32 のファームウェアである。
>
> **確度**: M5Stack が `Byte_Switch_protocol` という PDF を公開しており、レジスタ表は
> そこと `M5Unit-ByteButton` ライブラリのソースの両方に存在する。本ガイドは両方を
> 突き合わせて書いた。ただし `profiles/m5stack-mcu-u191.yaml` の evidence は
> `inferred` / `tentative` のままで、**実測では未確認**。転送長・アクセス方向は
> ライブラリのメソッド本体から起こした推定である。

## これは何か

物理的な**ラッチ式のトグルスイッチが 8 個**並んだ入力ユニット。各スイッチの脇に
WS2812C-2020 の RGB LED があり、全部で 9 個（8 個 + 予備 1 個）。それを STM32G031G8U6 に
載った M5Stack ファームウェアが束ねている。Grove ポートが 2 つあり、同じ I2C バスに
複数台を数珠つなぎできる。

**同じライブラリ・同じレジスタマップが Unit ByteButton (U192) と共用されている。**
`M5Unit-ByteButton` の `UnitByte` クラスには「どちらのユニットか」の分岐が一切無く、
2 つを分けているのは**バスアドレスだけ**である（ByteSwitch = `0x46`、ByteButton = `0x47`）。
レジスタ番号も長さも方向も完全に同一で、違うのは入力の意味の言い回し
（こちらは倒したら倒れたままのトグルスイッチ、あちらは押している間だけのモーメンタリな
ボタン）だけである。付属サンプルも `examples/unit-byteSwitch` が `0x46` を、
`examples/unit_byteButton` が `0x47` を使うだけの差になっている。

できること:

- 8 個のスイッチの状態を、1 バイトにビットで詰めた形（`0x00`）でも、1 個 1 バイトの形
  （`0x60`〜`0x67`）でも読める。
- LED 9 個の明るさを個別に設定できる（0〜255）。
- LED の色を、フル階調の RGB888 でも、1 バイトに詰めた RGB233 でも指定できる。
- 「スイッチが off のときの色」「on のときの色」をユニット側に登録しておき、
  **ホストが何もしなくてもファームウェアが色を切り替える**モードにできる（`0x19`）。
- 設定を内部 flash に保存できる。I2C アドレスも変更できる（1〜127）。

できないこと・持っていないもの:

- **イベントは返らない。現在の状態だけである。** 「いつ倒れたか」「何回倒したか」は
  持っていない。エッジ検出も回数カウントもホスト側で差分を取る。
- **Grove の 4 ピンに割り込みは出てこない。** `0xF1` の IRQ を有効にすると
  SWDIO(PA13) が IRQ ピンになる、と PDF は書いている。Grove コネクタは GND / 5V / SDA / SCL
  の 4 本しかないので、**Grove 経由では結局ポーリングになる**。PA13 が基板上のどこに
  出ているかは未確認。
- **LED の明るさは書けても読み戻せない**（ライブラリの制約。後述）。
- 9 個目の LED（index 8）に対応するスイッチは無い。`0x70` / `0x90` の「システム定義色」
  ブロックもスイッチ 0〜7 の 8 個分しか無い。
- チャタリング除去の設定や、スイッチの機械的な仕様は firmware からは読めない。

## 早見表

| 項目 | 値 |
|---|---|
| I2C アドレス | `0x46`（既定）。Unit ByteButton (U192) は `0x47` |
| アドレス変更 | レジスタ `0xFF` に書き込み。範囲は 1〜127。**書いた直後から新アドレスで応答する** |
| 電源 | DC 5V（Grove の赤）。待機 9.03mA |
| インタフェース | Grove HY2.0-4P × 2（I2C 引き通し。カスケード用） |
| I2C 速度 | サンプルは 400kHz を渡す（未実測。上限は firmware 依存） |
| 中身 | STM32G031G8U6（Cortex-M0+ 64MHz）+ トグルスイッチ 8（DPDT）+ WS2812C-2020 × 9 |
| 割り込み | `0xF1` で有効化すると SWDIO(PA13) が IRQ になる。Grove には出ない |
| バイト順 | RGB888 は **B, G, R, 未使用** の順で線に出る（リトルエンディアン） |
| 寸法・重量 | 88.0 × 24.0 × 19.6 mm / 12.2 g。動作温度 0〜40 °C |

## レジスタ

| アドレス | 長さ | R/W | 意味 |
|---|---|---|---|
| `0x00` | 1 | R | 8 個のスイッチ状態を 1 バイトに詰めたもの。bit n = スイッチ n |
| `0x10` + n | 1 | R/W | LED n の明るさ（0〜255）。n は 0〜8 なので `0x10`〜`0x18` |
| `0x19` | 1 | R/W | LED の色の出どころ。`0`=ユーザ定義、`1`=システム定義 |
| `0x20` + n×4 | 4 | R/W | LED n の色（RGB888）。**線上の並びは B, G, R, 未使用の 4 バイト**。n は 0〜8 なので `0x20`〜`0x42` |
| `0x50` + n | 1 | R/W | LED n の色（RGB233）。bit7-6 = R、bit5-3 = G、bit2-0 = B。`0x50`〜`0x58` |
| `0x60` + n | 1 | R | スイッチ n の状態を 1 バイトで。値は 0 か 1。`0x60`〜`0x67` |
| `0x70` + n×4 | 4 | R/W | スイッチ n が **off** のときのシステム定義色（RGB888、B,G,R,未使用）。n は 0〜7 |
| `0x90` + n×4 | 4 | R/W | スイッチ n が **on** のときのシステム定義色（RGB888、B,G,R,未使用）。n は 0〜7 |
| `0xF0` | 1 | W | `1` を書くと現在の設定を内部 flash に保存する |
| `0xF1` | 1 | R/W | IRQ 出力の有効化。`0`=無効、`1`=有効。**反映には再起動が要る** |
| `0xFE` | 1 | R | ファームウェアのバージョン |
| `0xFF` | 1 | R/W | I2C アドレス（1〜127） |

**バイト順**: ライブラリは色を `uint32_t` のまま 4 バイト書き込みに渡している。
対象が ESP32 系のリトルエンディアンホストなので、コード上の `0x00RRGGBB` は線には
**B, G, R, 0x00** の順で出る。PDF の表も `LED0_RGB - B`, `LED0_RGB - G`, `LED0_RGB - R` と
並べていて一致する。**キャプチャを読むときに `0xRRGGBB` として読んではいけない。**

**インデックス付きブロック**: `0x10` `0x20` `0x50` `0x60` `0x70` `0x90` の 6 本は
配列で、ベースにインデックス（`0x20` 系は ×4）を足してアクセスする。`profiles/` には
先頭 1 要素だけを記録し、刻み幅を注記してある。

**転送の形**: 書き込みは「ポインタバイト + ペイロード」を 1 トランザクションで送る。
読み出しはポインタバイトを書いて **repeated START**（`endTransmission(false)`）を打ち、
続けて必要な長さを要求する。

## 典型的な使い方

1. `0xFE` を読んでファームウェアのバージョンを確認する（疎通確認も兼ねる）。
2. 状態を追うだけなら `0x00` を一定周期で読み、ビットを見る。個別に扱いたければ
   `0x60`+n を読む。どちらも「今の状態」なので、変化はホスト側で前回値と比べる。
3. 色をホストから直接決めるなら、まず `0x19` に `0`（ユーザ定義）を書く。そのうえで
   `0x20`+n×4 に 4 バイト（B, G, R, 任意）を書くか、`0x50`+n に RGB233 を 1 バイト書く。
4. ホストを介さずスイッチに追従させたいなら、`0x19` に `1`（システム定義）を書き、
   `0x70`+n×4 に off のときの色、`0x90`+n×4 に on のときの色を登録する。以後は
   ファームウェアが勝手に切り替える。
5. 電源を切っても残したい設定なら、最後に `0xF0` に `1` を書く。
6. 2 台以上を同じバスに並べるなら、片方の `0xFF` を書き換えてから繋ぐ。

## 利用上の注意

- **`0x19` を先に決めないと色が反映されない。** `0x20` / `0x50` はユーザ定義モードでのみ
  LED を駆動し、`0x70` / `0x90` はシステム定義モードでのみ駆動する。モードが逆だと
  「書いたのに光らない」になる。順番は「モードを決めてから色を書く」。
- **設定は `0xF0` を書くまで揮発する。** flash 書き込みなので、ループの中で毎回叩く
  ものではない。
- **ライブラリの既知の非対称 その 1 — 明るさは書けるが読み戻せない。**
  `setLEDBrightness(num, v)` は `0x10 + num` に書く（これは PDF の表と合っていて正しい）。
  ところが `getLEDBrightness()` は引数を取らず、**常に `0x10` を読む**。したがって
  LED 1〜8 に個別の明るさを設定しても、読み戻せるのは LED 0 の値だけである。
  「設定したのに読み値が変わらない」はデバイスの不具合ではない。
- **`setLEDBrightness()` はインデックスを制限していない。** `0x10` の並びは LED 0〜8 で
  `0x18` まで、**その次の `0x19` は LED の表示モードレジスタ**である。うっかり
  `num = 9` を渡すと、明るさのつもりで表示モードを上書きすることになる。付属サンプルが
  `for (i = 0; i <= 8; i++)` と書いているのはそのためで、この上限は自分で守る必要がある。
- **ライブラリの既知の非対称 その 2 — `getRGB233()` は 1 バイトのレジスタから 4 バイト
  読む。** `setRGB233()` は `0x50 + num` に 1 バイト書くのに、`getRGB233()` は同じ
  アドレスから `uint32_t` に 4 バイト要求する。バスには 4 バイト流れるが、意味があるのは
  先頭の 1 バイトだけで、残り 3 バイトは隣の LED の RGB233 値（index が大きければ
  `0x58` より先の未定義領域）である。キャプチャで 4 バイト転送を見ても、それは
  デバイスがそう定義しているからではない。
- **ユーザ定義色のインデックスは clamp されない。** `setRGB888` / `setRGB233` は
  index を制限しないので、9 個目より先のアドレスを叩ける。一方
  `setSwitchOffRGB888` / `setSwitchOnRGB888` / `getSwitchStatus(num)` は 7 で clamp される。
- **`0xF1`（IRQ）は書いても即座には効かない。** PDF に「need to restart」と明記がある。
  また IRQ を有効にすると、状態変化で PA13 が Low になり、**`0x00` を読むと High に
  戻る**という設計になっている。Grove からは使えないので、実質はポーリング前提と
  考えておくのが安全。
- **アドレス変更は即時反映。** `0xFF` に書いた次のトランザクションはもう新アドレス宛に
  出さないと応答しない。書き換えた値を忘れると `0x46` を探しても見つからないユニットが
  手元に残る。復旧は scan で全アドレスを舐めることになる。
- **`begin()` の既定 I2C 速度は 4000000（4MHz）になっている。** ヘッダの
  デフォルト引数がそうなっているだけで、付属サンプルは全部 400000 を明示的に渡している。
  デフォルトのまま呼ばないほうがよい。
- **本リポジトリの probe は read-only。** `scenarios/m5stack-mcu-u191/p0.yaml` は
  読めるレジスタを舐めるだけで、`0xF0`（flash 保存）を含む書き込みは `deferred` に
  置いてある。**書き込み側の挙動は実測で裏付けられていない。**

## どこに何が書いてあるか

| 知りたいこと | 参照先 |
|---|---|
| レジスタ表（ベンダー公式） | [Byte Switch Protocol PDF](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/820/Byte_Switch_protocol.pdf)（2024-06-19、firmware V1 向け。1 行 16 バイトの表） |
| 製品仕様・寸法・購入 | [M5Stack 製品ページ](https://docs.m5stack.com/en/unit/Unit%20ByteSwitch) |
| レジスタ定数の定義 | [`collection/vendor/M5Unit-ByteButton/src/unit_byte.hpp`](https://github.com/tanakamasayuki/I2CDeviceDB/blob/38f1d82/collection/vendor/M5Unit-ByteButton/src/unit_byte.hpp) |
| 転送長・アクセス方向・バイト順・上記 2 件の非対称 | [`collection/vendor/M5Unit-ByteButton/src/unit_byte.cpp`](https://github.com/tanakamasayuki/I2CDeviceDB/blob/38f1d82/collection/vendor/M5Unit-ByteButton/src/unit_byte.cpp) |
| U191 と U192 の差がアドレスだけであること | 同ライブラリの `examples/unit-byteSwitch/` と `examples/unit_byteButton/` |
| ライブラリの取得元と commit 固定 | [`collection/vendor_libraries.toml`](https://github.com/tanakamasayuki/I2CDeviceDB/blob/38f1d82/collection/vendor_libraries.toml) の `["M5Unit-ByteButton"]` |
| 機械可読なレジスタマップ | [`profiles/m5stack-mcu-u191.yaml`](https://github.com/tanakamasayuki/I2CDeviceDB/blob/38f1d82/profiles/m5stack-mcu-u191.yaml) |
| ユニットの構成・搭載 IC | [`chips/m5stack-mcu-u191.yaml`](../../chips/m5stack-mcu-u191.yaml) |
| 製品（SKU・ベンダー URL） | [`products/m5stack-u191.yaml`](../../products/m5stack-u191.yaml) |
| 測定計画と安全性の宣言 | [`scenarios/m5stack-mcu-u191/p0.yaml`](https://github.com/tanakamasayuki/I2CDeviceDB/blob/38f1d82/scenarios/m5stack-mcu-u191/p0.yaml) |
| read-only probe のソース | [`collection/sketches/m5stack-mcu-u191__characterize/`](https://github.com/tanakamasayuki/I2CDeviceDB/tree/38f1d82/collection/sketches/m5stack-mcu-u191__characterize/) |
| 兄弟ユニット（U192） | [`guides/chips/m5stack-mcu-u192.md`](m5stack-mcu-u192.md) |
| 実測データ | [`observations/`](https://github.com/tanakamasayuki/I2CDeviceDB/tree/38f1d82/observations/) — 本ユニットのディレクトリはまだ無い |

## 出典と確度

- レジスタのアドレスと意味、RGB888 のバイト順（B, G, R, 未使用）、RGB233 のビット割り、
  IRQ が PA13 に出ること: ベンダーの Protocol PDF に記載がある。
- 転送長・アクセス方向、および上記 2 件のライブラリの非対称: `M5Unit-ByteButton`
  ライブラリのソース（`collection/vendor/M5Unit-ByteButton` に commit `05d0629` で固定）
  から起こした**推定**。`profiles/` の evidence は `inferred` / `tentative`。
- U191 と U192 がアドレス以外同一であること: ライブラリに分岐が無いことと、付属サンプル
  2 本のアドレスの差から読んだもの。**ファームウェアの実装が本当に同一かは未確認。**
- **未確認**: 実機のキャプチャがまだ無い。レジスタが本当に表のとおり応答するか、
  未使用アドレス（`0x59`〜`0x5F` など）がどう振る舞うか、400kHz で動くかは確かめて
  いない。IRQ を有効にしたときの PA13 の実挙動も見ていない。スイッチの状態が
  `1` = on なのか `0` = on なのかも、PDF は「0〜1」としか書いておらず**未確認**。
- **未調査**: 書き込み側の挙動全般（色、明るさ、モード、flash 保存、アドレス変更）は
  probe が read-only のため触れていない。
