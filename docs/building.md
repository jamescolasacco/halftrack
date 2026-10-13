# Building Halftrack

Requires a C++20 toolchain, CMake 3.24+, and platform SDKs. macOS: Xcode command-line tools. Windows: Visual Studio 2022 with Desktop development with C++ and Windows SDK.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel 3
ctest --test-dir build -C Release --output-on-failure
```

CMake downloads exact, SHA-256-verified JUCE and Tracktion revisions from their upstream GitHub repositories. There is no paid SDK or account required for the enabled build configuration. Native VST3 and macOS Audio Unit hosting are enabled. VST2 and CLAP are tracked separately and not advertised as implemented.

The application is produced in `build/Halftrack_artefacts/Release`. On macOS open `Halftrack.app`; on Windows run `Halftrack.exe`. The macOS preview is locally built and unsigned for distribution; release signing/notarization is a later packaging step.

Run the application with `--self-test /absolute/output/directory` to execute generated-fixture acceptance tests. No microphone capture occurs in these tests. A test output directory may be reused, but should be dedicated to disposable fixtures.

Generate an original audio demo with `python3 scripts/create_demo.py /absolute/output/demo`, then launch with `--demo-dir /absolute/output/demo`. All demo audio is synthesized locally by the script; no vendor samples are needed.
