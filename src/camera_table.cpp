// Copyright (c) 2026 Giovanni Remigi
// SPDX-License-Identifier: MIT

#include "dngconv/camera_table.hpp"

namespace dngconv
{

namespace
{

// Every value here is read from a DNG that Adobe DNG Converter wrote from a
// raw file of that camera; `source` names the file. Values are facts about
// the cameras: nothing of Adobe's software or profiles is copied. To add a
// camera, convert one of its raw files with Adobe DNG Converter and read the
// values with exiftool:
//
//   exiftool -n -UniqueCameraModel -BaselineExposure -BaselineNoise
//     -BaselineSharpness -BayerGreenSplit -SubIFD:ActiveArea file.dng
//
// Guesses do not belong here: a camera without a row gets the DNG defaults.
CameraTable makeBuiltIn()
{
  CameraTable table;
  table.set("Canon EOS 5D Mark II", { 0.4, 0.8, 1.2, 100,
                                      "Adobe DNG Converter 16.3 on ten of Giovanni's own CR2 files (capt0526 to "
                                      "capt0535, ISO 400), all with the same values",
                                      std::array<uint32_t, 4>{ 51, 158, 3804, 5792 } });
  table.set("Canon EOS 5D Mark III", { 0.25, 0.8, 1.2, 100,
                                       "Adobe DNG Converter 9.8 on raw.pixls.us sample 771 (5G4A9394.CR2, ISO 200), "
                                       "its DNG being sample 1024",
                                       std::array<uint32_t, 4>{ 80, 122, 3950, 5918 } });
  return table;
}

}  // namespace

const CameraTable& CameraTable::builtIn()
{
  static const CameraTable table = makeBuiltIn();
  return table;
}

void CameraTable::set(const std::string& unique_camera_model, CameraTraits traits)
{
  traits_[unique_camera_model] = std::move(traits);
}

const CameraTraits* CameraTable::find(const std::string& unique_camera_model) const
{
  auto it = traits_.find(unique_camera_model);
  return it == traits_.end() ? nullptr : &it->second;
}

}  // namespace dngconv
