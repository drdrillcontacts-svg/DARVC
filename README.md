# DARVC — RVC Voice Converter (UI + CI starter)

DARVC is a JUCE-based VST3 plugin UI prototype inspired by the provided dark, premium neon reference. GitHub Actions builds Windows VST3 and macOS ARM64 VST3/AU bundles.

## Important scope note

**This repository is a buildable UI/plug-in scaffold, not a working RVC converter yet.** The plugin currently passes audio through and the Convert button reports that inference is not connected. `worker/worker.py` is a placeholder. No conversion is claimed to have occurred.

The UI currently supports choosing an audio file and choosing a `.pth` model file. “From DAW” is a visual workflow placeholder; generic VST3/AU plugins cannot directly read arbitrary selected tracks from every DAW. A real DAW-capture workflow needs a DAW-specific integration or a deliberate record/render workflow.

## Build locally

Requirements:
- CMake 3.22+
- Git
- A C++17 toolchain (Visual Studio 2022 on Windows, Xcode command line tools on macOS)
- Internet access on the first configure so CMake FetchContent can fetch JUCE 8.0.4

Configure and build:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

On Windows, use a Visual Studio generator (for example `cmake -S . -B build -A x64`). On macOS this project builds ARM64 (`arm64`) in CI. Remove or change `CMAKE_OSX_ARCHITECTURES` if you need an Intel or universal build.

## GitHub Actions

Push this folder to a GitHub repository. The workflow in `.github/workflows/build.yml` runs on pushes to `main`/`master`, pull requests, and manual dispatch. After a successful run, open **Actions → Build DARVC → Artifacts** and download the Windows VST3 or macOS VST3/AU ZIP.

The CI workflow packages plugin bundles for testing; it does not sign/notarize them for distribution. macOS installation may require code signing and notarization for a public release.

## Planned RVC integration

1. Pin and review a specific RVC inference implementation and its license.
2. Confirm its exact CLI/API and model compatibility (PTH, optional INDEX, F0 extractor).
3. Add a job protocol with validated JSON and unique per-job temporary directories.
4. Launch the Python worker on a background thread/process, never from the audio callback.
5. Verify the output WAV, report progress/errors, support cancellation, and test CPU/GPU fallback.
6. Bundle Python and dependencies separately for Windows/macOS after testing redistribution licenses and GPU support.

Do not load untrusted model files. Use only voice models you have permission to use and distribute.

## Branding asset

`assets/darvc-ui-reference.png` is the supplied visual reference, included for design context only. The interface itself is drawn in JUCE and does not use the image as its UI.
