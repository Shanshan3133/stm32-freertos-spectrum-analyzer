#!/usr/bin/env python3
"""Decode, reassemble, log, and optionally plot spectrum telemetry."""

from __future__ import annotations

import argparse
import csv
import json
import math
import struct
import sys
from dataclasses import asdict, dataclass
from typing import BinaryIO, Iterator

SOF = 0x7E
ESC = 0x7D
VERSION = 2
PACKET_TYPE = 1
CHANNELS = 2
FFT_SIZE = 1024
BINS = FFT_SIZE // 2
BINS_PER_CHUNK = 128
CHUNKS = BINS // BINS_PER_CHUNK
PREVIEW_SAMPLES = 128
PREVIEW_PER_CHUNK = 32
MAX_PENDING_SPECTRA = 16
PAYLOAD_LENGTH = 228
RAW_LENGTH = 236
HEADER = struct.Struct("<BBHHIIBBBBIHHHHIII")


class TelemetryError(ValueError):
    pass


class FormatError(TelemetryError):
    pass


class CRCError(TelemetryError):
    pass


def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


@dataclass(frozen=True)
class SpectrumChunk:
    sequence: int
    timestamp_us: int
    status: int
    channel: int
    chunk_index: int
    sample_rate_hz: int
    bin_start: int
    rms: float
    peak: float
    dominant_hz: float
    processing_us: int
    dropped_blocks: int
    magnitudes: tuple[float, ...]
    preview: tuple[float, ...]


@dataclass(frozen=True)
class SpectrumFrame:
    timestamp_us: int
    status: int
    channel: int
    sample_rate_hz: int
    rms: float
    peak: float
    dominant_hz: float
    processing_us: int
    dropped_blocks: int
    magnitudes: tuple[float, ...]
    preview: tuple[float, ...]

    @property
    def bin_hz(self) -> float:
        return self.sample_rate_hz / FFT_SIZE


def decode_raw(raw: bytes) -> SpectrumChunk:
    if len(raw) != RAW_LENGTH:
        raise FormatError(f"wrong raw length: {len(raw)}")
    if crc16(raw[:-2]) != struct.unpack_from("<H", raw, 234)[0]:
        raise CRCError("CRC mismatch")
    values = HEADER.unpack_from(raw)
    (version, packet_type, payload_length, sequence, timestamp_us, status,
     channel, chunk_index, chunk_count, reserved, sample_rate_hz, bin_start,
     bin_count, rms_q15, peak_q15, dominant_millihz, processing_us,
     dropped_blocks) = values
    if version != VERSION or packet_type != PACKET_TYPE:
        raise FormatError("unsupported version or packet type")
    if payload_length != PAYLOAD_LENGTH or reserved != 0:
        raise FormatError("invalid payload header")
    if channel >= CHANNELS or chunk_count != CHUNKS or chunk_index >= CHUNKS:
        raise FormatError("invalid channel or chunk index")
    if bin_count != BINS_PER_CHUNK or bin_start != chunk_index * BINS_PER_CHUNK:
        raise FormatError("invalid bin range")
    magnitudes_u8 = raw[42:170]
    preview_q15 = struct.unpack_from(f"<{PREVIEW_PER_CHUNK}h", raw, 170)
    return SpectrumChunk(
        sequence, timestamp_us, status, channel, chunk_index, sample_rate_hz,
        bin_start, rms_q15 / 32768.0, peak_q15 / 32768.0,
        dominant_millihz / 1000.0, processing_us, dropped_blocks,
        tuple(value / 255.0 for value in magnitudes_u8),
        tuple(value / 32768.0 for value in preview_q15),
    )


def escape(raw: bytes) -> bytes:
    output = bytearray([SOF])
    for byte in raw:
        if byte in (SOF, ESC):
            output.extend((ESC, byte ^ 0x20))
        else:
            output.append(byte)
    output.append(SOF)
    return bytes(output)


def make_test_frame(sequence: int = 7, channel: int = 0,
                    chunk_index: int = 0, timestamp_us: int = 123456) -> bytes:
    magnitudes = [min(255, ((chunk_index * BINS_PER_CHUNK + index) * 17 + 64) >> 7)
                  for index in range(BINS_PER_CHUNK)]
    preview = [((chunk_index * PREVIEW_PER_CHUNK + index) * 257) - 16384
               for index in range(PREVIEW_PER_CHUNK)]
    header = HEADER.pack(
        VERSION, PACKET_TYPE, PAYLOAD_LENGTH, sequence, timestamp_us, 3,
        channel, chunk_index, CHUNKS, 0, 100000,
        chunk_index * BINS_PER_CHUNK, BINS_PER_CHUNK, 12000, 20000,
        1000000 + channel * 500000, 2300, 2,
    )
    raw = header + bytes(magnitudes)
    raw += struct.pack(f"<{PREVIEW_PER_CHUNK}h", *preview)
    raw += struct.pack("<H", crc16(raw))
    return escape(raw)


class FrameDecoder:
    def __init__(self) -> None:
        self.buffer = bytearray()
        self.escaped = False
        self.dropping = False
        self.frames = 0
        self.errors = 0
        self.crc_errors = 0
        self.format_errors = 0
        self.overflow_errors = 0
        self.escape_errors = 0
        self.resync_events = 0
        self.lost = 0
        self.duplicates = 0
        self.out_of_order = 0
        self.last_sequence: int | None = None

    def feed(self, data: bytes) -> Iterator[SpectrumChunk]:
        for byte in data:
            if byte == SOF:
                if self.escaped:
                    self.escaped = False
                    self.buffer.clear()
                    self.errors += 1
                    self.escape_errors += 1
                    self.resync_events += 1
                    continue
                if self.dropping:
                    self.dropping = False
                    self.buffer.clear()
                    self.resync_events += 1
                    continue
                if self.buffer:
                    try:
                        chunk = decode_raw(bytes(self.buffer))
                    except CRCError:
                        self.errors += 1
                        self.crc_errors += 1
                        self.resync_events += 1
                    except FormatError:
                        self.errors += 1
                        self.format_errors += 1
                        self.resync_events += 1
                    else:
                        if self.last_sequence is not None:
                            delta = (chunk.sequence - self.last_sequence) & 0xFFFF
                            if delta == 0:
                                self.duplicates += 1
                            elif delta < 0x8000:
                                self.lost += delta - 1
                                self.last_sequence = chunk.sequence
                            else:
                                self.out_of_order += 1
                        else:
                            self.last_sequence = chunk.sequence
                        self.frames += 1
                        yield chunk
                self.buffer.clear()
                continue
            if self.dropping:
                continue
            if self.escaped:
                if byte not in (SOF ^ 0x20, ESC ^ 0x20):
                    self.escaped = False
                    self.buffer.clear()
                    self.dropping = True
                    self.errors += 1
                    self.escape_errors += 1
                    continue
                self.buffer.append(byte ^ 0x20)
                self.escaped = False
            elif byte == ESC:
                self.escaped = True
            elif len(self.buffer) < RAW_LENGTH:
                self.buffer.append(byte)
            else:
                self.buffer.clear()
                self.dropping = True
                self.errors += 1
                self.overflow_errors += 1


class SpectrumAssembler:
    def __init__(self) -> None:
        self.pending: dict[tuple[int, int], dict[int, SpectrumChunk]] = {}
        self.incomplete_evictions = 0
        self.duplicate_chunks = 0
        self.inconsistent_chunks = 0

    def push(self, chunk: SpectrumChunk) -> SpectrumFrame | None:
        key = (chunk.timestamp_us, chunk.channel)
        if key not in self.pending and len(self.pending) >= MAX_PENDING_SPECTRA:
            oldest = next(iter(self.pending))
            del self.pending[oldest]
            self.incomplete_evictions += 1
        parts = self.pending.setdefault(key, {})
        if parts:
            reference = next(iter(parts.values()))
            invariant = (
                chunk.status, chunk.sample_rate_hz, chunk.rms, chunk.peak,
                chunk.dominant_hz, chunk.processing_us, chunk.dropped_blocks,
            )
            reference_invariant = (
                reference.status, reference.sample_rate_hz, reference.rms,
                reference.peak, reference.dominant_hz,
                reference.processing_us, reference.dropped_blocks,
            )
            if invariant != reference_invariant:
                del self.pending[key]
                self.inconsistent_chunks += 1
                return None
        if chunk.chunk_index in parts:
            self.duplicate_chunks += 1
        parts[chunk.chunk_index] = chunk
        if len(parts) != CHUNKS:
            return None
        ordered = [parts[index] for index in range(CHUNKS)]
        del self.pending[key]
        first = ordered[0]
        magnitudes = tuple(value for part in ordered for value in part.magnitudes)
        preview = tuple(value for part in ordered for value in part.preview)
        return SpectrumFrame(
            first.timestamp_us, first.status, first.channel,
            first.sample_rate_hz, first.rms, first.peak, first.dominant_hz,
            first.processing_us, first.dropped_blocks, magnitudes, preview,
        )


def chunks(stream: BinaryIO, size: int = 512) -> Iterator[bytes]:
    while data := stream.read(size):
        yield data


def record(frame: SpectrumFrame) -> dict[str, int | float]:
    return {
        "timestamp_us": frame.timestamp_us,
        "channel": frame.channel,
        "sample_rate_hz": frame.sample_rate_hz,
        "rms": frame.rms,
        "peak": frame.peak,
        "dominant_hz": frame.dominant_hz,
        "processing_us": frame.processing_us,
        "dropped_blocks": frame.dropped_blocks,
        "status": frame.status,
    }


def spectrum_records(frame: SpectrumFrame) -> Iterator[dict[str, int | float]]:
    for bin_index, magnitude in enumerate(frame.magnitudes):
        yield {
            "timestamp_us": frame.timestamp_us,
            "channel": frame.channel,
            "bin": bin_index,
            "frequency_hz": bin_index * frame.bin_hz,
            "magnitude": magnitude,
        }


def waveform_records(frame: SpectrumFrame) -> Iterator[dict[str, int | float]]:
    sample_step = FFT_SIZE / PREVIEW_SAMPLES
    for sample_index, amplitude in enumerate(frame.preview):
        yield {
            "timestamp_us": frame.timestamp_us,
            "channel": frame.channel,
            "sample": sample_index,
            "time_us": sample_index * sample_step * 1_000_000.0 /
                       frame.sample_rate_hz,
            "amplitude": amplitude,
        }


class LivePlot:
    def __init__(self) -> None:
        try:
            import matplotlib.pyplot as plt
        except ImportError as exc:
            raise RuntimeError("--plot requires matplotlib") from exc
        self.plt = plt
        plt.ion()
        self.figure, (self.wave_axes, self.spectrum_axes) = plt.subplots(2, 1)
        self.wave_lines = [self.wave_axes.plot([], [], label=f"CH{c + 1}")[0]
                           for c in range(CHANNELS)]
        self.spectrum_lines = [
            self.spectrum_axes.plot([], [], label=f"CH{c + 1}")[0]
            for c in range(CHANNELS)
        ]
        self.wave_axes.set(xlabel="Time (ms)", ylabel="Normalized amplitude",
                           ylim=(-1.05, 1.05))
        self.spectrum_axes.set(xlabel="Frequency (Hz)", ylabel="Magnitude",
                               ylim=(0.0, 1.05))
        self.wave_axes.legend()
        self.spectrum_axes.legend()

    def update(self, frame: SpectrumFrame) -> None:
        step = FFT_SIZE / PREVIEW_SAMPLES
        times = [index * step * 1000.0 / frame.sample_rate_hz
                 for index in range(PREVIEW_SAMPLES)]
        frequencies = [index * frame.bin_hz for index in range(BINS)]
        self.wave_lines[frame.channel].set_data(times, frame.preview)
        self.spectrum_lines[frame.channel].set_data(frequencies, frame.magnitudes)
        self.wave_axes.set_xlim(0.0, times[-1])
        self.spectrum_axes.set_xlim(0.0, frame.sample_rate_hz / 2.0)
        self.figure.suptitle(
            f"CH{frame.channel + 1}: {frame.dominant_hz:.1f} Hz, "
            f"RMS {frame.rms:.3f}, DSP {frame.processing_us} us"
        )
        self.figure.canvas.draw_idle()
        self.figure.canvas.flush_events()


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group()
    source.add_argument("--input", type=argparse.FileType("rb"))
    source.add_argument("--port", help="ST-LINK virtual COM port, for example COM5")
    parser.add_argument("--baud", type=int, default=921600,
                        help="USART2 baud rate (default: 921600)")
    parser.add_argument("--csv", type=argparse.FileType("w"))
    parser.add_argument("--spectrum-csv", type=argparse.FileType("w"),
                        help="write every frequency bin in long CSV format")
    parser.add_argument("--waveform-csv", type=argparse.FileType("w"),
                        help="write every preview sample in long CSV format")
    parser.add_argument("--plot", action="store_true",
                        help="show live waveform and spectrum (requires matplotlib)")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args(argv)

    if args.self_test:
        decoder = FrameDecoder()
        assembler = SpectrumAssembler()
        frames = []
        for index in range(CHUNKS):
            for chunk in decoder.feed(make_test_frame(index, 0, index)):
                complete = assembler.push(chunk)
                if complete is not None:
                    frames.append(complete)
        assert crc16(b"123456789") == 0x29B1
        assert len(frames) == 1 and len(frames[0].magnitudes) == BINS
        assert math.isclose(frames[0].dominant_hz, 1000.0)
        print("spectrum monitor self-test: PASS")
        return 0

    stream: BinaryIO
    if args.port:
        try:
            import serial  # type: ignore
        except ImportError:
            parser.error("--port requires pyserial: python -m pip install pyserial")
        stream = serial.Serial(args.port, args.baud, timeout=1)
    else:
        stream = args.input or sys.stdin.buffer

    decoder = FrameDecoder()
    assembler = SpectrumAssembler()
    writer = None
    spectrum_writer = None
    waveform_writer = None
    try:
        plotter = LivePlot() if args.plot else None
    except RuntimeError as error:
        parser.error(str(error))
    for data in chunks(stream):
        for chunk in decoder.feed(data):
            frame = assembler.push(chunk)
            if frame is None:
                continue
            summary = record(frame)
            print(json.dumps(summary, separators=(",", ":")))
            if plotter is not None:
                plotter.update(frame)
            if args.csv:
                writer = writer or csv.DictWriter(args.csv, summary.keys())
                if args.csv.tell() == 0:
                    writer.writeheader()
                writer.writerow(summary)
                args.csv.flush()
            if args.spectrum_csv:
                rows = list(spectrum_records(frame))
                spectrum_writer = spectrum_writer or csv.DictWriter(
                    args.spectrum_csv, rows[0].keys())
                if args.spectrum_csv.tell() == 0:
                    spectrum_writer.writeheader()
                spectrum_writer.writerows(rows)
                args.spectrum_csv.flush()
            if args.waveform_csv:
                rows = list(waveform_records(frame))
                waveform_writer = waveform_writer or csv.DictWriter(
                    args.waveform_csv, rows[0].keys())
                if args.waveform_csv.tell() == 0:
                    waveform_writer.writeheader()
                waveform_writer.writerows(rows)
                args.waveform_csv.flush()
    print(f"frames={decoder.frames} errors={decoder.errors} "
          f"crc={decoder.crc_errors} format={decoder.format_errors} "
          f"overflow={decoder.overflow_errors} escape={decoder.escape_errors} "
          f"resync={decoder.resync_events} lost={decoder.lost} "
          f"duplicates={decoder.duplicates} "
          f"out_of_order={decoder.out_of_order} "
          f"incomplete_evictions={assembler.incomplete_evictions} "
          f"duplicate_chunks={assembler.duplicate_chunks} "
          f"inconsistent_chunks={assembler.inconsistent_chunks}",
          file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
