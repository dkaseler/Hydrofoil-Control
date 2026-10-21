# Host checks

Run the commands in the repository README after a PlatformIO build resolves `QuickPID` and `ArduinoJson` into `.pio/libdeps/esp32-c3-devkitm-1/`. The controller test also accepts `--dependency-root PATH` to an existing environment dependency directory. It does not install or copy dependencies.

- `run_controller_r5_tests.py`: bounded sensor read, invalidation/recovery, median20, actual QuickPID scaling, real filesystem parser/writer, validation, receiver timeout, bench/control transitions, settings, ESP-NOW receive queuing, control task structure and diagnostic caching.
- `run_controller_r51_defaults_tests.py`: actual explicit recovery handler with instrumented persistence primitives, target and confirmation guards, write/read failure, all 33 readback fields, flag preservation and detached output.
- `run_standalone_tests.py`: actual recorder and sensor-wait headers, buffer bounds, statistics, warm-up, recording completion and timeout boundary behavior.
- `run_controller_r5_ui_tests.mjs`: source/embedded-asset equality, browser script syntax, no save on initial load, role/board switching, rejected saves and recorder reconnection.

C++ harnesses use `-Wall -Wextra -Werror` and UndefinedBehaviorSanitizer. GPIO, networking and selected Arduino primitives are stubbed. UI checks use a minimal simulated DOM. No script in this folder contacts a board or uploads firmware.

Packaging changes to these test harnesses are limited to repository-root layout, dependency lookup, default project selection and synthetic locally administered MAC fixtures. Test scenarios are retained. The firmware source is unchanged. The standalone test runner is a new convenience wrapper around the unchanged C++ cases.
