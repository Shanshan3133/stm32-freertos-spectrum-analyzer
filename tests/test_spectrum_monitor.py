import pathlib
import struct
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tools"))
import spectrum_monitor as monitor


class SpectrumMonitorTests(unittest.TestCase):
    def test_01_standard_crc_vector(self):
        self.assertEqual(monitor.crc16(b"123456789"), 0x29B1)

    def test_02_round_trip(self):
        chunks = list(monitor.FrameDecoder().feed(monitor.make_test_frame(9, 1, 2)))
        self.assertEqual(len(chunks), 1)
        self.assertEqual((chunks[0].sequence, chunks[0].channel,
                          chunks[0].chunk_index), (9, 1, 2))

    def test_03_reserved_bytes_are_escaped(self):
        self.assertEqual(monitor.escape(bytes([0x7E, 0x7D])),
                         bytes([0x7E, 0x7D, 0x5E, 0x7D, 0x5D, 0x7E]))

    def test_04_crc_corruption_rejected(self):
        frame = bytearray(monitor.make_test_frame())
        frame[20] ^= 1
        decoder = monitor.FrameDecoder()
        self.assertEqual(list(decoder.feed(frame)), [])
        self.assertEqual(decoder.crc_errors, 1)

    def test_05_three_byte_stream_chunks(self):
        frame = monitor.make_test_frame(12)
        decoder = monitor.FrameDecoder()
        found = []
        for offset in range(0, len(frame), 3):
            found.extend(decoder.feed(frame[offset:offset + 3]))
        self.assertEqual([item.sequence for item in found], [12])

    def test_06_sequence_loss(self):
        decoder = monitor.FrameDecoder()
        list(decoder.feed(monitor.make_test_frame(10) + monitor.make_test_frame(13)))
        self.assertEqual(decoder.lost, 2)

    def test_07_truncated_frame_resynchronizes(self):
        decoder = monitor.FrameDecoder()
        stream = monitor.make_test_frame(1)[:20] + bytes([monitor.SOF])
        found = list(decoder.feed(stream + monitor.make_test_frame(2)))
        self.assertEqual([item.sequence for item in found], [2])
        self.assertEqual(decoder.format_errors, 1)

    def test_08_invalid_escape_recovers(self):
        decoder = monitor.FrameDecoder()
        invalid = bytes([monitor.SOF, monitor.ESC, 0, monitor.SOF])
        found = list(decoder.feed(invalid + monitor.make_test_frame(3)))
        self.assertEqual([item.sequence for item in found], [3])
        self.assertEqual(decoder.escape_errors, 1)

    def test_09_oversized_frame_recovers(self):
        decoder = monitor.FrameDecoder()
        garbage = bytes([monitor.SOF]) + bytes(monitor.RAW_LENGTH + 1) + bytes([monitor.SOF])
        found = list(decoder.feed(garbage + monitor.make_test_frame(4)))
        self.assertEqual([item.sequence for item in found], [4])
        self.assertEqual(decoder.overflow_errors, 1)

    def test_10_wrong_length_rejected(self):
        with self.assertRaises(monitor.FormatError):
            monitor.decode_raw(bytes(20))

    def test_11_all_chunks_reassemble(self):
        decoder = monitor.FrameDecoder()
        assembler = monitor.SpectrumAssembler()
        complete = None
        for index in (2, 0, 3, 1):
            chunk = list(decoder.feed(monitor.make_test_frame(index, 0, index)))[0]
            complete = assembler.push(chunk) or complete
        self.assertIsNotNone(complete)
        self.assertEqual(len(complete.magnitudes), monitor.BINS)
        self.assertEqual(len(complete.preview), monitor.PREVIEW_SAMPLES)

    def test_12_incomplete_spectrum_not_emitted(self):
        chunk = list(monitor.FrameDecoder().feed(monitor.make_test_frame()))[0]
        self.assertIsNone(monitor.SpectrumAssembler().push(chunk))

    def test_13_invalid_chunk_metadata_rejected(self):
        frame = monitor.make_test_frame()
        raw = bytearray()
        escaped = False
        for byte in frame[1:-1]:
            if escaped:
                raw.append(byte ^ 0x20)
                escaped = False
            elif byte == monitor.ESC:
                escaped = True
            else:
                raw.append(byte)
        raw[16] = 3
        struct.pack_into("<H", raw, 234, monitor.crc16(raw[:-2]))
        with self.assertRaises(monitor.FormatError):
            monitor.decode_raw(bytes(raw))

    def test_14_worst_case_line_rate_fits(self):
        maximum_escaped_packet = monitor.RAW_LENGTH * 2 + 2
        required_bytes_per_second = maximum_escaped_packet * 8 * 20
        available_bytes_per_second = 921600 // 10
        self.assertLess(required_bytes_per_second, available_bytes_per_second)

    def test_15_complete_spectrum_csv_records(self):
        decoder = monitor.FrameDecoder()
        assembler = monitor.SpectrumAssembler()
        complete = None
        for index in range(monitor.CHUNKS):
            chunk = list(decoder.feed(
                monitor.make_test_frame(index, 0, index)))[0]
            complete = assembler.push(chunk) or complete
        rows = list(monitor.spectrum_records(complete))
        self.assertEqual(len(rows), monitor.BINS)
        self.assertEqual(rows[10]["frequency_hz"], 976.5625)

    def test_16_complete_waveform_csv_records(self):
        decoder = monitor.FrameDecoder()
        assembler = monitor.SpectrumAssembler()
        complete = None
        for index in range(monitor.CHUNKS):
            chunk = list(decoder.feed(
                monitor.make_test_frame(index, 0, index)))[0]
            complete = assembler.push(chunk) or complete
        rows = list(monitor.waveform_records(complete))
        self.assertEqual(len(rows), monitor.PREVIEW_SAMPLES)
        self.assertEqual(rows[1]["time_us"], 80.0)

    def test_17_duplicate_and_out_of_order_do_not_inflate_loss(self):
        decoder = monitor.FrameDecoder()
        stream = (monitor.make_test_frame(10) + monitor.make_test_frame(10) +
                  monitor.make_test_frame(9) + monitor.make_test_frame(12))
        list(decoder.feed(stream))
        self.assertEqual(decoder.duplicates, 1)
        self.assertEqual(decoder.out_of_order, 1)
        self.assertEqual(decoder.lost, 1)

    def test_18_sequence_wrap_is_contiguous(self):
        decoder = monitor.FrameDecoder()
        list(decoder.feed(monitor.make_test_frame(65535) +
                          monitor.make_test_frame(0)))
        self.assertEqual(decoder.lost, 0)
        self.assertEqual(decoder.out_of_order, 0)

    def test_19_incomplete_spectra_are_bounded(self):
        assembler = monitor.SpectrumAssembler()
        decoder = monitor.FrameDecoder()
        for timestamp in range(monitor.MAX_PENDING_SPECTRA + 3):
            chunk = list(decoder.feed(
                monitor.make_test_frame(timestamp, 0, 0, timestamp)))[0]
            self.assertIsNone(assembler.push(chunk))
        self.assertEqual(len(assembler.pending), monitor.MAX_PENDING_SPECTRA)
        self.assertEqual(assembler.incomplete_evictions, 3)

    def test_20_inconsistent_chunks_rejected(self):
        assembler = monitor.SpectrumAssembler()
        decoder = monitor.FrameDecoder()
        first = list(decoder.feed(
            monitor.make_test_frame(1, 0, 0, 100)))[0]
        second_frame = bytearray(monitor.make_test_frame(2, 0, 1, 100))
        raw = bytearray()
        escaped = False
        for byte in second_frame[1:-1]:
            if escaped:
                raw.append(byte ^ 0x20)
                escaped = False
            elif byte == monitor.ESC:
                escaped = True
            else:
                raw.append(byte)
        struct.pack_into("<I", raw, 18, 96000)
        struct.pack_into("<H", raw, 234, monitor.crc16(raw[:-2]))
        second = monitor.decode_raw(bytes(raw))
        self.assertIsNone(assembler.push(first))
        self.assertIsNone(assembler.push(second))
        self.assertEqual(assembler.inconsistent_chunks, 1)
        self.assertEqual(len(assembler.pending), 0)

    def test_21_duplicate_chunk_is_counted(self):
        assembler = monitor.SpectrumAssembler()
        chunk = list(monitor.FrameDecoder().feed(
            monitor.make_test_frame(1, 0, 0, 100)))[0]
        assembler.push(chunk)
        assembler.push(chunk)
        self.assertEqual(assembler.duplicate_chunks, 1)


if __name__ == "__main__":
    unittest.main()
