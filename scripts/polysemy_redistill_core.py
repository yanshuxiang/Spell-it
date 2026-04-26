#!/usr/bin/env python3
from __future__ import annotations

import csv
import json
import os
import re
import threading
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from typing import Any

from tqdm import tqdm

try:
    from openai import OpenAI
except Exception:  # noqa: BLE001
    OpenAI = None  # type: ignore[assignment]


MODEL_CONFIGS = {
    "3": {
        "name": "DeepSeek-Chat (句中生义重蒸馏)",
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

DEFAULT_MODEL_CONFIG_KEY = "3"
DEFAULT_BATCH_SIZE = 12
DEFAULT_MAX_WORKERS = 20
DEFAULT_MAX_RETRIES = 3
DEFAULT_MAX_OUTPUT_TOKENS = 1200

ENABLE_MULTI_THREAD = True
FALLBACK_TO_SINGLE_THREAD = True
FORCE_SINGLE_THREAD_ONLY = False

_csv_lock = threading.Lock()
_cjk_re = re.compile(r"[\u4e00-\u9fff]")


def get_sentence_polysemy_prompt() -> str:
    return (
        "You are an IELTS reading lexicography annotator.\n"
        "Task: Keep ONLY words whose provided sentence context uses a non-default, exam-tricky meaning.\n\n"
        "HARD RULES:\n"
        "1) Context-first. If context is common meaning, omit the word.\n"
        "2) No dictionary expansion. Do not list all meanings.\n"
        "3) Each returned meaning must be sentence-specific and concise Chinese.\n"
        "4) Return shortest valid JSON only.\n\n"
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


def _clean_text(v: Any) -> str:
    if v is None:
        return ""
    return str(v).strip()


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
    for r in results:
        w = _clean_text(r.get("word")).lower()
        if w:
            result_map[w] = r

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


def _run_multithread(
    items: list[dict[str, Any]],
    clients: list[Any],
    model_name: str,
    batch_size: int,
    max_retries: int,
    max_output_tokens: int,
    output_csv: str,
    max_workers: int,
) -> None:
    batches = [items[i : i + batch_size] for i in range(0, len(items), batch_size)]
    with tqdm(total=len(items), desc="句中生义重蒸馏[多线程]") as pbar:
        with ThreadPoolExecutor(max_workers=max_workers) as executor:
            futures = []
            for i, batch in enumerate(batches):
                futures.append(
                    executor.submit(
                        process_batch,
                        batch,
                        pbar,
                        clients[i % len(clients)],
                        model_name,
                        max_retries,
                        max_output_tokens,
                        output_csv,
                    )
                )
            for future in as_completed(futures):
                try:
                    future.result()
                except Exception:
                    pass


def _run_singlethread(
    items: list[dict[str, Any]],
    client: Any,
    model_name: str,
    batch_size: int,
    max_retries: int,
    max_output_tokens: int,
    output_csv: str,
) -> None:
    batches = [items[i : i + batch_size] for i in range(0, len(items), batch_size)]
    with tqdm(total=len(items), desc="句中生义重蒸馏[单线程]") as pbar:
        for batch in batches:
            try:
                process_batch(
                    batch=batch,
                    pbar=pbar,
                    client=client,
                    model_name=model_name,
                    max_retries=max_retries,
                    max_output_tokens=max_output_tokens,
                    output_csv=output_csv,
                )
            except Exception:
                pbar.update(len(batch))


def run_redistillation(
    candidates: list[dict[str, Any]],
    output_csv: str,
    *,
    model_config_key: str = DEFAULT_MODEL_CONFIG_KEY,
    batch_size: int = DEFAULT_BATCH_SIZE,
    max_workers: int = DEFAULT_MAX_WORKERS,
    max_retries: int = DEFAULT_MAX_RETRIES,
    max_output_tokens: int = DEFAULT_MAX_OUTPUT_TOKENS,
) -> None:
    if OpenAI is None:
        raise RuntimeError("缺少 openai 依赖，请先安装: pip install openai")
    if model_config_key not in MODEL_CONFIGS:
        raise ValueError(f"未知 MODEL_CONFIG_KEY: {model_config_key}")

    config = MODEL_CONFIGS[model_config_key]
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
        _run_singlethread(
            items=to_process,
            client=clients[0],
            model_name=config["model"],
            batch_size=batch_size,
            max_retries=max_retries,
            max_output_tokens=max_output_tokens,
            output_csv=output_csv,
        )
        return

    try:
        _run_multithread(
            items=to_process,
            clients=clients,
            model_name=config["model"],
            batch_size=batch_size,
            max_retries=max_retries,
            max_output_tokens=max_output_tokens,
            output_csv=output_csv,
            max_workers=max_workers,
        )
    except Exception as e:  # noqa: BLE001
        if not FALLBACK_TO_SINGLE_THREAD:
            raise
        print(f"⚠️ 多线程异常，回退单线程: {e}")
        _run_singlethread(
            items=to_process,
            client=clients[0],
            model_name=config["model"],
            batch_size=batch_size,
            max_retries=max_retries,
            max_output_tokens=max_output_tokens,
            output_csv=output_csv,
        )

