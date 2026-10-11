// Copyright (c) 2026 Giovanni Remigi
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <array>
#include <map>
#include <optional>
#include <string>

namespace rawconv
{

// What a DNG says about a camera model that is not in its raw files: values
// Adobe DNG Converter writes from its own camera table, and raw editors use
// when rendering. Each is optional: what is not known is left out of the DNG,
// which then holds the DNG default.
struct CameraTraits
{
  // Exposure, in stops, a raw editor adds to an unedited photo. Leaving it out
  // makes the photo open darker than Adobe's DNG of the same raw file: by
  // 0.25 EV for the Canon EOS 5D Mark III.
  std::optional<double> baseline_exposure;
  // The camera's noise and sharpness relative to a reference camera, which
  // scale a raw editor's default noise reduction and sharpening.
  std::optional<double> baseline_noise;
  std::optional<double> baseline_sharpness;
  // How much the two greens of the Bayer pattern differ, 0 to 5000, which
  // Adobe's demosaicing uses to avoid maze patterns.
  std::optional<uint32_t> bayer_green_split;
  // Where the values come from, e.g. the Adobe DNG they were read from.
  std::string source;
  // The pixels of the sensor that hold the image, in the raw file's whole
  // frame, as Adobe's ActiveArea says: top, left, bottom, right. When it is
  // not known, LibRaw's is used, which can differ by a row or a column.
  std::optional<std::array<uint32_t, 4>> active_area;
};

// Camera traits by DNG UniqueCameraModel, e.g. "Canon EOS 5D Mark III".
class CameraTable
{
public:
  // The values verified against Adobe DNG Converter's output, compiled in.
  static const CameraTable& builtIn();

  // Adds or replaces the traits of a model.
  void set(const std::string& unique_camera_model, CameraTraits traits);
  // Nothing for an unknown model.
  const CameraTraits* find(const std::string& unique_camera_model) const;
  size_t size() const
  {
    return traits_.size();
  }

private:
  std::map<std::string, CameraTraits> traits_;
};

}  // namespace rawconv
