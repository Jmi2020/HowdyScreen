UI Asset Guide — 800×800 Round Display (ESP32‑P4)

Goal
- Prepare and use optimized images for the 3.4" 800×800 round display in LVGL.

Recommended formats
- Use LVGL image converter to generate C arrays (`.c/.h`) in RGB565 with Alpha.
- Target sizes:
  - Full‑screen: 800×800 (round mask applied by panel; keep corners transparent).
  - Character/center visuals: 240–320 px width typical to keep frame updates light.

Conversion steps
- Use LVGL Image Converter (local or web) with these settings:
  - Color format: RGB565A8 (or RGB565, if fully opaque)
  - Dithering: enabled (optional)
  - Output: C array, little‑endian, aligned
  - Compression: none (faster decode on device)
- Place generated files under: `components/ui_manager/images/`
- Register in `components/ui_manager/images/howdy_images.h` and include from `ui_manager.c`.

Sizing and layout
- Keep circular composition: avoid placing key visuals near corners.
- Leave margins for rings:
  - Outer mic ring: ~700 px diameter
  - Inner TTS ring: ~560 px diameter
  - Center control area: ~280×400 px touch button

Performance tips
- Prefer a small number of static images + ring/arc animations over frequent image swaps.
- Avoid large PNG decoding at runtime unless LV_PNG is enabled and PSRAM bandwidth allows it.
- Preload images at init to avoid stutters.

Checklist
- [ ] Convert assets from `Visual/` into `.c/.h` using LVGL converter
- [ ] Drop generated files in `components/ui_manager/images/`
- [ ] Update `howdy_images.h` includes and swap sources in `ui_manager.c` as desired
- [ ] Build and verify memory usage + refresh performance

