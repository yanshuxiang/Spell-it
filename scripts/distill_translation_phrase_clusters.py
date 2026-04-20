#!/usr/bin/env python3
"""
Distill CET translation passages into phrase-cluster cards.

Features:
- Reads API config from existing project scripts (no duplicate key maintenance).
- Multi-threaded distillation with multi-key client pool.
- Retry + JSON schema guard.
- Resume from existing candidates jsonl.
- Outputs:
  1) candidate-level jsonl
  2) merged card-level json

Usage:
  python3 scripts/distill_translation_phrase_clusters.py \
    --input data/phrase_clusters/cet_translation_extracted_high_confidence.jsonl \
    --candidates-out data/phrase_clusters/cet_phrase_cluster_candidates.jsonl \
    --cards-out data/phrase_clusters/cet_phrase_cluster_cards.json \
    --workers 8
"""

from __future__ import annotations

import argparse
import ast
import json
import re
import threading
import time
from collections import defaultdict
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass
from pathlib import Path
from typing import Any

DEFAULT_MODEL_NAME = "deepseek-chat"
DEFAULT_BASE_URL = "https://api.deepseek.com"
DEFAULT_CONFIG_KEY = "3"
MAX_RETRIES = 3
DEFAULT_WORKERS = 8


@dataclass
class PassageRecord:
    record_id: str
    source_file: str
    block_index: int
    exam_label: str
    text_cn: str


def extract_model_configs_from_file(path: Path) -> dict[str, Any]:
    if not path.exists():
        return {}
    try:
        tree = ast.parse(path.read_text(encoding="utf-8"))
    except Exception:
        return {}
    for node in tree.body:
        if isinstance(node, ast.Assign):
            for target in node.targets:
                if isinstance(target, ast.Name) and target.id == "MODEL_CONFIGS":
                    try:
                        return ast.literal_eval(node.value)
                    except Exception:
                        return {}
    return {}


def discover_api_config(root: Path) -> tuple[str, str, list[str]]:
    candidates = [
        root / "scripts" / "countability_fetcher.py",
        root / "tools" / "distiller.py",
        root / "scripts" / "distiller.py",
        root / "tools" / "countability_fetcher.py",
    ]
    for file_path in candidates:
        cfg = extract_model_configs_from_file(file_path)
        if not cfg:
            continue
        chosen = cfg.get(DEFAULT_CONFIG_KEY) or next(iter(cfg.values()))
        api_keys = [k for k in chosen.get("api_keys", []) if str(k).strip()]
        if api_keys:
            return (
                chosen.get("base_url", DEFAULT_BASE_URL),
                chosen.get("model", DEFAULT_MODEL_NAME),
                api_keys,
            )
    raise RuntimeError("无法从现有脚本中读取 MODEL_CONFIGS / api_keys。")


def load_input_records(path: Path) -> list[PassageRecord]:
    rows: list[PassageRecord] = []
    with path.open("r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            obj = json.loads(line)
            source_file = str(obj.get("source_file", ""))
            block_index = int(obj.get("block_index", 0))
            text_cn = str(obj.get("text_cn", "")).strip()
            if not text_cn:
                continue
            record_id = f"{source_file}#{block_index}"
            rows.append(
                PassageRecord(
                    record_id=record_id,
                    source_file=source_file,
                    block_index=block_index,
                    exam_label=str(obj.get("exam_label", "")).strip(),
                    text_cn=text_cn,
                )
            )
    return rows


def load_processed_ids(path: Path) -> set[str]:
    if not path.exists():
        return set()
    done: set[str] = set()
    with path.open("r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                obj = json.loads(line)
                rid = str(obj.get("record_id", "")).strip()
                if rid:
                    done.add(rid)
            except Exception:
                continue
    return done


def build_prompt() -> str:
    return (
        "你是英语四六级翻译命题分析助手。"
        "请从给定中文翻译原文中提取“可训练的词群/主题短语”，只要高价值项。\n"
        "要求：\n"
        "1) 必须是中文词群（2~8字优先），不能是完整长句。\n"
        "2) 每个词群给出2~5个高频英文表达（短语或词组）。\n"
        "3) 每个词群给出1个来自原文的中文例句片段（source_sentence）。\n"
        "4) 避免泛词（如'问题'/'发展'）除非在原文里有强语义限定。\n"
        "5) 如果文本里没什么可提取内容，返回空数组。\n\n"
        "严格输出 JSON：\n"
        "{\n"
        '  "clusters":[\n'
        "    {\n"
        '      "cluster_zh":"生态文明",\n'
        '      "keywords_en":["ecological civilization","eco-civilization"],\n'
        '      "source_sentence":"...生态文明建设..."\n'
        "    }\n"
        "  ]\n"
        "}\n"
    )


def call_llm(
    client: Any,
    model_name: str,
    exam_label: str,
    text_cn: str,
) -> list[dict[str, Any]]:
    user_content = (
        f"题源标签: {exam_label or '未知'}\n"
        "中文原文:\n"
        f"{text_cn}\n\n"
        "请直接输出符合格式的 JSON。"
    )
    for attempt in range(MAX_RETRIES):
        try:
            resp = client.chat.completions.create(
                model=model_name,
                messages=[
                    {"role": "system", "content": build_prompt()},
                    {"role": "user", "content": user_content},
                ],
                response_format={"type": "json_object"},
                temperature=0.1,
            )
            data = json.loads(resp.choices[0].message.content)
            clusters = data.get("clusters", [])
            if isinstance(clusters, list):
                return [c for c in clusters if isinstance(c, dict)]
            return []
        except Exception:
            if attempt < MAX_RETRIES - 1:
                time.sleep(0.8 * (attempt + 1))
                continue
            return []
    return []


def clean_cluster_name(name: str) -> str:
    s = str(name).strip()
    s = re.sub(r"[“”\"'`]", "", s)
    s = re.sub(r"\s+", "", s)
    return s


def clean_keywords_en(values: Any) -> list[str]:
    if not isinstance(values, list):
        return []
    out: list[str] = []
    seen: set[str] = set()
    for v in values:
        item = str(v).strip()
        if not item:
            continue
        key = item.lower()
        if key in seen:
            continue
        seen.add(key)
        out.append(item)
    return out[:8]


def write_candidates_append(path: Path, rows: list[dict[str, Any]], lock: threading.Lock) -> None:
    if not rows:
        return
    with lock:
        with path.open("a", encoding="utf-8") as f:
            for row in rows:
                f.write(json.dumps(row, ensure_ascii=False) + "\n")


def process_one(
    rec: PassageRecord,
    client: Any,
    model_name: str,
) -> list[dict[str, Any]]:
    extracted = call_llm(client, model_name, rec.exam_label, rec.text_cn)
    output: list[dict[str, Any]] = []
    for item in extracted:
        cluster_zh = clean_cluster_name(item.get("cluster_zh", ""))
        if not cluster_zh or len(cluster_zh) > 16:
            continue
        keywords_en = clean_keywords_en(item.get("keywords_en", []))
        source_sentence = str(item.get("source_sentence", "")).strip()
        if not source_sentence:
            source_sentence = rec.text_cn[:80]
        output.append(
            {
                "record_id": rec.record_id,
                "source_file": rec.source_file,
                "block_index": rec.block_index,
                "exam_label": rec.exam_label,
                "cluster_zh": cluster_zh,
                "keywords_en": keywords_en,
                "source_sentence": source_sentence,
            }
        )
    return output


def merge_cards(candidates_path: Path) -> list[dict[str, Any]]:
    if not candidates_path.exists():
        return []
    grouped: dict[str, dict[str, Any]] = {}

    with candidates_path.open("r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            obj = json.loads(line)
            name = clean_cluster_name(obj.get("cluster_zh", ""))
            if not name:
                continue
            key = name.lower()
            if key not in grouped:
                grouped[key] = {
                    "cluster_zh": name,
                    "keywords_en": [],
                    "exam_labels": set(),
                    "source_sentences": [],
                    "source_count": 0,
                }
            g = grouped[key]
            g["source_count"] += 1
            for kw in obj.get("keywords_en", []):
                kw_s = str(kw).strip()
                if kw_s and kw_s not in g["keywords_en"]:
                    g["keywords_en"].append(kw_s)
            lbl = str(obj.get("exam_label", "")).strip()
            if lbl:
                g["exam_labels"].add(lbl)
            sent = str(obj.get("source_sentence", "")).strip()
            if sent and sent not in g["source_sentences"]:
                g["source_sentences"].append(sent)

    cards: list[dict[str, Any]] = []
    for _, g in grouped.items():
        cards.append(
            {
                "cluster_zh": g["cluster_zh"],
                "keywords_en": g["keywords_en"][:12],
                "exam_labels": sorted(g["exam_labels"]),
                "source_count": g["source_count"],
                "examples_cn": g["source_sentences"][:3],
            }
        )

    cards.sort(key=lambda x: (-x["source_count"], x["cluster_zh"]))
    return cards


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    default_input = root / "data" / "phrase_clusters" / "cet_translation_extracted_high_confidence.jsonl"
    default_candidates = root / "data" / "phrase_clusters" / "cet_phrase_cluster_candidates.jsonl"
    default_cards = root / "data" / "phrase_clusters" / "cet_phrase_cluster_cards.json"

    parser = argparse.ArgumentParser(description="Distill CET translation passages into phrase clusters.")
    parser.add_argument(
        "--input",
        default=str(default_input),
        help=f"Input JSONL from translation extractor. Default: {default_input}",
    )
    parser.add_argument(
        "--candidates-out",
        default=str(default_candidates),
        help=f"Output candidate JSONL. Default: {default_candidates}",
    )
    parser.add_argument(
        "--cards-out",
        default=str(default_cards),
        help=f"Output merged cards JSON. Default: {default_cards}",
    )
    parser.add_argument(
        "--workers",
        type=int,
        default=DEFAULT_WORKERS,
        help=f"Thread workers for passage tasks (set 1 for single-thread). Default: {DEFAULT_WORKERS}",
    )
    args = parser.parse_args()
    try:
        from openai import OpenAI  # type: ignore
        from tqdm import tqdm  # type: ignore
    except Exception as e:
        print(f"[ERROR] 缺少依赖: {e}")
        print("请先安装: python3 -m pip install openai tqdm")
        return 1

    in_path = Path(args.input)
    cand_path = Path(args.candidates_out)
    cards_path = Path(args.cards_out)
    cand_path.parent.mkdir(parents=True, exist_ok=True)
    cards_path.parent.mkdir(parents=True, exist_ok=True)

    base_url, model_name, api_keys = discover_api_config(root)
    clients = [OpenAI(api_key=k, base_url=base_url) for k in api_keys]
    workers = max(1, args.workers)
    print(f"[INFO] model={model_name} base={base_url} keys={len(api_keys)} workers={workers}")

    all_rows = load_input_records(in_path)
    done_ids = load_processed_ids(cand_path)
    todo = [r for r in all_rows if r.record_id not in done_ids]
    print(f"[INFO] input={len(all_rows)} processed={len(done_ids)} remaining={len(todo)}")
    if not cand_path.exists():
        cand_path.touch()

    io_lock = threading.Lock()

    with tqdm(total=len(todo), desc="Distilling phrase clusters") as pbar:
        with ThreadPoolExecutor(max_workers=workers) as executor:
            futures = []
            for i, rec in enumerate(todo):
                client = clients[i % len(clients)]
                futures.append(executor.submit(process_one, rec, client, model_name))
            for fut in as_completed(futures):
                try:
                    rows = fut.result()
                    write_candidates_append(cand_path, rows, io_lock)
                except Exception:
                    pass
                pbar.update(1)

    cards = merge_cards(cand_path)
    with cards_path.open("w", encoding="utf-8") as f:
        json.dump(
            {
                "meta": {
                    "generated_at": int(time.time()),
                    "input_file": str(in_path),
                    "candidate_file": str(cand_path),
                    "total_cards": len(cards),
                },
                "cards": cards,
            },
            f,
            ensure_ascii=False,
            indent=2,
        )

    print(f"[OK] candidates={cand_path}")
    print(f"[OK] cards={cards_path}")
    print(f"[OK] total_cards={len(cards)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
