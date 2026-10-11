# Third-Party Licences

raw-converter is under the [MIT licence](LICENSE). This page lists all the third-party software it uses: what it is used for, its licence, and why that licence is compatible with MIT. Nothing listed here is copied into this repository. The libraries are installed from Ubuntu's packages, and the sample files are downloaded by a script.

## Linked into the library and the tool

These are what an application that uses `librawconv`, or ships `raw-convert`, distributes alongside the code.

| Software | Used for | Licence | Compatible with MIT because |
|---|---|---|---|
| [LibRaw](https://www.libraw.org) 0.21 | Decoding the raw files, and demosaicing them for the TIFF files | LGPL-2.1 **or** CDDL-1.0, at the user's choice | Linked dynamically, as `libraw_r.so`. Under the LGPL, a program that links it dynamically may have any licence, as long as LibRaw itself stays replaceable and its own licence comes with any copy of it that is distributed. Linking it statically is also allowed under the CDDL, which only covers LibRaw's own files. |
| [Little CMS](https://www.littlecms.com) 2 | Making the ICC profiles of the TIFF files; colour management, used by LibRaw | MIT | The same licence. |
| [libjpeg-turbo](https://libjpeg-turbo.org) | Decoding JPEG-compressed raw data, used by LibRaw | IJG and BSD-3-Clause, plus zlib for parts | Permissive. A binary distribution must credit the Independent JPEG Group in its documentation. |
| libgomp, libstdc++, libgcc | GCC's runtime libraries, used by LibRaw and the code | GPL-3 with the GCC Runtime Library Exception | The exception lets programs of any licence link them. |

The Ubuntu package of LibRaw includes no GPL code. The old GPL "demosaic packs" were removed from LibRaw in version 0.20. Debian's copyright file also lists a CC-BY-SA-3.0 data file, but it belongs to LibRaw's optional [RawSpeed](https://github.com/darktable-org/rawspeed) integration. That integration is not built in (`libraw_r.so` only has its empty hooks), so neither RawSpeed nor that file is part of the library.

The DNG and TIFF writers, the lossless JPEG encoder, the TIFF reader and writer, and the bad pixel detector are this project's own code, under MIT.

## Used only to build and test

None of these is part of what the project ships.

| Software | Used for | Licence |
|---|---|---|
| [GoogleTest](https://github.com/google/googletest) | The unit and end-to-end tests | BSD-3-Clause |
| [CMake](https://cmake.org), [Ninja](https://ninja-build.org) | Building | BSD-3-Clause, Apache-2.0 |
| [ExifTool](https://exiftool.org) | Checking the structure of the DNG files in the tests, as a separate program | Artistic or GPL, as Perl. It is run as a tool and never linked, so its licence does not apply to this project. |
| [Ubuntu 24.04](https://ubuntu.com) Docker image | The development container | Various free licences |

## Sample files

`bin/fetch-samples.sh` downloads raw files from [raw.pixls.us](https://raw.pixls.us) for the tests. They are all in the public domain under [CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/), and they are not committed to the repository.

## Not used

The [Adobe DNG SDK](https://helpx.adobe.com/camera-raw/digital-negative.html) is Adobe's reference implementation of DNG. It is published under Adobe's own licence agreement, which is not an OSI-approved open-source licence. So this project uses none of it, not even as a test tool. The DNG files are written from the published DNG specification, and checked with LibRaw and ExifTool.
