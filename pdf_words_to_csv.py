#!/usr/bin/env python3
"""Convert word list PDF (word + Chinese meaning) into CSV."""

from __future__ import annotations

import argparse
import csv
import re
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path
from typing import Dict, List, Tuple

from pypdf import PdfReader

WORD_LINE_RE = re.compile(r"^(\d+)\s+(.+)$")
MEANING_START_RE = re.compile(r"^(\d+)(?:\s+(.*))?$")
FOOTER_MARKERS = (
    "近日已学",
    "共 ",
    "扫码听单词",
    "纸上默写",
)
NOISE_LINES = {
    "Word Meaning",
}
NOISE_KEYWORDS = (
    "雅思词汇",
    "词根联想记忆法",
    "扫码听单词",
    "纸上默写",
)


def _is_footer(line: str) -> bool:
    return any(line.startswith(marker) for marker in FOOTER_MARKERS)


def _is_noise_line(line: str) -> bool:
    if line in NOISE_LINES:
        return True
    if line.startswith("Word Meaning"):
        return True
    if any(keyword in line for keyword in NOISE_KEYWORDS):
        return True
    return False


def parse_page(page_index: int, text: str) -> List[Tuple[int, str, str]]:
    """Parse one PDF page into [(number, word, meaning), ...]."""
    lines = [line.strip() for line in (text or "").splitlines() if line.strip()]
    header_positions = [i for i, line in enumerate(lines) if line == "Word Meaning"]
    if len(header_positions) < 2:
        return []

    word_start = header_positions[0] + 1
    word_end = header_positions[1]
    meaning_start = header_positions[1] + 1

    words_by_num: Dict[int, str] = {}
    for line in lines[word_start:word_end]:
        m = WORD_LINE_RE.match(line)
        if not m:
            continue
        num = int(m.group(1))
        words_by_num[num] = m.group(2).strip()
    expected_nums = set(words_by_num.keys())

    meanings_by_num: Dict[int, str] = {}
    current_num: int | None = None
    current_parts: List[str] = []

    def flush_current() -> None:
        nonlocal current_num, current_parts
        if current_num is None:
            return
        value = " ".join(part.strip() for part in current_parts if part.strip()).strip()
        meanings_by_num[current_num] = value
        current_num = None
        current_parts = []

    for line in lines[meaning_start:]:
        if _is_footer(line):
            break
        if _is_noise_line(line):
            continue

        m = MEANING_START_RE.match(line)
        if m:
            next_num = int(m.group(1))
            # Ignore numeric lines that don't belong to the current table.
            if next_num not in expected_nums:
                if expected_nums and expected_nums.issubset(meanings_by_num.keys()):
                    break
                continue
            flush_current()
            current_num = next_num
            rest = (m.group(2) or "").strip()
            if rest:
                current_parts.append(rest)
            continue

        if current_num is not None:
            current_parts.append(line)

    flush_current()

    entries: List[Tuple[int, str, str]] = []
    for num in sorted(words_by_num):
        word = words_by_num[num]
        meaning = meanings_by_num.get(num, "")
        entries.append((num, word, meaning))

    return entries


def extract_entries(pdf_path: Path, workers: int) -> List[Tuple[int, str, str]]:
    reader = PdfReader(str(pdf_path))
    total_pages = len(reader.pages)

    results_by_page: Dict[int, List[Tuple[int, str, str]]] = {}
    parsed_completed = 0
    with ThreadPoolExecutor(max_workers=workers) as executor:
        future_to_page = {}
        for page_idx, page in enumerate(reader.pages, start=1):
            text = page.extract_text() or ""
            future = executor.submit(parse_page, page_idx, text)
            future_to_page[future] = page_idx
            print(
                f"Progress: extracted {page_idx}/{total_pages} pages",
                flush=True,
            )

        for fut in as_completed(future_to_page):
            page_num = future_to_page[fut]
            results_by_page[page_num] = fut.result()
            parsed_completed += 1
            print(
                f"Progress: parsed {parsed_completed}/{total_pages} pages",
                flush=True,
            )

    all_entries: List[Tuple[int, str, str]] = []
    for page_num in sorted(results_by_page):
        all_entries.extend(results_by_page[page_num])

    # Keep the original global order by number.
    all_entries.sort(key=lambda x: x[0])
    return all_entries


def write_csv(entries: List[Tuple[int, str, str]], output_path: Path) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    with output_path.open("w", newline="", encoding="utf-8-sig") as f:
        writer = csv.writer(f)
        writer.writerow(["number", "word", "meaning"])
        writer.writerows(entries)


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Convert a vocabulary PDF to CSV (supports multithreaded parsing)."
    )
    parser.add_argument(
        "pdf",
        type=Path,
        nargs="?",
        default=Path("/Users/yanshuxiang/Downloads/words.pdf"),
        help="Input PDF path",
    )
    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        default=Path("words.csv"),
        help="Output CSV path",
    )
    parser.add_argument(
        "-w",
        "--workers",
        type=int,
        default=4,
        help="Thread worker count for page parsing",
    )
    args = parser.parse_args()

    if args.workers < 1:
        raise ValueError("workers must be >= 1")
    if not args.pdf.exists():
        raise FileNotFoundError(f"PDF not found: {args.pdf}")

    entries = extract_entries(args.pdf, args.workers)
    write_csv(entries, args.output)

    print(f"Done. Parsed {len(entries)} words -> {args.output}")


if __name__ == "__main__":
    main()
