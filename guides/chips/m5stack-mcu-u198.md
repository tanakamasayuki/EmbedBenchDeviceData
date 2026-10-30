# Unit Step16 (U198) — 16 ポジション ロータリスイッチ入力ユニット

> つまみを回すと 0〜F の位置が決まる 16 接点のロータリスイッチを、7 セグ表示と RGB LED
> ごと I2C のレジスタにしたユニット。ホストから見えるのはスイッチではなく、
> STM32 のファームウェアである。
>
> **確度**: M5Stack が `Unit-Step16_Protocol` という PDF を公開しており、レジスタ表は
> そこと `M5Unit-Step16` ライブラリのソースの両方に存在する。本ガイドは両方を突き合わせて
> 書いた（両者が食い違う箇所は後述する）。ただし `profiles/m5stack-mcu-u198.yaml` の
> evidence は `inferred` / `tentative` のままで、**実測では未確認**。

## これは何か

**インクリメンタルなロータリエンコーダではない。絶対位置のロータリスイッチである。**
16 接点の 8421 BCD ロータリスイッチが載っていて、つまみの止まっている位置がそのまま
`0x00`〜`0x0F` の値として読める。電源投入直後から「今どこを向いているか」が分かる。
差分でも累積でもない。

つまみの脇に 7 セグメント表示器が 1 桁あり、ファームウェアがその位置を 0〜F の 16 進で
表示する。さらに WS2812 の RGB LED が 1 個載っていて、色と明るさを指定できる。全体を
STM32G031G8U6 に載った M5Stack ファームウェアが束ねている。

できること:

- つまみの現在位置（0〜F）を 1 バイトで読める。
- 7 セグ表示の明るさ（0〜100）と、消灯までの時間を設定できる。**一定時間何も変化が
  なければ勝手に消え、次に動かすと復帰する**という省電力の挙動を持つ。
- 回転方向の意味を反転できる（時計回りで増える／反時計回りで増える）。
- RGB LED の on/off、明るさ（0〜100）、色（R, G, B 各 0〜255）を設定できる。
- 設定を内部 flash に保存できる。I2C アドレスも変更できる（0x08〜0x77）。

できないこと・持っていないもの:

- **押し込みスイッチが無い。** 回すだけである。
- **回転の向きや速度、回した回数は返らない。** 返るのは絶対位置だけなので、
  「右に回した」を知りたければホスト側で前回値と比べる。16 進で 0 と F が隣り合って
  いるため、**F → 0 の折り返しを自分で扱う必要がある**。
- 7 セグに任意の文字を出すことはできない。表示するのはつまみの位置だけで、表示内容の
  レジスタは無い（設定できるのは明るさと消灯時間だけ）。
- 割り込み線は無い。**変化を知るにはポーリングするしかない。** 製品ページの
  更新周期は 100 ms。
- ブザーは載っていない。ライブラリの `simple_buzzer` サンプルは**ホスト側の**ブザーを
  鳴らすもので、ユニットの機能ではない。

## 早見表

| 項目 | 値 |
|---|---|
| I2C アドレス | `0x48`（既定）。設定範囲は `0x08`〜`0x77` |
| アドレス変更 | レジスタ `0xFF` に書き込み。**flash に書くので約 50ms かかり、その後は新アドレス宛にする** |
| 電源 | DC 5V（Grove の赤）。待機 5.85mA、動作 14.76〜29.91mA |
| インタフェース | Grove HY2.0-4P |
| I2C 速度 | 未実測（ライブラリは `Wire.begin()` の既定のまま速度を設定しない） |
| 中身 | STM32G031G8U6（Cortex-M0+ 64MHz）+ 16 接点 8421 BCD ロータリスイッチ + 7 セグ 1 桁 + WS2812 × 1 |
| 割り込み | 無し（ポーリング）。更新周期 100 ms |
| バイト順 | **複数バイトの整数は 1 つも無い。** 色の `0x50` は R, G, B の 3 バイト配列 |
| 寸法・重量 | 32.0 × 24.0 × 16.9 mm / 5.5 g |

## レジスタ

| アドレス | 長さ | R/W | 意味 |
|---|---|---|---|
| `0x00` | 1 | R | つまみの現在位置。`0x00`〜`0x0F` |
| `0x10` | 1 | R/W | 7 セグ表示の動作。`0x00`=常時消灯、`0x01`〜=無変化が続いたら消灯するまでの秒数、最大値=常時点灯（**最大値がいくつかは PDF とライブラリで食い違う。後述**） |
| `0x20` | 1 | R/W | 7 セグ表示の明るさ。0〜100（既定 50）。ライブラリは 100 を超える値を clamp する |
| `0x30` | 1 | R/W | 回転方向。`0`=反時計回りで増える、`1`=時計回りで増える（既定 1） |
| `0x40` | 1 | R/W | RGB LED の電源。`0`=off、`1`=on（既定 1） |
| `0x41` | 1 | R/W | RGB LED の明るさ。0〜100（既定 50）。ライブラリは 100 を超える値を clamp する |
| `0x50` | 3 | R/W | RGB LED の色。**R, G, B の順の 3 バイト**（各 0〜255、既定は全部 0）。`0x50` `0x51` `0x52` に 1 バイトずつ個別にもアクセスできる |
| `0xF0` | 1 | W | 設定を flash に保存。**`1` で 7 セグ側、`2` で RGB 側。**約 50ms かかる |
| `0xFE` | 1 | R | ファームウェアのバージョン |
| `0xFF` | 1 | R/W | I2C アドレス（`0x08`〜`0x77`）。flash に書くので約 50ms かかる |

**バイト順**: このマップには**複数バイトの整数が 1 つも無い**ので、エンディアンを
気にする箇所は無い。`0x50` は 3 バイトの配列で、並びは R, G, B。ヘッダは `0x50` `0x51`
`0x52` を個別の R / G / B レジスタとしても定義していて、`setRValue()` `setGValue()`
`setBValue()` が 1 バイトずつ書く。3 バイト形式と 1 バイト形式は同じ場所を指している。

**転送の形（このユニットだけ他と違う）**: `UnitStep16::readRegister` は
ポインタバイトを書いたあと、そのトランザクションを **STOP で終える**
（`endTransmission()` を既定の `true` のまま呼ぶ）。そのうえで別途 `requestFrom` を
発行する。したがってキャプチャには**読み出し 1 回につき 2 つのトランザクション**が
現れる。`M5Unit-Scroll` / `M5Unit-Roller` / `M5Unit-ByteButton` が出す repeated START
とは形が違うので、比べるときに戸惑わないこと。

## 典型的な使い方

1. `0xFE` を読んでファームウェアのバージョンを確認する（疎通確認も兼ねる）。
2. つまみの位置は `0x00` を 1 バイト読むだけ。100 ms 程度の周期で読み、前回値と違ったら
   「回された」と判断する。0 と F の折り返しはホスト側で処理する。
3. 見た目を整えるなら `0x20`（7 セグの明るさ）、`0x10`（消灯までの秒数）、`0x40` `0x41`
   `0x50`（RGB の on/off・明るさ・色）を書く。
4. **RGB を有効にした直後は約 50ms 待つ。** PDF に「after enabling RGB, wait
   approximately 50ms for voltage stabilization before refreshing it」とある。
5. 電源を切っても残したいなら `0xF0` に書く。**7 セグ側と RGB 側は別々なので 2 回書く**
   （`1` と `2`）。ライブラリの `setDefaultConfig()` も実際にそうしている。

## 利用上の注意

- **`0xF0` は 1 回では終わらない。** `1` を書くと保存されるのは 7 セグ関係
  （`0x10` の表示設定、`0x20` の明るさ、`0x30` の回転方向）、`2` を書くと保存されるのは
  RGB 関係（`0x40` の電源、`0x41` の明るさ）。両方残したければ 2 回書く。片方だけ書いて
  「保存したのに戻る」となるのがいちばんありがちな失敗である。
- **PDF が挙げる保存対象に RGB の「色」が入っていない。** PDF の説明文は `2` で保存
  されるものとして「Step16 RGB power」「Step16 RGB brightness」の 2 つしか挙げておらず、
  `0x50` の色が flash に残るかどうかは書かれていない。**未確認。**
- **`0x10` の「常時点灯」の値が資料によって違う。** ベンダー PDF は
  「Config：0〜0xFE / `0x00`：Always Off / `0xFE`：Always On / `0x00`〜`0xFD`：秒数 /
  default `0xFE`」と書いている。一方ライブラリのヘッダは `LED_CONFIG_ON` を `0xFF`、
  `LED_CONFIG_DEFAULT` を `0xFE` と定義し、`0x01`〜`0xFE` を秒数としている。
  **どちらが正しいかは未確認。** 迷うなら PDF 側（`0xFE` = 常時点灯）に従い、`0xFF` は
  使わないでおくのが安全。
- **flash への書き込みは時間がかかり、寿命がある。** PDF の注記によれば、書き込みには
  先に消去が要り約 20ms かかる（ライブラリは 50ms 待つ）。頻繁に書かないこと。なお
  「書こうとした値が既に flash にある値と同じなら、書き込みそのものを省く」とも
  書かれているので、同じ値を書き直しても寿命は減らない。
- **アドレス変更は flash 経由だが即時反映。** `0xFF` に書くと新しいアドレスが flash に
  入り、**すぐに効く**。約 50ms 待ってから新アドレス宛に話しかける。書き換えた値を
  忘れると `0x48` を探しても見つからないユニットが手元に残る。復旧は scan で全アドレスを
  舐めることになる。
- **`0x30`（回転方向）はホスト側の解釈ではなくデバイス側の値を変える。** これを変えると
  同じ物理位置に対して `0x00` が返す値そのものが変わる。設定を切り替えた直後の値を
  「回された」と誤検出しないよう、切り替え後は基準値を取り直す。
- **絶対位置なので取りこぼしは無いが、素早い操作の途中経過は失う。** 100 ms 周期で
  読んでいる間につまみを 3 つ分回せば、見えるのは 3 つ動いた結果だけである。
- **明るさは 0〜100 であって 0〜255 ではない。** ライブラリは 100 を超える値を 100 に
  clamp するので、255 を渡しても最大にしかならない（エラーにもならない）。
- **本リポジトリの probe は read-only。** `scenarios/m5stack-mcu-u198/p0.yaml` は
  読めるレジスタを舐めるだけで、`0xF0`（flash 保存）を含む書き込みは `deferred` に
  置いてある。**書き込み側の挙動は実測で裏付けられていない。**

## どこに何が書いてあるか

| 知りたいこと | 参照先 |
|---|---|
| レジスタ表（ベンダー公式） | [Unit Step16 Protocol PDF](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1160/Unit-Step16_Protocol.pdf)（2025-07-04、firmware V1 向け。既定値・flash の注意もここ） |
| 製品仕様・寸法・購入 | [M5Stack 製品ページ](https://docs.m5stack.com/en/unit/Unit_Step16) |
| レジスタ定数と既定値の定義 | [`collection/vendor/M5Unit-Step16/src/unit_step16.hpp`](https://github.com/tanakamasayuki/I2CDeviceDB/blob/38f1d82/collection/vendor/M5Unit-Step16/src/unit_step16.hpp) |
| 転送の形（STOP で区切る点）・clamp・待ち時間 | [`collection/vendor/M5Unit-Step16/src/unit_step16.cpp`](https://github.com/tanakamasayuki/I2CDeviceDB/blob/38f1d82/collection/vendor/M5Unit-Step16/src/unit_step16.cpp) |
| ライブラリの取得元と commit 固定 | [`collection/vendor_libraries.toml`](https://github.com/tanakamasayuki/I2CDeviceDB/blob/38f1d82/collection/vendor_libraries.toml) の `["M5Unit-Step16"]` |
| 機械可読なレジスタマップ | [`profiles/m5stack-mcu-u198.yaml`](https://github.com/tanakamasayuki/I2CDeviceDB/blob/38f1d82/profiles/m5stack-mcu-u198.yaml) |
| ユニットの構成・搭載 IC | [`chips/m5stack-mcu-u198.yaml`](../../chips/m5stack-mcu-u198.yaml) |
| 製品（SKU・ベンダー URL） | [`products/m5stack-u198.yaml`](../../products/m5stack-u198.yaml) |
| 測定計画と安全性の宣言 | [`scenarios/m5stack-mcu-u198/p0.yaml`](https://github.com/tanakamasayuki/I2CDeviceDB/blob/38f1d82/scenarios/m5stack-mcu-u198/p0.yaml) |
| read-only probe のソース | [`collection/sketches/m5stack-mcu-u198__characterize/`](https://github.com/tanakamasayuki/I2CDeviceDB/tree/38f1d82/collection/sketches/m5stack-mcu-u198__characterize/) |
| 実測データ | [`observations/`](https://github.com/tanakamasayuki/I2CDeviceDB/tree/38f1d82/observations/) — 本ユニットのディレクトリはまだ無い |

## 出典と確度

- レジスタのアドレスと意味、既定値、`0xF0` の 1 と 2 の使い分け、flash 消去の時間、
  RGB 有効化後の 50ms 待ち: ベンダーの Protocol PDF に記載がある。
- 転送の形（ポインタバイトを STOP で区切ってから `requestFrom` する点）、明るさの
  clamp、`0x50` の 3 バイト形式と 1 バイト形式が同じ場所を指すこと: `M5Unit-Step16`
  ライブラリのソース（`collection/vendor/M5Unit-Step16` に commit `20c0445` で固定）
  から起こした**推定**。`profiles/` の evidence は `inferred` / `tentative`。
- **食い違い（未確認）**: `0x10` の「常時点灯」の値。PDF は `0xFE`、ライブラリは `0xFF`。
  また PDF の保存対象一覧に `0x50`（色）が含まれていない。
- **未確認**: 実機のキャプチャがまだ無い。レジスタが本当に表のとおり応答するか、
  未使用アドレスがどう振る舞うか、100kHz / 400kHz のどちらでも動くかは確かめていない。
- **未調査**: 書き込み側の挙動全般（表示設定、RGB、方向、flash 保存、アドレス変更）は
  probe が read-only のため触れていない。
