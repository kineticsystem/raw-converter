// Copyright (c) 2026 Giovanni Remigi
// SPDX-License-Identifier: MIT

#pragma once

#include <array>
#include <cstdint>
#include <iosfwd>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "dngconv/raw_image.hpp"

namespace dngconv
{

// Thresholds of the bad pixel detector. A pixel is compared with the nearest
// pixels of the same colour; the difference is measured in units of the
// image's noise at that brightness, which the detector estimates from the
// image itself.
struct BadPixelOptions
{
  // A pixel this many noise units above (hot) or below (dead) all of its
  // same-colour neighbours is bad, whatever else is known.
  double confident_sigma = 25.0;
  // From this many noise units it is only suspect: it is repaired when shots
  // of other scenes by the same camera confirm it (see DefectMap).
  double suspect_sigma = 8.0;
  // Besides the noise test, a hot pixel must be at least this many times
  // brighter than the brightest of its same-colour neighbours (above black);
  // a dead one at most 1 / this as bright as the darkest.
  double confident_ratio = 3.0;
  double suspect_ratio = 1.6;
  // A bright detail a lens can resolve covers more than one pixel, so its
  // direct neighbours stand out too. A pixel whose neighbours stand out by more
  // than this fraction of its own excess is taken for detail, not a defect.
  double max_neighbour_fraction = 0.25;
  // A suspect hot pixel next to a pixel of another colour at least this
  // fraction as bright, once white balanced, is taken for the edge of a
  // bright detail.
  double max_cross_colour_fraction = 0.5;
};

// The bad pixels found in one image.
struct BadPixelCandidates
{
  std::vector<PixelPos> confident;  // Repaired in any case.
  std::vector<PixelPos> suspect;    // Repaired once confirmed by a DefectMap.
};

// Finds the hot, stuck and dead pixels of a CFA image. Isolated single-pixel
// outliers are reported; detail that spans several pixels, such as a star or a
// specular highlight, is not. A point of light of a single, saturated colour
// can look like a defect, though; BadPixelRepair::OPCODE keeps the original
// values for when that matters. Returns nothing for a LINEAR_RAW image, whose pixels the
// camera has already interpolated.
BadPixelCandidates detectBadPixels(const RawImage& image, const BadPixelOptions& options = {});

// Finds the bad pixels of an image: the seam the converter uses, so that tests
// can give it candidates of their own and check what it does with them.
class BadPixelDetector
{
public:
  virtual ~BadPixelDetector() = default;
  // Must be safe to call from several threads at once.
  virtual BadPixelCandidates detect(const RawImage& image) const = 0;
};

// The detector of detectBadPixels().
class NoiseBadPixelDetector : public BadPixelDetector
{
public:
  explicit NoiseBadPixelDetector(BadPixelOptions options = {}) : options_(options)
  {
  }
  BadPixelCandidates detect(const RawImage& image) const override
  {
    return detectBadPixels(image, options_);
  }

private:
  BadPixelOptions options_;
};

// Replaces each listed pixel with the median of its nearest same-colour
// neighbours, leaving out neighbours that are listed too.
void repairBadPixels(RawImage& image, const std::vector<PixelPos>& pixels);

// A coarse, exposure independent picture of a shot: the brightness of an 8x8
// grid, in stops relative to the shot's average. Shots of the same scene --
// a bracket, a time-lapse -- have close signatures.
struct SceneSignature
{
  std::array<float, 64> stops{};
};
SceneSignature sceneSignature(const RawImage& image);

// The defects of each camera body, accumulated over many shots. A sensor
// defect is in the same place in every shot, whatever the scene, whereas
// detail that looks like one is only there in shots of that scene: a suspect
// pixel seen in shots of enough different scenes by the same camera is a real
// defect, and is repaired in every shot of that camera, including those where
// it does not show. Thread safe.
class DefectMap
{
public:
  // The identity of the sensor that took the image: model, serial number and
  // size.
  static std::string cameraKey(const RawImage& image);

  // Records the candidates found in one shot of the given scene.
  void addShot(const std::string& camera, const BadPixelCandidates& candidates, const SceneSignature& scene);

  // The pixels found in shots of at least `min_scenes` different scenes, and
  // of at least `min_fraction` of the scenes recorded for the camera; plus the
  // pixels read from a saved map, which are confirmed already.
  std::vector<PixelPos> confirmed(const std::string& camera, unsigned min_scenes = 2, double min_fraction = 0.25) const;

  unsigned shots(const std::string& camera) const;
  unsigned scenes(const std::string& camera) const;

  // Plain text: one "camera <key>" line per camera, followed by one
  // "<row> <col>" line per confirmed defect. Lines starting with # are
  // comments. Reading adds to what the map holds. Throws dngconv::Error on a
  // malformed line.
  void read(std::istream& in);
  void write(std::ostream& out, unsigned min_scenes = 2, double min_fraction = 0.25) const;

private:
  struct Camera
  {
    unsigned shots = 0;
    std::vector<SceneSignature> scenes;         // One per distinct scene.
    std::map<PixelPos, std::set<size_t>> seen;  // Pixel to the scenes it was seen in.
    std::set<PixelPos> known;                   // Read from a saved map.
  };
  static size_t sceneIndex(Camera& camera, const SceneSignature& scene);
  std::vector<PixelPos> confirmedLocked(const Camera& camera, unsigned min_scenes, double min_fraction) const;

  mutable std::mutex mutex_;
  std::map<std::string, Camera> cameras_;
};

}  // namespace dngconv
