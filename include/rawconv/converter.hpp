// Copyright (c) 2026 Giovanni Remigi
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <filesystem>
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "rawconv/bad_pixels.hpp"
#include "rawconv/dng_writer.hpp"
#include "rawconv/file_store.hpp"
#include "rawconv/raw_reader.hpp"
#include "rawconv/tiff_output.hpp"

namespace rawconv
{

enum class BadPixelMode
{
  // Leave the pixels as they are.
  OFF,
  // Repair the pixels each image shows, on its own, to be bad.
  PER_IMAGE,
  // Also learn the defects of each camera from all the files of the batch and
  // repair them in all of that camera's files. Decodes each file twice.
  BATCH,
};

enum class BadPixelRepair
{
  // Replace the values in the raw data, which every raw editor then shows
  // repaired.
  INTERPOLATE,
  // Keep the original values and add a DNG FixBadPixelsList opcode, which a
  // raw editor that supports DNG opcodes applies when rendering; one that
  // does not shows the pixels unrepaired.
  OPCODE,
};

// The files written for each raw file.
enum class OutputFormat
{
  // The DNG: the raw data, as Adobe DNG Converter writes it.
  DNG,
  // A TIFF: the image demosaiced, linear, 16 bits per sample.
  TIFF,
  DNG_AND_TIFF,
};

struct ConvertOptions
{
  OutputFormat format = OutputFormat::DNG;

  // OFF by default, as Adobe DNG Converter: the raw data is written as the
  // camera recorded it.
  BadPixelMode bad_pixels = BadPixelMode::OFF;
  // When bad pixels are repaired, INTERPOLATE by default: the DNG is then
  // repaired whatever opens it.
  BadPixelRepair repair = BadPixelRepair::INTERPOLATE;
  // Used to make the default detector; ignored when one is injected.
  BadPixelOptions detection;
  // A suspect pixel is confirmed as a defect when found in shots of at least
  // this many different scenes by the camera, and in at least this fraction
  // of them.
  unsigned batch_min_scenes = 2;
  double batch_min_fraction = 0.25;

  DngWriteOptions dng;
  TiffWriteOptions tiff;
  // Replace output files that already exist; otherwise they are not written
  // again, and a raw file whose outputs all exist is skipped.
  bool overwrite = false;
  // Files converted in parallel; 0 for one per CPU core, up to 8, as each
  // takes some 150 MB of memory, and some 600 MB with a TIFF.
  unsigned threads = 0;
};

struct ConvertResult
{
  std::filesystem::path input;
  // The job's output: the DNG, or, when only a TIFF is written, the TIFF.
  std::filesystem::path output;
  // Every file written, or, when skipped, every file that exists already.
  std::vector<std::filesystem::path> outputs;
  enum class Status
  {
    CONVERTED,
    SKIPPED,  // The outputs exist and overwrite is off.
    FAILED,
    CANCELLED,  // cancel() was called before the file was started.
  } status = Status::FAILED;
  std::string error;            // Why it failed.
  size_t bad_pixels_fixed = 0;  // Repaired, or listed in the opcode.
};

// One input file and where its DNG goes. A TIFF goes to the same path with
// the extension .tif: see outputPaths().
struct ConvertJob
{
  std::filesystem::path input;
  std::filesystem::path output;
};

// The files a job writes in the given format: job.output for the DNG, and
// job.output with the extension .tif for the TIFF, in that order.
std::vector<std::filesystem::path> outputPaths(const ConvertJob& job, OutputFormat format);

// What the converter works with. Every member left empty gets the production
// implementation: LibRaw, the noise based detector, the DNG 1.4 encoder, the
// local file system and the TIFF encoder, which renders with LibRaw. Tests
// replace them with fakes; an application can replace the file store to read
// from a camera or write elsewhere.
struct ConverterServices
{
  std::shared_ptr<const RawDecoder> decoder;
  std::shared_ptr<const BadPixelDetector> detector;
  std::shared_ptr<const DngEncoder> encoder;
  std::shared_ptr<FileStore> files;
  std::shared_ptr<const TiffEncoder> tiff_encoder;
};

// Converts raw files to DNG, TIFF or both: read, decode, find and repair bad
// pixels, encode, write. One Converter can be used for many batches: in BATCH
// mode, what it learns of each camera's defects carries over to the next
// batch, and can be loaded from and saved to a file through defectMap().
class Converter
{
public:
  explicit Converter(ConvertOptions options = {}, ConverterServices services = {});

  // Called after each file, from the thread that converted it, with the
  // number of files done so far and the total. Calls are serialised.
  using Progress = std::function<void(const ConvertResult& result, size_t done, size_t total)>;

  // Finds and repairs the bad pixels of an image already decoded, as
  // PER_IMAGE does, together with the defects the defect map confirms for its
  // camera. With BadPixelRepair::OPCODE the pixels go to
  // image.opcode_bad_pixels instead. Returns how many pixels.
  size_t repair(RawImage& image) const;

  ConvertResult convertFile(const std::filesystem::path& input, const std::filesystem::path& output);

  // Converts the jobs in parallel. Never throws for a single file: its error
  // is in its result. The results are in the order of the jobs.
  std::vector<ConvertResult> convert(const std::vector<ConvertJob>& jobs, const Progress& progress = {});

  // Asks a running convert() to stop: the files being converted are
  // finished, the others are CANCELLED. Safe to call from any thread, e.g.
  // a progress callback or a user interface, and from a signal handler. The
  // next convert() starts anew.
  void cancel()
  {
    cancelled_ = true;
  }

  DefectMap& defectMap()
  {
    return defects_;
  }
  const ConvertOptions& options() const
  {
    return options_;
  }

private:
  RawImage decode(const std::filesystem::path& input) const;
  size_t applyRepairs(RawImage& image, const BadPixelCandidates& candidates) const;
  unsigned threads(size_t jobs) const;

  ConvertOptions options_;
  ConverterServices services_;
  DefectMap defects_;
  std::atomic<bool> cancelled_{ false };
  static_assert(std::atomic<bool>::is_always_lock_free, "cancel() must be safe in a signal handler");
};

// The jobs to convert `input`, a raw file or a folder of them, into the folder
// `output_dir`: each DNG is named after its raw file. With `recursive`, the
// sub-folders of `input` are searched too, and mirrored in `output_dir`. Raw
// files that would share a DNG name (IMG_1.CR2 and IMG_1.CR3) get their
// extension added to it (IMG_1.dng and IMG_1_CR3.dng).
// Throws rawconv::Error when `input` does not exist.
std::vector<ConvertJob> planJobs(const std::filesystem::path& input, const std::filesystem::path& output_dir,
                                 bool recursive = false);

}  // namespace rawconv
