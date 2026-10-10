# DNG converter  <!-- omit in toc -->

[![CI](https://github.com/kineticsystem/dng-converter/actions/workflows/ci.yml/badge.svg)](https://github.com/kineticsystem/dng-converter/actions/workflows/ci.yml)
[![Format](https://github.com/kineticsystem/dng-converter/actions/workflows/ci-format.yml/badge.svg)](https://github.com/kineticsystem/dng-converter/actions/workflows/ci-format.yml)

## Table of Contents <!-- omit in toc -->

- [Introduction](#introduction)
- [Features](#features)
- [Prerequisites](#prerequisites)
- [Install dng-converter](#install-dng-converter)
  - [Check out the Git Repository](#check-out-the-git-repository)
  - [Pre-Commit Hooks](#pre-commit-hooks)
  - [Build the Project](#build-the-project)
- [Converting Files](#converting-files)
  - [Options](#options)
  - [Bad Pixels](#bad-pixels)
  - [In Lightroom](#in-lightroom)
- [Using the Library](#using-the-library)
  - [Adding It to a CMake Project](#adding-it-to-a-cmake-project)
  - [Converting from Code](#converting-from-code)
- [Adding a Camera](#adding-a-camera)
- [Tests](#tests)
  - [Our Own Raw Files](#our-own-raw-files)
- [Licence](#licence)
- [Continuous Integration](#continuous-integration)

## Introduction

dng-converter is a C++17 library, and a command line tool, that converts camera raw files to DNG files that Adobe Lightroom opens and edits as it does those of Adobe DNG Converter. It was written for the Canon EOS 5D Mark II and the Canon EOS 450D (Digital Rebel XSi, Kiss X2), whose CR2 files it is tested on, and converts the raw files of every camera with a Bayer sensor that [LibRaw](https://www.libraw.org) reads.

It also finds hot, stuck and dead pixels, as Adobe's software does, and either tells Lightroom to repair them or repairs them itself.

The tool takes a raw file or a folder of them, and an output folder:

```bash
dng-convert ~/Pictures/2009-09-02 ~/Pictures/2009-09-02/dng
```

The library does the same from code, so that a bigger application, e.g. one that downloads the pictures from the camera, converts them as they arrive. See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for how it is built.

## Features

- **The raw data, untouched.** Every sample is carried across bit for bit, compressed with lossless JPEG as Adobe DNG Converter does. The tests read each DNG back with LibRaw and compare it with the original file.
- **Rendered as Adobe renders it.** The DNG says what Lightroom needs, with Adobe's values:
  - the camera's name as Adobe knows it, by which Lightroom finds its Adobe Standard and Camera Matching profiles;
  - Adobe's colour matrix and the as-shot white balance;
  - the black and white levels, and the camera's own crop;
  - the baseline exposure, noise and sharpness.

  The tests compare the DNG of a 5D Mark III file with the one Adobe DNG Converter made of it.
- **Its metadata.** The original EXIF and GPS data, and the Canon maker note, which ExifTool and Lightroom still read, plus the lens model and the serial number as EXIF 2.3 fields.
- **Bad pixels found, by default never overwritten.** Hot and dead pixels are written as a DNG opcode, which Lightroom applies when it renders the photo, while the original values stay in the file. A faint defect is only repaired once photos of *different* scenes by the same camera body show it, so the details of a scene are left alone.
- **Fast.** Several files converted in parallel: about 2.5 seconds for the seven sample files (154 MB of CR2) on a 12-core computer.
- **Open source.** MIT licence, with only MIT-compatible dependencies. No Adobe code.

## Prerequisites

To build and run dng-converter, we need a computer with Docker; nothing else is installed on it. Please refer to the document [Install Docker Engine on Ubuntu](https://docs.docker.com/engine/install/ubuntu/), or run:

```bash
curl -fsSL https://get.docker.com -o get-docker.sh
sudo sh get-docker.sh
```

## Install dng-converter

### Check out the Git Repository

```bash
git clone git@github.com:kineticsystem/dng-converter.git
cd dng-converter
```

### Pre-Commit Hooks

Additionally, you should install git pre-commit hooks using the following command:

```
pre-commit install
```

The hooks, in [`.pre-commit-config.yaml`](.pre-commit-config.yaml), are:
- the standard checks of files and whitespace;
- codespell;
- clang-format, with the [`.clang-format`](.clang-format) of the StepIt projects.

clang-format runs as installed, so it is best to commit from the container, which has `pre-commit` and clang-format 18, the version the Format workflow uses. `pre-commit run -a` checks every file.

### Build the Project

The project is built and tested inside a Docker container, as explained in [docker/README.md](docker/README.md). Create the container and open a shell in it:

```bash
./docker/dock.sh dng-converter build
./docker/dock.sh dng-converter start
```

In the container, the repo is in `~/ws`, and the scripts in `bin` are on the `PATH`, with short aliases:

```bash
build      # bin/build.sh: configure and build in ./build
samples    # bin/fetch-samples.sh: download the sample raw files, once
test       # bin/test.sh: build, then run every test
```

The tool is then `build/apps/dng-convert/dng-convert`, which is on the `PATH` in the container too.

## Converting Files

```bash
dng-convert [options] <raw file or folder> <output folder>
```

Each DNG is named after its raw file, e.g. `IMG_1069.CR2` becomes `IMG_1069.dng`. A DNG that already exists is skipped, unless `--overwrite` is given. For example, in the container:

```bash
samples
dng-convert samples /tmp/dng
```

```
[1/7] samples/Canon_EOS_450D.CR2 -> /tmp/dng/Canon_EOS_450D.dng (2 bad pixels)
...
7 converted, 0 skipped, 0 failed
```

The tool exits with 0 when every file was converted or skipped, 1 when one failed, 2 on a bad command line, and 130 when stopped with Ctrl-C. On Ctrl-C, the files being converted are finished, and no other is started.

### Options

| Option | What it does |
|---|---|
| `-r`, `--recursive` | Also convert the raw files in sub-folders, mirroring them in the output folder. |
| `-j N`, `--jobs N` | Files converted in parallel. Default: one per core, at most 8, as each takes about 150 MB. |
| `--bad-pixels MODE` | `image`: find bad pixels in each file on its own. `batch`: also learn each camera's defects from all the files, and repair them in all of that camera's files. `off`: leave the pixels as they are. Default: `batch` for 3 files or more, `image` otherwise. |
| `--repair METHOD` | `opcode` (default): keep the raw data, and tell the raw converter to repair the pixels. `interpolate`: replace them in the raw data, for raw converters that ignore DNG opcodes. |
| `--defect-map FILE` | Known defects, repaired in addition to those found. The file is created, or updated with what the run learns, for the next run. |
| `--uncompressed` | Do not compress the raw data. The files are about twice as large. |
| `--no-preview` | Do not keep the camera's JPEG preview. |
| `--overwrite` | Replace DNG files that exist. |
| `-q`, `--quiet` | Only print errors. |

### Bad Pixels

A hot pixel is a single pixel brighter than the light falling on it; a dead one stays dark. The converter compares each pixel with its nearest pixels of the same colour, in units of the photo's own noise, so the same settings work at every ISO. Detail of the scene, even a star, spreads over several pixels through the lens, and is left alone.

A pixel far out of line is repaired in any case. One only somewhat out of line could also be detail: it is repaired once photos of at least two *different* scenes by the same camera body show it. A series of shots of the same scene, e.g. a bracket or a time-lapse, counts as one. That is what `batch` mode does with a folder.

To keep what is learnt from one folder for the next, give a defect map, e.g. one per camera body:

```bash
dng-convert --defect-map ~/5d-mark-ii-defects.txt ~/Pictures/2009-09-02 ~/Pictures/dng
```

### In Lightroom

Import the DNG files as usual. Lightroom recognises the camera, and offers the Adobe Standard and Camera Matching profiles as it does for the CR2 files. It applies the bad-pixel opcode when it renders the photos.

One difference with Adobe DNG Converter remains for the 5D Mark II and the 450D: Adobe adds a per-camera **baseline exposure**, and we do not know their values yet. Their photos may open a little darker than Adobe's DNGs, typically by 0.25 to 0.5 EV, until their row is added: see [Adding a Camera](#adding-a-camera). The 5D Mark III has its row, verified against Adobe's own DNG.

## Using the Library

### Adding It to a CMake Project

With the repo as a sub-folder of the project, e.g. a Git submodule in `modules/dng-converter`:

```cmake
add_subdirectory(modules/dng-converter)
target_link_libraries(my_app PRIVATE dngconv::dngconv)
```

Or fetched by CMake:

```cmake
include(FetchContent)
FetchContent_Declare(dngconv GIT_REPOSITORY https://github.com/kineticsystem/dng-converter.git GIT_TAG main)
FetchContent_MakeAvailable(dngconv)
target_link_libraries(my_app PRIVATE dngconv::dngconv)
```

As a sub-project, it builds only the library: the tool and the tests are off. Installed with `cmake --install build`, it is found with `find_package(dngconv REQUIRED)`. In all three cases, LibRaw must be installed, e.g. with `apt install libraw-dev` on Ubuntu.

### Converting from Code

```cpp
#include <dngconv/dngconv.hpp>

// A folder, as the tool does.
dngconv::Converter converter;
for (const auto& result : converter.convert(dngconv::planJobs("in", "out")))
{
  if (result.status == dngconv::ConvertResult::Status::FAILED)
  {
    std::cerr << result.input << ": " << result.error << '\n';
  }
}

// A file already in memory, e.g. just downloaded from the camera.
dngconv::RawImage image = dngconv::readRawBuffer(data, size, "IMG_0001.CR2");
converter.repair(image);
dngconv::writeDng(image, "out/IMG_0001.dng");
```

`Converter` takes its options and, optionally, the services it works with: the raw decoder, the bad pixel detector, the DNG encoder and the file store. An application can replace any of them, e.g. to read the files from a camera or to write them elsewhere. `Converter::convert` takes a progress callback, and `Converter::cancel()` stops a batch from any thread. See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md#dependency-injection-and-tests).

## Adding a Camera

Any camera that LibRaw reads converts as it is. For its photos to open in Lightroom exactly as bright as Adobe's DNGs do, the converter needs the values Adobe DNG Converter takes from its own table of cameras. Convert one raw file of the camera with Adobe DNG Converter, then read them:

```bash
exiftool -n -UniqueCameraModel -BaselineExposure -BaselineNoise -BaselineSharpness -BayerGreenSplit IMG_0001.dng
```

Then add a row to [`src/camera_table.cpp`](src/camera_table.cpp), with where the values come from. An application can also add rows at run time, to a `CameraTable` it gives to `LibRawDecoder`.

## Tests

In the container:

```bash
samples   # once: the sample raw files, from raw.pixls.us
test
```

The tests convert random images at awkward sizes, and the sample raw files: 5D Mark II (RAW, sRAW1 and sRAW2), 450D and Digital Rebel XSi, and a 5D Mark III. The 5D Mark III comes with the DNG Adobe DNG Converter made of it. LibRaw reads every DNG back and compares it with the original file, and ExifTool validates every DNG. The fakes in `tests/test_support.hpp` test the converter's logic without files. See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md#tests) for the list.

### Our Own Raw Files

To run the end-to-end tests on our own photos too, put their folder in `docker/.env`, which is not versioned, and rebuild the container:

```bash
echo "DNG_USER_SAMPLES=$HOME/Pictures/cr2" > docker/.env
./docker/dock.sh dng-converter build
```

The folder is mounted read only at `~/user-samples`, and `test` converts its raw files with the samples. Nothing is written into it.

## Licence

dng-converter is under the [MIT licence](LICENSE). The third-party software it uses, and why each licence is compatible, is listed in [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md). LibRaw is linked dynamically, under the LGPL-2.1. The sample raw files are in the public domain (CC0) and are downloaded, not committed.

## Continuous Integration

Two GitHub Actions workflows run on every push and pull request:

| Workflow | What it checks |
|---|---|
| [`ci.yml`](.github/workflows/ci.yml) | Builds the library, the tool and the tests, and runs the tests. |
| [`ci-format.yml`](.github/workflows/ci-format.yml) | The pre-commit hooks, on every file: the checks of files and whitespace, codespell and clang-format. |

`ci.yml` runs in the same container as development:
- it builds the image of [`docker/Dockerfile`](docker/Dockerfile), with its layers kept in the Actions cache;
- it downloads the sample raw files, also cached, until `bin/fetch-samples.sh` changes;
- it runs `bin/test.sh` in the container.

Our own raw files are not there, so CI runs every test but those on `~/user-samples`. When a test fails, its log is kept as an artifact of the run.
