#!/usr/bin/env python3
"""
清理可数性训练污染数据（不影响拼写训练）。
"""

from __future__ import annotations

import argparse
import datetime as dt
import shutil
import sqlite3
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser(description="清理可数性污染数据")
    parser.add_argument("--db", default="vibespeller.db", help="SQLite 路径")
    parser.add_argument("--no-backup", action="store_true", help="不创建备份")
    parser.add_argument("--keep-labels", action="store_true", help="保留 words.countability_* 字段内容")
    args = parser.parse_args()

    db_path = Path(args.db).resolve()
    if not db_path.exists():
        raise SystemExit(f"db not found: {db_path}")

    if not args.no_backup:
        ts = dt.datetime.now().strftime("%Y%m%d_%H%M%S")
        backup = db_path.with_name(f"{db_path.stem}.backup_countability_{ts}{db_path.suffix}")
        shutil.copy2(db_path, backup)
        print(f"[backup] {backup}")

    conn = sqlite3.connect(str(db_path))
    cur = conn.cursor()

    # 1) 清理可数性训练进度（不会动 spelling）。
    cur.execute("DELETE FROM training_progress WHERE training_type = 'countability'")
    deleted_progress = cur.rowcount

    # 2) 清理可数性会话断点。
    cur.execute("DELETE FROM session_progress WHERE mode IN ('countability_learning', 'countability_review')")
    deleted_session = cur.rowcount

    # 3) 可选：清理已经抓过的可数性标签字段。
    cur.execute("PRAGMA table_info(words)")
    columns = {r[1] for r in cur.fetchall()}
    deleted_labels = 0
    if not args.keep_labels and {"countability_label", "countability_source", "countability_updated_at"}.issubset(columns):
        cur.execute(
            """
            UPDATE words
            SET countability_label = NULL,
                countability_source = NULL,
                countability_updated_at = NULL
            """
        )
        deleted_labels = cur.rowcount

    conn.commit()
    conn.close()

    print(
        f"[done] deleted_progress={deleted_progress}, "
        f"deleted_session={deleted_session}, reset_labels_rows={deleted_labels}"
    )


if __name__ == "__main__":
    main()

