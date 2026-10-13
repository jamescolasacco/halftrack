# Building Halftrack

Requires a C++20 toolchain, CMake 3.25+, and platform SDKs. macOS: Xcode 16.3+ command-line tools (AppleClang 17+). Windows: Visual Studio 2022 with Desktop development with C++ and Windows SDK. Linux: a C++20 compiler and the JUCE ALSA/JACK, X11, OpenGL, and font development packages listed in the CI workflow; headless testing also needs Xvfb.

After installing the platform toolchain, a clean checkout configures, builds, and runs acceptance tests with one command:

```sh
cmake --workflow --preset preview
```

The equivalent individual commands remain available:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel 3
ctest --test-dir build -C Release --output-on-failure
```

CMake downloads exact, SHA-256-verified JUCE and Tracktion revisions from their upstream GitHub repositories. There is no paid SDK or account required for the enabled build configuration. Native VST3 and macOS Audio Unit hosting are enabled. VST2 and CLAP are tracked separately and not advertised as implemented.

The application is produced in `build/Halftrack_artefacts/Release`. On macOS open `Halftrack.app`; on Windows run `Halftrack.exe`; on Linux run `Halftrack`. The macOS preview is locally built and unsigned for distribution; release signing/notarization is a later packaging step.

Run the application with `--self-test /absolute/output/directory` to execute generated-fixture acceptance tests. No microphone capture occurs in these tests. A test output directory may be reused, but should be dedicated to disposable fixtures.

Generate an original audio demo with `python3 scripts/create_demo.py /absolute/output/demo`, then launch with `--demo-dir /absolute/output/demo`. All demo audio is synthesized locally by the script; no vendor samples are needed.

Package a clean, committed build with `python3 scripts/package_preview.py`. This includes the application, licenses, verification notes, and complete corresponding source archives. macOS and Linux use a `.tar.gz` to preserve executable permissions; Windows uses a `.zip`. The package script rejects uncommitted tracked changes; build before packaging the same revision.

The CI Mac runner uses `macos-15`, whose default Xcode toolchain meets the pinned engine's C++20 requirements. Older AppleClang on the initial macOS 14 runner failed on aggregate initialization in upstream Tracktion. [Runner toolchain inventory](https://github.com/actions/runner-images/blob/main/images/macos/macos-15-Readme.md).
