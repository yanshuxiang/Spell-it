#!/usr/bin/env python3
"""Fetch all IELTS reading passages from Jianya 20 -> 5.

This script:
1) queries labels for Jianya books,
2) requests the reading list for each target book (20..5),
3) creates an examId for each passage questionId,
4) fetches passage payload via /api/question/{examId}/{questionId},
5) extracts article text and merges into one text file.

Usage:
  python3 scripts/fetch_ieltscat_all_readings.py

Optional:
  python3 scripts/fetch_ieltscat_all_readings.py --limit 5 --save-raw
  python3 scripts/fetch_ieltscat_all_readings.py --cookie '...'
"""

from __future__ import annotations

import argparse
import html
import json
import os
import re
import sys
import time
import urllib.parse
import urllib.request
from typing import Any


BASE_URL = "https://ieltscat.xdf.cn"
READ_PAGE_URL = f"{BASE_URL}/practice/read"
USER_AGENT = (
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) "
    "AppleWebKit/537.36 (KHTML, like Gecko) "
    "Chrome/124.0.0.0 Safari/537.36"
)

# Cookie provided by user; can always override via --cookie or IELTSCAT_COOKIE.
DEFAULT_COOKIE = (
    "br-current-appid=9634ef2d15c443978bc68ec2eeffa54e; "
    "gr_user_id=ad8d8cbd-924a-4164-a52f-ae9ab7180fc6; "
    "a452cbd3bb114a68_gr_session_id=bf64faa5-484b-44dc-9779-a55214777914; "
    "br-client=332dcef6-2cbd-43cc-9b54-faf8802bc628; "
    "br-client-id=2a78a207-0c04-4deb-beaa-c6e1c638dd02; "
    "89eb99b12a2e6895_gr_session_id=1e3f75e6-142d-4a34-896c-db166caff4a5; "
    "U2ST=aea7b5df26972d7147b2a30d1f4d8fdf7feee83cebb3400eeb8ee20df99d1c82; "
    "U2AT=b0c2c863-37e3-4a55-90b3-315fc31d3621; "
    "U2UserId=b%2BjbE0xUnjZlg%2F%2FUnf6EnxaWT5TTQ410fZ4bUsRPkEjbzGg0Dz5fBE2q2WXlPKKy; "
    "U2Token=515DCB1969EA5755131F30E81C5B3526_FF4AA53B972332359DEF46023508CB93; "
    "U2User=Hx4p%2F5v%2FhotSAaHVXN91qw%3D%3D; "
    "U2NickName=150****5965; "
    "89eb99b12a2e6895_gr_last_sent_sid_with_cs1=1e3f75e6-142d-4a34-896c-db166caff4a5; "
    "89eb99b12a2e6895_gr_last_sent_cs1=52d9d090-6d12-4aec-b8e1-74b09e6eef7f; "
    "89eb99b12a2e6895_gr_cs1=52d9d090-6d12-4aec-b8e1-74b09e6eef7f; "
    "89eb99b12a2e6895_gr_session_id_sent_vst=1e3f75e6-142d-4a34-896c-db166caff4a5; "
    "userinfo_uc_ielts=23265900%24%24ieltscat.xdf.cn%24%241776766821052%24%240; "
    "token_uc=5a08cc10d553486fbfaedef0c12bdd3b; "
    "a452cbd3bb114a68_gr_last_sent_sid_with_cs1=bf64faa5-484b-44dc-9779-a55214777914; "
    "a452cbd3bb114a68_gr_last_sent_cs1=user_id:23265900; "
    "a452cbd3bb114a68_gr_session_id_sent_vst=bf64faa5-484b-44dc-9779-a55214777914; "
    "JSESSIONID=C75115648FB4F1A5AA4B124F2D325E25; "
    "a452cbd3bb114a68_gr_cs1=user_id:23265900; "
    "br-session-cache-9634ef2d15c443978bc68ec2eeffa54e=[{\"appId\":\"9634ef2d15c443978bc68ec2eeffa54e\",\"sessionID\":\"d335ad7f-e2ab-4bf1-bb21-8c7616730fdd\",\"lastVisitedTime\":1776768149108,\"startTime\":0,\"isRestSID\":false}]"
)

BOOK_NAME_RE = re.compile(r"^剑雅(\d+)$")
HTML_TAG_RE = re.compile(r"<[^>]+>")
SPACE_RE = re.compile(r"\s+")


def with_timestamp(url: str) -> str:
    parts = urllib.parse.urlsplit(url)
    q = dict(urllib.parse.parse_qsl(parts.query, keep_blank_values=True))
    q["_t"] = str(int(time.time() * 1000))
    new_query = urllib.parse.urlencode(q)
    return urllib.parse.urlunsplit((parts.scheme, parts.netloc, parts.path, new_query, parts.fragment))


def get_json(
    url: str,
    cookie: str,
    referer: str = READ_PAGE_URL,
    timeout: float = 20,
    retries: int = 3,
) -> dict[str, Any]:
    last_err: Exception | None = None
    for attempt in range(1, retries + 1):
        try:
            headers = {
                "User-Agent": USER_AGENT,
                "Accept": "application/json, text/plain, */*",
                "Referer": referer,
                "Origin": BASE_URL,
                "FromURL": "ieltscat.xdf.cn",
            }
            if cookie:
                headers["Cookie"] = cookie
            req = urllib.request.Request(with_timestamp(url), headers=headers, method="GET")
            with urllib.request.urlopen(req, timeout=timeout) as resp:
                body = resp.read().decode("utf-8", errors="replace")
            return json.loads(body)
        except Exception as e:  # noqa: BLE001
            last_err = e
            if attempt < retries:
                time.sleep(0.6 * attempt)
            else:
                raise
    if last_err:
        raise last_err
    raise RuntimeError("unreachable")


def clean_text(text: str) -> str:
    text = html.unescape(text)
    text = HTML_TAG_RE.sub(" ", text)
    text = SPACE_RE.sub(" ", text).strip()
    return text


def maybe_parse_json_string(value: str) -> Any:
    s = value.strip()
    if not s:
        return value
    if (s.startswith("{") and s.endswith("}")) or (s.startswith("[") and s.endswith("]")):
        try:
            return json.loads(s)
        except json.JSONDecodeError:
            return value
    return value


def html_to_paragraph_text(raw_html: str) -> str:
    s = html.unescape(raw_html)
    s = re.sub(r"</p\s*>", "\n\n", s, flags=re.IGNORECASE)
    s = re.sub(r"<br\s*/?>", "\n", s, flags=re.IGNORECASE)
    s = HTML_TAG_RE.sub(" ", s)

    lines: list[str] = []
    for line in s.splitlines():
        line = SPACE_RE.sub(" ", line).strip()
        if line:
            lines.append(line)
    return "\n\n".join(lines)


def _extract_from_doc_list(doc_list: Any) -> str:
    if not isinstance(doc_list, list):
        return ""

    passage_blocks: list[str] = []
    for item in doc_list:
        if not isinstance(item, dict):
            continue
        if str(item.get("code", "")) != "105":
            continue
        lst = item.get("list", [])
        if not isinstance(lst, list):
            continue
        for entry in lst:
            if not isinstance(entry, dict):
                continue
            body = entry.get("body", "")
            if not isinstance(body, str):
                continue
            parsed = maybe_parse_json_string(body)
            if isinstance(parsed, dict):
                title_html = parsed.get("title", "")
                if isinstance(title_html, str) and title_html.strip():
                    text = html_to_paragraph_text(title_html)
                    if text:
                        passage_blocks.append(text)

    out: list[str] = []
    seen: set[str] = set()
    for block in passage_blocks:
        if block not in seen:
            seen.add(block)
            out.append(block)
    return "\n\n".join(out).strip()


def walk_collect_strings(node: Any, out: list[str]) -> None:
    if isinstance(node, dict):
        for v in node.values():
            walk_collect_strings(v, out)
        return
    if isinstance(node, list):
        for v in node:
            walk_collect_strings(v, out)
        return
    if isinstance(node, str):
        parsed = maybe_parse_json_string(node)
        if parsed is not node:
            walk_collect_strings(parsed, out)
            return
        t = clean_text(node)
        if len(t) >= 60 and any(ch.isalpha() for ch in t):
            out.append(t)


def extract_article_text(payload: dict[str, Any]) -> str:
    data_node = payload.get("data", payload)
    if isinstance(data_node, dict):
        precise = _extract_from_doc_list(data_node.get("docList"))
        if precise:
            return precise

    if isinstance(data_node, dict) and isinstance(data_node.get("questionList"), list):
        blocks: list[str] = []
        for q in data_node["questionList"]:
            if isinstance(q, dict):
                t = _extract_from_doc_list(q.get("docList"))
                if t:
                    blocks.append(t)
        if blocks:
            merged: list[str] = []
            seen: set[str] = set()
            for b in blocks:
                if b not in seen:
                    seen.add(b)
                    merged.append(b)
            return "\n\n".join(merged).strip()

    candidates: list[str] = []
    walk_collect_strings(data_node, candidates)
    deduped: list[str] = []
    seen: set[str] = set()
    for c in candidates:
        if c not in seen:
            seen.add(c)
            deduped.append(c)
    deduped.sort(key=lambda s: (-(s.count(" ")), -len(s)))
    top = deduped[:200]
    if not top:
        return ""
    top = sorted(top, key=lambda s: -len(s))
    return "\n\n".join(top)


def ensure_dir(path: str) -> None:
    os.makedirs(path, exist_ok=True)


def get_book_keycodes(
    cookie: str,
    start_book: int,
    end_book: int,
    timeout: float,
    retries: int,
) -> list[tuple[int, int]]:
    labels_url = f"{BASE_URL}/api/label/query/all?funCode=7&typeCode=JY&used=1"
    payload = get_json(labels_url, cookie=cookie, timeout=timeout, retries=retries)
    if payload.get("code") != 200 or not isinstance(payload.get("data"), list):
        raise RuntimeError(f"Failed to load labels: {payload}")

    wanted_books = set(
        range(start_book, end_book - 1, -1) if start_book >= end_book else range(start_book, end_book + 1)
    )
    found: dict[int, int] = {}
    for item in payload["data"]:
        if not isinstance(item, dict):
            continue
        name = str(item.get("tagZhName", ""))
        m = BOOK_NAME_RE.match(name)
        if not m:
            continue
        book_no = int(m.group(1))
        if book_no in wanted_books:
            key_code = int(item.get("keyCode"))
            found[book_no] = key_code

    ordered: list[tuple[int, int]] = []
    for b in (
        range(start_book, end_book - 1, -1) if start_book >= end_book else range(start_book, end_book + 1)
    ):
        if b in found:
            ordered.append((b, found[b]))
        else:
            print(f"[WARN] Missing label for 剑雅{b}")
    return ordered


def get_passages_for_book(
    cookie: str,
    book_no: int,
    book_key_code: int,
    timeout: float,
    retries: int,
) -> list[dict[str, Any]]:
    url = f"{BASE_URL}/api/list/login/ielts/2/order?value={book_key_code}"
    payload = get_json(url, cookie=cookie, timeout=timeout, retries=retries)
    if payload.get("code") != 200:
        raise RuntimeError(f"List API failed for keyCode={book_key_code}: {payload}")
    data = payload.get("data")
    if not isinstance(data, list):
        return []

    passages: list[dict[str, Any]] = []
    for test_item in data:
        if not isinstance(test_item, dict):
            continue
        test_name = str(test_item.get("name", "")).strip() or "Test ?"
        test_key_code = str(test_item.get("keyCode", "")).strip()
        sections = test_item.get("sectionList", [])
        if not isinstance(sections, list):
            continue
        for sec in sections:
            if not isinstance(sec, dict):
                continue
            qid = str(sec.get("questionId", "")).strip()
            if qid.isdigit():
                passages.append(
                    {
                        "bookNo": book_no,
                        "bookLabel": f"剑雅{book_no}",
                        "bookKeyCode": book_key_code,
                        "testName": test_name,
                        "testKeyCode": test_key_code,
                        "passageName": str(sec.get("title", "")).strip() or "Passage ?",
                        "topicTitle": str(sec.get("subTitle", "")).strip() or "(无题目)",
                        "questionId": qid,
                    }
                )
    return passages


def format_labeled_text_block(meta: dict[str, Any], exam_id: str, text: str) -> str:
    header = (
        f"===== {meta.get('bookLabel', '')} | {meta.get('testName', '')} | "
        f"{meta.get('passageName', '')} ====="
    )
    lines = [
        header,
        f"题目: {meta.get('topicTitle', '(无题目)')}",
        f"questionId: {meta.get('questionId', '')}",
        f"examId: {exam_id}",
        "",
        text.strip(),
    ]
    return "\n".join(lines).strip()


def create_exam_id(
    cookie: str,
    question_id: str,
    timeout: float,
    retries: int,
) -> str:
    url = f"{BASE_URL}/api/exam/create/1/{question_id}"
    payload = get_json(
        url,
        cookie=cookie,
        referer=f"{BASE_URL}/practice/detail/read/{question_id}",
        timeout=timeout,
        retries=retries,
    )
    if payload.get("code") != 200:
        raise RuntimeError(f"Create exam failed for questionId={question_id}: {payload}")
    data = payload.get("data") or {}
    exam_id = str(data.get("examId", "")).strip()
    if not exam_id.isdigit():
        raise RuntimeError(f"Invalid examId for questionId={question_id}: {payload}")
    return exam_id


def fetch_passage_payload(
    cookie: str,
    question_id: str,
    exam_id: str,
    timeout: float,
    retries: int,
) -> dict[str, Any]:
    detail_url = f"{BASE_URL}/practice/detail/read/{question_id}/{exam_id}"
    api_url = f"{BASE_URL}/api/question/{exam_id}/{question_id}"
    payload = get_json(api_url, cookie=cookie, referer=detail_url, timeout=timeout, retries=retries)
    if payload.get("code") != 200:
        raise RuntimeError(f"Question API failed q={question_id}, e={exam_id}: {payload}")
    return payload


def main() -> int:
    parser = argparse.ArgumentParser(description="Fetch all ieltscat reading passage text (Jianya 20..5).")
    parser.add_argument(
        "--cookie",
        default=os.getenv("IELTSCAT_COOKIE", DEFAULT_COOKIE),
        help="Cookie header value",
    )
    parser.add_argument("--start-book", type=int, default=20, help="Start Jianya book number")
    parser.add_argument("--end-book", type=int, default=5, help="End Jianya book number")
    parser.add_argument("--limit", type=int, default=0, help="Only fetch first N passages (debug)")
    parser.add_argument("--delay", type=float, default=0.15, help="Delay between passage requests (seconds)")
    parser.add_argument("--timeout", type=float, default=20, help="HTTP timeout in seconds")
    parser.add_argument("--retries", type=int, default=3, help="Retry times")
    parser.add_argument("--out-dir", default="data", help="Output directory")
    parser.add_argument(
        "--out-file",
        default="ieltscat_reading_20_to_5_all.txt",
        help="Merged text output filename",
    )
    parser.add_argument(
        "--manifest-file",
        default="ieltscat_reading_20_to_5_manifest.json",
        help="Manifest JSON filename",
    )
    parser.add_argument("--save-raw", action="store_true", help="Save each raw JSON payload")
    args = parser.parse_args()

    cookie = args.cookie.strip()
    if not cookie:
        print("[ERROR] Empty cookie. Provide --cookie or set IELTSCAT_COOKIE.")
        return 2

    ensure_dir(args.out_dir)

    try:
        books = get_book_keycodes(
            cookie=cookie,
            start_book=args.start_book,
            end_book=args.end_book,
            timeout=args.timeout,
            retries=args.retries,
        )
    except Exception as e:  # noqa: BLE001
        print(f"[ERROR] Failed to query book labels: {e}")
        return 3

    if not books:
        print("[ERROR] No target books found.")
        return 4

    passage_metas_ordered: list[dict[str, Any]] = []
    seen_qids: set[str] = set()
    for book_no, key_code in books:
        try:
            passages = get_passages_for_book(
                cookie=cookie,
                book_no=book_no,
                book_key_code=key_code,
                timeout=args.timeout,
                retries=args.retries,
            )
        except Exception as e:  # noqa: BLE001
            print(f"[WARN] Failed to load 剑雅{book_no} list: {e}")
            continue

        for item in passages:
            qid = str(item.get("questionId", ""))
            if qid not in seen_qids:
                seen_qids.add(qid)
                passage_metas_ordered.append(item)
        print(f"[INFO] 剑雅{book_no}: {len(passages)} passages")

    if args.limit > 0:
        passage_metas_ordered = passage_metas_ordered[: args.limit]

    total = len(passage_metas_ordered)
    if total == 0:
        print("[ERROR] No passage questionId found.")
        return 5

    merged_text_blocks: list[str] = []
    manifest: list[dict[str, Any]] = []
    failures: list[dict[str, str]] = []

    for idx, meta in enumerate(passage_metas_ordered, start=1):
        qid = str(meta.get("questionId", ""))
        print(
            f"[INFO] ({idx}/{total}) {meta.get('bookLabel')} | "
            f"{meta.get('testName')} | {meta.get('passageName')} | questionId={qid}"
        )
        try:
            exam_id = create_exam_id(
                cookie=cookie,
                question_id=qid,
                timeout=args.timeout,
                retries=args.retries,
            )
            payload = fetch_passage_payload(
                cookie=cookie,
                question_id=qid,
                exam_id=exam_id,
                timeout=args.timeout,
                retries=args.retries,
            )
            text = extract_article_text(payload).strip()

            if text:
                merged_text_blocks.append(format_labeled_text_block(meta, exam_id, text))
            else:
                print(f"[WARN] Empty extracted text for questionId={qid}")

            if args.save_raw:
                raw_path = os.path.join(args.out_dir, f"ieltscat_{qid}_{exam_id}.json")
                with open(raw_path, "w", encoding="utf-8") as f:
                    json.dump(payload, f, ensure_ascii=False, indent=2)

            manifest.append(
                {
                    "index": idx,
                    "bookNo": meta.get("bookNo"),
                    "bookLabel": meta.get("bookLabel"),
                    "testName": meta.get("testName"),
                    "passageName": meta.get("passageName"),
                    "topicTitle": meta.get("topicTitle"),
                    "questionId": qid,
                    "examId": exam_id,
                    "textChars": len(text),
                }
            )
        except Exception as e:  # noqa: BLE001
            msg = str(e)
            print(f"[WARN] Failed questionId={qid}: {msg}")
            failures.append(
                {
                    "bookLabel": str(meta.get("bookLabel", "")),
                    "testName": str(meta.get("testName", "")),
                    "passageName": str(meta.get("passageName", "")),
                    "topicTitle": str(meta.get("topicTitle", "")),
                    "questionId": qid,
                    "error": msg,
                }
            )
        time.sleep(max(0.0, args.delay))

    merged_text = "\n\n\n".join(merged_text_blocks).strip()
    out_txt_path = os.path.join(args.out_dir, args.out_file)
    manifest_path = os.path.join(args.out_dir, args.manifest_file)

    with open(out_txt_path, "w", encoding="utf-8") as f:
        f.write(merged_text)

    manifest_payload = {
        "generatedAt": int(time.time()),
        "baseUrl": BASE_URL,
        "bookRange": {"start": args.start_book, "end": args.end_book},
        "totalQuestionIds": total,
        "successCount": len(manifest),
        "failureCount": len(failures),
        "items": manifest,
        "failures": failures,
    }
    with open(manifest_path, "w", encoding="utf-8") as f:
        json.dump(manifest_payload, f, ensure_ascii=False, indent=2)

    print(f"[OK] merged text: {out_txt_path}")
    print(f"[OK] manifest: {manifest_path}")
    print(
        f"[DONE] passages requested={total}, success={len(manifest)}, "
        f"failed={len(failures)}, non-empty text blocks={len(merged_text_blocks)}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
