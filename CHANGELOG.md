# Changelog

## 1.2.0 — 2026-11-08

- Content-adaptive SDR dimming: bright, white-heavy content receives additional
  dimming; dark content never exceeds the manual brightness ceiling.
- Sample a 32×32 luminance map once per second on KWin's existing renderer,
  reduce it to one pixel, and transfer only that averaged value to the CPU.
- Smooth brightness changes with separate histories for physical render views,
  including mirrored displays; stop adaptive repaint scheduling during DPMS sleep.
- Persist the threshold and adjust it with Meta+Alt+, / Meta+Alt+. in 0.01 steps,
  with an on-screen indicator and migration from the former -/= defaults.
- Keep Meta+Alt+D for toggling and Meta+Alt+F6/F7 for manual brightness.
- Use Plasma's OSD service for feedback without a native animated overlay.
- Add controller and real GLES shader/readback regression tests, Arch packaging,
  source downloads, and MIT-licensed public release documentation.

### Compatibility and limits

- Binary package: x86_64 Arch/CachyOS, built for **KWin 6.8.2**; exact KWin
  version dependency. Other systems must build from source with matching headers.
- Adaptive processing supports SDR sRGB/gamma-2.2 targets. Other transfer
  functions, including HDR PQ/HLG, use fixed dimming instead.
- Sampling once per second means a white page's initial flash can still appear.
  The adjustment affects the whole display, not individual windows.
- Automated tests passed on the build machine; this is not a clean-distribution
  certification or a guarantee of visual comfort on every panel.
- The native plugin is not a scripted “Get New Desktop Effects” archive.
