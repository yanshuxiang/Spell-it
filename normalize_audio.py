#!/usr/bin/env python3
"""
Standalone MP3 loudness normalization tool for VibeSpeller.

- Default input/output directory: assets/audio (same folder app reads from).
- Uses ffmpeg loudnorm 2-pass for consistent loudness.
- Supports resume with checkpoint; resumes from previous file index - 1
  to avoid half-processed edge cases.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import json
import os
import re
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any


DEFAULT_TARGET_I = -20.0
DEFAULT_TARGET_TP = -1.5
DEFAULT_TARGET_LRA = 11.0


@dataclass
class LoudnormStats:
    input_i: float
    input_tp: float
    input_lra: float
    input_thresh: float
    target_offset: float


def run_cmd(cmd: list[str]) -> tuple[int, str, str]:
    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    return proc.returncode, proc.stdout, proc.stderr


def parse_loudnorm_json(stderr_text: str) -> LoudnormStats:
    # ffmpeg prints JSON block in stderr when print_format=json.
    match = re.search(r"\{\s*\"input_i\".*?\}", stderr_text, flags=re.DOTALL)
    if not match:
        raise RuntimeError("Cannot parse loudnorm JSON from ffmpeg output.")

    data: dict[str, Any] = json.loads(match.group(0))

    def as_float(key: str) -> float:
        value = data.get(key)
        if value in (None, "", "-inf", "inf"):
            raise RuntimeError(f"Invalid loudnorm value for {key}: {value}")
        return float(value)

    return LoudnormStats(
        input_i=as_float("input_i"),
        input_tp=as_float("input_tp"),
        input_lra=as_float("input_lra"),
        input_thresh=as_float("input_thresh"),
        target_offset=as_float("target_offset"),
    )


def ffmpeg_loudnorm_pass1(src: Path, target_i: float, target_tp: float, target_lra: float) -> LoudnormStats:
    cmd = [
        "ffmpeg",
        "-hide_banner",
        "-nostats",
        "-i",
        str(src),
        "-af",
        f"loudnorm=I={target_i}:TP={target_tp}:LRA={target_lra}:print_format=json",
        "-f",
        "null",
        "-",
    ]
    code, _out, err = run_cmd(cmd)
    if code != 0:
        raise RuntimeError(f"ffmpeg pass1 failed: {err.strip()}")
    return parse_loudnorm_json(err)


def ffmpeg_loudnorm_pass2(
    src: Path,
    dst_tmp: Path,
    stats: LoudnormStats,
    target_i: float,
    target_tp: float,
    target_lra: float,
) -> None:
    af = (
        f"loudnorm=I={target_i}:TP={target_tp}:LRA={target_lra}:"
        f"measured_I={stats.input_i}:measured_TP={stats.input_tp}:"
        f"measured_LRA={stats.input_lra}:measured_thresh={stats.input_thresh}:"
        f"offset={stats.target_offset}:linear=true:print_format=summary"
    )
    cmd = [
        "ffmpeg",
        "-hide_banner",
        "-y",
        "-i",
        str(src),
        "-af",
        af,
        "-c:a",
        "libmp3lame",
        "-b:a",
        "160k",
        str(dst_tmp),
    ]
    code, _out, err = run_cmd(cmd)
    if code != 0:
        raise RuntimeError(f"ffmpeg pass2 failed: {err.strip()}")


class ProgressStore:
    def __init__(self, progress_path: Path) -> None:
        self.progress_path = progress_path

    def load_start_index(self) -> int:
        if not self.progress_path.exists():
            return 0
        try:
            data = json.loads(self.progress_path.read_text(encoding="utf-8"))
            last_done = int(data.get("last_done_index", -1))
            # Resume from previous index - 1 to avoid half-file edge cases.
            return max(0, last_done - 1)
        except Exception:
            return 0

    def save_done(self, idx: int, total: int, file_name: str) -> None:
        payload = {
            "last_done_index": idx,
            "total": total,
            "last_file": file_name,
            "updated_at": int(time.time()),
        }
        tmp_path = self.progress_path.with_name(self.progress_path.name + ".tmp")
        tmp_path.write_text(json.dumps(payload, ensure_ascii=False, indent=2), encoding="utf-8")
        tmp_path.replace(self.progress_path)

    def clear(self) -> None:
        if self.progress_path.exists():
            self.progress_path.unlink()


def normalize_one(src: Path, target_i: float, target_tp: float, target_lra: float) -> None:
    tmp = src.with_name(src.name + ".norm.tmp.mp3")
    try:
        stats = ffmpeg_loudnorm_pass1(src, target_i, target_tp, target_lra)
        ffmpeg_loudnorm_pass2(src, tmp, stats, target_i, target_tp, target_lra)
        if not tmp.exists() or tmp.stat().st_size < 1024:
            raise RuntimeError("Temporary normalized file is missing or too small.")
        # Atomic replace in same directory.
        tmp.replace(src)
    finally:
        if tmp.exists():
            tmp.unlink(missing_ok=True)


def collect_mp3(audio_dir: Path) -> list[Path]:
    return sorted(
        [
            p
            for p in audio_dir.iterdir()
            if p.is_file()
            and p.suffix.lower() == ".mp3"
            and not p.name.endswith(".norm.tmp.mp3")
        ],
        key=lambda p: p.name.lower(),
    )


def parse_args() -> argparse.Namespace:
    script_dir = Path(__file__).resolve().parent
    default_audio_dir = script_dir / "assets" / "audio"

    parser = argparse.ArgumentParser(description="Normalize MP3 loudness for VibeSpeller audio assets.")
    parser.add_argument("--audio-dir", type=Path, default=default_audio_dir, help="Directory containing mp3 files")
    parser.add_argument("--target-i", type=float, default=DEFAULT_TARGET_I, help="Target integrated loudness (LUFS)")
    parser.add_argument("--target-tp", type=float, default=DEFAULT_TARGET_TP, help="Target true peak (dBTP)")
    parser.add_argument("--target-lra", type=float, default=DEFAULT_TARGET_LRA, help="Target loudness range")
    parser.add_argument("--sleep-ms", type=int, default=120, help="Sleep between files in milliseconds")
    parser.add_argument("--limit", type=int, default=0, help="Process only first N files after resume point (0 = all)")
    parser.add_argument(
        "--workers",
        type=str,
        default="1",
        help='Number of files to normalize in parallel; use "max" to auto-detect the highest logical core count',
    )
    parser.add_argument("--reset-progress", action="store_true", help="Reset progress and start from beginning")
    parser.add_argument("--progress-file", type=Path, default=None, help="Custom progress file path")
    return parser.parse_args()


def resolve_worker_count(raw_workers: str) -> int:
    text = raw_workers.strip().lower()
    if text == "max":
        return max(1, os.cpu_count() or 1)

    try:
        workers = int(text)
    except ValueError as exc:
        raise ValueError('workers must be an integer or "max"') from exc

    return max(1, workers)


def main() -> int:
    args = parse_args()

    audio_dir = args.audio_dir.resolve()
    if not audio_dir.exists() or not audio_dir.is_dir():
        print(f"[ERROR] Audio directory not found: {audio_dir}", file=sys.stderr)
        return 1

    progress_path = args.progress_file.resolve() if args.progress_file else (audio_dir / ".normalize_progress.json")
    progress = ProgressStore(progress_path)

    if args.reset_progress:
        progress.clear()

    mp3_files = collect_mp3(audio_dir)
    if not mp3_files:
        print(f"[INFO] No mp3 files found in: {audio_dir}")
        return 0

    start_index = progress.load_start_index()
    if start_index >= len(mp3_files):
        start_index = max(0, len(mp3_files) - 1)

    end_index = len(mp3_files)
    if args.limit > 0:
        end_index = min(end_index, start_index + args.limit)

    print("[INFO] VibeSpeller audio normalization")
    print(f"[INFO] Directory : {audio_dir}")
    print(f"[INFO] Files     : {len(mp3_files)} total, process [{start_index}, {end_index})")
    print(f"[INFO] Target    : I={args.target_i} LUFS, TP={args.target_tp} dBTP, LRA={args.target_lra}")
    print(f"[INFO] Progress  : {progress_path}")

    ok_count = 0
    fail_count = 0
    workers = resolve_worker_count(args.workers)
    print(f"[INFO] Workers   : {workers}")
    scheduled: list[tuple[int, Path]] = [(idx, mp3_files[idx]) for idx in range(start_index, end_index)]

    def job(item: tuple[int, Path]) -> tuple[int, str, bool, str]:
        idx, src = item
        try:
            normalize_one(src, args.target_i, args.target_tp, args.target_lra)
            return idx, src.name, True, ""
        except Exception as exc:
            return idx, src.name, False, str(exc)

    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as executor:
        future_map = {executor.submit(job, item): item[0] for item in scheduled}
        completed_indices: list[int] = []

        for future in concurrent.futures.as_completed(future_map):
            idx, file_name, success, error_text = future.result()
            if success:
                ok_count += 1
                completed_indices.append(idx)
                print(f"[{idx + 1}/{len(mp3_files)}] Done {file_name}")
                progress.save_done(idx, len(mp3_files), file_name)
            else:
                fail_count += 1
                print(f"[{idx + 1}/{len(mp3_files)}] FAIL {file_name}: {error_text}", file=sys.stderr)
            if args.sleep_ms > 0:
                time.sleep(args.sleep_ms / 1000.0)

    print(f"[DONE] Success={ok_count}, Failed={fail_count}")
    if fail_count == 0 and end_index == len(mp3_files):
        print("[DONE] Full normalization completed.")
    else:
        print("[DONE] You can rerun script to continue from checkpoint.")
    return 0 if fail_count == 0 else 2


if __name__ == "__main__":
    raise SystemExit(main())
