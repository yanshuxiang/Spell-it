# 默认词书目录（初版）

应用启动初始化时会自动扫描本目录，并把 CSV 导入为默认词书。

## 子目录（对应四个功能卡片）
- `spelling`
- `countability`
- `polysemy`
- `phrase_cluster`

## 当前行为（初版）
- 每个子目录下的 `*.csv` 都会尝试导入。
- `spelling/countability/polysemy` 使用自动识别列名（启发式）。
- `phrase_cluster` 支持 `csv/json/jsonl`：
  - `csv` 走词群 CSV 导入逻辑（自动识别中文词群/英文表达列）。
  - `json/jsonl` 走词群 JSON 导入逻辑。
- 同一路径且文件未变化（mtime+size 一致）不会重复导入。

## 说明
后续可升级为“你指定的固定列映射”，避免自动识别误判。
