# Contour quality baseline — Something New

2026-10-27. Album excerpt: seconds 80–95 of `Something New.m4a` (MP3 audio), decoded to stereo float WAV, lowered 6 dB for headroom. The native 44.1 kHz source was also resampled to 48 kHz. These results are from the plugin processor in an offline harness, with all other modules off, a constant curve, and smooth set to zero. They are useful for relative comparison; they are not a Logic listening or host CPU test.

| Mode | Depth | 44.1 kHz, 64 samples | 44.1 kHz, 512 samples | Peak | RMS, left |
|---|---:|---:|---:|---:|---:|
| Dry bypass | — | 0.12% | 0.10% | -5.2 dBFS | -14.2 dBFS |
| Drive | 35% | 0.15% | 0.14% | -10.6 dBFS | -12.2 dBFS |
| Drive | 80% | 0.18% | 0.14% | -14.0 dBFS | -14.7 dBFS |
| Crush | 60% | 0.11% | 0.11% | -5.6 dBFS | -14.2 dBFS |

The CPU percentages are cumulative `processBlock` time divided by audio duration on the M4, not DAW meter readings. At 48 kHz the range was similar (0.11–0.16%). Occasional single-block timing spikes came from the offline process scheduler, so they are not a reliable glitch measurement. Drive reduces peaks while changing RMS; compare its tone at matched listening loudness. Crush's added roughness is intentional.

The same 15-second renders on the M1, using the same arm64 test binary, measured 0.29–0.33% for bypass, 0.40–0.45% for Drive and 0.31–0.35% for Crush across 44.1/48 kHz and 64/512-sample blocks. These are offline ratios too; they do not replace a DAW meter or deadline test.

The current Drive uses host-rate `tanh` without oversampling. A previous 8 kHz sine stress test at 44.1 kHz found folded harmonics; that synthetic result alone does not establish audibility in the song. No DSP change is justified yet. The next release gate is a blind or level-matched listen to the prints, then a lossless Logic print and host CPU measurement at 64 and 512 samples on both Macs if the sound is worth keeping. If aliasing is audible in context, compare 2× and 4× oversampling with latency and CPU before changing the algorithm.
