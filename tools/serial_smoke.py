#!/usr/bin/env python3
"""Bounded, no-loopback serial check for a flashed STM32F446RE."""

from __future__ import annotations

import argparse
import json
import time
from collections import Counter

from spectrum_monitor import FrameDecoder, SpectrumAssembler


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=921600)
    parser.add_argument("--seconds", type=float, default=10.0)
    args = parser.parse_args()
    if args.seconds <= 0:
        parser.error("--seconds must be positive")

    try:
        import serial  # type: ignore
    except ImportError as exc:
        parser.error(f"pyserial is required: {exc}")

    decoder = FrameDecoder()
    assembler = SpectrumAssembler()
    channels: Counter[int] = Counter()
    status: Counter[int] = Counter()
    timings: list[int] = []
    drops: list[int] = []
    t0 = time.monotonic()
    with serial.Serial(args.port, args.baud, timeout=0.2) as port:
        # An open serial stream can begin halfway through a frame. Discard
        # only that initial fragment; count every subsequent framing error.
        port.read_until(bytes([0x7E]))
        deadline = t0 + args.seconds
        while time.monotonic() < deadline:
            for chunk in decoder.feed(port.read(4096)):
                frame = assembler.push(chunk)
                if frame is not None:
                    channels[frame.channel] += 1
                    status[frame.status] += 1
                    timings.append(frame.processing_us)
                    drops.append(frame.dropped_blocks)

    elapsed = time.monotonic() - t0
    summary = {
        "elapsed_s": round(elapsed, 3),
        "chunks": decoder.frames,
        "frames_by_channel": dict(channels),
        "rate_hz_by_channel": {
            channel: round(count / elapsed, 2)
            for channel, count in channels.items()
        },
        "status_counts": {f"0x{key:08X}": value for key, value in status.items()},
        "processing_us_max": max(timings, default=None),
        "dropped_blocks_max": max(drops, default=None),
        "crc_errors": decoder.crc_errors,
        "format_errors": decoder.format_errors,
        "sequence_lost": decoder.lost,
        "incomplete_evictions": assembler.incomplete_evictions,
    }
    print(json.dumps(summary, indent=2, sort_keys=True))
    # 20 Hz per channel is the design output rate. Allow start/stop partials.
    return 0 if (all(channels[c] >= 0.8 * args.seconds * 20 for c in (0, 1))
                 and decoder.errors == 0 and decoder.lost == 0
                 and assembler.incomplete_evictions == 0
                 and max(drops, default=1) == 0) else 1


if __name__ == "__main__":
    raise SystemExit(main())
