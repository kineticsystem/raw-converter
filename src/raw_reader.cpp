// Copyright (c) 2026 Giovanni Remigi
// SPDX-License-Identifier: MIT

#include "rawconv/raw_reader.hpp"

#include <libraw/libraw.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <ctime>
#include <memory>
#include <regex>
#include <set>

#include "rawconv/error.hpp"
#include "rawconv/file_store.hpp"
#include "tiff/tiff_reader.hpp"

namespace rawconv
{

namespace
{

std::string trim(const char* text)
{
  std::string s(text);
  const auto first = s.find_first_not_of(" \t");
  const auto last = s.find_last_not_of(" \t");
  return first == std::string::npos ? std::string() : s.substr(first, last - first + 1);
}

void check(int code, const char* what, const std::string& file)
{
  if (code != LIBRAW_SUCCESS)
  {
    throw Error(file + ": " + what + ": " + libraw_strerror(code));
  }
}

// LibRaw's flip, which is how the image must be turned to be upright, to the
// TIFF orientation.
uint16_t orientationFromFlip(int flip)
{
  switch (flip)
  {
    case 3:
      return 3;  // 180 degrees.
    case 5:
      return 8;  // 90 degrees counter-clockwise.
    case 6:
      return 6;  // 90 degrees clockwise.
    default:
      return 1;
  }
}

// The colour index (0 = R, 1 = G, 2 = B) of a LibRaw colour slot, which can
// be 0 to 3 with two slots for green.
uint8_t colorOfSlot(const LibRaw& raw, int slot)
{
  switch (raw.imgdata.idata.cdesc[slot])
  {
    case 'R':
      return 0;
    case 'G':
      return 1;
    case 'B':
      return 2;
    default:
      return 255;
  }
}

std::string formatTimestamp(time_t timestamp)
{
  if (timestamp <= 0)
  {
    return {};
  }
  // LibRaw made the timestamp from the camera's local time with mktime(), so
  // localtime() gives that time back.
  std::tm tm{};
  localtime_r(&timestamp, &tm);
  char text[32];
  std::strftime(text, sizeof(text), "%Y:%m:%d %H:%M:%S", &tm);
  return text;
}

// The apertures of a lens name such as "EF-S 55-250mm f/4-5.6 IS".
void apertureFromLensName(const std::string& name, Metadata& metadata)
{
  static const std::regex pattern(R"(f/\s*([0-9.]+)(?:\s*-\s*([0-9.]+))?)", std::regex::icase);
  std::smatch match;
  if (std::regex_search(name, match, pattern))
  {
    metadata.lens_info[2] = std::stof(match[1].str());
    metadata.lens_info[3] = match[2].matched ? std::stof(match[2].str()) : metadata.lens_info[2];
  }
}

void readMetadata(const LibRaw& raw, Metadata& metadata)
{
  const auto& d = raw.imgdata;
  metadata.make = trim(d.idata.make);
  const std::string model = trim(d.idata.model);
  metadata.model = model.rfind(metadata.make, 0) == 0 ? model : metadata.make + " " + model;
  // Adobe's name for the camera, by which raw editors find their profiles
  // for it.
  // LibRaw gives every regional name of a camera the same normalised name,
  // e.g. "EOS 450D" for the Digital Rebel XSi and the Kiss X2.
  const std::string normalized_make = trim(d.idata.normalized_make);
  const std::string normalized_model = trim(d.idata.normalized_model);
  metadata.unique_camera_model =
      normalized_model.empty() ? metadata.model :
                                 (normalized_make.empty() ? metadata.make : normalized_make) + " " + normalized_model;
  metadata.serial_number = trim(d.shootinginfo.BodySerial);
  metadata.artist = trim(d.other.artist);
  metadata.description = trim(d.other.desc);
  metadata.date_time = formatTimestamp(d.other.timestamp);
  metadata.iso = d.other.iso_speed;
  metadata.exposure_time = d.other.shutter;
  metadata.f_number = d.other.aperture;
  metadata.focal_length = d.other.focal_len;

  const auto& lens = d.lens;
  metadata.lens_model = trim(lens.Lens);
  if (metadata.lens_model.empty())
  {
    metadata.lens_model = trim(lens.makernotes.Lens);
  }
  const float min_focal = lens.MinFocal > 0 ? lens.MinFocal : lens.makernotes.MinFocal;
  const float max_focal = lens.MaxFocal > 0 ? lens.MaxFocal : lens.makernotes.MaxFocal;
  if (min_focal > 0 && max_focal >= min_focal)
  {
    metadata.lens_info = { min_focal, max_focal, lens.MaxAp4MinFocal, lens.MaxAp4MaxFocal };
    if (metadata.lens_info[2] <= 0)
    {
      metadata.lens_info[2] = lens.makernotes.MaxAp4MinFocal;
      metadata.lens_info[3] = lens.makernotes.MaxAp4MaxFocal;
    }
    if (metadata.lens_info[2] <= 0)
    {
      apertureFromLensName(metadata.lens_model, metadata);
    }
  }
}

// The colour data shared by both layouts.
void readColor(const LibRaw& raw, RawImage& image)
{
  const auto& color = raw.imgdata.color;

  bool has_matrix = false;
  for (int row = 0; row < 3; ++row)
  {
    for (int col = 0; col < 3; ++col)
    {
      image.color_matrix[row * 3 + col] = color.cam_xyz[row][col];
      has_matrix = has_matrix || color.cam_xyz[row][col] != 0;
    }
  }
  if (!has_matrix)
  {
    throw Error("No colour matrix is known for the " + image.metadata.model);
  }

  // The white balance multipliers are the inverse of the neutral, relative to
  // green.
  const float* mul =
      color.cam_mul[0] > 0 && color.cam_mul[1] > 0 && color.cam_mul[2] > 0 ? color.cam_mul : color.pre_mul;
  if (mul[0] > 0 && mul[1] > 0 && mul[2] > 0)
  {
    for (int c = 0; c < 3; ++c)
    {
      image.as_shot_neutral[c] = mul[1] / mul[c];
    }
  }

  // Canon (and others) record the level at which the sensor really clips, per
  // ISO, in the maker notes; LibRaw's `maximum` can be above it, and a white
  // level above the clipping point turns blown highlights magenta.
  uint32_t white = color.maximum;
  for (int c = 0; c < 4; ++c)
  {
    if (color.linear_max[c] > 0 && color.linear_max[c] < white)
    {
      white = static_cast<uint32_t>(color.linear_max[c]);
    }
  }
  image.white_level = white;
}

void readCrop(const LibRaw& raw, RawImage& image)
{
  const auto& sizes = raw.imgdata.sizes;
  image.default_crop = { 0, 0, image.height, image.width };

  // The crop the camera applies to its own JPEG, in raw coordinates. Canon's
  // maker notes have it exactly; LibRaw's inset crop is next best.
  long top = -1, left = -1, height = 0, width = 0;
  const auto& canon = raw.imgdata.makernotes.canon.DefaultCropAbsolute;
  const auto& inset = sizes.raw_inset_crops[0];
  if (image.metadata.make == "Canon" && canon.b > canon.t && canon.r > canon.l)
  {
    top = canon.t;
    left = canon.l;
    height = canon.b - canon.t + 1;
    width = canon.r - canon.l + 1;
  }
  else if (inset.cwidth > 0 && inset.cheight > 0 && inset.cleft != 0xffff && inset.ctop != 0xffff)
  {
    top = inset.ctop;
    left = inset.cleft;
    height = inset.cheight;
    width = inset.cwidth;
  }
  if (top < 0)
  {
    return;
  }
  // Relative to the image: the active area, or LibRaw's when the whole frame
  // is not kept.
  top -= image.sensor.empty() ? sizes.top_margin : image.active_area.top;
  left -= image.sensor.empty() ? sizes.left_margin : image.active_area.left;
  if (top < 0 || left < 0 || top + height > image.height || left + width > image.width)
  {
    return;
  }
  image.default_crop = { static_cast<uint32_t>(top), static_cast<uint32_t>(left), static_cast<uint32_t>(height),
                         static_cast<uint32_t>(width) };
}

void readCfa(LibRaw& raw, RawImage& image, const std::string& name, const CameraTraits* traits)
{
  const auto& d = raw.imgdata;
  if (d.idata.filters == 9)
  {
    throw Error(name + ": Fujifilm X-Trans sensors are not supported");
  }
  // Below 1000, `filters` is a code for a layout other than a pattern of 8
  // rows by 2 columns (e.g. 1 for Leaf's 16x16), which COLOR() does not tell.
  if (d.idata.filters < 1000)
  {
    throw Error(name + ": unsupported colour filter array");
  }
  if (d.idata.colors != 3)
  {
    throw Error(name + ": only RGB colour filter arrays are supported");
  }
  // The pattern must repeat every two rows and columns: Bayer.
  for (int row = 0; row < 8; ++row)
  {
    for (int col = 0; col < 8; ++col)
    {
      if (raw.COLOR(row, col) != raw.COLOR(row & 1, col & 1))
      {
        throw Error(name + ": only 2x2 Bayer colour filter arrays are supported");
      }
    }
  }

  // The active area: Adobe's for the camera when the table has it, else
  // LibRaw's. They can differ: LibRaw starts the 5D Mark II's one row lower.
  const auto& sizes = d.sizes;
  Rect active{ sizes.top_margin, sizes.left_margin, sizes.height, sizes.width };
  if (traits != nullptr && traits->active_area)
  {
    const auto& a = *traits->active_area;
    if (a[0] < a[2] && a[1] < a[3] && a[2] <= sizes.raw_height && a[3] <= sizes.raw_width)
    {
      active = { a[0], a[1], a[2] - a[0], a[3] - a[1] };
    }
  }

  image.layout = RawLayout::CFA;
  image.samples_per_pixel = 1;
  image.width = active.width;
  image.height = active.height;
  image.bits_per_sample = d.color.raw_bps > 0 ? d.color.raw_bps : 16;

  // LibRaw's COLOR() counts from its own active area. The pattern repeats
  // every two rows and columns, so only the parity of the shift matters.
  const int row_shift = ((static_cast<int>(active.top) - sizes.top_margin) % 2 + 2) % 2;
  const int col_shift = ((static_cast<int>(active.left) - sizes.left_margin) % 2 + 2) % 2;
  const auto& color = d.color;
  for (int i = 0; i < 4; ++i)
  {
    const int row = i / 2 + row_shift;
    const int col = i % 2 + col_shift;
    const int slot = raw.COLOR(row, col);
    const uint8_t rgb = colorOfSlot(raw, slot);
    if (rgb > 2)
    {
      throw Error(name + ": unsupported colour filter array");
    }
    image.cfa_pattern[i] = rgb;
    double black = color.black + color.cblack[slot];
    if (color.cblack[4] > 0 && color.cblack[5] > 0)
    {
      black += color.cblack[6 + (row % color.cblack[4]) * color.cblack[5] + col % color.cblack[5]];
    }
    image.black_level[i] = black;
  }

  const uint16_t* raw_data = d.rawdata.raw_image;
  const size_t pitch = sizes.raw_pitch / 2;
  image.data.resize(static_cast<size_t>(image.width) * image.height);
  for (uint32_t row = 0; row < image.height; ++row)
  {
    const uint16_t* src = raw_data + (row + active.top) * pitch + active.left;
    std::copy(src, src + image.width, image.data.begin() + static_cast<size_t>(row) * image.width);
  }

  // The whole frame, masked border included, as Adobe DNG Converter keeps it.
  image.sensor_width = sizes.raw_width;
  image.sensor_height = sizes.raw_height;
  image.active_area = active;
  image.sensor.resize(static_cast<size_t>(image.sensor_width) * image.sensor_height);
  for (uint32_t row = 0; row < image.sensor_height; ++row)
  {
    const uint16_t* src = raw_data + row * pitch;
    std::copy(src, src + image.sensor_width, image.sensor.begin() + static_cast<size_t>(row) * image.sensor_width);
  }
}

void readLinear(LibRaw& raw, RawImage& image)
{
  const auto& d = raw.imgdata;
  image.layout = RawLayout::LINEAR_RAW;
  image.samples_per_pixel = 3;
  image.width = d.sizes.width;
  image.height = d.sizes.height;
  image.bits_per_sample = d.color.raw_bps > 0 ? d.color.raw_bps : 16;
  for (int c = 0; c < 3; ++c)
  {
    image.black_level[c] = d.color.black + d.color.cblack[c];
  }

  const size_t pixels = static_cast<size_t>(image.width) * image.height;
  image.data.resize(pixels * 3);
  const size_t raw_width = d.sizes.raw_width;
  for (uint32_t row = 0; row < image.height; ++row)
  {
    for (uint32_t col = 0; col < image.width; ++col)
    {
      const size_t src = (row + d.sizes.top_margin) * raw_width + col + d.sizes.left_margin;
      for (int c = 0; c < 3; ++c)
      {
        image.at(row, col, c) =
            d.rawdata.color4_image ? d.rawdata.color4_image[src][c] : d.rawdata.color3_image[src][c];
      }
    }
  }
}

void readPreview(LibRaw& raw, RawImage& image)
{
  if (raw.unpack_thumb() != LIBRAW_SUCCESS)
  {
    return;
  }
  const auto& thumb = raw.imgdata.thumbnail;
  if (thumb.tformat != LIBRAW_THUMBNAIL_JPEG || thumb.tlength < 4 || thumb.thumb == nullptr ||
      static_cast<uint8_t>(thumb.thumb[0]) != 0xff || static_cast<uint8_t>(thumb.thumb[1]) != 0xd8)
  {
    return;
  }
  image.preview_jpeg.assign(thumb.thumb, thumb.thumb + thumb.tlength);
  image.preview_width = thumb.twidth;
  image.preview_height = thumb.theight;
}

}  // namespace

RawImage readRawBuffer(const uint8_t* data, size_t size, const std::string& file_name)
{
  return LibRawDecoder().decode(data, size, file_name);
}

LibRawDecoder::LibRawDecoder()
  // The built-in table lives as long as the program: no ownership to share.
  : cameras_(std::shared_ptr<const CameraTable>(std::shared_ptr<void>(), &CameraTable::builtIn()))
{
}

LibRawDecoder::LibRawDecoder(std::shared_ptr<const CameraTable> cameras) : cameras_(std::move(cameras))
{
}

RawImage LibRawDecoder::decode(const uint8_t* data, size_t size, const std::string& file_name) const
{
  // LibRaw is large (several hundred KB): keep it off the stack.
  auto raw = std::make_unique<LibRaw>();
  check(raw->open_buffer(data, size), "cannot open", file_name);
  check(raw->unpack(), "cannot decode", file_name);

  RawImage image;
  readMetadata(*raw, image.metadata);
  image.metadata.original_file_name = file_name;
  // The original file's own EXIF, GPS and maker note, when it is TIFF based.
  tiff::copyTiffMetadata(data, size, image.metadata);

  const CameraTraits* traits = cameras_ ? cameras_->find(image.metadata.unique_camera_model) : nullptr;
  const auto& d = raw->imgdata;
  if (d.rawdata.raw_image != nullptr && d.idata.filters != 0)
  {
    readCfa(*raw, image, file_name, traits);
  }
  else if (d.rawdata.color4_image != nullptr || d.rawdata.color3_image != nullptr)
  {
    readLinear(*raw, image);
  }
  else
  {
    throw Error(file_name + ": unsupported raw data layout");
  }
  readColor(*raw, image);
  readCrop(*raw, image);
  // As Adobe DNG Converter does: the original's own orientation, else LibRaw's.
  // They can differ: Canon's maker note records how the camera was held.
  image.orientation = image.metadata.orientation != 0 ? image.metadata.orientation : orientationFromFlip(d.sizes.flip);
  readPreview(*raw, image);
  if (traits != nullptr)
  {
    image.camera_traits = *traits;
  }
  return image;
}

RawImage readRawFile(const std::filesystem::path& path)
{
  const std::vector<uint8_t> data = LocalFileStore().read(path);
  return LibRawDecoder().decode(data.data(), data.size(), path.filename().string());
}

bool isRawFileName(const std::filesystem::path& path)
{
  static const std::set<std::string> EXTENSIONS = { ".3fr", ".ari", ".arw", ".bay", ".cap", ".cr2", ".cr3", ".crw",
                                                    ".dcr", ".dcs", ".erf", ".fff", ".iiq", ".k25", ".kdc", ".mef",
                                                    ".mos", ".mrw", ".nef", ".nrw", ".orf", ".ori", ".pef", ".raf",
                                                    ".raw", ".rw2", ".rwl", ".sr2", ".srf", ".srw", ".x3f" };
  std::string ext = path.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
  return EXTENSIONS.count(ext) != 0;
}

std::string librawVersion()
{
  return LibRaw::version();
}

}  // namespace rawconv
