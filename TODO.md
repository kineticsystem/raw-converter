# TODO

Where we are, what is needed from Giovanni, and what comes next. Last updated 2026-10-10.

## Where we are

- The library, `dng-convert` and 64 tests build and pass in the container: `./docker/dock.sh dng-converter exec test.sh`.
- Our DNG of the public 5D Mark III sample has the same raw data as Adobe DNG Converter 9.8's DNG of the same file, and the same crop, colour matrix, white balance and camera traits (`tests/test_adobe_reference.cpp`).
- `docs/ARCHITECTURE.md`, `README.md`, `docker/README.md` and `THIRD_PARTY_LICENSES.md` are written.
- Nothing has been opened in Lightroom yet.
- The folder is not a Git repository yet.

## Needed from Giovanni

- [ ] **Adobe DNGs of the 5D Mark II and the 450D.** Convert one raw file of each camera with Adobe DNG Converter, e.g. `~/Downloads/cr2/IMG_1069.CR2`, with the default settings (lossless compression). Then either put the DNGs somewhere we can read them, or send the output of:
  ```bash
  exiftool -n -UniqueCameraModel -BaselineExposure -BaselineNoise -BaselineSharpness -BayerGreenSplit -WhiteLevel -DNGVersion file.dng
  ```
  Without these values, their photos open darker in Lightroom than Adobe's DNGs, probably by 0.25 to 0.5 EV.
- [ ] **Try the DNGs in Lightroom.** In the container, run `dng-convert ~/user-samples /home/developer/ws/build/out/user`, which writes the DNGs of `~/Downloads/cr2` to `build/out/user` in the repo. Import them in Lightroom next to Adobe's DNGs of the same files, then compare:
  - brightness;
  - colour, with the Adobe Standard and Camera Matching profiles both offered;
  - the crop;
  - the white balance shown in Kelvin;
  - the lens and EXIF data;
  - that editing works as usual.

  Note anything that differs.
- [ ] **Try the bad-pixel opcode in Lightroom.** Convert `samples/Canon_EOS_5D_Mark_II_2.CR2`, which has 22 hot pixels, both ways:
  - default: `dng-convert samples/Canon_EOS_5D_Mark_II_2.CR2 /tmp/opcode`
  - `--repair interpolate`
  - and, for reference, `--bad-pixels off`

  At 100% zoom in Lightroom, check that the hot pixels are gone in the first two. One is at row 361, column 4070 of the raw image (red, about 15 times its neighbours).
- [ ] **The GitHub repository.** Its owner and name: the README assumes `kineticsystem/dng-converter`. Also: should `TODO.md` stay in the repo, or be ignored like `task.md` in the StepIt repos?
- [ ] **Any 450D raw files of your own**, if you have some, to add to `~/Downloads/cr2` for the tests.

## Next, for us

### Once Giovanni's values are in

- [ ] Add the 5D Mark II and 450D rows to `src/camera_table.cpp`, with their source.
- [ ] If the Adobe DNGs can be shared, or are of public files: add them as reference pairs, like the 5D Mark III's, and extend `test_adobe_reference` to every pair. Otherwise, check the pairs by hand with the same comparison.
- [ ] Revisit the white level: ours is Canon's clipping level per ISO, Adobe's a fixed value (15000 on the 5D Mark III). Decide with Lightroom side by side whether to match Adobe's, e.g. through the camera table.

### Repository and CI

- [ ] `git init`, first commit, push to GitHub.
- [ ] GitHub Actions workflow:
  - build the image from `docker/Dockerfile`;
  - cache `samples/` by the checksums in `bin/fetch-samples.sh`;
  - run `fetch-samples.sh` and `test.sh`.

  `~/user-samples` is not there in CI, so only the public samples run.
- [ ] Format workflow and pre-commit hooks as in the StepIt repos: clang-format with the shared `.clang-format`, codespell, the whitespace checks.
- [ ] CI badges in the README, and its "Continuous Integration" section rewritten.
- [ ] Optionally, `CLAUDE.md` as in the StepIt repos.

### Converter

- [ ] `NewRawImageDigest` (an MD5 of the raw data, computed as the DNG specification defines it), so that Lightroom's "Validate DNG Files" can check our files. It must be exactly right: a wrong digest makes Lightroom report the file as damaged. Test it against Adobe's digest in the 5D Mark III reference file.
- [ ] Optionally, keep the masked border with `ActiveArea`, as Adobe does: the full sensor, 5920x3950 on the 5D Mark III.
- [ ] Optionally, embed the original raw file (Adobe's "Embed Original Raw File" option).
- [ ] sRAW and mRAW: their samples round-trip exactly and render like the CR2 in LibRaw. Check them in Lightroom too.
- [ ] Bad pixels: detect up to the image edge (the four-pixel margin), with bounds checks on the neighbours.
- [ ] Bad pixels: try the detector on long exposures and astro shots, if Giovanni has some. A dark frame, i.e. a shot with the lens cap on, would show the sensor's real defects.
- [ ] Memory: each file in flight takes about 150 MB. Write the tiles to the file as they are encoded, rather than building the whole DNG in memory, if a host application needs to convert many files at once.
- [ ] Windows and macOS: replace `getpid` and `localtime_r`, and find LibRaw without pkg-config (vcpkg, Homebrew).

### Integration into the bigger software

- [ ] Decide where it is used: e.g. converting the pictures StepIt Camera downloads, as they arrive. The pieces for it are `readRawBuffer`, `Converter::repair` and `encodeDng`, or a `FileStore` of its own.
- [ ] If it is used from a ROS 2 package: how to bring it in, e.g. a Git submodule with `add_subdirectory`, or an installed package found with `find_package(dngconv)`. Both work today.
