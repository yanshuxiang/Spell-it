#!/usr/bin/env python3
"""
自动重蒸馏入口：
1) 若检测到“已蒸馏 CSV”（含 polysemy_data 列），则仅保留 polysemy_data != None 的词重蒸馏；
2) 否则回退到“纯原文（manifest + all.txt）抽词后重蒸馏”。
"""

from __future__ import annotations

import ast
import csv
import json
import os
import re
import sys
from collections import defaultdict
from typing import Any

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

from polysemy_redistill_core import run_redistillation  # noqa: E402


PROJECT_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, ".."))
DISTILLED_CSV_PATH = os.path.join(PROJECT_ROOT, "default_books", "polysemy", "雅思熟词生义.csv")
MANIFEST_PATH = os.path.join(PROJECT_ROOT, "data", "ieltscat_reading_20_to_5_manifest.json")
ALL_TEXT_PATH = os.path.join(PROJECT_ROOT, "data", "ieltscat_reading_20_to_5_all.txt")
OUTPUT_CSV_PATH = os.path.join(PROJECT_ROOT, "data", "ieltscat_polysemy_redistilled_auto.csv")

MODEL_CONFIG_KEY = "3"
BATCH_SIZE = 12
MAX_WORKERS = 20
MAX_RETRIES = 3
MAX_OUTPUT_TOKENS = 1200

TOP_N = 0  # 0 = 全量
MAX_CONTEXTS_PER_WORD = 6
MIN_WORD_LEN = 3
MIN_FREQUENCY = 1

HEADER_RE = re.compile(
    r"^===== (?P<book>.+?) \| (?P<test>.+?) \| (?P<passage>.+?) =====\s*\n"
    r"题目:\s*(?P<title>.*?)\s*\n"
    r"questionId:\s*(?P<qid>\d+)\s*\n"
    r"examId:\s*(?P<eid>\d+)\s*\n",
    flags=re.MULTILINE,
)
TOKEN_RE = re.compile(r"[A-Za-z][A-Za-z'-]*")
SENTENCE_SPLIT_RE = re.compile(r"(?<=[.!?;])\s+")

STOPWORDS = {
    "a",
    "an",
    "the",
    "and",
    "or",
    "but",
    "if",
    "then",
    "else",
    "when",
    "while",
    "for",
    "to",
    "of",
    "in",
    "on",
    "at",
    "by",
    "from",
    "with",
    "without",
    "as",
    "is",
    "are",
    "was",
    "were",
    "be",
    "been",
    "being",
    "do",
    "does",
    "did",
    "doing",
    "have",
    "has",
    "had",
    "having",
    "can",
    "could",
    "may",
    "might",
    "must",
    "shall",
    "should",
    "will",
    "would",
    "this",
    "that",
    "these",
    "those",
    "it",
    "its",
    "they",
    "them",
    "their",
    "we",
    "our",
    "you",
    "your",
    "he",
    "she",
    "his",
    "her",
    "i",
    "me",
    "my",
    "mine",
    "ours",
    "yours",
    "hers",
    "him",
    "who",
    "whom",
    "whose",
    "which",
    "what",
    "where",
    "why",
    "how",
    "not",
    "no",
    "yes",
    "very",
    "also",
    "just",
    "than",
    "into",
    "onto",
    "about",
    "over",
    "under",
    "up",
    "down",
    "out",
    "off",
    "again",
    "more",
    "most",
    "such",
    "other",
    "another",
    "same",
    "own",
    "both",
    "each",
    "every",
    "any",
    "some",
    "few",
    "many",
    "much",
    "all",
    "per",
    "via",
}


def _clean_text(v: Any) -> str:
    if v is None:
        return ""
    return str(v).strip()


def _to_int(v: Any, default: int = 0) -> int:
    s = _clean_text(v)
    if s.isdigit():
        return int(s)
    return default


def _parse_list_field(v: Any) -> list[str]:
    s = _clean_text(v)
    if not s or s.lower() == "none":
        return []
    for parser in (json.loads, ast.literal_eval):
        try:
            parsed = parser(s)
            if isinstance(parsed, list):
                out = []
                for item in parsed:
                    t = _clean_text(item)
                    if t:
                        out.append(t)
                return out
        except Exception:  # noqa: BLE001
            pass
    return []


def detect_distilled_file(path: str) -> bool:
    if not os.path.exists(path):
        return False
    try:
        with open(path, "r", encoding="utf-8-sig", newline="") as f:
            reader = csv.DictReader(f)
            headers = reader.fieldnames or []
            if "polysemy_data" not in headers:
                return False
            for _ in reader:
                return True
            return False
    except Exception:  # noqa: BLE001
        return False


def load_non_none_words(path: str) -> set[str]:
    keep: set[str] = set()
    with open(path, "r", encoding="utf-8-sig", newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            word = _clean_text(row.get("word")).lower()
            pdata = _clean_text(row.get("polysemy_data"))
            if not word:
                continue
            if pdata and pdata.lower() != "none":
                keep.add(word)
    return keep


def load_candidates_from_existing_csv(path: str) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    with open(path, "r", encoding="utf-8-sig", newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            word = _clean_text(row.get("word")).lower()
            if not word:
                continue
            contexts = _parse_list_field(row.get("contexts"))[:MAX_CONTEXTS_PER_WORD]
            if not contexts:
                continue
            sources = _parse_list_field(row.get("sources"))

            context_sources: list[str] = []
            if len(sources) >= len(contexts):
                context_sources = sources[: len(contexts)]
            elif len(sources) == 1:
                context_sources = [sources[0]] * len(contexts)
            elif 1 < len(sources) < len(contexts):
                context_sources = sources + [sources[-1]] * (len(contexts) - len(sources))
            else:
                context_sources = [""] * len(contexts)

            frequency = _to_int(row.get("frequency"), 1)
            source_count = _to_int(row.get("source_count"), 0)
            if source_count <= 0:
                source_count = len(set([s for s in sources if s]))

            rows.append(
                {
                    "word": word,
                    "frequency": frequency,
                    "source_count": source_count,
                    "sources": sources,
                    "contexts": contexts,
                    "context_sources": context_sources,
                }
            )

    rows.sort(key=lambda x: (-int(x["frequency"]), x["word"]))
    return rows


def parse_labeled_text_blocks(path: str) -> dict[str, dict[str, Any]]:
    text = open(path, "r", encoding="utf-8").read()
    matches = list(HEADER_RE.finditer(text))
    blocks: dict[str, dict[str, Any]] = {}
    for i, m in enumerate(matches):
        start = m.end()
        end = matches[i + 1].start() if i + 1 < len(matches) else len(text)
        body = text[start:end].strip()
        qid = m.group("qid")
        blocks[qid] = {
            "bookLabel": m.group("book").strip(),
            "testName": m.group("test").strip(),
            "passageName": m.group("passage").strip(),
            "topicTitle": m.group("title").strip(),
            "questionId": qid,
            "examId": m.group("eid").strip(),
            "text": body,
        }
    return blocks


def load_manifest(path: str) -> list[dict[str, Any]]:
    with open(path, "r", encoding="utf-8") as f:
        payload = json.load(f)
    items = payload.get("items", [])
    if not isinstance(items, list):
        return []
    return [x for x in items if isinstance(x, dict)]


def build_passages(manifest_items: list[dict[str, Any]], text_blocks: dict[str, dict[str, Any]]) -> list[dict[str, Any]]:
    passages: list[dict[str, Any]] = []
    for item in manifest_items:
        qid = str(item.get("questionId", "")).strip()
        if not qid:
            continue
        block = text_blocks.get(qid)
        if not block:
            continue
        passages.append(
            {
                "bookLabel": str(item.get("bookLabel", block.get("bookLabel", ""))),
                "testName": str(item.get("testName", block.get("testName", ""))),
                "passageName": str(item.get("passageName", block.get("passageName", ""))),
                "topicTitle": str(item.get("topicTitle", block.get("topicTitle", ""))),
                "questionId": qid,
                "examId": str(item.get("examId", block.get("examId", ""))),
                "text": block.get("text", ""),
            }
        )
    return passages


def sentence_split(text: str) -> list[str]:
    raw = SENTENCE_SPLIT_RE.split(text.replace("\n", " ").strip())
    out = []
    for s in raw:
        s = re.sub(r"\s+", " ", s).strip()
        if len(s) >= 20:
            out.append(s)
    return out


def normalize_token(tok: str) -> str:
    tok = tok.lower().strip("-'")
    if not tok:
        return ""
    if tok.endswith("'s"):
        tok = tok[:-2]
    return tok


def collect_candidates_from_raw(passages: list[dict[str, Any]]) -> list[dict[str, Any]]:
    stats: dict[str, dict[str, Any]] = defaultdict(
        lambda: {"frequency": 0, "contexts": [], "context_sources": [], "sources": set()}
    )

    for p in passages:
        source = f"{p['bookLabel']}|{p['testName']}|{p['passageName']}|{p['questionId']}"
        seen_sentence_for_word: dict[str, set[str]] = defaultdict(set)
        for sent in sentence_split(p.get("text", "")):
            found = TOKEN_RE.findall(sent)
            for raw in found:
                w = normalize_token(raw)
                if len(w) < MIN_WORD_LEN:
                    continue
                if w in STOPWORDS:
                    continue
                if any(ch.isdigit() for ch in w):
                    continue
                st = stats[w]
                st["frequency"] += 1
                st["sources"].add(source)
                if sent in seen_sentence_for_word[w]:
                    continue
                seen_sentence_for_word[w].add(sent)
                if len(st["contexts"]) < MAX_CONTEXTS_PER_WORD:
                    st["contexts"].append(sent)
                    st["context_sources"].append(source)

    rows: list[dict[str, Any]] = []
    for word, st in stats.items():
        freq = int(st["frequency"])
        if freq < MIN_FREQUENCY:
            continue
        contexts = list(st["contexts"])
        if not contexts:
            continue
        context_sources = list(st["context_sources"])
        sources = sorted(st["sources"])
        rows.append(
            {
                "word": word,
                "frequency": freq,
                "source_count": len(sources),
                "sources": sources,
                "contexts": contexts,
                "context_sources": context_sources,
            }
        )
    rows.sort(key=lambda x: (-int(x["frequency"]), x["word"]))
    return rows


def build_candidates_auto() -> tuple[list[dict[str, Any]], str]:
    if detect_distilled_file(DISTILLED_CSV_PATH):
        all_candidates = load_candidates_from_existing_csv(DISTILLED_CSV_PATH)
        keep_words = load_non_none_words(DISTILLED_CSV_PATH)
        candidates = [x for x in all_candidates if _clean_text(x.get("word")).lower() in keep_words]
        return candidates, "distilled_csv_non_none"

    manifest_items = load_manifest(MANIFEST_PATH)
    text_blocks = parse_labeled_text_blocks(ALL_TEXT_PATH)
    passages = build_passages(manifest_items, text_blocks)
    candidates = collect_candidates_from_raw(passages)
    return candidates, "raw_articles"


def main() -> None:
    print("\n" + "=" * 64)
    print("   IELTS 熟词生义重蒸馏器（自动模式）")
    print("=" * 64)

    candidates, mode = build_candidates_auto()
    if TOP_N > 0:
        candidates = candidates[:TOP_N]
    if not candidates:
        print("❌ 没有候选词可重蒸馏。")
        return

    if mode == "distilled_csv_non_none":
        print("📌 检测到已蒸馏 CSV：仅重蒸馏 polysemy_data != None 的词")
        print(f"📥 输入: {DISTILLED_CSV_PATH}")
    else:
        print("📌 未检测到可用已蒸馏 CSV：回退为纯原文抽词重蒸馏")
        print(f"📥 输入: {MANIFEST_PATH} + {ALL_TEXT_PATH}")

    print(f"📤 输出: {OUTPUT_CSV_PATH}")
    run_redistillation(
        candidates=candidates,
        output_csv=OUTPUT_CSV_PATH,
        model_config_key=MODEL_CONFIG_KEY,
        batch_size=BATCH_SIZE,
        max_workers=MAX_WORKERS,
        max_retries=MAX_RETRIES,
        max_output_tokens=MAX_OUTPUT_TOKENS,
    )
    print(f"\n✅ 完成: {OUTPUT_CSV_PATH}")


if __name__ == "__main__":
    main()
