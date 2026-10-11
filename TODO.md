# TODO

Where we are, what is needed from Giovanni, and what comes next. Last updated 2026-10-12.

## Where we are

- The library, `raw-convert` and their tests build and pass in the container: `./docker/dock.sh raw-converter exec test.sh`.
- Our DNG of the public 5D Mark III sample has the same raw data as Adobe DNG Converter 9.8's DNG of the same file, and the same crop, colour matrix, white balance and camera traits (`tests/test_adobe_reference.cpp`).
- Our DNGs of ten 5D Mark II files (`tmp/raw`) were compared by hand with Adobe DNG Converter 16.3's (`tmp/dng`). The camera traits are now in the camera table, and the orientation is the CR2's own, as Adobe's is: before, the DNGs opened on their side. The differences left are in [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md#the-5d-mark-ii).
- With the default settings, our DNGs of the ten 5D Mark II files hold the same samples as Adobe's, bit for bit, over the whole frame of the sensor, with the same `ActiveArea`, CFA pattern, crop, orientation and camera traits.
- Adobe DNG Converter repairs no bad pixel: its DNGs hold the CR2's samples, with no opcode. Neither do we, by default. `--bad-pixels image`, `batch` or `auto` repairs them in the raw data, and changes no other sample; `--repair opcode` keeps the raw data and adds an opcode instead.
- `--format tiff` or `both` also writes a TIFF: the image demosaiced by LibRaw, with the bad pixels repaired, linear, 16 bits per sample, in Rec. 2020 (default), ProPhoto RGB or sRGB, with its ICC profile. It is for StepIt Stacker's develop step, in place of darktable.
- The code is on GitHub, in the private repository [kineticsystem/raw-converter](https://github.com/kineticsystem/raw-converter), branch `main`.

## Needed from Giovanni

- [x] **Adobe DNGs of the 5D Mark II**: ten, in `tmp/dng`.
- [ ] **An Adobe DNG of the 450D.** Convert one raw file with Adobe DNG Converter, with the default settings, and put it with its raw file in `tmp`. Without its values, its photos open darker than Adobe's DNGs, probably by 0.25 to 0.5 EV.
- [ ] **Open our DNGs next to Adobe's**, in the raw editor you use. In the container, run `raw-convert tmp/raw build/out/ours`, then compare them with `tmp/dng`:
  - brightness;
  - colour;
  - the crop and the orientation;
  - the white balance;
  - the lens and EXIF data;
  - with `raw-convert --bad-pixels image tmp/raw build/out/fixed`, the hot pixels at 100% zoom, e.g. at row 99, column 3117 of the sensor (row 48, column 2959 of the active area): repaired in those, there in Adobe's unless the raw editor hides them.

  Note anything that differs.
- [ ] **Can the ten 5D Mark II pairs be kept?** They are about 450 MB, too much for the repository. If they may be published, e.g. on raw.pixls.us, `test_adobe_reference` can check them on every run, as it checks the 5D Mark III's. Otherwise, `tmp` stays out of Git and the comparison stays by hand.
- [x] **The GitHub repository**: [kineticsystem/raw-converter](https://github.com/kineticsystem/raw-converter), private.
- [ ] Should `TODO.md` stay in the repo, or be ignored like `task.md` in the StepIt repos?
- [ ] **Any 450D raw files of your own**, if you have some, for the tests.

## Next, for us

### Matching Adobe DNG Converter

- [x] Add the 5D Mark II row to `src/camera_table.cpp`, with its source.
- [ ] Add the 450D row, once its Adobe DNG is in.
- [x] Take the orientation from the original's own `Orientation` tag, as Adobe does, not from LibRaw.
- [ ] Revisit the white level: ours is Canon's clipping level per ISO, Adobe's a fixed value per camera (15000 on the 5D Mark III, 15600 on the 5D Mark II at ISO 400, where ours is 15312). Decide, with both DNGs side by side, whether to match Adobe's, e.g. through the camera table.
- [ ] Revisit the black level: Adobe writes a fixed 1024 on every cell for the 5D Mark II, we write what the masked pixels measure, 1024 or 1025.

### Repository and CI

- [x] `git init`, first commit, push to GitHub.
- [x] GitHub Actions workflow, `.github/workflows/ci.yml`: builds the image of `docker/Dockerfile`, caches the image layers and `samples/`, runs `fetch-samples.sh` and `test.sh`.
- [x] Format workflow and pre-commit hooks as in the StepIt repos: `.pre-commit-config.yaml` and `.github/workflows/ci-format.yml`, with clang-format, codespell and the whitespace checks.
- [x] CI badge in the README, and its "Continuous Integration" section rewritten.
- [ ] Optionally, `CLAUDE.md` as in the StepIt repos.

### Converter

- [ ] `NewRawImageDigest` (an MD5 of the raw data, computed as the DNG specification defines it), so that DNG validators can check our files. It must be exactly right: a wrong digest makes a validator report the file as damaged. Test it against Adobe's digest in the 5D Mark III reference file, and in the 5D Mark II files.
- [x] Keep the masked border with `ActiveArea`, as Adobe does: the whole frame, with Adobe's active area from the camera table.
- [ ] Optionally, embed the original raw file (Adobe's "Embed Original Raw File" option).
- [ ] sRAW and mRAW: their samples round-trip exactly and render like the CR2 in LibRaw. Check them in another raw editor too.
- [ ] Bad pixels: detect up to the image edge (the four-pixel margin), with bounds checks on the neighbours.
- [ ] Bad pixels: try the detector on long exposures and astro shots, if Giovanni has some. A dark frame, i.e. a shot with the lens cap on, would show the sensor's real defects.
- [ ] Memory: each file in flight takes about 150 MB. Write the tiles to the file as they are encoded, rather than building the whole DNG in memory, if a host application needs to convert many files at once.
- [ ] Windows and macOS: replace `getpid` and `localtime_r`, and find LibRaw without pkg-config (vcpkg, Homebrew).

### TIFF

- [ ] Compare a 5D Mark II TIFF with darktable's development of the same frame (StepIt Stacker's `develop.xmp`): brightness, colour, hot pixels.
- [ ] Optionally, other demosaicing methods (LibRaw's DHT, AAHD, DCB), as an option.
- [ ] Optionally, Deflate compression, if the files' size matters more than the time.

### Integration into the bigger software

- [ ] StepIt Stacker: develop each angle's frames with `raw-convert --format tiff --bad-pixels image --defect-map <map>`, then merge them.

- [ ] Decide where it is used: e.g. converting the pictures StepIt Camera downloads, as they arrive. The pieces for it are `readRawBuffer`, `Converter::repair` and `encodeDng`, or a `FileStore` of its own.
- [ ] If it is used from a ROS 2 package: how to bring it in, e.g. a Git submodule with `add_subdirectory`, or an installed package found with `find_package(rawconv)`. Both work today.
