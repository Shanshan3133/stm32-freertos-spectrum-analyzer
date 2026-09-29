# Float versus Q15 numerical comparison

This pre-hardware report runs the same quantized ADC samples through a float
pipeline and a deterministic Q15 radix-2 model. Both perform block-mean removal,
Hann windowing, FFT, magnitude extraction, and parabolic peak interpolation.

The model includes Q15 window/twiddle quantization, saturating butterflies, and
one-bit scaling per FFT stage. It approximates the numerical effects expected
from CMSIS-DSP, but it is **not** a measured substitute for `arm_rfft_q15` on
the STM32F446RE. The target table remains pending until the board is run.

| Input Hz | Amplitude | Float Hz | Q15-model Hz | abs df Hz | Float RMS | Q15 RMS | abs dRMS | abs dPeak |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 440.0000 | 0.10 | 440.4664 | 441.8019 | 1.3354 | 0.051499 | 0.051499 | 0.000000 | 0.000266 |
| 440.0000 | 0.50 | 440.4809 | 440.7415 | 0.2606 | 0.257492 | 0.257492 | 0.000000 | 0.000265 |
| 440.0000 | 0.90 | 440.4830 | 440.5954 | 0.1124 | 0.463479 | 0.463479 | 0.000000 | 0.000160 |
| 976.5625 | 0.10 | 976.5625 | 976.6432 | 0.0808 | 0.051803 | 0.051803 | 0.000000 | 0.000420 |
| 976.5625 | 0.50 | 976.5625 | 976.5790 | 0.0165 | 0.258951 | 0.258951 | 0.000000 | 0.000358 |
| 976.5625 | 0.90 | 976.5625 | 976.5717 | 0.0092 | 0.466096 | 0.466096 | 0.000000 | 0.000300 |
| 1000.0000 | 0.10 | 995.2243 | 995.0589 | 0.1654 | 0.051716 | 0.051716 | 0.000000 | 0.000099 |
| 1000.0000 | 0.50 | 995.2243 | 995.1732 | 0.0511 | 0.258643 | 0.258643 | 0.000000 | 0.000061 |
| 1000.0000 | 0.90 | 995.2243 | 995.2108 | 0.0135 | 0.465563 | 0.465563 | 0.000000 | 0.000053 |
| 4000.0000 | 0.10 | 4000.9700 | 4001.0390 | 0.0690 | 0.051869 | 0.051869 | 0.000000 | 0.000168 |
| 4000.0000 | 0.50 | 4000.9700 | 4000.9802 | 0.0101 | 0.259086 | 0.259086 | 0.000000 | 0.000173 |
| 4000.0000 | 0.90 | 4000.9700 | 4000.9785 | 0.0085 | 0.466318 | 0.466318 | 0.000000 | 0.000037 |

Across this matrix, fixed-versus-float frequency deviation is at most 1.3354 Hz
at the lowest-amplitude 440 Hz case. Peak-amplitude deviation is at most
0.000420 normalized units. RMS is computed directly from the same Q15 samples,
so the model and float reference agree to the shown precision.

Regenerate and enforce the pre-hardware bounds with:

```powershell
python tools\fft_precision_report.py --check
```

## Target results to add

After flashing the board, repeat these cases through DAC loopback and add
`CMSIS-DSP Hz`, `CMSIS-DSP RMS`, cycle count, compiler flags, and firmware
commit columns. The target acceptance limits are 1 FFT bin frequency error,
RMS error below 1%, and observed DSP processing time below the 9 ms design
deadline. The 30-minute loopback run observed a maximum of 1.742 ms; this is
not a proven WCET bound or an independent absolute frequency measurement.
