# knowledge/

チップごとの振る舞いの表（`knowledge/<chip>.yaml`）。カタログ（`chips/`）の項目は繰り返さない。
形の正本は `docs/DATA_MODEL.ja.md` §5。最初の 1 件は QMP6988（EmbedBench の X68 で模型と照合済み）。

- 値ごとに根拠（`evidence`）を書く: `runs/` の run の名前、`i2cdevicedb-pilot:<observation id>`、または `datasheet`。
- 時間は値と窓を分ける。`value_us` は模型が使う値（式でもよい）、`windows` は記録が許す範囲。
- `tools/knowledge_table.py <chip>` が、レジスタの表を EmbedBench の `RegTableSpec` に書き出す（模型の雛形）。

I2CDeviceDB（commit 38f1d82）の `profiles/<chip>.yaml` は、ここへ移すまでの参照として残る。
