# EmbedBenchDeviceData

[English](README.md)

[EmbedBench](https://github.com/tanakamasayuki/EmbedBench) のデバイスの模型を作り・確かめるための、**実デバイスの振る舞いのデータ**。

EmbedBench はライブラリやアプリをホストで、デバイスの模型を相手に試す。その模型の正しさは、実物の記録で決まる。
このリポジトリは、その記録（run）と、記録を読むためのカタログ・ケース・振る舞いの表を持つ。

- **記録は試験ではなく、試験の前の準備。** 実物を一度動かして線を記録し、解析して、ホストの模型（とその照合の試験）の材料にする。
- **記録を取るのは OEP の probe**（キャプチャと、同じ時計のケースの目印）、**解析は wireskein**。
  EmbedBench は `runs/` を読むだけ（`skein2trace.py` → 表の雛形・tape・照合）。
- 形式はバスに依存しない一般的なもの（wireskein の run と分析）で、ほかの用途に使うことは妨げない。

## 構成

```text
chips/<chip>.yaml          カタログ。チップが何者で、どのバスのどこで答えるか
products/<product>.yaml    部品表。製品にどのチップがどのバスのどこに載るか（ベンダーのライン全体）
libraries/<library>.yaml   ライブラリと、それが叩くチップ
surveys/                   ベンダーのラインをどう列挙したか
guides/chips/<chip>.md     人が読むデバイスの解説（日本語）
cases/<case>/              収集のケース: case.yaml（意図・対象・段階）と、OEP の LibraryTest のソース
runs/<run-id>/             1 回の記録: run.json、キャプチャ、復号、provenance.yaml
knowledge/<chip>.yaml      振る舞いの表（レジスタ・コマンド・時間）。根拠の run を引く
tools/                     validate、run の名前、索引の build
docs/DATA_MODEL.ja.md      各ファイルの形と規則（正本）
```

## 使い方

```sh
python3 tools/validate.py            # 形と参照の検査。編集のたびに
python3 tools/run_id.py runs/<dir>   # run のディレクトリが持つべき名前
python3 tools/build.py               # 逆引きの索引を build/ に（コミットしない）
```

必要なもの: Python 3.9 以上と PyYAML。

## 出自

`chips/`・`products/`・`libraries/`・`surveys/`・`guides/` は
[I2CDeviceDB](https://github.com/tanakamasayuki/I2CDeviceDB)（commit 38f1d82）から移した（履歴は引き継いでいない）。
チップのアドレスはバスごとの `interfaces` に、製品の部品は `bus` 付きに組み直した。
I2CDeviceDB の `profiles/`・`observations/` は移していない。ガイドからの参照は、その commit への固定のリンクにしてある。
`profiles/` の表は `knowledge/` へ作り直す。

## ライセンス

[MIT](LICENSE)
