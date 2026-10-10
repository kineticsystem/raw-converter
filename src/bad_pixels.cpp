// Copyright (c) 2026 Giovanni Remigi
// SPDX-License-Identifier: MIT

#include "dngconv/bad_pixels.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <istream>
#include <ostream>
#include <set>
#include <sstream>

#include "dngconv/error.hpp"

namespace dngconv
{

namespace
{

struct Offset
{
  int dy;
  int dx;
};

// The eight nearest pixels of the same colour in a Bayer pattern.
constexpr std::array<Offset, 8> GREEN_NEIGHBOURS = {
  { { -1, -1 }, { -1, 1 }, { 1, -1 }, { 1, 1 }, { -2, 0 }, { 2, 0 }, { 0, -2 }, { 0, 2 } }
};
constexpr std::array<Offset, 8> RED_BLUE_NEIGHBOURS = {
  { { -2, -2 }, { -2, 0 }, { -2, 2 }, { 0, -2 }, { 0, 2 }, { 2, -2 }, { 2, 0 }, { 2, 2 } }
};
// The eight pixels around a pixel, whatever their colour.
constexpr std::array<Offset, 8> ADJACENT = {
  { { -1, -1 }, { -1, 0 }, { -1, 1 }, { 0, -1 }, { 0, 1 }, { 1, -1 }, { 1, 0 }, { 1, 1 } }
};

// Pixels this close to an edge are not examined: their neighbours'
// neighbours would be outside the image.
constexpr uint32_t MARGIN = 4;

const std::array<Offset, 8>& sameColourNeighbours(const RawImage& image, uint32_t row, uint32_t col)
{
  return image.cfaColor(row, col) == 1 ? GREEN_NEIGHBOURS : RED_BLUE_NEIGHBOURS;
}

double median8(std::array<double, 8> v)
{
  std::sort(v.begin(), v.end());
  return (v[3] + v[4]) / 2;
}

// The noise of the image as a function of the signal: the standard deviation
// of a pixel at each level above black. It is measured on pairs of pixels of
// the same colour two columns apart, by median absolute difference, so that
// edges and detail barely affect it.
class NoiseModel
{
public:
  explicit NoiseModel(const RawImage& image)
  {
    constexpr int BINS = 17;  // By powers of two of the signal.
    std::array<std::vector<double>, BINS> diffs;
    for (uint32_t row = 0; row < image.height; row += 3)
    {
      for (uint32_t col = 0; col + 2 < image.width; col += 5)
      {
        const double a = image.at(row, col) - image.blackAt(row, col);
        const double b = image.at(row, col + 2) - image.blackAt(row, col + 2);
        const double level = std::max(0.0, (a + b) / 2);
        diffs[bin(level)].push_back(std::abs(a - b));
      }
    }
    // MAD to sigma for a normal distribution, and the difference of two
    // samples has sqrt(2) times their noise.
    constexpr double SCALE = 1.4826 / 1.41421356;
    std::array<double, BINS> sigma{};
    std::array<bool, BINS> known{};
    for (int i = 0; i < BINS; ++i)
    {
      auto& d = diffs[i];
      if (d.size() >= 64)
      {
        std::nth_element(d.begin(), d.begin() + d.size() / 2, d.end());
        sigma[i] = std::max(1.0, d[d.size() / 2] * SCALE);
        known[i] = true;
      }
    }
    // Levels too rare to measure take the nearest measured one; and noise
    // never decreases with the signal.
    double last = 1.0;
    int first_known = -1;
    for (int i = 0; i < BINS; ++i)
    {
      if (known[i])
      {
        if (first_known < 0)
        {
          first_known = i;
        }
        last = std::max(last, sigma[i]);
      }
      sigma[i] = last;
    }
    for (int i = 0; i < first_known; ++i)
    {
      sigma[i] = sigma[first_known];
    }
    sigma_ = sigma;
  }

  double at(double level) const
  {
    return sigma_[bin(std::max(0.0, level))];
  }

private:
  static int bin(double level)
  {
    int b = 0;
    auto v = static_cast<uint32_t>(level);
    while (v != 0 && b < 16)
    {
      v >>= 1;
      ++b;
    }
    return b;
  }

  std::array<double, 17> sigma_{};
};

class Detector
{
public:
  Detector(const RawImage& image, const BadPixelOptions& options) : image_(image), options_(options), noise_(image)
  {
  }

  BadPixelCandidates run() const
  {
    BadPixelCandidates result;
    if (image_.width <= 2 * MARGIN || image_.height <= 2 * MARGIN)
    {
      return result;
    }
    for (uint32_t row = MARGIN; row < image_.height - MARGIN; ++row)
    {
      for (uint32_t col = MARGIN; col < image_.width - MARGIN; ++col)
      {
        classify(row, col, result);
      }
    }
    return result;
  }

private:
  double value(uint32_t row, uint32_t col) const
  {
    return image_.at(row, col) - image_.blackAt(row, col);
  }

  // The same-colour neighbours of a pixel, black subtracted.
  std::array<double, 8> neighbours(uint32_t row, uint32_t col) const
  {
    std::array<double, 8> n{};
    const auto& offsets = sameColourNeighbours(image_, row, col);
    for (size_t i = 0; i < 8; ++i)
    {
      n[i] = value(row + offsets[i].dy, col + offsets[i].dx);
    }
    return n;
  }

  // How far a pixel stands above (positive) or below (negative) its
  // same-colour surroundings.
  double localExcess(uint32_t row, uint32_t col) const
  {
    return value(row, col) - median8(neighbours(row, col));
  }

  // Whether the pixels around (row, col) stand out from their own
  // surroundings, in the same direction, by more than a fraction of `excess`:
  // the mark of detail, which spreads over several pixels, rather than of a
  // defect, which is confined to one.
  bool neighboursStandOut(uint32_t row, uint32_t col, double excess, double sigma) const
  {
    const double limit = options_.max_neighbour_fraction * std::abs(excess) + 2 * sigma;
    for (const auto& o : ADJACENT)
    {
      const double e = localExcess(row + o.dy, col + o.dx);
      if ((excess > 0 ? e : -e) > limit)
      {
        return true;
      }
    }
    return false;
  }

  // Whether a pixel of another colour next to (row, col) is, white balanced,
  // nearly as bright as `v`: the pixel is then on the edge of a bright detail
  // rather than a lone defect.
  bool besideBrightColour(uint32_t row, uint32_t col, double v) const
  {
    const double centre = v / neutral(row, col);
    for (const auto& o : ADJACENT)
    {
      const uint32_t r = row + o.dy;
      const uint32_t c = col + o.dx;
      if (image_.cfaColor(r, c) != image_.cfaColor(row, col) &&
          value(r, c) / neutral(r, c) > options_.max_cross_colour_fraction * centre)
      {
        return true;
      }
    }
    return false;
  }

  double neutral(uint32_t row, uint32_t col) const
  {
    return std::max(0.01, image_.as_shot_neutral[image_.cfaColor(row, col)]);
  }

  void classify(uint32_t row, uint32_t col, BadPixelCandidates& result) const
  {
    const double v = value(row, col);
    // Most pixels are neither above nor below all of their neighbours: tell
    // that from the first few, before any statistics.
    const auto& offsets = sameColourNeighbours(image_, row, col);
    bool above_all = true;
    bool below_all = true;
    for (const auto& o : offsets)
    {
      const double n = value(row + o.dy, col + o.dx);
      above_all = above_all && v > n;
      below_all = below_all && v < n;
      if (!above_all && !below_all)
      {
        return;
      }
    }

    const auto n = neighbours(row, col);
    const double median = median8(n);
    const double sigma = noise_.at(median);
    if (above_all)
    {
      const double max_n = *std::max_element(n.begin(), n.end());
      const double excess = v - max_n;
      const double reference = std::max(max_n, sigma);
      const bool confident = excess > options_.confident_sigma * sigma && v > options_.confident_ratio * reference;
      const bool suspect = excess > options_.suspect_sigma * sigma && v > options_.suspect_ratio * reference &&
                           !besideBrightColour(row, col, v);
      if ((confident || suspect) && !neighboursStandOut(row, col, excess, sigma))
      {
        (confident ? result.confident : result.suspect).push_back({ row, col });
      }
    }
    else
    {
      const double min_n = *std::min_element(n.begin(), n.end());
      const double deficit = min_n - v;
      // A dead pixel only shows against a background well above the noise.
      const double floor = std::max(0.0, v);
      const bool confident = deficit > options_.confident_sigma * sigma && floor * options_.confident_ratio < min_n;
      const bool suspect = deficit > options_.suspect_sigma * sigma && floor * options_.suspect_ratio < min_n;
      if ((confident || suspect) && !neighboursStandOut(row, col, -deficit, sigma))
      {
        (confident ? result.confident : result.suspect).push_back({ row, col });
      }
    }
  }

  const RawImage& image_;
  const BadPixelOptions& options_;
  NoiseModel noise_;
};

}  // namespace

BadPixelCandidates detectBadPixels(const RawImage& image, const BadPixelOptions& options)
{
  if (image.layout != RawLayout::CFA || image.data.empty())
  {
    return {};
  }
  return Detector(image, options).run();
}

void repairBadPixels(RawImage& image, const std::vector<PixelPos>& pixels)
{
  const std::set<PixelPos> bad(pixels.begin(), pixels.end());
  // Computed from the original values first, then written: a repaired pixel
  // must not feed into the repair of another.
  std::vector<std::pair<size_t, uint16_t>> repairs;
  for (const auto& p : bad)
  {
    if (p.row >= image.height || p.col >= image.width)
    {
      continue;
    }
    for (uint32_t s = 0; s < image.samples_per_pixel; ++s)
    {
      const auto& offsets = image.layout == RawLayout::CFA ? sameColourNeighbours(image, p.row, p.col) : ADJACENT;
      std::vector<uint16_t> values;
      for (const auto& o : offsets)
      {
        const int64_t row = static_cast<int64_t>(p.row) + o.dy;
        const int64_t col = static_cast<int64_t>(p.col) + o.dx;
        if (row < 0 || col < 0 || row >= image.height || col >= image.width)
        {
          continue;
        }
        const PixelPos q{ static_cast<uint32_t>(row), static_cast<uint32_t>(col) };
        if (bad.count(q) == 0)
        {
          values.push_back(image.at(q.row, q.col, s));
        }
      }
      if (values.empty())
      {
        continue;
      }
      std::sort(values.begin(), values.end());
      const size_t m = values.size() / 2;
      const uint16_t median =
          values.size() % 2 ? values[m] : static_cast<uint16_t>((uint32_t(values[m - 1]) + values[m] + 1) / 2);
      repairs.emplace_back((static_cast<size_t>(p.row) * image.width + p.col) * image.samples_per_pixel + s, median);
    }
  }
  for (const auto& [index, value] : repairs)
  {
    image.data[index] = value;
  }
}

SceneSignature sceneSignature(const RawImage& image)
{
  SceneSignature signature;
  if (image.width == 0 || image.height == 0)
  {
    return signature;
  }
  constexpr uint32_t GRID = 8;
  std::array<double, GRID * GRID> sum{};
  std::array<uint32_t, GRID * GRID> count{};
  // Every fourth row and column is plenty for a picture this coarse.
  for (uint32_t row = 0; row < image.height; row += 4)
  {
    const uint32_t gy = row * GRID / image.height;
    for (uint32_t col = 0; col < image.width; col += 4)
    {
      const uint32_t cell = gy * GRID + col * GRID / image.width;
      for (uint32_t s = 0; s < image.samples_per_pixel; ++s)
      {
        const double black = image.layout == RawLayout::CFA ? image.blackAt(row, col) : image.black_level[s];
        sum[cell] += std::max(0.0, image.at(row, col, s) - black);
        ++count[cell];
      }
    }
  }
  double mean = 0;
  for (size_t i = 0; i < sum.size(); ++i)
  {
    signature.stops[i] = static_cast<float>(std::log2(1 + (count[i] ? sum[i] / count[i] : 0)));
    mean += signature.stops[i];
  }
  mean /= sum.size();
  for (auto& v : signature.stops)
  {
    v -= static_cast<float>(mean);
  }
  return signature;
}

std::string DefectMap::cameraKey(const RawImage& image)
{
  const auto& m = image.metadata;
  std::ostringstream key;
  key << (m.unique_camera_model.empty() ? m.model : m.unique_camera_model) << '|'
      << (m.serial_number.empty() ? "?" : m.serial_number) << '|' << image.width << 'x' << image.height;
  return key.str();
}

size_t DefectMap::sceneIndex(Camera& camera, const SceneSignature& scene)
{
  // Two shots are of the same scene when their pictures differ by less than
  // a third of a stop on average.
  constexpr double SAME_SCENE = 0.33;
  for (size_t i = 0; i < camera.scenes.size(); ++i)
  {
    double distance = 0;
    for (size_t k = 0; k < scene.stops.size(); ++k)
    {
      distance += std::abs(scene.stops[k] - camera.scenes[i].stops[k]);
    }
    if (distance / scene.stops.size() < SAME_SCENE)
    {
      return i;
    }
  }
  camera.scenes.push_back(scene);
  return camera.scenes.size() - 1;
}

void DefectMap::addShot(const std::string& camera, const BadPixelCandidates& candidates, const SceneSignature& scene)
{
  std::lock_guard<std::mutex> lock(mutex_);
  Camera& c = cameras_[camera];
  ++c.shots;
  const size_t index = sceneIndex(c, scene);
  for (const auto* list : { &candidates.confident, &candidates.suspect })
  {
    for (const auto& p : *list)
    {
      c.seen[p].insert(index);
    }
  }
}

std::vector<PixelPos> DefectMap::confirmedLocked(const Camera& camera, unsigned min_scenes, double min_fraction) const
{
  std::set<PixelPos> result = camera.known;
  const double needed = std::max<double>(min_scenes, std::ceil(min_fraction * camera.scenes.size()));
  for (const auto& [pixel, scenes] : camera.seen)
  {
    if (scenes.size() >= needed)
    {
      result.insert(pixel);
    }
  }
  return { result.begin(), result.end() };
}

std::vector<PixelPos> DefectMap::confirmed(const std::string& camera, unsigned min_scenes, double min_fraction) const
{
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = cameras_.find(camera);
  return it == cameras_.end() ? std::vector<PixelPos>{} : confirmedLocked(it->second, min_scenes, min_fraction);
}

unsigned DefectMap::shots(const std::string& camera) const
{
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = cameras_.find(camera);
  return it == cameras_.end() ? 0 : it->second.shots;
}

unsigned DefectMap::scenes(const std::string& camera) const
{
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = cameras_.find(camera);
  return it == cameras_.end() ? 0 : static_cast<unsigned>(it->second.scenes.size());
}

void DefectMap::read(std::istream& in)
{
  std::lock_guard<std::mutex> lock(mutex_);
  Camera* camera = nullptr;
  std::string line;
  int number = 0;
  while (std::getline(in, line))
  {
    ++number;
    if (!line.empty() && line.back() == '\r')
    {
      line.pop_back();  // A file edited on Windows.
    }
    if (line.empty() || line[0] == '#')
    {
      continue;
    }
    if (line.rfind("camera ", 0) == 0)
    {
      camera = &cameras_[line.substr(7)];
      continue;
    }
    std::istringstream fields(line);
    PixelPos p;
    std::string rest;
    if (camera == nullptr || !(fields >> p.row >> p.col) || (fields >> rest))
    {
      throw Error("Defect map line " + std::to_string(number) + ": expected \"camera <key>\" or \"<row> <col>\"");
    }
    camera->known.insert(p);
  }
}

void DefectMap::write(std::ostream& out, unsigned min_scenes, double min_fraction) const
{
  std::lock_guard<std::mutex> lock(mutex_);
  out << "# dng-converter defect map: the sensor defects of each camera, as <row> <col>\n"
      << "# in raw image coordinates. Edit freely; reading adds to what is known.\n";
  for (const auto& [key, camera] : cameras_)
  {
    const auto pixels = confirmedLocked(camera, min_scenes, min_fraction);
    if (pixels.empty())
    {
      continue;
    }
    out << "camera " << key << '\n';
    for (const auto& p : pixels)
    {
      out << p.row << ' ' << p.col << '\n';
    }
  }
}

}  // namespace dngconv
