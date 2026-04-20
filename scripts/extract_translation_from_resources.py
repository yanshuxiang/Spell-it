#!/usr/bin/env python3
"""
Extract CET translation Chinese source passages from resource files (.doc/.docx/.pdf).

Features:
- Recursively scans a directory.
- Parses .doc/.docx via macOS textutil.
- Parses .pdf via pypdf.
- Extracts translation blocks around markers like "翻译部分" / "Part IV Translation".
- Cleans footer/header noise.
- Splits output into high-confidence and needs-review sets.

Usage:
  python3 scripts/extract_translation_from_resources.py \
    --input assets/resources \
    --output-dir data/phrase_clusters
"""

from __future__ import annotations

import argparse
import os
import json
import re
import subprocess
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

from pypdf import PdfReader


START_RE = re.compile(
    r"(翻译部分|汉译英|中译英|part\s*iv\s*translation|translation\s*part\s*iv)",
    re.IGNORECASE,
)

STOP_RE = re.compile(
    r"(^\s*0?1\s*听力|^\s*02\s*作文|^\s*03\s*阅读|part\s*i\s*writing|listening\s*comprehension|reading\s*comprehension)",
    re.IGNORECASE,
)

NOISE_PATTERNS = [
    r"20\d{2}年\d+月英语六级真题第\d+套",
    r"第\s*\d+\s*页共\s*\d+\s*页",
    r"淘宝店铺[:：]?【[^】]*】",
]


@dataclass
class Record:
    source_file: str
    source_type: str
    exam_label: str
    block_index: int
    text_cn: str
    quality: str
    issues: list[str]


def normalize(s: str) -> str:
    s = s.replace("\u3000", " ").replace("\xa0", " ")
    s = re.sub(r"\s+", " ", s).strip()
    return s


def zh_count(s: str) -> int:
    return len(re.findall(r"[\u4e00-\u9fff]", s))


def read_doc_like(path: Path) -> str:
    cmd = ["/usr/bin/textutil", "-convert", "txt", "-stdout", str(path)]
    proc = subprocess.run(cmd, capture_output=True)
    if proc.returncode != 0:
        return ""
    return proc.stdout.decode("utf-8", "ignore")


def read_pdf(path: Path) -> str:
    reader = PdfReader(str(path))
    parts: list[str] = []
    for i, page in enumerate(reader.pages, start=1):
        text = page.extract_text() or ""
        parts.append(f"\n===PAGE {i}===\n{text}")
    return "\n".join(parts)


def clean_text(text: str) -> str:
    t = text
    for pat in NOISE_PATTERNS:
        t = re.sub(pat, "", t)
    t = normalize(t)
    t = re.sub(r"\s+([，。；：！？）】》])", r"\1", t)
    t = re.sub(r"([（【《])\s+", r"\1", t)
    return t


def score_quality(text: str) -> tuple[str, list[str]]:
    issues: list[str] = []
    zhc = zh_count(text)
    if zhc < 40:
        issues.append("too_short")
    if "因此享有。" in text or "成为 一个。" in text:
        issues.append("likely_truncated")
    if re.search(r"[_□■◆◇]{1,}|�", text):
        issues.append("ocr_noise")
    if re.search(r"[A-Za-z]{8,}", text) and zhc < 120:
        issues.append("too_much_english")
    quality = "high_confidence" if not issues else "needs_review"
    return quality, issues


def extract_blocks(raw: str) -> list[str]:
    lines = raw.splitlines()
    in_translation = False
    blocks: list[str] = []
    cur: list[str] = []

    def flush() -> None:
        nonlocal cur
        if not cur:
            return
        text = clean_text(" ".join(cur))
        if zh_count(text) >= 35:
            blocks.append(text)
        cur = []

    for line in lines:
        l = normalize(line)
        if not l:
            flush()
            continue

        if START_RE.search(l):
            in_translation = True
            flush()
            continue

        if in_translation and STOP_RE.search(l):
            flush()
            in_translation = False
            continue

        if not in_translation:
            continue

        if zh_count(l) >= 3:
            if re.search(r"(答案|解析|参考译文)", l):
                continue
            cur.append(l)
        else:
            flush()

    flush()

    deduped: list[str] = []
    seen: set[str] = set()
    for b in blocks:
        key = re.sub(r"\s+", "", b)
        if key in seen:
            continue
        seen.add(key)
        deduped.append(b)
    return deduped


def collect_files(root: Path) -> list[Path]:
    exts = {".doc", ".docx", ".pdf"}
    return sorted(p for p in root.rglob("*") if p.is_file() and p.suffix.lower() in exts)


def extract_exam_label(path: Path, text: str = "") -> str:
    combined = f"{path.stem} {text}"

    # e.g. 2024.12 ... 第1套
    m = re.search(r"(20\d{2})[.\-年]?\s*(\d{1,2})?\s*月?.*?第\s*([123])\s*套", combined)
    if m:
        year = m.group(1)
        month = m.group(2)
        suite = m.group(3)
        if month:
            return f"{year}.{int(month):02d} 第{suite}套"
        return f"{year} 第{suite}套"

    # e.g. 2023年3月
    m2 = re.search(r"(20\d{2})\s*年\s*(\d{1,2})\s*月", combined)
    if m2:
        return f"{m2.group(1)}.{int(m2.group(2)):02d}"

    # e.g. 2018-2022.06 汇编
    m3 = re.search(r"(20\d{2}\s*[-~]\s*20\d{2})[.\-年]?\s*(\d{1,2})?\s*月?", combined)
    if m3:
        span = re.sub(r"\s+", "", m3.group(1))
        month = m3.group(2)
        if month:
            return f"{span}.{int(month):02d} 汇编"
        return f"{span} 汇编"

    m4 = re.search(r"(20\d{2})", combined)
    if m4:
        return m4.group(1)
    return path.stem


def process_one_file(file_path: Path) -> list[Record]:
    suffix = file_path.suffix.lower().lstrip(".")
    if suffix in {"doc", "docx"}:
        raw = read_doc_like(file_path)
    else:
        raw = read_pdf(file_path)
    if not raw:
        return []

    blocks = extract_blocks(raw)
    rows: list[Record] = []
    for idx, block in enumerate(blocks, start=1):
        quality, issues = score_quality(block)
        rows.append(
            Record(
                source_file=str(file_path),
                source_type=suffix,
                exam_label=extract_exam_label(file_path, block),
                block_index=idx,
                text_cn=block,
                quality=quality,
                issues=issues,
            )
        )
    return rows


def write_jsonl(path: Path, rows: Iterable[Record]) -> None:
    with path.open("w", encoding="utf-8") as f:
        for row in rows:
            f.write(
                json.dumps(
                    {
                        "source_file": row.source_file,
                        "source_type": row.source_type,
                        "exam_label": row.exam_label,
                        "block_index": row.block_index,
                        "quality": row.quality,
                        "issues": row.issues,
                        "text_cn": row.text_cn,
                    },
                    ensure_ascii=False,
                )
                + "\n"
            )


def main() -> int:
    parser = argparse.ArgumentParser(description="Extract CET translation Chinese passages from resources.")
    parser.add_argument("--input", required=True, help="Input directory containing .doc/.docx/.pdf")
    parser.add_argument("--output-dir", required=True, help="Output directory")
    parser.add_argument(
        "--workers",
        type=int,
        default=max(1, min(8, (os.cpu_count() or 4))),
        help="Worker threads for parallel file extraction",
    )
    args = parser.parse_args()

    in_dir = Path(args.input).expanduser().resolve()
    out_dir = Path(args.output_dir).expanduser().resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    if not in_dir.exists() or not in_dir.is_dir():
        print(f"[ERROR] input dir not found: {in_dir}")
        return 1

    files = collect_files(in_dir)
    if not files:
        print(f"[ERROR] no supported files found in: {in_dir}")
        return 1

    all_rows: list[Record] = []
    with ThreadPoolExecutor(max_workers=max(1, args.workers)) as executor:
        futures = [executor.submit(process_one_file, file_path) for file_path in files]
        for future in as_completed(futures):
            all_rows.extend(future.result())

    all_rows.sort(key=lambda r: (r.source_file, r.block_index))

    raw_file = out_dir / "cet_translation_extracted_raw.jsonl"
    high_file = out_dir / "cet_translation_extracted_high_confidence.jsonl"
    review_file = out_dir / "cet_translation_extracted_needs_review.jsonl"
    summary_file = out_dir / "cet_translation_extracted_summary.json"

    write_jsonl(raw_file, all_rows)
    write_jsonl(high_file, [r for r in all_rows if r.quality == "high_confidence"])
    write_jsonl(review_file, [r for r in all_rows if r.quality == "needs_review"])

    summary = {
        "input_dir": str(in_dir),
        "scanned_files": len(files),
        "extracted_blocks": len(all_rows),
        "high_confidence": sum(1 for r in all_rows if r.quality == "high_confidence"),
        "needs_review": sum(1 for r in all_rows if r.quality == "needs_review"),
        "outputs": {
            "raw": str(raw_file),
            "high_confidence": str(high_file),
            "needs_review": str(review_file),
        },
        "workers": max(1, args.workers),
    }
    summary_file.write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding="utf-8")

    print(f"[OK] scanned_files={summary['scanned_files']} extracted_blocks={summary['extracted_blocks']}")
    print(f"[OK] high_confidence={summary['high_confidence']} needs_review={summary['needs_review']}")
    print(f"[OK] raw={raw_file}")
    print(f"[OK] high={high_file}")
    print(f"[OK] review={review_file}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())


"""
python3 /Users/yanshuxiang/programs/Spelling/scripts/extract_translation_from_resources.py \
  --input /Users/yanshuxiang/programs/Spelling/assets/resources \
  --output-dir /Users/yanshuxiang/programs/Spelling/data/phrase_clusters \
  --workers 6
"""