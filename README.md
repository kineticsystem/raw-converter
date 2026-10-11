# Raw converter  <!-- omit in toc -->

[![CI](https://github.com/kineticsystem/raw-converter/actions/workflows/ci.yml/badge.svg)](https://github.com/kineticsystem/raw-converter/actions/workflows/ci.yml)
[![Format](https://github.com/kineticsystem/raw-converter/actions/workflows/ci-format.yml/badge.svg)](https://github.com/kineticsystem/raw-converter/actions/workflows/ci-format.yml)

## Table of Contents <!-- omit in toc -->

- [Introduction](#introduction)
- [Features](#features)
- [Prerequisites](#prerequisites)
- [Install raw-converter](#install-raw-converter)
  - [Check out the Git Repository](#check-out-the-git-repository)
  - [Pre-Commit Hooks](#pre-commit-hooks)
  - [Build the Project](#build-the-project)
- [Converting Files](#converting-files)
  - [Options](#options)
  - [Bad Pixels](#bad-pixels)
  - [Opening the DNG Files](#opening-the-dng-files)
  - [TIFF Files](#tiff-files)
- [Using the Library](#using-the-library)
  - [Adding It to a CMake Project](#adding-it-to-a-cmake-project)
  - [Converting from Code](#converting-from-code)
- [Adding a Camera](#adding-a-camera)
- [Tests](#tests)
  - [Our Own Raw Files](#our-own-raw-files)
- [Licence](#licence)
- [Continuous Integration](#continuous-integration)

## Introduction

raw-converter is a C++17 library, and a command line tool, that converts camera raw files into one of two files, or both:

- **DNG**, the raw data, for raw editors. Written as Adobe DNG Converter writes it: the same raw data, described with the same values, so that any raw editor that reads DNG renders it as it renders Adobe's.
- **TIFF**, the image, for programs that take pixels rather than raw data, e.g. a focus stacker whose result is graded afterwards. Demosaiced, white balanced as shot, linear, 16 bits per sample, in Rec. 2020, ProPhoto RGB or sRGB, with nothing else done to it.

On the way, it can find hot, stuck and dead pixels, and repair them in the raw data, so that both files have them repaired. This is off by default, because Adobe DNG Converter does not do it: its DNGs keep every bad pixel, and only Adobe's own raw editors hide them, when rendering.

It was written for the Canon EOS 5D Mark II and the Canon EOS 450D (Digital Rebel XSi, Kiss X2), whose CR2 files it is tested on, and converts the raw files of every camera with a Bayer sensor that [LibRaw](https://www.libraw.org) reads.

The tool takes a raw file or a folder of them, and an output folder. It writes DNG files by default, TIFF files with `--format tiff`, and both with `--format both`:

```bash
raw-convert ~/Pictures/2009-09-02 ~/Pictures/2009-09-02/dng
raw-convert --format tiff --bad-pixels image ~/Pictures/2009-09-02 ~/Pictures/2009-09-02/tiff
```

The library does the same from code, so that a bigger application, e.g. one that downloads the pictures from the camera, converts them as they arrive. See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for how it is built.

## Features

For both files:
- **Bad pixels repaired, on request.** With `--bad-pixels`, hot and dead pixels are replaced by the median of their same-colour neighbours, in the raw data, before either file is written. A faint defect is only repaired once photos of *different* scenes by the same camera body show it, so the details of a scene are left alone.
- **Their metadata.** The original EXIF and GPS data, plus the lens model and the serial number as EXIF 2.3 fields.
- **Fast.** Several files converted in parallel: about 2.5 seconds for the seven sample files (154 MB of CR2) on a 12-core computer, to DNG. A TIFF takes about 1.2 seconds per 5D Mark II file, on one core.
- **Open source.** MIT licence, with only MIT-compatible dependencies. No Adobe code.

The DNG:
- **The raw data, untouched.** The whole frame of the sensor, masked border included, is carried across bit for bit, and compressed with lossless JPEG, as Adobe DNG Converter does. The tests read each DNG back with LibRaw and compare it with the original file, sample by sample.
- **Described as Adobe describes it.** The DNG says what a raw editor needs, with Adobe's values:
  - the camera's name as Adobe knows it, by which raw editors find their profiles for the camera;
  - Adobe's colour matrix and the as-shot white balance;
  - the active area of the sensor, the camera's own crop and its orientation;
  - the black and white levels;
  - the baseline exposure, noise and sharpness.

  The tests compare the DNG of a 5D Mark III file with the one Adobe DNG Converter made of it. Ten 5D Mark II files were compared with Adobe's DNGs of them in the same way: the same samples, bit for bit, and the same tags that define the image.
- **The Canon maker note**, which ExifTool and photo managers still read.
- **Repaired for every raw editor.** The repaired pixels replace the bad ones in the raw data, so that the DNG looks right whatever opens it; or, with `--repair opcode`, are listed in a DNG opcode for the raw editors that apply it.

The TIFF:
- **The image, linear.** Demosaiced by LibRaw (AHD), with the camera's white balance and colour matrix, and no exposure, tone curve, sharpening or noise reduction: twice the light is twice the value.
- **The camera's crop**, e.g. 5616 × 3744 pixels for the 5D Mark II, as the sensor saw it, with the orientation as a tag.
- **Its colour space described.** Rec. 2020 by default, or ProPhoto RGB or sRGB, with its ICC profile embedded. 16 bits per sample, uncompressed.

## Prerequisites

To build and run raw-converter, we need a computer with Docker; nothing else is installed on it. Please refer to the document [Install Docker Engine on Ubuntu](https://docs.docker.com/engine/install/ubuntu/), or run:

```bash
curl -fsSL https://get.docker.com -o get-docker.sh
sudo sh get-docker.sh
```

## Install raw-converter

### Check out the Git Repository

```bash
git clone git@github.com:kineticsystem/raw-converter.git
cd raw-converter
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
./docker/dock.sh raw-converter build
./docker/dock.sh raw-converter start
```

In the container, the repo is in `~/ws`, and the scripts in `bin` are on the `PATH`, with short aliases:

```bash
build      # bin/build.sh: configure and build in ./build
samples    # bin/fetch-samples.sh: download the sample raw files, once
test       # bin/test.sh: build, then run every test
```

The tool is then `build/apps/raw-convert/raw-convert`, which is on the `PATH` in the container too.

## Converting Files

```bash
raw-convert [options] <raw file or folder> <output folder>
```

Each file is named after its raw file, e.g. `IMG_1069.CR2` becomes `IMG_1069.dng` and `IMG_1069.tif`. A file that already exists is not written again, unless `--overwrite` is given, and a raw file whose files all exist is skipped. For example, in the container:

```bash
samples
raw-convert samples /tmp/dng
```

```
[1/7] samples/Canon_EOS_450D.CR2 -> /tmp/dng/Canon_EOS_450D.dng
...
7 converted, 0 skipped, 0 failed
```

The tool exits with 0 when every file was converted or skipped, 1 when one failed, 2 on a bad command line, and 130 when stopped with Ctrl-C. On Ctrl-C, the files being converted are finished, and no other is started.

### Options

| Option | What it does |
|---|---|
| `-r`, `--recursive` | Also convert the raw files in sub-folders, mirroring them in the output folder. |
| `--format FORMAT` | The files written for each raw file. `dng` (default): the DNG. `tiff`: a TIFF, see [TIFF Files](#tiff-files). `both`: the DNG and the TIFF. |
| `--colour-space SPACE` | The colour space of the TIFF files, all linear. `rec2020` (default): ITU-R BT.2020. `prophoto`: ProPhoto RGB. `srgb`: the sRGB primaries, which clip saturated colours. |
| `-j N`, `--jobs N` | Files converted in parallel. Default: one per core, at most 8, as each takes about 150 MB, and about 600 MB with a TIFF. |
| `--bad-pixels MODE` | `off` (default): leave the pixels as they are, as Adobe DNG Converter does. `image`: repair the bad pixels of each file, found in it on its own. `batch`: also learn each camera's defects from all the files, and repair them in all of that camera's files. `auto`: `batch` for 3 files or more, `image` otherwise. |
| `--repair METHOD` | How `--bad-pixels` repairs them. `interpolate` (default): replace the pixels in the raw data, so that every raw editor shows them repaired. `opcode`: keep the raw data as it is, and list the pixels in a DNG `FixBadPixelsList` opcode, which only raw editors that support DNG opcodes apply. |
| `--defect-map FILE` | Known defects, repaired in addition to those found. The file is created, or updated with what the run learns, for the next run. Needs `--bad-pixels`. |
| `--uncompressed` | Do not compress the raw data. The files are about twice as large. |
| `--no-preview` | Do not keep the camera's JPEG preview. |
| `--overwrite` | Replace the files that exist. |
| `-q`, `--quiet` | Only print errors. |

### Bad Pixels

By default, the bad pixels stay as the camera recorded them, as in Adobe's DNGs. `--bad-pixels` repairs them in the active area of the raw data, and leaves every other sample as it is.

A hot pixel is a single pixel brighter than the light falling on it; a dead one stays dark. The converter compares each pixel with its nearest pixels of the same colour, in units of the photo's own noise, so the same settings work at every ISO. Detail of the scene, even a star, spreads over several pixels through the lens, and is left alone.

A pixel far out of line is repaired in any case. One only somewhat out of line could also be detail: it is repaired once photos of at least two *different* scenes by the same camera body show it. A series of shots of the same scene, e.g. a bracket or a time-lapse, counts as one. That is what `batch` mode does with a folder.

To keep what is learnt from one folder for the next, give a defect map, e.g. one per camera body:

```bash
raw-convert --bad-pixels batch --defect-map ~/5d-mark-ii-defects.txt ~/Pictures/2009-09-02 ~/Pictures/dng
```

### Opening the DNG Files

Any raw editor that reads DNG opens the files. They carry the camera name, colour matrix, white balance, crop, orientation and baseline values of Adobe's DNG of the same raw file, so a raw editor that has a profile for the camera finds it, and renders the photo as it renders Adobe's DNG. By default, the raw data is the same as in Adobe's DNG, sample for sample, bad pixels included: Adobe's raw editors hide hot pixels when rendering, and other raw editors may show them. Converted with `--bad-pixels`, the files have them repaired whatever opens them.

One difference with Adobe DNG Converter remains for the 450D: Adobe adds a per-camera **baseline exposure**, and we do not know its value yet. Its photos open a little darker than Adobe's DNGs, typically by 0.25 to 0.5 EV, until its row is added: see [Adding a Camera](#adding-a-camera). The 5D Mark II and the 5D Mark III have their rows, read from Adobe's own DNGs.

### TIFF Files

```bash
raw-convert --format tiff --bad-pixels image IMG_1069.CR2 out
```

`out/IMG_1069.tif` is the camera's crop of the image, e.g. 5616 × 3744 pixels for the 5D Mark II, demosaiced with AHD. Its values are linear, the light the sensor measured: twice the light is twice the value. Nothing is added to them: no exposure, no tone curve, no sharpening, no noise reduction. Opened as it is, the image looks dark and flat, as a raw file does before it is edited.

- **White balance:** the camera's, as shot.
- **Colour:** the camera's colour matrix, to the colour space of `--colour-space`, whose ICC profile the file embeds. Rec. 2020, the default, holds nearly every colour a camera records. In sRGB, saturated colours, e.g. of flowers or lights, are clipped.
- **Levels:** the sensor's clipping level is 65535, for the colour the white balance amplifies least. A highlight the sensor clipped is 65535 in every colour: white.
- **Orientation:** the pixels are as the sensor saw them, and the file's Orientation tag says how to turn them upright, as the camera recorded it.
- **Bad pixels:** repaired with `--bad-pixels`, with `--repair opcode` too.
- **Metadata:** the original's EXIF and GPS data, with the colour space marked "uncalibrated", as it is in the ICC profile rather than sRGB.

The files are uncompressed, about 6 bytes per pixel: 126 MB for the 5D Mark II.

## Using the Library

### Adding It to a CMake Project

With the repo as a sub-folder of the project, e.g. a Git submodule in `modules/raw-converter`:

```cmake
add_subdirectory(modules/raw-converter)
target_link_libraries(my_app PRIVATE rawconv::rawconv)
```

Or fetched by CMake:

```cmake
include(FetchContent)
FetchContent_Declare(rawconv GIT_REPOSITORY https://github.com/kineticsystem/raw-converter.git GIT_TAG main)
FetchContent_MakeAvailable(rawconv)
target_link_libraries(my_app PRIVATE rawconv::rawconv)
```

As a sub-project, it builds only the library: the tool and the tests are off. Installed with `cmake --install build`, it is found with `find_package(rawconv REQUIRED)`. In all three cases, LibRaw and Little CMS must be installed, e.g. with `apt install libraw-dev liblcms2-dev` on Ubuntu.

### Converting from Code

```cpp
#include <rawconv/rawconv.hpp>

// A folder, as the tool does: the raw data as the camera recorded it.
rawconv::Converter converter;
for (const auto& result : converter.convert(rawconv::planJobs("in", "out")))
{
  if (result.status == rawconv::ConvertResult::Status::FAILED)
  {
    std::cerr << result.input << ": " << result.error << '\n';
  }
}

// A file already in memory, e.g. just downloaded from the camera, with its
// bad pixels repaired.
rawconv::ConvertOptions options;
options.bad_pixels = rawconv::BadPixelMode::PER_IMAGE;
rawconv::Converter repairing(options);
rawconv::RawImage image = rawconv::readRawBuffer(data, size, "IMG_0001.CR2");
repairing.repair(image);
rawconv::writeDng(image, "out/IMG_0001.dng");

// The same image, demosaiced, as a linear 16-bit TIFF; or rendered in memory.
rawconv::writeTiff(image, "out/IMG_0001.tif");
rawconv::RenderedImage rendered = rawconv::renderImage(image, rawconv::ColorSpace::REC2020);

// A folder, to DNG and TIFF.
options.format = rawconv::OutputFormat::DNG_AND_TIFF;
rawconv::Converter both(options);
both.convert(rawconv::planJobs("in", "out"));
```

`Converter` takes its options and, optionally, the services it works with: the raw decoder, the bad pixel detector, the DNG encoder, the file store and the TIFF encoder. An application can replace any of them, e.g. to read the files from a camera or to write them elsewhere. `Converter::convert` takes a progress callback, and `Converter::cancel()` stops a batch from any thread. See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md#dependency-injection-and-tests).

## Adding a Camera

Any camera that LibRaw reads converts as it is. For its DNGs to be the same as Adobe's, the converter needs the values Adobe DNG Converter takes from its own table of cameras: without them, the photos open darker, and the active area may differ from Adobe's by a row or a column. Convert one raw file of the camera with Adobe DNG Converter, then read them:

```bash
exiftool -n -UniqueCameraModel -BaselineExposure -BaselineNoise -BaselineSharpness -BayerGreenSplit -SubIFD:ActiveArea IMG_0001.dng
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
./docker/dock.sh raw-converter build
```

The folder is mounted read only at `~/user-samples`, and `test` converts its raw files with the samples. Nothing is written into it.

## Licence

raw-converter is under the [MIT licence](LICENSE). The third-party software it uses, and why each licence is compatible, is listed in [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md). LibRaw is linked dynamically, under the LGPL-2.1. The sample raw files are in the public domain (CC0) and are downloaded, not committed.

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
