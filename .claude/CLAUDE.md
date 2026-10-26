# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What This Repository Is

raw-converter is a C++17 library, `rawconv`, and its command line tool, `raw-convert`, that convert camera raw files into DNG, as Adobe DNG Converter would, and into 16-bit TIFF, rendered and nothing more, repairing hot and dead pixels when asked. How it is built: [docs/ARCHITECTURE.md](../docs/ARCHITECTURE.md); how to use it: the [README](../README.md).

## Working Environment

The preferred way is the Docker container; inside it the scripts of `bin/` are on the `PATH` and aliased:

```bash
./docker/dock.sh raw-converter build
./docker/dock.sh raw-converter start
build       # bin/build.sh: configure and build in ./build
samples     # bin/fetch-samples.sh: download the sample raw files, once
test        # bin/test.sh: build, then ctest --output-on-failure
```

One test: `ctest --test-dir build -R test_bad_pixels --output-on-failure`. `test_samples` and `test_adobe_reference` need the samples; raw files of our own in `~/user-samples` are tested too. As a subproject, with `add_subdirectory()`, only the library is built: `RAWCONV_BUILD_APP`, `RAWCONV_BUILD_TESTS` and `RAWCONV_INSTALL` are on only at the top level.

## Architecture

The converter reads a raw file into one model, `RawImage`, repairs its bad pixels if asked, and writes it as a DNG, a TIFF, or both, from the same `RawImage`.

- **The DNG is the camera's raw data, plus what a raw editor needs to render it**: the sensor values bit for bit, described exactly as Adobe DNG Converter would. By default, with no repair, our DNG holds the same samples as Adobe's, masked border included; `test_adobe_reference` and `test_samples` hold us to that.
- **The TIFF is the image, rendered and nothing more**: demosaiced by LibRaw, white balanced as shot, in a colour space whose ICC profile it carries; no exposure, no tone curve, no sharpening.
- **LibRaw is included only by `raw_reader.cpp` and `renderer.cpp`**, Little CMS only by `tiff_output.cpp`. `tiff_writer` knows nothing of DNG, `lj92_encoder` nothing of TIFF.
- **`Converter` uses every part through an interface** (`RawDecoder`, `BadPixelDetector`, `DngEncoder`, `TiffEncoder`, `FileStore`), so its tests run with fakes; `LocalFileStore` writes atomically.
- Per-model values that are not in the raw file, e.g. the baseline exposure, go in `CameraTable`.

## Architecture and Review Documents

For the `architect` and `reviewer` skills: what they cannot find out alone.

- `docs/ARCHITECTURE.md` follows the two halves above: a DNG that matches Adobe's, and a TIFF that adds nothing. Its sections, in order: Introduction, The Big Picture, The Modules (`| Path | Role | Depends on |`), The Classes, SOLID and Where It Stops, Dependency Injection and Tests, The DNG File, Lossless JPEG, The TIFF File, Bad Pixels, Threads, Compared with Adobe DNG Converter (one subsection per camera body measured), The Container, Tests, How to Extend the Converter, Design Decisions and Trade-offs, Known Limitations.
- The review goes in `docs/Review.md`, the skill's default. Known Limitations and the LibRaw reader's quirks noted under Tests are documented: judge them, do not report them as bugs.
- Includes to count: LibRaw (`#include <libraw`) and Little CMS (`#include <lcms2`), which must stay in the files above; and the public headers of `include/rawconv/`, which must not name either.
- **Diagrams** follow the `beautiful-mermaid-diagrams` skill, not the blue style of `write-docs`: colours by role, compact, readable in light and dark mode. Each is shown as an image: its source is `docs/diagrams/<name>.mmd`, rendered by `bin/render-diagrams.sh` into an SVG beside it, outside the container (it needs Node.js and Chrome); commit both. `bin/check-diagrams.sh`, run by `bin/test.sh` and by `ci-format.yml`, fails when an SVG is older than its source. A class diagram shows no empty compartment: a class without fields or methods sets `hideEmptyMembersBox`, and classes with methods but no fields are drawn as a flowchart, each box a name and its methods (Mermaid always draws the fields box of a class that has methods).
- **Probes**: a throwaway test is a GoogleTest file in `tests/`, added to `tests/CMakeLists.txt`, built and run with `ctest --test-dir build -R <test> --output-on-failure`, in the container. A DNG is checked against `exiftool -validate`, as `tests/validate_with_exiftool.sh` does.

## Pull Request Notes

Each pull request has a notes file in [docs/pr/](../docs/pr/README.md): what we discussed, what we chose, and why. One file per pull request, written while the branch is open. Keep them simple: short sentences, one idea per paragraph, and a diagram when it explains faster than words.

## Conventions

- C++17, `-Wall -Wextra -Wpedantic`, `clang-format` with the repo's file, pre-commit hooks.
- Update the README (options, examples) and `docs/ARCHITECTURE.md` when an option, a tag written or a module changes.
