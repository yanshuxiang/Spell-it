#!/usr/bin/env python3
"""
IELTS 熟词生义统一蒸馏入口：
1) 从原文（manifest + all.txt）抽词；
2) 使用 redistill 的一遍式严格提示词直接产出最终结果；
3) 不生成中间蒸馏产物。
"""

from __future__ import annotations

import csv
import json
import os
import re
import sys
import threading
import time
from collections import defaultdict
from concurrent.futures import ThreadPoolExecutor, as_completed
from typing import Any

from tqdm import tqdm

try:
    from openai import OpenAI
except Exception:  # noqa: BLE001
    OpenAI = None  # type: ignore[assignment]

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)


PROJECT_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, ".."))
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

MODEL_CONFIGS = {
    "3": {
        "name": "DeepSeek-Chat (句中生义一遍式蒸馏)",
        "api_keys": [
            "sk-dae1bc4b18034ecdbd5365c1348234ad",
            "sk-31b092afea204db4b07f1570e7042fff",
            "sk-9df1e5bc83d64142916751dee3c7b7cd",
            "sk-af0218aef6944fe6ae23e2962074470a",
            "sk-bb44ce68cdcb426da287685fe432f869",
            "sk-c7dab615130548559426796b3984cae6",
            "sk-3a2c0a5540a048ea98a6058421ce6d19",
            "sk-a701d45a2c4a46e1a38ef1b5605815e8",
            "sk-a365304d6a204fe38f969d46ee65d64d",
        ],
        "base_url": "https://api.deepseek.com",
        "model": "deepseek-chat",
    }
}

ENABLE_MULTI_THREAD = True
FALLBACK_TO_SINGLE_THREAD = True
FORCE_SINGLE_THREAD_ONLY = False

_csv_lock = threading.Lock()
_cjk_re = re.compile(r"[\u4e00-\u9fff]")

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


def get_sentence_polysemy_prompt() -> str:
    return (
        "You are a World-Class Lexicographer and Elite IELTS/TOEFL Examiner.\n"
        "Task: From the given IELTS reading sentences, output ONLY words whose PROVIDED CONTEXT uses a "
        "non-default, exam-tricky secondary meaning (熟词生义 / 金蝉脱壳).\n\n"
        "NON-NEGOTIABLE FILTERS:\n"
        "1) Context first. A word qualifies ONLY if one of the numbered sentences actually uses the non-default meaning.\n"
        "2) Surprise threshold. Keep meanings that can genuinely trap a strong learner; skip normal meanings, mild nuances, "
        "transparent metaphor, and routine part-of-speech changes.\n"
        "3) No dictionary expansion. Do not include a word just because it has rare meanings in general.\n"
        "4) Aggressive negation. When uncertain, omit the word completely.\n"
        "5) Evidence required. Every kept sense must cite hit_context_index and an exact evidence_quote copied from that sentence.\n"
        "6) Chinese only for meaning. The meaning must be a short Chinese gloss, not an English definition.\n"
        "7) Keep it sparse. Most input words should be omitted from results.\n\n"
        "BAD KEEP EXAMPLES:\n"
        "- common noun-to-verb changes such as 'book' = reserve, unless the sentence meaning is truly exam-tricky.\n"
        "- broad explanations like 'has multiple meanings' or any note not tied to a numbered sentence.\n\n"
        "Output schema:\n"
        "{\n"
        "  'results': [\n"
        "    {\n"
        "      'word': 'str',\n"
        "      'sense_items': [\n"
        "        {\n"
        "          'hit_context_index': 1,\n"
        "          'meaning': '中文短释义',\n"
        "          'evidence_quote': '短证据原文片段'\n"
        "        }\n"
        "      ]\n"
        "    }\n"
        "  ]\n"
        "}\n"
    )


def _normalize_match(s: str) -> str:
    return re.sub(r"\s+", " ", s).strip().lower()


def _to_positive_int(v: Any) -> int | None:
    if isinstance(v, bool):
        return None
    if isinstance(v, int):
        return v if v > 0 else None
    s = _clean_text(v)
    if not s.isdigit():
        return None
    n = int(s)
    return n if n > 0 else None


def _contains_cjk(s: str) -> bool:
    return bool(_cjk_re.search(s))


def read_processed_words(output_csv: str) -> set[str]:
    done: set[str] = set()
    if not os.path.exists(output_csv):
        return done
    try:
        with open(output_csv, "r", encoding="utf-8-sig", newline="") as f:
            reader = csv.DictReader(f)
            for row in reader:
                w = _clean_text(row.get("word")).lower()
                if w:
                    done.add(w)
    except Exception:  # noqa: BLE001
        return done
    return done


def write_rows(output_csv: str, rows: list[dict[str, Any]]) -> None:
    if not rows:
        return
    fieldnames = [
        "word",
        "frequency",
        "source_count",
        "sources",
        "contexts",
        "has_gem",
        "polysemy_data",
    ]
    with _csv_lock:
        exists = os.path.exists(output_csv)
        with open(output_csv, "a", encoding="utf-8-sig", newline="") as f:
            writer = csv.DictWriter(f, fieldnames=fieldnames)
            if not exists:
                writer.writeheader()
            for row in rows:
                writer.writerow(row)


def call_api(
    client: Any,
    model_name: str,
    content: str,
    max_retries: int,
    max_output_tokens: int,
) -> list[dict[str, Any]]:
    for attempt in range(max_retries):
        try:
            response = client.chat.completions.create(
                model=model_name,
                messages=[
                    {"role": "system", "content": get_sentence_polysemy_prompt()},
                    {"role": "user", "content": content},
                ],
                response_format={"type": "json_object"},
                temperature=0.1,
                max_tokens=max_output_tokens,
            )
            data = json.loads(response.choices[0].message.content)
            results = data.get("results", [])
            if isinstance(results, list):
                return [x for x in results if isinstance(x, dict)]
            return []
        except Exception:  # noqa: BLE001
            if attempt < max_retries - 1:
                time.sleep(1 + attempt)
    return []


def _extract_sense_items(
    result: dict[str, Any],
    contexts: list[str],
    context_sources: list[str],
) -> list[dict[str, str]]:
    if result.get("has_gem") is False:
        return []

    raw_items = result.get("sense_items")
    if not isinstance(raw_items, list):
        raw_items = result.get("senses")
    if not isinstance(raw_items, list):
        raw_poly = result.get("polysemy_data")
        if isinstance(raw_poly, dict):
            raw_items = [raw_poly]
        else:
            raw_items = []

    kept: list[dict[str, str]] = []
    seen_meanings: set[str] = set()
    for raw in raw_items:
        if not isinstance(raw, dict):
            continue
        idx = _to_positive_int(raw.get("hit_context_index"))
        if idx is None or idx > len(contexts):
            continue

        meaning = _clean_text(raw.get("meaning"))
        if not meaning or meaning.lower() == "none":
            continue
        if not _contains_cjk(meaning):
            continue

        norm_meaning = _normalize_match(meaning)
        if norm_meaning in seen_meanings:
            continue

        context_text = _clean_text(contexts[idx - 1])
        if not context_text:
            continue
        evidence_quote = _clean_text(raw.get("evidence_quote"))
        if evidence_quote and _normalize_match(evidence_quote) not in _normalize_match(context_text):
            continue

        source = ""
        if idx - 1 < len(context_sources):
            source = _clean_text(context_sources[idx - 1])

        kept.append(
            {
                "meaning": meaning,
                "example": context_text,
                "source": source,
                "context_index": str(idx),
                "evidence_quote": evidence_quote,
            }
        )
        seen_meanings.add(norm_meaning)
    return kept


def _build_polysemy_json(sense_items: list[dict[str, str]]) -> str:
    first = sense_items[0]
    payload = {
        "meaning": first["meaning"],
        "value": first["meaning"],
        "example": first["example"],
        "sense_items": sense_items,
    }
    return json.dumps(payload, ensure_ascii=False)


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


def build_candidates_from_raw() -> list[dict[str, Any]]:
    manifest_items = load_manifest(MANIFEST_PATH)
    text_blocks = parse_labeled_text_blocks(ALL_TEXT_PATH)
    passages = build_passages(manifest_items, text_blocks)
    return collect_candidates_from_raw(passages)


def process_batch(
    batch: list[dict[str, Any]],
    pbar: Any,
    client: Any,
    model_name: str,
    max_retries: int,
    max_output_tokens: int,
    output_csv: str,
) -> None:
    lines: list[str] = []
    for item in batch:
        ctxs: list[str] = item.get("contexts", [])
        if not isinstance(ctxs, list):
            ctxs = []
        lines.append(
            f"Word: {item['word']}\n"
            "Contexts:\n"
            + "\n".join([f"[{i+1}] {c}" for i, c in enumerate(ctxs)])
        )

    user_content = (
        "Find context-true polysemy from these words.\n"
        "Return ONLY words that are true '熟词生义' in given contexts.\n\n"
        + "\n\n---\n\n".join(lines)
    )
    results = call_api(
        client=client,
        model_name=model_name,
        content=user_content,
        max_retries=max_retries,
        max_output_tokens=max_output_tokens,
    )
    result_map: dict[str, dict[str, Any]] = {}
    for result in results:
        word = _clean_text(result.get("word")).lower()
        if word:
            result_map[word] = result

    out_rows: list[dict[str, Any]] = []
    for item in batch:
        word = _clean_text(item.get("word")).lower()
        contexts: list[str] = item.get("contexts", [])
        sources: list[str] = item.get("sources", [])
        context_sources: list[str] = item.get("context_sources", [])
        if not isinstance(contexts, list):
            contexts = []
        if not isinstance(sources, list):
            sources = []
        if not isinstance(context_sources, list):
            context_sources = []

        result = result_map.get(word, {})
        sense_items = _extract_sense_items(result, contexts, context_sources) if result else []
        has_gem = len(sense_items) > 0
        polysemy_data = _build_polysemy_json(sense_items) if has_gem else "None"

        out_rows.append(
            {
                "word": item["word"],
                "frequency": int(item.get("frequency", 0)),
                "source_count": int(item.get("source_count", 0)),
                "sources": json.dumps(sources, ensure_ascii=False),
                "contexts": json.dumps(contexts, ensure_ascii=False),
                "has_gem": has_gem,
                "polysemy_data": polysemy_data,
            }
        )

    write_rows(output_csv, out_rows)
    pbar.update(len(batch))


def run_multithread(
    items: list[dict[str, Any]],
    clients: list[Any],
    model_name: str,
    output_csv: str,
) -> None:
    batches = [items[i : i + BATCH_SIZE] for i in range(0, len(items), BATCH_SIZE)]
    with tqdm(total=len(items), desc="句中生义一遍式蒸馏[多线程]") as pbar:
        with ThreadPoolExecutor(max_workers=MAX_WORKERS) as executor:
            futures = []
            for i, batch in enumerate(batches):
                futures.append(
                    executor.submit(
                        process_batch,
                        batch,
                        pbar,
                        clients[i % len(clients)],
                        model_name,
                        MAX_RETRIES,
                        MAX_OUTPUT_TOKENS,
                        output_csv,
                    )
                )
            for future in as_completed(futures):
                try:
                    future.result()
                except Exception:
                    pass


def run_singlethread(
    items: list[dict[str, Any]],
    client: Any,
    model_name: str,
    output_csv: str,
) -> None:
    batches = [items[i : i + BATCH_SIZE] for i in range(0, len(items), BATCH_SIZE)]
    with tqdm(total=len(items), desc="句中生义一遍式蒸馏[单线程]") as pbar:
        for batch in batches:
            try:
                process_batch(
                    batch=batch,
                    pbar=pbar,
                    client=client,
                    model_name=model_name,
                    max_retries=MAX_RETRIES,
                    max_output_tokens=MAX_OUTPUT_TOKENS,
                    output_csv=output_csv,
                )
            except Exception:
                pbar.update(len(batch))


def run_distillation(candidates: list[dict[str, Any]], output_csv: str) -> None:
    if OpenAI is None:
        raise RuntimeError("缺少 openai 依赖，请先安装: pip install openai")
    if MODEL_CONFIG_KEY not in MODEL_CONFIGS:
        raise ValueError(f"未知 MODEL_CONFIG_KEY: {MODEL_CONFIG_KEY}")

    config = MODEL_CONFIGS[MODEL_CONFIG_KEY]
    done_words = read_processed_words(output_csv)
    to_process = [x for x in candidates if _clean_text(x.get("word")).lower() not in done_words]

    print(f"🧠 候选词数: {len(candidates)}")
    print(f"🔄 已处理词数: {len(done_words)}")
    print(f"🆕 本轮待处理: {len(to_process)}")
    if not to_process:
        print("🎉 没有需要处理的新词。")
        return

    clients = [OpenAI(api_key=k, base_url=config["base_url"]) for k in config["api_keys"]]
    if FORCE_SINGLE_THREAD_ONLY or not ENABLE_MULTI_THREAD:
        run_singlethread(
            items=to_process,
            client=clients[0],
            model_name=config["model"],
            output_csv=output_csv,
        )
        return

    try:
        run_multithread(
            items=to_process,
            clients=clients,
            model_name=config["model"],
            output_csv=output_csv,
        )
    except Exception as exc:  # noqa: BLE001
        if not FALLBACK_TO_SINGLE_THREAD:
            raise
        print(f"⚠️ 多线程异常，回退单线程: {exc}")
        run_singlethread(
            items=to_process,
            client=clients[0],
            model_name=config["model"],
            output_csv=output_csv,
        )


def main() -> None:
    print("\n" + "=" * 64)
    print("   IELTS 熟词生义一遍式蒸馏器（无中间产物）")
    print("=" * 64)

    candidates = build_candidates_from_raw()
    if TOP_N > 0:
        candidates = candidates[:TOP_N]
    if not candidates:
        print("❌ 没有候选词可蒸馏。")
        return

    print("📌 使用 redistill 严格提示词，一遍输出最终结果")
    print(f"📥 输入: {MANIFEST_PATH} + {ALL_TEXT_PATH}")
    print(f"🧠 原文候选词数: {len(candidates)}")
    print(f"📤 输出: {OUTPUT_CSV_PATH}")
    run_distillation(candidates=candidates, output_csv=OUTPUT_CSV_PATH)
    print(f"\n✅ 完成: {OUTPUT_CSV_PATH}")


if __name__ == "__main__":
    main()
