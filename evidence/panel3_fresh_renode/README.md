# Real-Time GNC Loop Under Emulation
## Fresh Build and Renode Reproducibility Guide

### 1. Project Overview

This project evaluates a Real-Time Guidance, Navigation and Control (GNC) loop using FreeRTOS under Renode emulation on an STM32F4 Cortex-M4 platform.

The system contains four periodic tasks:

| Task | Period | Priority |
|---|---:|---:|
| GNC / Control | 10 ms | 4 |
| Sensing / Estimation | 20 ms | 3 |
| Communications | 50 ms | 2 |
| Monitoring / Telemetry | 100 ms | 1 |

The GNC task executes the controller and publishes telemetry. The Communications task consumes the GNC telemetry interface. Sensing and Telemetry contain placeholder workloads in the supplied source.

### 2. Build Environment

- Operating system: Windows
- Emulator: Renode 1.17.0
- Target architecture: ARM Cortex-M4
- Compiler: ARM GNU Toolchain GCC 15.3.1
- Linker: GNU ld 2.45.1
- FreeRTOS: V11.1.0+
- Renode performance setting: 125 MIPS
- FreeRTOS tick rate: 1000 Hz
- RTOS tick resolution: 1 ms
- High-resolution timer: TIM2, nominal 10 MHz
- Timer resolution: 0.1 microseconds per count

Fresh build artifacts are kept in this directory. The original project files were not overwritten.

### 3. Fresh Build Artifacts

The fresh build contains these object files:

- phase3_timing.o
- tasks.o
- port.o
- control.o
- list.o
- queue.o
- rtos_lib.o
- startup_rtos.o

Main output files:

- fresh_gnc.elf
- fresh_gnc.map
- fresh_gnc.resc

The source files were compiled for ARM Cortex-M4 using Thumb mode and freestanding compilation. The main application and FreeRTOS sources used debug information and -O0. The ARM_CM4F port used the FPv4-SP-D16 target configuration.

The ELF was verified as a 32-bit little-endian ARM EABI executable with entry point 0x08003551.

### 4. Run the Fresh Build in Renode

Open PowerShell or CMD and execute:

```powershell
& 'C:\Program Files\Renode\renode.exe' 'C:\Users\DELL\Week4_GNC_Renode\c_port\fresh_rebuild\fresh_gnc.resc'
```

At the Renode prompt:

```text
start
```

Allow the emulation to run, then pause it:

```text
pause
```

The script loads the STM32F4 Discovery platform and fresh_gnc.elf with PerformanceInMips set to 125.

### 5. Timing Measurement Structure

The TaskTiming structure contains 13 32-bit fields.

| Offset | Field |
|---|---|
| 0x00 | samples |
| 0x04 | period_min_ticks |
| 0x08 | period_max_ticks |
| 0x0C | execution_min_ticks |
| 0x10 | execution_max_ticks |
| 0x14 | activation_period_violations |
| 0x18 | execution_deadline_misses |
| 0x1C | peak_to_peak_jitter_ticks |
| 0x20 | timer_period_min_counts |
| 0x24 | timer_period_max_counts |
| 0x28 | timer_execution_min_counts |
| 0x2C | timer_execution_max_counts |
| 0x30 | timer_peak_to_peak_jitter_counts |

TaskTiming base addresses:

| Task | Base address |
|---|---|
| GNC | 0x20000000 |
| Sensing | 0x20000034 |
| Communications | 0x20000068 |
| Telemetry | 0x2000009C |

### 6. Reading Timing Data in Renode

Example: GNC timing sample count:

```text
sysbus ReadDoubleWord 0x20000000
```

GNC execution minimum and maximum timer counts:

```text
sysbus ReadDoubleWord 0x20000028
sysbus ReadDoubleWord 0x2000002C
```

GNC deadline misses:

```text
sysbus ReadDoubleWord 0x20000018
```

GNC activation-period violations:

```text
sysbus ReadDoubleWord 0x20000014
```

For another task, use its base address plus the appropriate field offset from Section 5.

### 7. Fresh Renode Measurement Results

The following results were captured by reading the TaskTiming structures from Renode RAM during the fresh emulation run.

| Task | Samples | Period min (ms) | Period max (ms) | Jitter (us) | Execution min (us) | Execution max (us) |
|---|---:|---:|---:|---:|---:|---:|
| GNC | 582 | 9.9818 | 10.0034 | 21.6 | 11.1 | 11.8 |
| Sensing | 172 | 19.9834 | 20.0021 | 18.7 | 0.7 | 1.3 |
| Communications | 69 | 49.9762 | 50.0088 | 32.6 | 0.9 | 1.5 |
| Telemetry | 35 | 99.9854 | 99.9987 | 13.3 | 0.7 | 1.3 |

All four tasks recorded:

- Zero activation-period violations.
- Zero execution deadline misses.

RTOS tick measurements:

| Task | Minimum ticks | Maximum ticks |
|---|---:|---:|
| GNC | 10 | 10 |
| Sensing | 20 | 20 |
| Communications | 50 | 50 |
| Telemetry | 100 | 100 |

Conversion:

- Timer frequency = 10 MHz.
- One timer count = 0.1 us.
- Execution time in microseconds = timer counts / 10.
- Period in milliseconds = timer counts / 10,000.

These are observed measurements from this emulation run. They are not formal mathematical WCET guarantees.

### 8. Historical Verification Evidence

The project also contains separate historical verification results.

Week 9 interface verification:
- GNC_Task publishes timestamp, status and torque fields.
- Communications_Task consumes timestamp, status and torque fields.
- Repeated non-zero timestamps and valid status demonstrated producer-consumer runtime activity.
- No numerical torque value was claimed from the Week 9 runtime memory reads.

Week 9 cross-code verification:
- C and Python reference torque results matched on all three axes.
- X: -0.010000 Nm
- Y: +0.007000 Nm
- Z: -0.003300 Nm

Week 11 measurements:
- 100 GNC period measurements at 1 ms RTOS tick resolution.
- Separate controller benchmark: 300 executions.
- Minimum observed controller execution: 5.968 us.
- Maximum observed controller execution: 7.288 us.
- Reference baseline: 8.20 us.

These historical measurements have different measurement scopes from the fresh four-task results in Section 7. They must not be combined as though they were one measurement campaign.

### 9. Evidence Files

The fresh rebuild folder contains:

- fresh_gnc.elf
- fresh_gnc.map
- fresh_gnc.resc
- fresh_renode_run.log
- rtos_wcet_traces_fresh_run.csv
- 05_fresh_renode_task_counters.png
- 06_fresh_renode_deadline_misses.png
- 07_fresh_renode_period_jitter.png

The CSV preserves the existing reference and instruction-trace fields while recording the fresh timing measurements.

### 10. Known Limitations

1. The observed execution maxima are not formal WCET proofs.
2. Sensing and Telemetry workloads are placeholders.
3. Communications contains a telemetry consumer and remaining placeholder workload.
4. The 1 ms RTOS tick cannot resolve microsecond-level execution variation. TIM2 provides the higher-resolution timing measurements.
5. Renode reported NVIC priority warnings for interrupt 16, PendSV and SysTick. The warnings stated that priority 0xFF should be maskable with 0xF0.
6. The run log records machine start, pause and disposal; it does not independently contain the task timing measurements. Those were read from RAM.
7. Results apply to the recorded emulation configuration and run.

### 11. Reproducibility and Integrity

- The fresh ELF and MAP were generated in a separate build directory.
- The original project source and original submission ZIP files were preserved.
- The fresh Renode script points to the fresh ELF.
- Timing values were read from the TaskTiming structures in emulated RAM.
- The separate fresh-run CSV was checked after updating.
- Screenshots and the Renode log are retained with the fresh build artifacts.

End of README.
