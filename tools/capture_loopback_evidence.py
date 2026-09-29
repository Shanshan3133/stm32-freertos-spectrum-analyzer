#!/usr/bin/env python3
"""Capture one real, paired DAC-to-ADC spectrum and render evidence files."""

from __future__ import annotations

import argparse
import csv
import json
import time
from datetime import datetime, timezone
from pathlib import Path

from spectrum_monitor import FFT_SIZE, FrameDecoder, SpectrumAssembler, SpectrumFrame


def write_csv(path: Path, columns: tuple[str, ...], rows: list[tuple]) -> None:
    with path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.writer(stream)
        writer.writerow(columns)
        writer.writerows(rows)


def capture(port_name: str, baud: int, timeout_s: float) -> tuple[dict[int, SpectrumFrame], FrameDecoder]:
    import serial  # type: ignore

    decoder = FrameDecoder()
    assembler = SpectrumAssembler()
    by_timestamp: dict[int, dict[int, SpectrumFrame]] = {}
    deadline = time.monotonic() + timeout_s
    with serial.Serial(port_name, baud, timeout=0.2) as port:
        port.read_until(bytes([0x7E]))
        while time.monotonic() < deadline:
            for chunk in decoder.feed(port.read(4096)):
                frame = assembler.push(chunk)
                if frame is None:
                    continue
                pair = by_timestamp.setdefault(frame.timestamp_us, {})
                pair[frame.channel] = frame
                if len(pair) == 2:
                    if decoder.errors or decoder.lost or assembler.incomplete_evictions:
                        raise RuntimeError("stream errors occurred during capture")
                    for channel in (0, 1):
                        item = pair[channel]
                        if item.status != 0x203 or item.dropped_blocks:
                            raise RuntimeError(f"channel {channel} has unexpected status or drops")
                        if max(range(len(item.magnitudes)), key=item.magnitudes.__getitem__) != 10:
                            raise RuntimeError(f"channel {channel} did not peak at bin 10")
                    return pair, decoder
    raise TimeoutError(f"no paired spectrum arrived within {timeout_s:g} s")


def render_plot(frames: dict[int, SpectrumFrame], path: Path) -> None:
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    fig, (wave_ax, spectrum_ax) = plt.subplots(2, 1, figsize=(11, 6.5), dpi=150)
    colors = ("#1769aa", "#e87924")
    for channel in (0, 1):
        frame = frames[channel]
        times_ms = [i * FFT_SIZE / len(frame.preview) * 1000 / frame.sample_rate_hz
                    for i in range(len(frame.preview))]
        frequencies = [i * frame.bin_hz for i in range(len(frame.magnitudes))]
        wave_ax.plot(times_ms, frame.preview, color=colors[channel],
                     label=f"ADC{channel + 1}: RMS {frame.rms:.4f}", linewidth=1.4)
        spectrum_ax.plot(frequencies, frame.magnitudes, color=colors[channel],
                         label=f"ADC{channel + 1}: bin 10, reported {frame.dominant_hz:.2f} Hz",
                         linewidth=1.5)
    wave_ax.set(title="DC-removed waveform preview", xlabel="Nominal time (ms)",
                ylabel="Normalized amplitude", xlim=(0, 10.24))
    spectrum_ax.set(title="1024-point Q15 FFT (first 5 kHz shown)",
                    xlabel="Nominal frequency (Hz)", ylabel="Normalized magnitude",
                    xlim=(0, 5000))
    for axis in (wave_ax, spectrum_ax):
        axis.grid(alpha=0.25)
        axis.legend(loc="upper right", frameon=False)
    fig.suptitle("STM32F446RE DAC-to-dual-ADC loopback | one measured telemetry snapshot")
    fig.text(0.5, 0.005,
             "DAC and ADC share TIM2; this plot does not independently verify the 100 kS/s clock.",
             ha="center", fontsize=9, color="#555555")
    fig.tight_layout(rect=(0, 0.035, 1, 0.95))
    fig.savefig(path)
    plt.close(fig)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=921600)
    parser.add_argument("--timeout", type=float, default=10.0)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    frames, decoder = capture(args.port, args.baud, args.timeout)
    args.output.mkdir(parents=True, exist_ok=True)
    spectrum_rows = []
    waveform_rows = []
    for channel in (0, 1):
        frame = frames[channel]
        spectrum_rows.extend((channel, i, round(i * frame.bin_hz, 6), value)
                             for i, value in enumerate(frame.magnitudes))
        waveform_rows.extend((channel, i,
                              round(i * FFT_SIZE / len(frame.preview) * 1e6 /
                                    frame.sample_rate_hz, 3), value)
                             for i, value in enumerate(frame.preview))
    write_csv(args.output / "spectrum-snapshot.csv",
              ("channel", "bin", "nominal_frequency_hz", "magnitude"),
              spectrum_rows)
    write_csv(args.output / "waveform-snapshot.csv",
              ("channel", "sample", "nominal_time_us", "amplitude"),
              waveform_rows)
    render_plot(frames, args.output / "spectrum-snapshot.png")
    summary = {
        "captured_at_utc": datetime.now(timezone.utc).isoformat(),
        "source": "live STM32F446RE telemetry, DAC-to-dual-ADC loopback",
        "timestamp_us": frames[0].timestamp_us,
        "status_by_channel": {str(c): f"0x{frames[c].status:08X}" for c in (0, 1)},
        "dominant_hz_by_channel": {str(c): frames[c].dominant_hz for c in (0, 1)},
        "rms_by_channel": {str(c): frames[c].rms for c in (0, 1)},
        "crc_errors": decoder.crc_errors,
        "sequence_lost": decoder.lost,
        "clock_caveat": "DAC and ADC share TIM2; frequencies use nominal sample rate",
    }
    (args.output / "snapshot-metadata.json").write_text(
        json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(summary, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
