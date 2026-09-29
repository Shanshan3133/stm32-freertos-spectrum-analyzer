#!/usr/bin/env python3
"""Streaming endurance, timing and tone check without retaining full spectra."""

from __future__ import annotations

import argparse
import json
import math
import time
from collections import Counter

from spectrum_monitor import FrameDecoder, SpectrumAssembler

EXPECTED_HZ = 100000 * 10 / 1024
FAULT_MASK = 0x7FFFFFFF & ~(1 | 2 | 512)


def percentile(histogram: Counter[int], fraction: float) -> int | None:
    total = sum(histogram.values())
    if not total:
        return None
    rank = math.ceil(total * fraction)
    seen = 0
    for value in sorted(histogram):
        seen += histogram[value]
        if seen >= rank:
            return value
    return None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=921600)
    parser.add_argument("--seconds", type=float, default=1800.0)
    parser.add_argument("--progress-seconds", type=float, default=60.0)
    args = parser.parse_args()
    if args.seconds <= 0 or args.progress_seconds <= 0:
        parser.error("durations must be positive")
    try:
        import serial  # type: ignore
    except ImportError as exc:
        parser.error(f"pyserial is required: {exc}")

    decoder = FrameDecoder()
    assembler = SpectrumAssembler()
    counts = Counter()
    statuses = Counter()
    processing_us = Counter()
    tone_pass = Counter()
    rms_min = {0: float("inf"), 1: float("inf")}
    rms_max = {0: 0.0, 1: 0.0}
    max_drop = 0
    deadline_miss = 0
    timestamp_discontinuities = 0
    board_elapsed_us = 0
    first_host_ns = None
    last_host_ns = None
    previous_timestamp = None

    start = time.monotonic()
    next_progress = start + args.progress_seconds
    with serial.Serial(args.port, args.baud, timeout=0.2) as port:
        port.read_until(bytes([0x7E]))  # synchronize at the first delimiter
        end = start + args.seconds
        while time.monotonic() < end:
            for chunk in decoder.feed(port.read(4096)):
                frame = assembler.push(chunk)
                if frame is None:
                    continue
                host_ns = time.perf_counter_ns()
                channel = frame.channel
                counts[channel] += 1
                statuses[frame.status] += 1
                processing_us[frame.processing_us] += 1
                rms_min[channel] = min(rms_min[channel], frame.rms)
                rms_max[channel] = max(rms_max[channel], frame.rms)
                max_drop = max(max_drop, frame.dropped_blocks)
                deadline_miss += frame.processing_us >= 9000
                peak_bin = max(range(len(frame.magnitudes)),
                               key=frame.magnitudes.__getitem__)
                if (peak_bin == 10 and
                        abs(frame.dominant_hz - EXPECTED_HZ) <= frame.bin_hz):
                    tone_pass[channel] += 1
                if channel == 0:
                    if first_host_ns is None:
                        first_host_ns = host_ns
                    if previous_timestamp is not None:
                        step = (frame.timestamp_us - previous_timestamp) & 0xFFFFFFFF
                        if step > 1_000_000:
                            timestamp_discontinuities += 1
                        else:
                            board_elapsed_us += step
                    previous_timestamp = frame.timestamp_us
                    last_host_ns = host_ns
            now = time.monotonic()
            if now >= next_progress:
                print(json.dumps({"progress_s": round(now - start, 1),
                                  "frames_by_channel": dict(counts),
                                  "crc_errors": decoder.crc_errors,
                                  "sequence_lost": decoder.lost,
                                  "processing_us_max": max(processing_us, default=None)}),
                      flush=True)
                next_progress += args.progress_seconds

    elapsed = time.monotonic() - start
    host_elapsed_s = ((last_host_ns - first_host_ns) / 1e9
                      if first_host_ns is not None and last_host_ns is not None
                      else None)
    clock_ratio = (board_elapsed_us / (host_elapsed_s * 1e6)
                   if host_elapsed_s and not timestamp_discontinuities else None)
    passed = (decoder.errors == 0 and decoder.lost == 0
              and assembler.incomplete_evictions == 0
              and max_drop == 0 and deadline_miss == 0
              and timestamp_discontinuities == 0)
    for channel in (0, 1):
        passed &= counts[channel] >= args.seconds * 20 * 0.98
        passed &= tone_pass[channel] >= counts[channel] * 0.99
        passed &= rms_min[channel] >= 0.05
    passed &= all(status & 2 and not (status & FAULT_MASK)
                  for status in statuses)
    summary = {
        "elapsed_s": round(elapsed, 3),
        "frames_by_channel": dict(counts),
        "rate_hz_by_channel": {str(c): round(counts[c] / elapsed, 3)
                               for c in (0, 1)},
        "tone_pass_by_channel": dict(tone_pass),
        "rms_range_by_channel": {str(c): [round(rms_min[c], 5), round(rms_max[c], 5)]
                                 for c in (0, 1)},
        "processing_us_max": max(processing_us, default=None),
        "processing_us_p99_9": percentile(processing_us, 0.999),
        "dsp_deadline_misses": deadline_miss,
        "dropped_blocks_max": max_drop,
        "status_counts": {f"0x{k:08X}": v for k, v in statuses.items()},
        "crc_errors": decoder.crc_errors,
        "format_errors": decoder.format_errors,
        "sequence_lost": decoder.lost,
        "incomplete_evictions": assembler.incomplete_evictions,
        "timestamp_discontinuities": timestamp_discontinuities,
        "board_elapsed_us": board_elapsed_us,
        "host_elapsed_s": round(host_elapsed_s, 6) if host_elapsed_s else None,
        "host_clock_ratio_estimate": round(clock_ratio, 8) if clock_ratio else None,
        "sample_rate_hz_host_estimate": round(100000 * clock_ratio, 2)
                                        if clock_ratio else None,
        "pass": bool(passed),
    }
    print(json.dumps(summary, indent=2, sort_keys=True), flush=True)
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
