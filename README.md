# DARVC — RVC Voice Converter

DARVC is a JUCE-based VST3 (and macOS AU) plug-in that converts a vocal audio
file with an RVC (Retrieval-based Voice Conversion) `.pth` voice model,
directly from inside your DAW's plug-in window. Conversion runs **offline**:
the plug-in writes a job file and launches a Python worker process in the
background — inference never happens on the audio thread, and live audio
simply passes through.

## First-time setup (required once)

The RVC engine (PyTorch + [`rvc-python`](https://pypi.org/project/rvc-python/))
is several GB, so it is installed separately from the plug-in binary:

1. Install **Python 3.10, 3.11 or 3.12** from [python.org](https://www.python.org/downloads/)
   (on Windows, tick *"Add python.exe to PATH"*).
2. Open the plug-in in your DAW and go to **Settings**.
3. Pick your compute **Device** (Auto/CPU, or NVIDIA CUDA if you have a GPU).
4. Press **Install / Repair Engine** and wait for the download to finish
   (the base HuBERT/RMVPE models are also downloaded on the first conversion).
5. **Check Engine** should now report *"RVC engine ready"*.

If you already have a Python environment with `rvc-python` installed, just
point **Settings → Python** at that interpreter instead.

## Using it

1. **From File** — choose the vocal WAV/FLAC/MP3 you want to convert.
   (*From DAW* capture is not possible for generic VST3/AU plug-ins; bounce or
   render the track to a file first.)
2. Pick a voice model with **Browse**, or open **Models** to manage a library
   of models (add `.pth`/`.index` files or import a model `.zip`). A matching
   `.index` next to the `.pth` is detected automatically.
3. Adjust **Pitch Shift**, **Index Rate**, **Protect**, the F0 method and the
   model format (RVC v1/v2), then press **Convert**.
4. When it finishes, the waveform preview shows the result. Converted files
   are saved to the **Library** folder; open **Library** to browse, reveal,
   delete or reload them as input, or press **Reveal Output**.

Settings (folders, Python, device) persist between sessions in the DARVC
data folder (`%APPDATA%\DARVC` on Windows, `~/Library/DARVC` on macOS), where
the managed engine, models, library and job files also live.

> ⚠️ Only load `.pth` model files from sources you trust — PyTorch
> checkpoints can execute code when loaded. Use only voice models you have
> permission to use.

## Build locally

Requirements:
- CMake 3.22+
- Git
- A C++17 toolchain (Visual Studio 2022+ on Windows, Xcode command line tools on macOS)
- Internet access on the first configure so CMake FetchContent can fetch JUCE 8.0.4

Configure and build:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

On Windows, use a Visual Studio generator (for example `cmake -S . -B build -A x64`). On macOS this project builds ARM64 (`arm64`) in CI. Remove or change `CMAKE_OSX_ARCHITECTURES` if you need an Intel or universal build.

## GitHub Actions

The workflow in `.github/workflows/build.yml` runs on pushes to `main`/`master`, pull requests, and manual dispatch. After a successful run, open **Actions → Build DARVC → Artifacts** and download the Windows VST3 or macOS VST3/AU ZIP.

The CI workflow packages plug-in bundles for testing; it does not sign/notarize them for distribution. macOS installation may require code signing and notarization for a public release.

## Worker development

`worker/worker.py` implements the plug-in↔engine protocol (one JSON object
per stdout line; see `worker/job.schema.example.json`). It is embedded into
the plug-in binary and extracted to the data folder at runtime. Protocol
tests use a fake engine, so they need no GPU or models:

```bash
python -m unittest worker/test_worker.py
python worker/worker.py --check   # describe the current Python environment
```

## Branding asset

`assets/darvc-ui-reference.png` is the supplied visual reference, included for design context only. The interface itself is drawn in JUCE and does not use the image as its UI.
