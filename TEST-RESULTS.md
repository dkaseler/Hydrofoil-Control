# Package checks — October 20, 2026

The repository-root copy passed the controller, explicit defaults recovery, recorder/header and embedded UI suites. C++ harnesses used strict warnings and UndefinedBehaviorSanitizer. The controller suite used the existing tested R5.1 QuickPID/ArduinoJson dependency directory through `--dependency-root`; the package does not include that cache. Fresh dependency installation was not repeated.

All 25 source/build/data files recorded in `BUILD-MANIFEST.json` match the tested firmware inputs byte-for-byte. The upstream license is byte-for-byte identical. No firmware rebuild or hardware write was performed for packaging. Hardware results in README are prior observations and are identified by firmware version.

The source defaults contain empty credentials and no peers. Source and exact compiled application were checked for known private board identifiers, personal paths, router name and common token/private-key patterns; none were found. This is a bounded scan, not proof against every possible secret. Local board histories, captures, upload scripts and toolchain caches are omitted.

Test fixtures use synthetic addresses. Changes to the test harnesses are listed in `tests/README.md`; firmware source was not changed to package or run them.
