# Repository Guidelines

## Project Structure & Modules
- `main/`: App entry points and HowdyTTS integrations (e.g., `howdy_phase6_howdytts_integration.c`).
- `components/`: Reusable modules (`audio_processor/`, `ui_manager/`, `wifi_manager/`, `websocket_client/`).
- `managed_components/`: ESP‑IDF managed deps (LVGL, `esp_wifi_remote`, touch drivers).
- `tools/`: Utilities (`esp32p4-diagnostic-script.sh`, `validate_phase4b.sh`).
- Config & docs: `sdkconfig*`, `partitions.csv`, `README.md`, `ARCHITECTURE.md`.

## Build, Run, Test
- `idf.py set-target esp32p4`: Configure target once per workspace.
- `idf.py build`: Compile firmware with ESP‑IDF 5.3+.
- `idf.py menuconfig`: Configure WiFi/HowdyTTS under HowdyScreen menu.
- `./esp32p4-diagnostic-script.sh`: Board/peripheral health check.
- `./validate_phase4b.sh`: Validate audio duplex + TTS playback.
- Python tests (optional): `pytest -q` with `pytest-embedded` (see `pytest_hello_world.py`).
- Note: Humans run `idf.py flash`/`monitor`. Agents must only build and prompt the user to flash when needed.

## Coding Style & Naming
- Language: C (ESP‑IDF). Indentation: 4 spaces, no tabs.
- Files: snake_case (`howdytts_client.c`, `wifi_provisioning_ui.c`).
- Identifiers: `snake_case` for functions/vars; `UPPER_SNAKE` for macros/consts; `s_` for static globals.
- Prefix modules by domain: `howdy_`, `ui_`, `wifi_`, `audio_`.
- Logging: use `ESP_LOGx(TAG, ...)` with clear `TAG`.

## Testing Guidelines
- On‑device: After flashing, verify logs, UI states, and recovery paths.
- Pytests: Use `pytest-embedded-idf`; see `pytest_hello_world.py` for patterns.
- Prefer tests under `<component>/test/`; mark host/QEMU vs hardware as needed.

## Commits & Pull Requests
- Commit style: Conventional Commits (`feat:`, `fix:`, `refactor:`), imperative, scoped.
  - Example: `feat(ui): add touch-to-talk visual state`
- Branches: `feature/<slug>`, `fix/<slug>`, `chore/<slug>`.
- PRs include: purpose, linked issues, test plan (commands/logs), target board, screenshots/video if UI.
- Keep diffs minimal; call out changes to `sdkconfig`/partitions.

## Security & Configuration
- Do not commit secrets. Use `credentials.example` to create `credentials.conf` locally.
- `sdkconfig` is environment‑specific; regenerate with `menuconfig` rather than editing by hand.

## Agentic Workflow & Dual-Repo Coordination
- Dual-project rule: Mirror protocol/config changes in HowdyTTS at `/Users/silverlinings/Desktop/Coding/RBP/HowdyTTS/` and update docs in both repos.
- Discovery & control: UDP discovery on `8001` (broadcast + subnet), optional mDNS `_howdytts._udp.local.`; WebSocket control channel; audio streaming on `8003`.
- Roles (from `.claude/agents`):
  - `@api-architect`: `esp_wifi_remote` setup, WebSocket client, mDNS.
  - `@performance-optimizer`: I2S/`dual_i2s_manager`, VAD, latency/DMA tuning.
  - `@backend-developer`: Orchestrate capture → WebSocket → TTS → output; error recovery; task priorities.
  - `@frontend-developer`: CST9217 touch integration and real-time UI state/feedback.
- Safety: Agents must not run `idf.py flash`/`monitor`; request human action after a successful `idf.py build`.
- Example prompts: "@api-architect integrate WebSocket client with HowdyTTS protocol"; "@performance-optimizer reduce audio latency <50ms in audio_processor".
