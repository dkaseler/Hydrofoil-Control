# Hydrofoil Control — R5.1 experimental integration

This is a bench-tested development variant of [Tamas Nagy’s Hydrofoil Control](https://github.com/makszimilien/Hydrofoil-Control), based on upstream commit `313d65f16c7b0e84973f84f94e2e8b482160a663`. “R5” and “R5.1” are local development labels, not upstream release names.

The main aim is to keep the web interface and Wi-Fi service responsive when the capacitance sensor does not complete a reading. This integration also adds a separate control task, diagnostic recording, and explicit settings recovery. It is offered for review and selective incorporation. It is not an on-water validated release.

## Contents and behavior

- `src/`, `include/`, `data/` and `platformio.ini` are the unmodified source and build inputs from the R5.1 application tested on hardware.
- The normal controller is at `/`, testing and CSV recording at `/bench`, and role/peer setup at `/setup`.
- Sensor waits are bounded at approximately 2 ms. A failed reading invalidates the rolling window and suspends PID updates; 20 fresh readings are required before normal calculation resumes. The last commanded servo pulse is held during a normal-control sensor fault; it is not a boat-specific fail-safe policy.
- Normal acquisition targets 500 Hz with a 100 Hz PID cadence. The separate bench recorder targets 100 Hz for 30 seconds. Bench/test actions pause the controller and detach software servo output; resuming control is explicit.
- Static image/partition metadata is cached before serving diagnostics. UI files are embedded in the application, so application-only updates do not depend on replacing old filesystem HTML.
- The board provides its own `HF-Bench-<AP-MAC-without-colons>` access point in either role. This integration uses that direct access point; entering router credentials is not needed for its normal UI. HTTP and ArduinoOTA access are unauthenticated, intended for a controlled development environment.
- R5.1 adds only an explicit defaults-recovery endpoint and version labels relative to R5. It does not change R5’s sensor/control logic or master/slave packet layout. Defaults recovery requires paused bench mode, matching station MAC, and explicit confirmation; it remains paused and does not replace Wi-Fi or peer files. MAC matching protects against an accidental wrong target; it is not authentication.

## Build

Use Python 3 and PlatformIO Core. The tested build used PlatformIO Core 6.2.0, `espressif32@6.12.0`, Arduino ESP32 2.0.17 and the `esp32-c3-devkitm-1` environment.

```sh
python3 -m venv .venv
. .venv/bin/activate
python -m pip install platformio==6.2.0
pio run -e esp32-c3-devkitm-1
```

The application is produced at `.pio/build/esp32-c3-devkitm-1/firmware.bin`. The tested application was 997,024 bytes and used a 1,310,720-byte application slot. `BUILD-MANIFEST.json` records its hash, source hashes and the library versions resolved for that build.

The original `platformio.ini` library constraints are preserved. They permit compatible updates, so check the resolved libraries against the manifest when reproducing the tested environment. Identical binary hashes after a fresh build are not asserted; build paths, timestamps and dependency resolution can differ.

The supplied `data/wifi.json` has blank credentials and `data/addresses.json` has no peers. These are generic source defaults, not board backups. The existing controller’s measured calibration/settings are in its filesystem. This source package does not include an uploader or an instruction to overwrite that filesystem. Existing-device upgrades were application-only, after confirming chip, target identity, image and compatible partition capacity. Do not assume this application is compatible with an arbitrary ESP32 or partition layout.

## Software checks

After the PlatformIO build has resolved the dependencies, use Python 3, `clang++` with UndefinedBehaviorSanitizer, and Node.js:

```sh
python3 tests/run_controller_r5_tests.py
python3 tests/run_controller_r51_defaults_tests.py
python3 tests/run_standalone_tests.py
node tests/run_controller_r5_ui_tests.mjs
```

These tests compile the actual firmware routines into host harnesses, exercise parser/write failures and settings validation, and execute the shipped browser scripts against a simulated DOM. `tests/README.md` explains coverage and the packaging-only adaptations. They do not substitute for radio, timing or physical servo tests.

## Observed bench results and limits

On the R5 board, normal acquisition averaged about 499.60 Hz during a deliberately stalled HTTP request, with no sensor timeouts. A forced timeout left diagnostics accessible, and real readings recovered. A complete 30-second bench recording had 2,973 valid samples, no timeouts and all 2,954 populated median values recomputed correctly.

On a second board running this exact R5.1 application, normal acquisition produced 2,516 good readings over 5,055 ms (497.725 Hz), with no normal timeouts. The deliberate timeout check left HTTP access available; sensing and controller output generation recovered. All 33 factory defaults were written and read back after an explicit reset request. That board did not repeat the stalled-client or full recording tests.

Physical servo direction/travel, final sensor calibration, actual paired-board delivery, loaded control and on-water behavior have not been established by these checks. Factory calibration values are placeholders. The source retains some older bench-only comments from earlier development; the running behavior is the integrated controller described above. No original full-flash backup or guaranteed rollback is provided.

## Attribution and license

Original software copyright (c) 2025 Tamas Nagy. The upstream `LICENSE` is included verbatim. Although its heading says “MIT License,” it contains additional non-commercial-use, attribution and hardware-exclusion conditions; it should not be described as standard MIT. This package does not relicense the upstream software or include rights to the hardware design. Libraries are obtained through PlatformIO and retain their own licenses.
