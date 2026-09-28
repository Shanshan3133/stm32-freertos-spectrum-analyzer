#!/usr/bin/env python3
"""Check live two-channel DAC-to-ADC loopback telemetry on NUCLEO-F446RE."""

from __future__ import annotations

import argparse
import json
import statistics
import time
from collections import Counter

from spectrum_monitor import FrameDecoder, SpectrumAssembler, SpectrumFrame

EXPECTED_HZ = 100000 * 10 / 1024
EXPECTED_BIN = 10
FAULT_MASK = 0x7FFFFFFF & ~(1 | 2 | 512)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=921600)
    parser.add_argument("--seconds", type=float, default=60.0)
    args = parser.parse_args()
    if args.seconds <= 0:
        parser.error("--seconds must be positive")

    try:
        import serial  # type: ignore
    except ImportError as exc:
        parser.error(f"pyserial is required: {exc}")

    decoder = FrameDecoder()
    assembler = SpectrumAssembler()
    frames: dict[int, list[SpectrumFrame]] = {0: [], 1: []}
    t0 = time.monotonic()
    with serial.Serial(args.port, args.baud, timeout=0.2) as port:
        port.read_until(bytes([0x7E]))  # discard only the opening partial frame
        deadline = t0 + args.seconds
        while time.monotonic() < deadline:
            for chunk in decoder.feed(port.read(4096)):
                frame = assembler.push(chunk)
                if frame is not None:
                    frames[frame.channel].append(frame)

    elapsed = time.monotonic() - t0
    status_counts = Counter(f.status for values in frames.values() for f in values)
    channel_results = {}
    passed = decoder.errors == 0 and decoder.lost == 0
    passed &= assembler.incomplete_evictions == 0
    for channel, values in frames.items():
        if not values:
            channel_results[str(channel)] = {"frames": 0}
            passed = False
            continue
        frequencies = [f.dominant_hz for f in values]
        rms = [f.rms for f in values]
        peaks = [max(range(len(f.magnitudes)), key=f.magnitudes.__getitem__)
                 for f in values]
        good_tone = sum(abs(f.dominant_hz - EXPECTED_HZ) <= f.bin_hz
                        and peak == EXPECTED_BIN
                        for f, peak in zip(values, peaks))
        channel_results[str(channel)] = {
            "frames": len(values),
            "rate_hz": round(len(values) / elapsed, 2),
            "dominant_hz_median": round(statistics.median(frequencies), 3),
            "dominant_hz_min": round(min(frequencies), 3),
            "dominant_hz_max": round(max(frequencies), 3),
            "rms_median": round(statistics.median(rms), 5),
            "rms_min": round(min(rms), 5),
            "rms_max": round(max(rms), 5),
            "tone_and_peak_bin_pass": good_tone,
            "processing_us_max": max(f.processing_us for f in values),
            "dropped_blocks_max": max(f.dropped_blocks for f in values),
        }
        passed &= len(values) >= 0.8 * args.seconds * 20
        passed &= good_tone >= 0.95 * len(values)
        passed &= min(rms) >= 0.05
        passed &= all(f.status & 2 and not (f.status & FAULT_MASK)
                      and f.dropped_blocks == 0 for f in values)

    summary = {
        "expected_hz": EXPECTED_HZ,
        "expected_peak_bin": EXPECTED_BIN,
        "elapsed_s": round(elapsed, 3),
        "chunks": decoder.frames,
        "channels": channel_results,
        "status_counts": {f"0x{s:08X}": n for s, n in status_counts.items()},
        "crc_errors": decoder.crc_errors,
        "format_errors": decoder.format_errors,
        "sequence_lost": decoder.lost,
        "incomplete_evictions": assembler.incomplete_evictions,
        "pass": bool(passed),
    }
    print(json.dumps(summary, indent=2, sort_keys=True))
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
