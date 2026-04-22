#!/usr/bin/env python3
"""
从 ieltscat 抓取结果中蒸馏“熟词生义”：
1) 读取刚生成的 manifest JSON（以及同批次 all.txt）；
2) 逐个单词扫描并汇总上下文；
3) 多线程 + 多 API Key 并发调用模型判断是否“熟词生义”；
4) 输出 CSV（含 polysemy_data 字段，风格参考 given_vocabulary_distiller.py）。

默认输入:
  - ../data/ieltscat_reading_20_to_5_manifest.json
  - ../data/ieltscat_reading_20_to_5_all.txt

默认输出:
  - ../data/ieltscat_polysemy_from_readings.csv
"""

from __future__ import annotations

import csv
import json
import os
import re
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


# --- 模型配置（沿用 given_vocabulary_distiller.py 风格） ---
MODEL_CONFIGS = {
    "3": {
        "name": "DeepSeek-Chat (熟词生义蒸馏专用)",
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


# --- 全局固定参数（按你的要求写死，不走命令行） ---
MANIFEST_PATH = "../data/ieltscat_reading_20_to_5_manifest.json"
ALL_TEXT_PATH = "../data/ieltscat_reading_20_to_5_all.txt"
OUTPUT_CSV_PATH = "../data/ieltscat_polysemy_from_readings.csv"

MODEL_CONFIG_KEY = "3"

BATCH_SIZE = 15
MAX_WORKERS = 20
MAX_RETRIES = 3
MAX_CONTEXTS_PER_WORD = 3
MIN_WORD_LEN = 3
MIN_FREQUENCY = 1
TOP_N = 0  # 0 表示全量
MAX_OUTPUT_TOKENS_PER_BATCH = 1200

ENABLE_MULTI_THREAD = True
FALLBACK_TO_SINGLE_THREAD = True  # 多线程主路径异常时，自动降级单线程
FORCE_SINGLE_THREAD_ONLY = False  # 调试开关：True 时只跑单线程

csv_lock = threading.Lock()


# 常见功能词过滤，聚焦“有词义负载”的实词
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


HEADER_RE = re.compile(
    r"^===== (?P<book>.+?) \| (?P<test>.+?) \| (?P<passage>.+?) =====\s*\n"
    r"题目:\s*(?P<title>.*?)\s*\n"
    r"questionId:\s*(?P<qid>\d+)\s*\n"
    r"examId:\s*(?P<eid>\d+)\s*\n",
    flags=re.MULTILINE,
)
TOKEN_RE = re.compile(r"[A-Za-z][A-Za-z'-]*")
SENTENCE_SPLIT_RE = re.compile(r"(?<=[.!?;])\s+")
CJK_RE = re.compile(r"[\u4e00-\u9fff]")


def get_polysemy_prompt() -> str:
    """熟词生义提示词（与参考脚本同风格）"""
    return (
        "You are a World-Class Lexicographer and Elite IELTS/TOEFL Examiner.\n"
        "Your goal: Identify ONLY CONTEXT-TRUE secondary meanings (金蝉脱壳) from the provided passage contexts.\n\n"
        "ULTRA-STRICT FILTERING RULES:\n"
        "1. THE 1:15 RATIO GUIDELINE: Expect a yield of around 5-7% (approx. 1 out of 15 words). Most words should be omitted from results.\n"
        "2. THE SURPRISE THRESHOLD: A 'Gem' must be a meaning that makes a learner say 'I didn't know the word could mean THAT!'. It must be a radical departure, not just a related nuance.\n"
        "3. NO PART-OF-SPEECH TRAPS: Simply changing POS is almost never a gem unless meaning shifts strongly.\n"
        "4. EXAM FATALITY: Only include meanings that can truly trap test-takers in IELTS/TOEFL/GRE/SAT reading.\n"
        "5. NO FILLER: 'exam_value' must be one sharp sentence about WHY this is a trap.\n"
        "6. AGGRESSIVE NEGATION: When in doubt, omit the word from results.\n\n"
        "IMPORTANT:\n"
        "- You will receive words with corpus contexts from IELTS passages.\n"
        "- CONTEXT FIRST: A word qualifies ONLY IF the PROVIDED CONTEXT uses a non-default meaning.\n"
        "- If a word merely 'has polysemy' in general, but context uses the common meaning, omit it.\n"
        "- Return ONLY gem words in results; omitted words are treated as None.\n"
        "- For NON-gem words: DO NOT output any definition, nuance, or explanation text.\n"
        "- Never output English dictionary-style explanations for non-gem words.\n"
        "- Keep output as short as possible.\n\n"
        "JSON SCHEMA:\n"
        "{\n"
        "  'results': [{\n"
        "    'word': 'str',\n"
        "    'polysemy_data': {\n"
        "      'meaning': '极简中文释义',\n"
        "      'value': '考点说明（1句）',\n"
        "      'hit_context_index': '1-based int index from given contexts',\n"
        "      'evidence_quote': 'short exact quote from that context sentence',\n"
        "      'example': 'use the original selected context sentence'\n"
        "    }\n"
        "  }]\n"
        "}"
    )


def load_manifest(path: str) -> list[dict[str, Any]]:
    if not os.path.exists(path):
        raise FileNotFoundError(f"manifest not found: {path}")
    with open(path, "r", encoding="utf-8") as f:
        payload = json.load(f)
    items = payload.get("items", [])
    if not isinstance(items, list):
        raise ValueError("manifest items is not a list")
    return [x for x in items if isinstance(x, dict)]


def parse_labeled_text_blocks(path: str) -> dict[str, dict[str, Any]]:
    """
    返回:
      questionId -> {
        bookLabel, testName, passageName, topicTitle, questionId, examId, text
      }
    """
    if not os.path.exists(path):
        raise FileNotFoundError(f"all-text not found: {path}")

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


def collect_word_candidates(
    passages: list[dict[str, Any]],
    min_word_len: int,
    min_frequency: int,
    max_contexts: int,
) -> list[dict[str, Any]]:
    word_stats: dict[str, dict[str, Any]] = defaultdict(
        lambda: {"frequency": 0, "contexts": [], "sources": set()}
    )

    for p in passages:
        source = f"{p['bookLabel']}|{p['testName']}|{p['passageName']}|{p['questionId']}"
        for sent in sentence_split(p.get("text", "")):
            found = TOKEN_RE.findall(sent)
            for raw in found:
                w = normalize_token(raw)
                if len(w) < min_word_len:
                    continue
                if w in STOPWORDS:
                    continue
                if any(ch.isdigit() for ch in w):
                    continue
                stat = word_stats[w]
                stat["frequency"] += 1
                if len(stat["contexts"]) < max_contexts and sent not in stat["contexts"]:
                    stat["contexts"].append(sent)
                if len(stat["sources"]) < 8:
                    stat["sources"].add(source)

    rows: list[dict[str, Any]] = []
    for w, stat in word_stats.items():
        if stat["frequency"] < min_frequency:
            continue
        rows.append(
            {
                "word": w,
                "frequency": stat["frequency"],
                "source_count": len(stat["sources"]),
                "sources": sorted(stat["sources"]),
                "contexts": stat["contexts"],
            }
        )

    rows.sort(key=lambda x: (-int(x["frequency"]), x["word"]))
    return rows


def call_api(
    prompt: str,
    content: str,
    client: Any,
    model_name: str,
    max_retries: int,
    temp: float = 0.1,
) -> list[dict[str, Any]]:
    for attempt in range(max_retries):
        try:
            response = client.chat.completions.create(
                model=model_name,
                messages=[
                    {"role": "system", "content": prompt},
                    {"role": "user", "content": content},
                ],
                response_format={"type": "json_object"},
                temperature=temp,
                max_tokens=MAX_OUTPUT_TOKENS_PER_BATCH,
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


def read_processed_words(output_csv: str) -> set[str]:
    done: set[str] = set()
    if not os.path.exists(output_csv):
        return done
    try:
        with open(output_csv, "r", encoding="utf-8-sig", newline="") as f:
            reader = csv.DictReader(f)
            for row in reader:
                w = str(row.get("word", "")).strip().lower()
                if w:
                    done.add(w)
    except Exception:  # noqa: BLE001
        return done
    return done


def _contains_cjk(text: str) -> bool:
    return bool(CJK_RE.search(text))


def _clean_text(value: Any) -> str:
    if value is None:
        return ""
    return str(value).strip()


def _normalize_for_match(text: str) -> str:
    return re.sub(r"\s+", " ", text).strip().lower()


def _coerce_positive_int(value: Any) -> int | None:
    if isinstance(value, bool):
        return None
    if isinstance(value, int):
        return value if value > 0 else None
    s = str(value).strip()
    if not s:
        return None
    if not s.isdigit():
        return None
    n = int(s)
    return n if n > 0 else None


def _extract_strict_polysemy_data(result: dict[str, Any], contexts: list[str]) -> dict[str, str] | None:
    """
    严格模式：
    - 仅在“明确是 gem 且在当前文章上下文成立”时返回数据；
    - 非 gem 或不确定场景一律返回 None；
    - 过滤掉英文释义型噪音结果，避免把非熟词生义写入 CSV。
    """
    has_gem = result.get("has_gem")
    if has_gem is False:
        return None

    pdata = result.get("polysemy_data")
    if not isinstance(pdata, dict):
        # 兼容旧 schema：仅当 has_gem 明确为 True 时才尝试读取旧字段
        if has_gem is True:
            pdata = {
                "meaning": result.get("gem_meaning"),
                "value": result.get("exam_value"),
                "example": result.get("example"),
            }
        else:
            return None

    meaning = _clean_text(pdata.get("meaning"))
    value = _clean_text(pdata.get("value"))
    example = _clean_text(pdata.get("example"))

    # 严格门槛：meaning/value 至少要有，且 meaning 必须包含中文，避免英文词典释义污染
    if not meaning or not value:
        return None
    if not _contains_cjk(meaning):
        return None

    # 上下文强校验：必须能定位到给定上下文中的命中句，并给出可匹配证据短语
    hit_idx = _coerce_positive_int(pdata.get("hit_context_index"))
    evidence_quote = _clean_text(pdata.get("evidence_quote"))
    if hit_idx is None or hit_idx > len(contexts):
        return None
    if not evidence_quote:
        return None

    hit_context = _clean_text(contexts[hit_idx - 1])
    if not hit_context:
        return None

    if _normalize_for_match(evidence_quote) not in _normalize_for_match(hit_context):
        return None

    if not example:
        # 优先使用原文命中句，避免额外生成长句
        example = hit_context

    out = {
        "meaning": meaning,
        "value": value,
        "example": example,
    }
    return out


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
    with csv_lock:
        exists = os.path.exists(output_csv)
        with open(output_csv, "a", encoding="utf-8-sig", newline="") as f:
            writer = csv.DictWriter(f, fieldnames=fieldnames)
            if not exists:
                writer.writeheader()
            for row in rows:
                writer.writerow(row)


def process_batch(
    batch: list[dict[str, Any]],
    pbar: Any,
    client: Any,
    model_name: str,
    max_retries: int,
    output_csv: str,
) -> None:
    words_payload_lines: list[str] = []
    for item in batch:
        contexts = item.get("contexts", [])
        sources = item.get("sources", [])
        context_lines = []
        for idx, sent in enumerate(contexts[:MAX_CONTEXTS_PER_WORD], start=1):
            context_lines.append(f"[{idx}] {sent}")
        context_text = "\n".join(context_lines)
        source_text = " | ".join(sources[:3])
        words_payload_lines.append(
            f"Word: {item['word']}\n"
            f"Frequency: {item['frequency']}\n"
            f"Sources: {source_text}\n"
            f"Contexts:\n{context_text}"
        )
    content = (
        "Distill gems for these words from IELTS reading contexts:\n\n"
        + "\n\n---\n\n".join(words_payload_lines)
        + "\n\nSTRICT OUTPUT RULE: Return ONLY gem words in results. "
          "For non-gem words, OMIT them completely and output nothing about them. "
          "For each kept word, polysemy_data.hit_context_index and evidence_quote are mandatory."
    )

    res_list = call_api(get_polysemy_prompt(), content, client, model_name, max_retries=max_retries)
    result_map: dict[str, dict[str, Any]] = {}
    for r in res_list:
        w = str(r.get("word", "")).strip().lower()
        if w:
            result_map[w] = r

    out_rows: list[dict[str, Any]] = []
    for item in batch:
        w = str(item["word"]).lower()
        r = result_map.get(w, {})
        item_contexts = item.get("contexts", [])
        if not isinstance(item_contexts, list):
            item_contexts = []
        pdata = _extract_strict_polysemy_data(r, item_contexts) if r else None
        has_gem = isinstance(pdata, dict)
        polysemy_data = "None"
        if has_gem:
            polysemy_data = json.dumps(pdata, ensure_ascii=False)

        out_rows.append(
            {
                "word": item["word"],
                "frequency": int(item["frequency"]),
                "source_count": int(item["source_count"]),
                "sources": json.dumps(item.get("sources", []), ensure_ascii=False),
                "contexts": json.dumps(item.get("contexts", []), ensure_ascii=False),
                "has_gem": has_gem,
                "polysemy_data": polysemy_data,
            }
        )

    write_rows(output_csv, out_rows)
    pbar.update(len(batch))


def run_multithread(
    to_process: list[dict[str, Any]],
    clients: list[Any],
    model_name: str,
    output_csv: str,
) -> None:
    batches = [to_process[i : i + BATCH_SIZE] for i in range(0, len(to_process), BATCH_SIZE)]
    with tqdm(total=len(to_process), desc="熟词生义蒸馏中[多线程]") as pbar:
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
                        output_csv,
                    )
                )
            for future in as_completed(futures):
                try:
                    future.result()
                except Exception:
                    # 单个批次失败不影响整体推进
                    pass


def run_singlethread(
    to_process: list[dict[str, Any]],
    client: Any,
    model_name: str,
    output_csv: str,
) -> None:
    batches = [to_process[i : i + BATCH_SIZE] for i in range(0, len(to_process), BATCH_SIZE)]
    with tqdm(total=len(to_process), desc="熟词生义蒸馏中[单线程]") as pbar:
        for batch in batches:
            try:
                process_batch(
                    batch=batch,
                    pbar=pbar,
                    client=client,
                    model_name=model_name,
                    max_retries=MAX_RETRIES,
                    output_csv=output_csv,
                )
            except Exception:
                # 与多线程路径保持一致：单批失败不中断
                pbar.update(len(batch))


def main() -> None:
    print("\n" + "=" * 64)
    print("      IELTS 阅读熟词生义蒸馏器（Manifest/JSON 驱动）")
    print("=" * 64)

    if OpenAI is None:
        print("❌ 缺少 openai 依赖，请先安装: pip install openai")
        return

    config = MODEL_CONFIGS[MODEL_CONFIG_KEY]
    print(f"\n🚀 引擎: {config['name']} | 并行池 Keys: {len(config['api_keys'])}\n")

    try:
        manifest_items = load_manifest(MANIFEST_PATH)
        text_blocks = parse_labeled_text_blocks(ALL_TEXT_PATH)
        passages = build_passages(manifest_items, text_blocks)
    except Exception as e:  # noqa: BLE001
        print(f"❌ 读取输入失败: {e}")
        return

    if not passages:
        print("❌ 没有可用 passage 数据。")
        return

    candidates = collect_word_candidates(
        passages=passages,
        min_word_len=MIN_WORD_LEN,
        min_frequency=MIN_FREQUENCY,
        max_contexts=MAX_CONTEXTS_PER_WORD,
    )

    if TOP_N > 0:
        candidates = candidates[:TOP_N]

    if not candidates:
        print("❌ 没有可处理单词（可能被筛选条件过滤）。")
        return

    processed_words = read_processed_words(OUTPUT_CSV_PATH)
    to_process = [x for x in candidates if x["word"].lower() not in processed_words]

    print(f"📚 Passage 数: {len(passages)}")
    print(f"🧠 候选词数: {len(candidates)}")
    print(f"🔄 已处理词数: {len(processed_words)}")
    print(f"🆕 本轮待处理: {len(to_process)}")

    if not to_process:
        print("🎉 所有候选词已处理完毕。")
        return

    clients = [OpenAI(api_key=k, base_url=config["base_url"]) for k in config["api_keys"]]

    if FORCE_SINGLE_THREAD_ONLY or not ENABLE_MULTI_THREAD:
        run_singlethread(
            to_process=to_process,
            client=clients[0],
            model_name=config["model"],
            output_csv=OUTPUT_CSV_PATH,
        )
    else:
        try:
            run_multithread(
                to_process=to_process,
                clients=clients,
                model_name=config["model"],
                output_csv=OUTPUT_CSV_PATH,
            )
        except Exception as e:  # noqa: BLE001
            if not FALLBACK_TO_SINGLE_THREAD:
                raise
            print(f"⚠️ 多线程主路径异常，回退单线程: {e}")
            run_singlethread(
                to_process=to_process,
                client=clients[0],
                model_name=config["model"],
                output_csv=OUTPUT_CSV_PATH,
            )

    print(f"\n✅ 蒸馏完成，CSV 已输出到: {OUTPUT_CSV_PATH}")


if __name__ == "__main__":
    main()
