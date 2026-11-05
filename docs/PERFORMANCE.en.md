# Performance comparison notes

[简体中文](PERFORMANCE.md) | [English](PERFORMANCE.en.md) · [Back to comparison](../README.en.md#latency-and-performance)

The overview uses representative workloads. Latency is a stage-budget estimate; CPU is measured usage in equivalent logical cores. One core equals 1,000 CPU ms/s. Measurements use an i7-13700KF / RTX 4080.

Latency includes waiting for the game to sample input, the game's response, and presentation. Device input, display scanout and panel response are excluded.

## Xbox 360 / Xenia

The port figures use optional 0V with Reflex enabled on the RTX 4080. Xenia uses the original timing: 1V in Futari, 2V in G2, 3V in SDOJ and the original frame loop in Guwange. Futari also offers 0V in its own settings on Xenia. [Configure 0V and Reflex](SETTINGS.en.md#futari-guwange-and-nvidia-reflex).

The estimate adds the average wait for a 60 Hz input read (8.33 ms), the time until the first image showing movement, and the historical mean from Present to the Windows display event on this PC (0.92 ms in independent flip for the ports, 11.18 ms in desktop composition for Xenia). Display mode and queue depth affect the last term. In Guwange the character starts moving two game updates after the sampled update’s output; this offset was checked visually and applied to each captured input.

CPU and latency are captured separately. CPU sampling uses process time with the image and event probes disabled. The Reflex comparison uses the same scene and 0V setting for both runs. [Stage timings and sample counts](ANALYSIS.md#v095--xbox-360-低延迟).

## PS4 / shadPS4

The ports default to NEO (PS4 Pro) mode with low-latency presentation; shadPS4 v0.20.0 uses ordinary mode and default FIFO. Latency estimates include average sampling wait and the same-PC presentation budget. CPU figures are measured equivalent cores from short Stage 1 scenes. [Method and data (Chinese)](ANALYSIS.md#v082-neo-对照).

See the [settings guide](SETTINGS.en.md#neo-mode) to configure or disable NEO. PS4 games automatically adapt BMI1, LZCNT and MOVBE instructions to the available CPU features.

Both Battle Garegga runs used Silver Sword in Arcade Stage 1, Valley. During alternating turns, the time from the game reading a direction to submitting the first visible movement averaged 5.70 ms in the port and 33.09 ms in shadPS4. [Measurements (Chinese)](ANALYSIS.md#v094-garegga-and-ibara)

## PS2 / PCSX2

The comparison uses PCSX2 2.8.2 with D3D12 at native resolution and VSync off.

For ESPGaluda, the estimate combines an average sampling wait of 8.33 ms, a game response of 16.87 ms in the port or two updates in PCSX2, and presentation time from the same PC's Vulkan IMMEDIATE microbenchmark or D3D12 independent-flip capture. The port CPU figure uses the existing v0.9.1 Stage 1 sample; PCSX2 uses the earlier boss scene. The port's P95 frame interval was 17.32 ms, with a 17.93 ms maximum. [Method and data (Chinese)](ANALYSIS.md#v090-ps2-latency-estimate).

Both Ibara runs used Arcade Normal Stage 1, Porttown. The port moves idle time ahead of the input read. The first visible movement was presented an average of 22.02 ms after that read across 12 direction changes. In PCSX2, a right-input run first diverged from a neutral run two updates after the read, or 33.33 ms at 60 Hz. Including average sampling wait and presentation time, the expected totals are 32 and 43 ms. [Measurements (Chinese)](ANALYSIS.md#v094-garegga-and-ibara)
