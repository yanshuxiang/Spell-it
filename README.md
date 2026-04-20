# Spell it

Spell it 是一个面向拼写训练的极简桌面工具。  
它参考记忆曲线做复习调度，把重点放在“会拼、能默写”，适合备考场景下的高频、低负担训练。

## 总览

<p align="center">
  <img src="https://raw.githubusercontent.com/yanshuxiang/Spell-it/main/assets/screenshots/%E9%A6%96%E9%A1%B5.png" alt="首页" width="24%" />
  <img src="https://raw.githubusercontent.com/yanshuxiang/Spell-it/main/assets/screenshots/%E8%AF%8D%E6%95%B0%E9%A1%B5.png" alt="词书页" width="24%" />
  <img src="https://raw.githubusercontent.com/yanshuxiang/Spell-it/main/assets/screenshots/%E7%BB%9F%E8%AE%A1%E9%A1%B5.png" alt="统计页" width="24%" />
  <img src="https://raw.githubusercontent.com/yanshuxiang/Spell-it/main/assets/screenshots/%E6%8B%BC%E5%86%99%E9%A1%B5.png" alt="拼写页" width="24%" />
</p>

- 技术栈：`C++17 + Qt Widgets + QtSql + SQLite`
- 数据库位置：项目根目录 `vibespeller.db`
- UI 风格：白底极简、竖屏 16:9 视觉比例、代码构建界面（无 `.ui` 文件）

## 更新

- 新增词书管理页：支持多词书、当前词书高亮、切换学习词书、删除词书（二次确认）。
- CSV 导入增强：支持表头映射（单词/释义/音标），并修复多行字段、引号字段等解析问题。
- 首页计数逻辑优化：
  - 学习显示当前词书剩余未学总词数；
  - 复习显示今日应复习总词数；
  - 每次实际进入练习仍按 20 词一组加载。
- 拼写流程支持断点续练：退出后保存当前组进度，下次可继续本组 20 词。
- 统计页升级：
  - 上半区显示学习/复习词量；
  - 下半区显示学习时长折线（分钟）。
- 学习时长统计更准确：
  - 仅在学习/复习页面计时；
  - 窗口失焦不计时；
  - 超过 2 分钟无输入自动停表，并扣除等待时段。

## 详细功能

### 1. 学习与复习调度

- 复习阶梯：`1, 2, 4, 7, 15, 30` 天。
- 学习模式：从当前词书抽取未学习词，按 20 词一组练习。
- 复习模式：抽取 `next_review` 到期（按日期）词条，按 20 词一组练习。

### 2. 拼写判定规则

按 Enter 提交后判定：

- `熟悉`：拼写完全正确。
- `模糊`：Levenshtein 距离在 `1~3`。
- `不熟悉`：距离大于 `3`，或点击“跳过”。

### 3. 练习流程

- 顶部显示模式和组内进度（如 `1/20`）。
- 结束后进入小结页，展示正确率、熟悉/模糊/不熟悉统计与本组词条安排。
- 支持“返回首页”或“继续下一组”。

### 4. 词书与导入

- 启动或手动添加时可导入 CSV。
- 读取 CSV 首行后，用户通过下拉框映射字段：
  - 单词列（必选）
  - 释义列（必选）
  - 音标列（可选）
- 词书名默认取文件名（自动裁剪规则已处理）。

### 5. 统计与数据记录

- 每日记录学习词数、复习词数、学习时长。
- 学习统计页支持按最近 7 天可视化查看。
- 本地 SQLite 存储全部学习状态、调度时间、会话进度与统计数据。

### 6. 四六级翻译词群数据（新增种子）

- 词群数据目录：`data/phrase_clusters/`
- 结构定义：`data/phrase_clusters/cet_phrase_cluster_schema.json`
- 种子样例：`data/phrase_clusters/cet_phrase_cluster_seed.json`
- 快速校验：
  - `python3 scripts/validate_phrase_cluster_json.py data/phrase_clusters/cet_phrase_cluster_seed.json`
- 从 `docx` 自动提取翻译题中文原文（批量）：
  - `python3 scripts/extract_translation_from_docx.py --input <你的docx目录或文件> --output data/phrase_clusters/cet_translation_corpus.jsonl`
- 从资源目录（`doc/docx/pdf`）自动提取翻译原文（推荐）：
  - `python3 scripts/extract_translation_from_resources.py --input assets/resources --output-dir data/phrase_clusters`
  - 输出文件：
    - `data/phrase_clusters/cet_translation_extracted_raw.jsonl`
    - `data/phrase_clusters/cet_translation_extracted_high_confidence.jsonl`
    - `data/phrase_clusters/cet_translation_extracted_needs_review.jsonl`
    - `data/phrase_clusters/cet_translation_extracted_summary.json`

---
