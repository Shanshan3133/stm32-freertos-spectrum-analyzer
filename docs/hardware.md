# Minimum hardware and wiring

## Required

| Item | Quantity | Note |
|---|---:|---|
| NUCLEO-F446RE | 1 | includes ST-LINK debugger and virtual COM port |
| USB-A to Mini-USB data cable | 1 | many listings incorrectly say Micro-USB; the board uses Mini-USB |
| small breadboard | 1 | fans the single PA4/A2 socket out to two ADC inputs |
| male-to-male jumper wire | 3 | A2, A0 and A1 each to one connected breadboard strip |

No resistor, soldering iron, signal generator, external ST-LINK,
USB-to-UART adapter, or sensor is required for the first hardware milestone.

## Loopback wiring

Power the board only from its ST-LINK USB connector. With power disconnected,
connect the Arduino-header aliases:

Insert the three jumper ends into **one five-hole connected strip** on the
same side of the breadboard's center gap. Do not put two jumper pins into the
single A2 socket or assume holes across the center gap are connected.

| Source | Destination | Purpose |
|---|---|---|
| A2 / PA4 / DAC_OUT1 | A0 / PA0 / ADC1_IN0 | channel 1 self-test |
| A2 / PA4 / DAC_OUT1 | A1 / PA1 / ADC2_IN1 | channel 2 self-test |

One output may drive these two high-impedance ADC inputs for this low-frequency
validation. Never connect phone/headphone audio directly: it can swing below
ground and exceed the ADC input range. An external signal requires biasing and
protection designed for 0..3.3 V.

The user completed this connection on 2026-09-28; the 60 s result is in
[`evidence/run-20260928/metadata.md`](../evidence/run-20260928/metadata.md).

## Optional evidence equipment

An inexpensive 8-channel 24 MHz logic analyzer can capture UART and timing
GPIO markers. It cannot observe DMA memory transfers directly; firmware must
toggle spare GPIO pins at DMA callbacks and DSP entry/exit. An oscilloscope is
helpful for analog quality but not required for the minimum result.
