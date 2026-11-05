# Performance comparison notes

[简体中文](PERFORMANCE.md) | [English](PERFORMANCE.en.md) · [Back to comparison](../README.en.md#latency-and-performance)

The overview uses representative workloads. Latency is a stage-budget estimate; CPU is measured usage in equivalent logical cores. One core equals 1,000 CPU ms/s. Measurements use an i7-13700KF / RTX 4080.

Latency includes waiting for the game to sample input, the game's response, and presentation. Device input, display scanout and panel response are excluded.

## Xbox 360 / Xenia

The port figures use 0V (selected in-game for Futari) with Reflex enabled on the RTX 4080. Xenia uses the original timing: 1V in Futari, 2V in G2, 3V in SDOJ and the original frame loop in Guwange. Futari also offers 0V in its own settings on Xenia. [Configure 0V and Reflex](SETTINGS.en.md#futari-guwange-and-nvidia-reflex).

The estimate adds the average wait for a 60 Hz input read (8.33 ms), the time until the first image showing movement, and the historical mean from Present to the Windows display event on this PC (0.92 ms in independent flip for the ports, 11.18 ms in desktop composition for Xenia). Display mode and queue depth affect the last term. At 0V, the Guwange port shows movement in the same update that reads the input.

CPU and latency are captured separately. CPU sampling uses process time with the image and event probes disabled. The Reflex comparison uses the same scene and 0V setting for both runs. [Stage timings and sample counts](ANALYSIS.md#v095--xbox-360-低延迟).

## PS4 / shadPS4

The ports use NEO, Vulkan Immediate and automatic Reflex. The estimate adds the average 60 Hz sampling wait (8.33 ms) to the time from the game's input read to the presentation completion notification for its first visible movement. Each title uses its own matched frames and Vulkan present IDs. The notification includes driver and thread wake-up time. CPU was measured separately while moving and firing in Stage 1 with the production graphics backend.

The shadPS4 v0.20.0 figures come from the earlier runs on the same PC in base mode with default FIFO. Both Battle Garegga runs use Silver Sword in Arcade Stage 1, Valley. [Samples and timings (Chinese)](ANALYSIS.md#v095-ps4) · [NEO and Reflex settings](SETTINGS.en.md#neo-mode)

## PS2 / PCSX2

The comparison uses PCSX2 2.8.2 with D3D12 at native resolution and VSync off.

The PS2 measurements predate the switch to shader-based window scaling in v0.9.5. With Reflex enabled on the RTX 4080, we matched 12 direction changes per title. From the game's input read to the return from Present for the first frame showing movement, ESPGaluda averaged 7.86 ms and Ibara 8.29 ms. Adding the average sampling wait of 8.33 ms and the same PC's earlier 1.33 ms Vulkan Immediate presentation estimate gives 17.53 ms and 17.96 ms. Both PCSX2 games first showed movement on the second update. The 43 ms estimate uses two 60 Hz updates and the earlier D3D12 independent-flip measurement. [Timings and samples (Chinese)](ANALYSIS.md#v095-ps2)

CPU usage was measured separately with per-frame capture disabled. ESPGaluda used Arcade Stage 1 with Ageha in the port and a boss scene in PCSX2. Both Ibara samples used Arcade Normal Stage 1, Porttown.
