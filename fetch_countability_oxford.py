#!/usr/bin/env python3
"""
抓取 Oxford Learner's Dictionaries 的可数性标记，并写回 vibespeller.db。

说明：
- 仅处理能明确判断为 C/U/B 的单词。
- 不依赖主程序，可独立运行。
- 默认单线程、带请求间隔，尽量降低被限流风险。
"""

from __future__ import annotations

import argparse
import datetime as dt
import html
import re
import sqlite3
import time
import urllib.parse
import urllib.request
from dataclasses import dataclass
from typing import Optional, Tuple


USER_AGENT = (
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 14_0) "
    "AppleWebKit/537.36 (KHTML, like Gecko) "
    "Chrome/124.0 Safari/537.36"
)


@dataclass
class FetchResult:
    label: str  # C / U / B / NA
    source_url: str
    detail: str = ""


def ensure_columns(conn: sqlite3.Connection) -> None:
    cur = conn.cursor()
    cur.execute("PRAGMA table_info(words)")
    columns = {row[1] for row in cur.fetchall()}
    if "countability_label" not in columns:
        cur.execute("ALTER TABLE words ADD COLUMN countability_label TEXT")
    if "countability_source" not in columns:
        cur.execute("ALTER TABLE words ADD COLUMN countability_source TEXT")
    if "countability_updated_at" not in columns:
        cur.execute("ALTER TABLE words ADD COLUMN countability_updated_at TEXT")
    conn.commit()


def http_get(url: str, timeout: int = 20) -> str:
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        charset = resp.headers.get_content_charset() or "utf-8"
        return resp.read().decode(charset, errors="replace")


def normalize_html_text(raw: str) -> str:
    text = html.unescape(raw).lower()
    text = re.sub(r"<script\b[^>]*>.*?</script>", " ", text, flags=re.S)
    text = re.sub(r"<style\b[^>]*>.*?</style>", " ", text, flags=re.S)
    text = re.sub(r"<[^>]+>", " ", text)
    text = re.sub(r"\s+", " ", text).strip()
    return text


def detect_countability_from_html(html_text: str) -> str:
    text = normalize_html_text(html_text)

    has_ucn_cn = (
        "countable and uncountable" in text
        or "[c, u]" in text
        or "[u, c]" in text
        or "countable, uncountable" in text
        or "uncountable, countable" in text
    )
    if has_ucn_cn:
        return "B"

    has_uncountable = bool(re.search(r"\buncountable\b|\b\[u\]\b", text))
    # 防止 uncountable 被 countable 子串误伤
    text_wo_un = text.replace("uncountable", " ")
    has_countable = bool(re.search(r"\bcountable\b|\b\[c\]\b", text_wo_un))

    if has_uncountable and has_countable:
        return "B"
    if has_uncountable:
        return "U"
    if has_countable:
        return "C"
    return "NA"


def first_oxford_definition_link(search_html: str) -> Optional[str]:
    # 匹配 /definition/english/xxxx 形式链接
    m = re.search(
        r'href="(/definition/english/[^"#?]+)"',
        search_html,
        flags=re.I,
    )
    if not m:
        return None
    return urllib.parse.urljoin("https://www.oxfordlearnersdictionaries.com", m.group(1))


def fetch_countability(word: str, timeout: int) -> FetchResult:
    encoded = urllib.parse.quote(word.strip())
    direct_url = f"https://www.oxfordlearnersdictionaries.com/definition/english/{encoded}"
    try:
        html_text = http_get(direct_url, timeout=timeout)
        label = detect_countability_from_html(html_text)
        if label != "NA":
            return FetchResult(label=label, source_url=direct_url, detail="direct")
    except Exception:
        pass

    search_url = f"https://www.oxfordlearnersdictionaries.com/search/english/?q={encoded}"
    try:
        search_html = http_get(search_url, timeout=timeout)
        first_link = first_oxford_definition_link(search_html)
        if first_link:
            detail_html = http_get(first_link, timeout=timeout)
            label = detect_countability_from_html(detail_html)
            return FetchResult(label=label, source_url=first_link, detail="search")
        return FetchResult(label="NA", source_url=search_url, detail="no_definition_link")
    except Exception as e:
        return FetchResult(label="NA", source_url=search_url, detail=f"error:{e}")


def iter_words(conn: sqlite3.Connection, only_missing: bool) -> list[Tuple[int, str]]:
    cur = conn.cursor()
    if only_missing:
        cur.execute(
            """
            SELECT id, word
            FROM words
            WHERE word IS NOT NULL
              AND trim(word) <> ''
              AND (countability_label IS NULL OR trim(countability_label) = '' OR countability_label = 'NA')
            ORDER BY id
            """
        )
    else:
        cur.execute(
            """
            SELECT id, word
            FROM words
            WHERE word IS NOT NULL
              AND trim(word) <> ''
            ORDER BY id
            """
        )
    return [(int(r[0]), str(r[1])) for r in cur.fetchall()]


def main() -> None:
    parser = argparse.ArgumentParser(description="抓取 Oxford 可数性并写回数据库")
    parser.add_argument("--db", default="vibespeller.db", help="SQLite 路径")
    parser.add_argument("--delay", type=float, default=0.9, help="请求间隔秒")
    parser.add_argument("--timeout", type=int, default=20, help="单次请求超时秒")
    parser.add_argument("--limit", type=int, default=0, help="仅处理前 N 个，0=全部")
    parser.add_argument("--only-missing", action="store_true", help="只处理未标注可数性的单词")
    args = parser.parse_args()

    conn = sqlite3.connect(args.db)
    conn.execute("PRAGMA journal_mode=WAL")
    ensure_columns(conn)

    words = iter_words(conn, only_missing=args.only_missing)
    if args.limit > 0:
        words = words[: args.limit]

    total = len(words)
    print(f"[countability] total={total}")
    if total == 0:
        return

    updated = 0
    na_count = 0
    now = dt.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    for idx, (word_id, word) in enumerate(words, 1):
        result = fetch_countability(word, timeout=args.timeout)
        if result.label in {"C", "U", "B"}:
            conn.execute(
                """
                UPDATE words
                SET countability_label = ?,
                    countability_source = ?,
                    countability_updated_at = ?
                WHERE id = ?
                """,
                (result.label, f"oxford:{result.source_url}", now, word_id),
            )
            updated += 1
        else:
            na_count += 1
            conn.execute(
                """
                UPDATE words
                SET countability_label = 'NA',
                    countability_source = ?,
                    countability_updated_at = ?
                WHERE id = ?
                """,
                (f"oxford:{result.source_url}", now, word_id),
            )

        if idx % 20 == 0:
            conn.commit()
        print(f"[{idx}/{total}] {word} -> {result.label} ({result.detail})")
        if args.delay > 0:
            time.sleep(args.delay)

    conn.commit()
    conn.close()
    print(f"[countability] done, updated={updated}, na={na_count}, total={total}")


if __name__ == "__main__":
    main()

