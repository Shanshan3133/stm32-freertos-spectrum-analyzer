# Binary telemetry protocol v2

Transport framing is `7E <escaped body> 7E`. Body bytes `7E` and `7D` are sent
as `7D 5E` and `7D 5D`. Multibyte fields are little-endian. CRC-16/CCITT-FALSE
uses polynomial `0x1021`, initial value `0xFFFF`, no reflection, and no final
XOR; `123456789` checks to `0x29B1`.

Each analysis result is eight packets: four 128-bin chunks for each of two
channels. Each chunk also carries 32 decimated waveform samples.

| Offset | Type | Meaning |
|---:|---|---|
| 0 | u8 | version = 2 |
| 1 | u8 | type = 1, spectrum chunk |
| 2 | u16 | payload length = 228 |
| 4 | u16 | wrapping packet sequence |
| 6 | u32 | timestamp, microseconds |
| 10 | u32 | status bitmap |
| 14 | u8 | channel, 0 or 1 |
| 15 | u8 | chunk index, 0..3 |
| 16 | u8 | chunk count = 4 |
| 17 | u8 | reserved = 0 |
| 18 | u32 | sample rate in Hz |
| 22 | u16 | first spectrum bin |
| 24 | u16 | bin count = 128 |
| 26 | u16 | RMS, Q15 |
| 28 | u16 | peak, Q15 |
| 30 | u32 | dominant frequency, millihertz |
| 34 | u32 | DSP processing time, microseconds |
| 38 | u32 | dropped block count |
| 42 | 128 x u8 | compressed magnitude, approximately Q15 / 128 |
| 170 | 32 x i16 | waveform preview, Q15 |
| 234 | u16 | CRC over bytes 0..233 |

The raw body is 236 bytes and the maximum escaped packet is 474 bytes. Eight
packets at 20 Hz consume at most 75,840 bytes/s on the wire, below the 92,160
bytes/s payload capacity of 921600 baud 8-N-1. This is why magnitudes are
compressed for transport instead of sending 16-bit bins.

## Status bitmap

| Bit | Meaning |
|---:|---|
| 0 | ADC acquisition running |
| 1 | both channels passed signal-quality checks |
| 2 | ADC/DMA overrun or acquisition timeout |
| 3 | generation or timestamp gap |
| 4 | DSP deadline exceeded |
| 5 | UART backpressure/timeout |
| 6 | numeric or frequency plausibility failure |
| 7 | previous reset was caused by IWDG |
| 8 | result or acquisition frame dropped |
| 9 | DAC loopback signal enabled |
| 10 | queued DMA half-buffer was stale |
| 11 | signal RMS below minimum |
| 12 | ADC signal clipped near full scale |
| 13 | waveform preview was effectively frozen |
| 14 | dedicated fault-injection build activated a fault |

The firmware and Python decoders bound input length and separately count CRC,
format, overflow, escape, resynchronization, true sequence loss, duplicates,
and out-of-order packets. Modular sequence comparison handles the `65535 -> 0`
wrap without reporting a false loss. A bad frame cannot contaminate the next
delimiter-bounded frame. The host also caps incomplete spectrum assemblies at
16, preventing unbounded memory growth during sustained packet loss, and
rejects chunks whose per-result metadata disagree.
