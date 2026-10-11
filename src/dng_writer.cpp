// Copyright (c) 2026 Giovanni Remigi
// SPDX-License-Identifier: MIT

#include "rawconv/dng_writer.hpp"

#include <algorithm>
#include <cmath>

#include "rawconv/error.hpp"
#include "rawconv/file_store.hpp"
#include "lj92_encoder.hpp"
#include "parallel.hpp"
#include "thumbnail.hpp"
#include "tiff/tiff_writer.hpp"

namespace rawconv
{

namespace
{

using tiff::Ifd;

// TIFF/EP, EXIF and DNG tags.
enum Tag : uint16_t
{
  NEW_SUBFILE_TYPE = 254,
  IMAGE_WIDTH = 256,
  IMAGE_LENGTH = 257,
  BITS_PER_SAMPLE = 258,
  COMPRESSION = 259,
  PHOTOMETRIC = 262,
  IMAGE_DESCRIPTION = 270,
  MAKE = 271,
  MODEL = 272,
  STRIP_OFFSETS = 273,
  ORIENTATION = 274,
  SAMPLES_PER_PIXEL = 277,
  ROWS_PER_STRIP = 278,
  STRIP_BYTE_COUNTS = 279,
  PLANAR_CONFIGURATION = 284,
  SOFTWARE = 305,
  DATE_TIME = 306,
  ARTIST = 315,
  TILE_WIDTH = 322,
  TILE_LENGTH = 323,
  TILE_OFFSETS = 324,
  TILE_BYTE_COUNTS = 325,
  SUB_IFDS = 330,
  YCBCR_SUBSAMPLING = 530,
  CFA_REPEAT_PATTERN_DIM = 33421,
  CFA_PATTERN = 33422,
  COPYRIGHT = 33432,
  EXIF_IFD = 34665,
  GPS_IFD = 34853,
  DNG_VERSION = 50706,
  DNG_BACKWARD_VERSION = 50707,
  UNIQUE_CAMERA_MODEL = 50708,
  CFA_PLANE_COLOR = 50710,
  CFA_LAYOUT = 50711,
  BLACK_LEVEL_REPEAT_DIM = 50713,
  BLACK_LEVEL = 50714,
  WHITE_LEVEL = 50717,
  DEFAULT_SCALE = 50718,
  DEFAULT_CROP_ORIGIN = 50719,
  DEFAULT_CROP_SIZE = 50720,
  COLOR_MATRIX_1 = 50721,
  AS_SHOT_NEUTRAL = 50728,
  BASELINE_EXPOSURE = 50730,
  BASELINE_NOISE = 50731,
  BASELINE_SHARPNESS = 50732,
  BAYER_GREEN_SPLIT = 50733,
  CAMERA_SERIAL_NUMBER = 50735,
  LENS_INFO = 50736,
  DNG_PRIVATE_DATA = 50740,
  CALIBRATION_ILLUMINANT_1 = 50778,
  ORIGINAL_RAW_FILE_NAME = 50827,
  RAW_DATA_UNIQUE_ID = 50781,
  ACTIVE_AREA = 50829,
  PREVIEW_APPLICATION_NAME = 50966,
  PREVIEW_APPLICATION_VERSION = 50967,
  PREVIEW_COLOR_SPACE = 50970,
  OPCODE_LIST_1 = 51008,
};

enum ExifTag : uint16_t
{
  EXPOSURE_TIME = 0x829a,
  F_NUMBER = 0x829d,
  ISO_SPEED = 0x8827,
  EXIF_VERSION = 0x9000,
  DATE_TIME_ORIGINAL = 0x9003,
  FOCAL_LENGTH = 0x920a,
  BODY_SERIAL_NUMBER = 0xa431,
  LENS_SPECIFICATION = 0xa432,
  LENS_MODEL = 0xa434,
};

constexpr uint16_t COMPRESSION_NONE = 1;
constexpr uint16_t COMPRESSION_JPEG = 7;
constexpr uint16_t PHOTOMETRIC_RGB = 2;
constexpr uint16_t PHOTOMETRIC_YCBCR = 6;
constexpr uint16_t PHOTOMETRIC_CFA = 32803;
constexpr uint16_t PHOTOMETRIC_LINEAR_RAW = 34892;
constexpr uint16_t ILLUMINANT_D65 = 21;
constexpr uint32_t TILE_SIZE = 256;
constexpr uint32_t OPCODE_FIX_BAD_PIXELS_LIST = 5;

void appendBE32(std::vector<uint8_t>& out, uint32_t v)
{
  for (int i = 3; i >= 0; --i)
  {
    out.push_back(static_cast<uint8_t>(v >> (8 * i)));
  }
}

// A fraction close to `value`, as a numerator/denominator pair, exact for the
// usual exposure times (1/200) and apertures (5.6).
std::vector<uint32_t> fraction(double value)
{
  if (value <= 0)
  {
    return { 0, 1 };
  }
  if (value < 1)
  {
    const double inverse = 1 / value;
    if (std::abs(inverse - std::round(inverse)) < 1e-3)
    {
      return { 1, static_cast<uint32_t>(std::lround(inverse)) };
    }
  }
  return { static_cast<uint32_t>(std::lround(value * 1000)), 1000 };
}

// The image the raw directory stores: the whole frame of the sensor, with
// the image's samples, repaired or not, in its active area; or, without a
// frame, the image alone.
class StoredImage
{
public:
  explicit StoredImage(const RawImage& image)
    : image_(image)
    , frame_(!image.sensor.empty())
    , width(frame_ ? image.sensor_width : image.width)
    , height(frame_ ? image.sensor_height : image.height)
    , top(frame_ ? image.active_area.top : 0)
    , left(frame_ ? image.active_area.left : 0)
  {
  }

  uint16_t at(uint32_t row, uint32_t col, uint32_t sample) const
  {
    if (row >= top && col >= left && row - top < image_.height && col - left < image_.width)
    {
      return image_.at(row - top, col - left, sample);
    }
    return image_.sensor[static_cast<size_t>(row) * width + col];
  }

  bool hasFrame() const
  {
    return frame_;
  }

private:
  const RawImage& image_;
  bool frame_;

public:
  const uint32_t width;
  const uint32_t height;
  // Where the image's first sample is.
  const uint32_t top;
  const uint32_t left;
};

// The tiles of the stored image, encoded, in TIFF order (left to right, top
// to bottom). Tiles across the right and bottom edges are padded by repeating
// the last column and row, as Adobe DNG Converter does.
std::vector<std::vector<uint8_t>> encodeTiles(const RawImage& image, Compression compression, unsigned threads)
{
  const StoredImage stored(image);
  const uint32_t spp = image.samples_per_pixel;
  const uint32_t across = (stored.width + TILE_SIZE - 1) / TILE_SIZE;
  const uint32_t down = (stored.height + TILE_SIZE - 1) / TILE_SIZE;
  std::vector<std::vector<uint8_t>> tiles(static_cast<size_t>(across) * down);

  auto encode = [&](size_t index) {
    const uint32_t tile_top = static_cast<uint32_t>(index / across) * TILE_SIZE;
    const uint32_t tile_left = static_cast<uint32_t>(index % across) * TILE_SIZE;
    std::vector<uint16_t> samples(static_cast<size_t>(TILE_SIZE) * TILE_SIZE * spp);
    for (uint32_t y = 0; y < TILE_SIZE; ++y)
    {
      const uint32_t row = std::min(tile_top + y, stored.height - 1);
      for (uint32_t x = 0; x < TILE_SIZE; ++x)
      {
        const uint32_t col = std::min(tile_left + x, stored.width - 1);
        for (uint32_t s = 0; s < spp; ++s)
        {
          samples[(static_cast<size_t>(y) * TILE_SIZE + x) * spp + s] = stored.at(row, col, s);
        }
      }
    }
    if (compression == Compression::LOSSLESS_JPEG)
    {
      // A CFA tile as two interleaved components, so that each sample is
      // predicted from its neighbour of the same colour; a linear raw tile
      // with one component per colour.
      const uint32_t components = spp == 1 ? 2 : spp;
      tiles[index] = encodeLosslessJpeg(samples.data(), TILE_SIZE * spp, TILE_SIZE, components);
    }
    else
    {
      auto& out = tiles[index];
      out.resize(samples.size() * 2);
      for (size_t i = 0; i < samples.size(); ++i)
      {
        out[2 * i] = static_cast<uint8_t>(samples[i]);
        out[2 * i + 1] = static_cast<uint8_t>(samples[i] >> 8);
      }
    }
  };

  parallelFor(tiles.size(), std::max(1u, threads), encode);
  return tiles;
}

// The DNG FixBadPixelsList opcode, in OpcodeList1, which is applied to the
// raw data as read from the file: its pixels and its phase are those of the
// stored image, whole frame included. Opcode lists are always big-endian.
std::vector<uint8_t> badPixelOpcodeList(const RawImage& image, std::vector<PixelPos> pixels)
{
  const StoredImage stored(image);
  for (auto& pixel : pixels)
  {
    pixel.row += stored.top;
    pixel.col += stored.left;
  }
  std::sort(pixels.begin(), pixels.end());
  pixels.erase(std::unique(pixels.begin(), pixels.end()), pixels.end());

  // The phase of the Bayer pattern at the stored image's top-left pixel: 0 =
  // red, 1 = green on a red row, 2 = green on a blue row, 3 = blue. The
  // image's pattern starts at the active area.
  std::array<uint8_t, 4> p{};
  for (uint32_t i = 0; i < 4; ++i)
  {
    p[i] = image.cfa_pattern[((i / 2 + stored.top) & 1) * 2 + ((i % 2 + stored.left) & 1)];
  }
  uint32_t phase = 0;
  if (p[0] == 1)
  {
    phase = p[1] == 0 ? 1 : 2;
  }
  else if (p[0] == 2)
  {
    phase = 3;
  }

  std::vector<uint8_t> params;
  appendBE32(params, phase);
  appendBE32(params, static_cast<uint32_t>(pixels.size()));
  appendBE32(params, 0);  // No rectangles.
  for (const auto& pixel : pixels)
  {
    appendBE32(params, pixel.row);
    appendBE32(params, pixel.col);
  }

  std::vector<uint8_t> list;
  appendBE32(list, 1);  // One opcode.
  appendBE32(list, OPCODE_FIX_BAD_PIXELS_LIST);
  list.insert(list.end(), { 1, 3, 0, 0 });  // DNG version the opcode needs.
  // Flags: optional, so that a reader that cannot apply it still opens the
  // file; and to be applied to previews too.
  appendBE32(list, 1);
  appendBE32(list, static_cast<uint32_t>(params.size()));
  list.insert(list.end(), params.begin(), params.end());
  return list;
}

// RawDataUniqueID: 16 bytes that tell this raw image from any other, and that
// are the same for every DNG made from it, which is how photo managers
// recognise two copies of one photo. Any 16 unique bytes will do: here, two 64-bit
// FNV-1a hashes of the samples and the size, from different seeds.
std::vector<uint8_t> rawDataUniqueId(const RawImage& image)
{
  uint64_t a = 0xcbf29ce484222325ull;
  uint64_t b = 0x84222325cbf29ce4ull;
  auto mix = [&](uint64_t v) {
    for (int i = 0; i < 8; ++i)
    {
      const uint8_t byte = static_cast<uint8_t>(v >> (8 * i));
      a = (a ^ byte) * 0x100000001b3ull;
      b = (b ^ byte) * 0x100000001b3ull;
      b ^= b >> 29;
    }
  };
  mix(image.width);
  mix(image.height);
  mix(image.samples_per_pixel);
  for (size_t i = 0; i + 4 <= image.data.size(); i += 4)
  {
    mix(static_cast<uint64_t>(image.data[i]) | static_cast<uint64_t>(image.data[i + 1]) << 16 |
        static_cast<uint64_t>(image.data[i + 2]) << 32 | static_cast<uint64_t>(image.data[i + 3]) << 48);
  }
  for (size_t i = image.data.size() & ~size_t(3); i < image.data.size(); ++i)
  {
    mix(image.data[i]);
  }
  std::vector<uint8_t> id;
  for (uint64_t v : { a, b })
  {
    for (int i = 7; i >= 0; --i)
    {
      id.push_back(static_cast<uint8_t>(v >> (8 * i)));
    }
  }
  return id;
}

// The maker note in Adobe's DNGPrivateData format: "Adobe", then a "MakN"
// block with the byte order of the original file and the maker note's offset
// in it, which readers need to follow the offsets inside the maker note.
std::vector<uint8_t> makerNotePrivateData(const Metadata& metadata)
{
  std::vector<uint8_t> out = { 'A', 'd', 'o', 'b', 'e', 0, 'M', 'a', 'k', 'N' };
  appendBE32(out, static_cast<uint32_t>(metadata.maker_note.size() + 6));
  out.push_back(metadata.maker_note_big_endian ? 'M' : 'I');
  out.push_back(metadata.maker_note_big_endian ? 'M' : 'I');
  appendBE32(out, metadata.maker_note_offset);
  out.insert(out.end(), metadata.maker_note.begin(), metadata.maker_note.end());
  return out;
}

std::shared_ptr<Ifd> exifIfd(const Metadata& metadata)
{
  auto exif = std::make_shared<Ifd>();
  for (const auto& entry : metadata.exif)
  {
    exif->addRaw(entry.tag, entry.type, entry.count, entry.data);
  }

  // A file that is not TIFF based has no EXIF to copy: rebuild the essentials.
  if (metadata.exif.empty())
  {
    if (metadata.exposure_time > 0)
    {
      exif->addRationalPairs(EXPOSURE_TIME, fraction(metadata.exposure_time));
    }
    if (metadata.f_number > 0)
    {
      exif->addRationalPairs(F_NUMBER, fraction(metadata.f_number));
    }
    if (metadata.iso > 0)
    {
      exif->addShort(ISO_SPEED, { static_cast<uint16_t>(std::min(65535.0f, metadata.iso)) });
    }
    if (!metadata.date_time.empty())
    {
      exif->addAscii(DATE_TIME_ORIGINAL, metadata.date_time);
    }
    if (metadata.focal_length > 0)
    {
      exif->addRationalPairs(FOCAL_LENGTH, fraction(metadata.focal_length));
    }
  }

  // The EXIF 2.3 lens and body fields, which older cameras such as the 5D Mark
  // II only have in their maker notes, and which photo managers show and
  // filter by.
  bool added = false;
  if (!exif->has(LENS_MODEL) && !metadata.lens_model.empty())
  {
    exif->addAscii(LENS_MODEL, metadata.lens_model);
    added = true;
  }
  if (!exif->has(LENS_SPECIFICATION) && metadata.lens_info[0] > 0)
  {
    std::vector<uint32_t> pairs;
    for (float v : metadata.lens_info)
    {
      if (v > 0)
      {
        const auto f = fraction(v);
        pairs.insert(pairs.end(), f.begin(), f.end());
      }
      else
      {
        pairs.insert(pairs.end(), { 0, 0 });  // Unknown, as EXIF 2.3 allows.
      }
    }
    exif->addRationalPairs(LENS_SPECIFICATION, pairs);
    added = true;
  }
  if (!exif->has(BODY_SERIAL_NUMBER) && !metadata.serial_number.empty())
  {
    exif->addAscii(BODY_SERIAL_NUMBER, metadata.serial_number);
    added = true;
  }
  if (added || !exif->has(EXIF_VERSION))
  {
    exif->addBytes(EXIF_VERSION, tiff::UNDEFINED, { '0', '2', '3', '0' });
  }
  return exif;
}

// The size and chroma subsampling of a baseline JPEG, from its frame header.
// Returns false if the data is not a JPEG this can parse.
bool jpegFrame(const std::vector<uint8_t>& jpeg, uint32_t& width, uint32_t& height, uint16_t& h_sub, uint16_t& v_sub)
{
  size_t pos = 2;
  while (pos + 4 <= jpeg.size())
  {
    if (jpeg[pos] != 0xff)
    {
      return false;
    }
    const uint8_t marker = jpeg[pos + 1];
    if (marker == 0xff)
    {
      ++pos;
      continue;
    }
    const size_t length = static_cast<size_t>(jpeg[pos + 2]) << 8 | jpeg[pos + 3];
    if (marker >= 0xc0 && marker <= 0xc2)
    {
      if (pos + 2 + length > jpeg.size() || length < 8 + 3)
      {
        return false;
      }
      height = static_cast<uint32_t>(jpeg[pos + 5]) << 8 | jpeg[pos + 6];
      width = static_cast<uint32_t>(jpeg[pos + 7]) << 8 | jpeg[pos + 8];
      const uint8_t components = jpeg[pos + 9];
      if (components != 3 || length < 8u + 3u * components)
      {
        return false;
      }
      // The luma sampling factors relative to the chroma ones.
      const uint8_t luma = jpeg[pos + 11];
      const uint8_t chroma = jpeg[pos + 14];
      const int ch = std::max(1, chroma >> 4);
      const int cv = std::max(1, chroma & 15);
      h_sub = static_cast<uint16_t>((luma >> 4) / ch);
      v_sub = static_cast<uint16_t>((luma & 15) / cv);
      return width > 0 && height > 0 && h_sub > 0 && v_sub > 0;
    }
    if (marker == 0xd9 || marker == 0xda)
    {
      return false;
    }
    pos += 2 + length;
  }
  return false;
}

std::shared_ptr<Ifd> previewIfd(const RawImage& image, const DngWriteOptions& options)
{
  uint32_t width = 0, height = 0;
  uint16_t h_sub = 0, v_sub = 0;
  if (!jpegFrame(image.preview_jpeg, width, height, h_sub, v_sub))
  {
    return nullptr;
  }
  auto ifd = std::make_shared<Ifd>();
  ifd->addLong(NEW_SUBFILE_TYPE, { 1 });  // A reduced resolution image.
  ifd->addLong(IMAGE_WIDTH, { width });
  ifd->addLong(IMAGE_LENGTH, { height });
  ifd->addShort(BITS_PER_SAMPLE, { 8, 8, 8 });
  ifd->addShort(COMPRESSION, { COMPRESSION_JPEG });
  ifd->addShort(PHOTOMETRIC, { PHOTOMETRIC_YCBCR });
  ifd->addShort(SAMPLES_PER_PIXEL, { 3 });
  ifd->addLong(ROWS_PER_STRIP, { height });
  ifd->addShort(PLANAR_CONFIGURATION, { 1 });
  ifd->addShort(YCBCR_SUBSAMPLING, { h_sub, v_sub });
  ifd->addAscii(PREVIEW_APPLICATION_NAME, image.metadata.make.empty() ? "Camera" : image.metadata.make);
  ifd->addAscii(PREVIEW_APPLICATION_VERSION, options.software);
  ifd->addLong(PREVIEW_COLOR_SPACE, { 2 });  // sRGB, as camera JPEGs are by default.
  ifd->addDataOffsets(STRIP_OFFSETS, STRIP_BYTE_COUNTS, { image.preview_jpeg });
  return ifd;
}

void validate(const RawImage& image)
{
  if (image.width == 0 || image.height == 0)
  {
    throw Error("The image is empty");
  }
  if (image.data.size() != static_cast<size_t>(image.width) * image.height * image.samples_per_pixel)
  {
    throw Error("The image data does not match its size");
  }
  if ((image.layout == RawLayout::CFA && image.samples_per_pixel != 1) ||
      (image.layout == RawLayout::LINEAR_RAW && image.samples_per_pixel != 3))
  {
    throw Error("The samples per pixel do not match the image layout");
  }
  if (!image.sensor.empty())
  {
    const Rect& a = image.active_area;
    if (image.layout != RawLayout::CFA ||
        image.sensor.size() != static_cast<size_t>(image.sensor_width) * image.sensor_height ||
        a.height != image.height || a.width != image.width || a.top + a.height > image.sensor_height ||
        a.left + a.width > image.sensor_width)
    {
      throw Error("The sensor frame does not match the image");
    }
  }
}

}  // namespace

std::vector<uint8_t> encodeDng(const RawImage& image, const DngWriteOptions& options)
{
  validate(image);
  const Metadata& meta = image.metadata;
  const bool cfa = image.layout == RawLayout::CFA;
  // The opcode pixels inside the image; any other would be an invalid file.
  std::vector<PixelPos> bad_pixels;
  for (const auto& p : image.opcode_bad_pixels)
  {
    if (p.row < image.height && p.col < image.width)
    {
      bad_pixels.push_back(p);
    }
  }
  const bool opcodes = cfa && !bad_pixels.empty();

  // The raw image.
  auto raw = std::make_shared<Ifd>();
  raw->addLong(NEW_SUBFILE_TYPE, { 0 });
  const StoredImage stored(image);
  raw->addLong(IMAGE_WIDTH, { stored.width });
  raw->addLong(IMAGE_LENGTH, { stored.height });
  raw->addShort(BITS_PER_SAMPLE, std::vector<uint16_t>(image.samples_per_pixel, 16));
  raw->addShort(COMPRESSION,
                { options.compression == Compression::LOSSLESS_JPEG ? COMPRESSION_JPEG : COMPRESSION_NONE });
  raw->addShort(PHOTOMETRIC, { cfa ? PHOTOMETRIC_CFA : PHOTOMETRIC_LINEAR_RAW });
  raw->addShort(SAMPLES_PER_PIXEL, { static_cast<uint16_t>(image.samples_per_pixel) });
  raw->addShort(PLANAR_CONFIGURATION, { 1 });
  raw->addLong(TILE_WIDTH, { TILE_SIZE });
  raw->addLong(TILE_LENGTH, { TILE_SIZE });
  if (cfa)
  {
    raw->addShort(CFA_REPEAT_PATTERN_DIM, { 2, 2 });
    raw->addBytes(CFA_PATTERN, tiff::BYTE, { image.cfa_pattern.begin(), image.cfa_pattern.end() });
    raw->addBytes(CFA_PLANE_COLOR, tiff::BYTE, { 0, 1, 2 });
    raw->addShort(CFA_LAYOUT, { 1 });  // Rectangular.
    raw->addShort(BLACK_LEVEL_REPEAT_DIM, { 2, 2 });
    raw->addRational(BLACK_LEVEL, { image.black_level.begin(), image.black_level.end() }, 100);
    raw->addLong(WHITE_LEVEL, { image.white_level });
  }
  else
  {
    raw->addShort(BLACK_LEVEL_REPEAT_DIM, { 1, 1 });
    raw->addRational(BLACK_LEVEL, { image.black_level.begin(), image.black_level.begin() + 3 }, 100);
    raw->addLong(WHITE_LEVEL, std::vector<uint32_t>(3, image.white_level));
  }
  if (stored.hasFrame())
  {
    // The image within the frame. The CFA pattern and the default crop count
    // from its top-left corner.
    raw->addLong(ACTIVE_AREA, { stored.top, stored.left, stored.top + image.height, stored.left + image.width });
  }
  raw->addRationalPairs(DEFAULT_SCALE, { 1, 1, 1, 1 });
  Rect crop = image.default_crop;
  if (crop.width == 0 || crop.height == 0 || crop.left + crop.width > image.width ||
      crop.top + crop.height > image.height)
  {
    crop = { 0, 0, image.height, image.width };
  }
  // Origin then size, each as horizontal then vertical.
  raw->addLong(DEFAULT_CROP_ORIGIN, { crop.left, crop.top });
  raw->addLong(DEFAULT_CROP_SIZE, { crop.width, crop.height });
  const CameraTraits& traits = image.camera_traits;
  if (cfa && traits.bayer_green_split)
  {
    raw->addLong(BAYER_GREEN_SPLIT, { *traits.bayer_green_split });
  }
  if (opcodes)
  {
    raw->addBytes(OPCODE_LIST_1, tiff::UNDEFINED, badPixelOpcodeList(image, bad_pixels));
  }
  raw->addDataOffsets(TILE_OFFSETS, TILE_BYTE_COUNTS, encodeTiles(image, options.compression, options.threads));

  std::vector<std::shared_ptr<Ifd>> sub_ifds = { raw };
  if (options.embed_preview && !image.preview_jpeg.empty())
  {
    if (auto preview = previewIfd(image, options))
    {
      sub_ifds.push_back(preview);
    }
  }

  // IFD 0: the thumbnail, and what describes the whole file.
  auto ifd0 = std::make_shared<Ifd>();
  const RgbImage thumbnail = renderThumbnail(image);
  ifd0->addLong(NEW_SUBFILE_TYPE, { 1 });
  ifd0->addLong(IMAGE_WIDTH, { thumbnail.width });
  ifd0->addLong(IMAGE_LENGTH, { thumbnail.height });
  ifd0->addShort(BITS_PER_SAMPLE, { 8, 8, 8 });
  ifd0->addShort(COMPRESSION, { COMPRESSION_NONE });
  ifd0->addShort(PHOTOMETRIC, { PHOTOMETRIC_RGB });
  ifd0->addShort(SAMPLES_PER_PIXEL, { 3 });
  ifd0->addLong(ROWS_PER_STRIP, { thumbnail.height });
  ifd0->addShort(PLANAR_CONFIGURATION, { 1 });
  ifd0->addDataOffsets(STRIP_OFFSETS, STRIP_BYTE_COUNTS, { thumbnail.rgb });
  ifd0->addShort(ORIENTATION, { image.orientation });

  if (!meta.make.empty())
  {
    ifd0->addAscii(MAKE, meta.make);
  }
  if (!meta.model.empty())
  {
    ifd0->addAscii(MODEL, meta.model);
  }
  if (!meta.description.empty())
  {
    ifd0->addAscii(IMAGE_DESCRIPTION, meta.description);
  }
  ifd0->addAscii(SOFTWARE, options.software);
  if (!meta.date_time.empty())
  {
    ifd0->addAscii(DATE_TIME, meta.date_time);
  }
  if (!meta.artist.empty())
  {
    ifd0->addAscii(ARTIST, meta.artist);
  }
  if (!meta.copyright.empty())
  {
    ifd0->addAscii(COPYRIGHT, meta.copyright);
  }
  ifd0->addChildren(SUB_IFDS, sub_ifds);
  ifd0->addChild(EXIF_IFD, exifIfd(meta));
  if (!meta.gps.empty())
  {
    auto gps = std::make_shared<Ifd>();
    for (const auto& entry : meta.gps)
    {
      gps->addRaw(entry.tag, entry.type, entry.count, entry.data);
    }
    ifd0->addChild(GPS_IFD, gps);
  }

  ifd0->addBytes(DNG_VERSION, tiff::BYTE, { 1, 4, 0, 0 });
  // Opcode lists need a reader of DNG 1.3 at least.
  ifd0->addBytes(DNG_BACKWARD_VERSION, tiff::BYTE, { 1, static_cast<uint8_t>(opcodes ? 3 : 1), 0, 0 });
  ifd0->addAscii(UNIQUE_CAMERA_MODEL, meta.unique_camera_model.empty() ?
                                          (meta.model.empty() ? "Unknown camera" : meta.model) :
                                          meta.unique_camera_model);
  ifd0->addSRational(COLOR_MATRIX_1, { image.color_matrix.begin(), image.color_matrix.end() });
  ifd0->addShort(CALIBRATION_ILLUMINANT_1, { ILLUMINANT_D65 });
  ifd0->addRational(AS_SHOT_NEUTRAL, { image.as_shot_neutral.begin(), image.as_shot_neutral.end() }, 1000000);
  ifd0->addSRational(BASELINE_EXPOSURE, { traits.baseline_exposure.value_or(0) }, 100);
  if (traits.baseline_noise)
  {
    ifd0->addRational(BASELINE_NOISE, { *traits.baseline_noise }, 100);
  }
  if (traits.baseline_sharpness)
  {
    ifd0->addRational(BASELINE_SHARPNESS, { *traits.baseline_sharpness }, 100);
  }
  ifd0->addBytes(RAW_DATA_UNIQUE_ID, tiff::BYTE, rawDataUniqueId(image));
  if (!meta.serial_number.empty())
  {
    ifd0->addAscii(CAMERA_SERIAL_NUMBER, meta.serial_number);
  }
  if (meta.lens_info[0] > 0 && meta.lens_info[1] > 0 && meta.lens_info[2] > 0 && meta.lens_info[3] > 0)
  {
    std::vector<uint32_t> pairs;
    for (float v : meta.lens_info)
    {
      const auto f = fraction(v);
      pairs.insert(pairs.end(), f.begin(), f.end());
    }
    ifd0->addRationalPairs(LENS_INFO, pairs);
  }
  if (!meta.maker_note.empty())
  {
    ifd0->addBytes(DNG_PRIVATE_DATA, tiff::BYTE, makerNotePrivateData(meta));
  }
  if (!meta.original_file_name.empty())
  {
    ifd0->addAscii(ORIGINAL_RAW_FILE_NAME, meta.original_file_name);
  }

  try
  {
    return tiff::TiffWriter::write(*ifd0);
  }
  catch (const std::exception& e)
  {
    throw Error(e.what());
  }
}

void writeDng(const RawImage& image, const std::filesystem::path& path, const DngWriteOptions& options)
{
  LocalFileStore().write(path, encodeDng(image, options));
}

}  // namespace rawconv
