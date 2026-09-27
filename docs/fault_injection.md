# Fault-injection builds

All injection options default to zero in `include/app_config.h`. Change only
one option at a time, label the binary as a fault build, and restore zero before
normal measurements.

| Option | Example | Expected behavior |
|---|---:|---|
| `FAULT_INJECT_DROP_EVERY_N_BLOCKS` | `10` | every tenth acquisition descriptor is dropped and flagged |
| `FAULT_INJECT_DSP_STALL_AFTER_BLOCKS` | `100` | DSP suspends after 100 blocks; missing health vote causes IWDG reset |
| `FAULT_INJECT_UART_FAIL_EVERY_N_FRAMES` | `20` | every twentieth frame reports UART backpressure and receiver resynchronizes |
| `FAULT_INJECT_FREEZE_ADC` | `1` | every block contains one repeated ADC word; frozen/weak signal is rejected |

For each run, record the option, value, commit, observed status bits, reset
cause, and recovery time. Never present injected faults as naturally occurring
hardware failures.

The standalone target build supports a DSP-stall validation binary without
editing the default configuration:

```powershell
powershell -ExecutionPolicy Bypass -File tools\build_target.ps1 -WatchdogFaultAfterBlocks 100
```

It is written to `build/target-stm32f446-watchdog-fault/`, separate from the
normal binary. After testing, run `tools/flash_target.ps1` to restore the
normal build; the helper uses SWD under reset because a watchdog reset loop
may prevent a normal debug connection. The 2026-09-27 result is recorded in
[`evidence/run-20260927/metadata.md`](../evidence/run-20260927/metadata.md).
