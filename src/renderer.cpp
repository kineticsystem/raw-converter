// Copyright (c) 2026 Giovanni Remigi
// SPDX-License-Identifier: MIT

#include "rawconv/renderer.hpp"

#include <libraw/libraw.h>

#include <algorithm>
#include <memory>
#include <string>

#include "rawconv/bad_pixels.hpp"
#include "rawconv/dng_writer.hpp"
#include "rawconv/error.hpp"

namespace rawconv
{

namespace
{

// LibRaw's output_color for each colour space.
int librawColorSpace(ColorSpace color_space)
{
  switch (color_space)
  {
    case ColorSpace::SRGB:
      return 1;
    case ColorSpace::PROPHOTO:
      return 4;
    case ColorSpace::REC2020:
      return 8;
  }
  return 8;
}

void check(int code, const char* what)
{
  if (code != LIBRAW_SUCCESS)
  {
    throw Error(std::string("cannot render the image: ") + what + ": " + libraw_strerror(code));
  }
}

// LibRaw 0.21 misplaces the samples of a DNG whose stored image is at most two
// tiles of 256 pixels wide (see test_dng_writer.cpp). No camera makes images
// that narrow, but an image can be: it is widened past this.
constexpr uint32_t MIN_STORED_WIDTH = 512;

// Widens the image past MIN_STORED_WIDTH, without its sensor frame, by
// repeating its last two columns, which keeps the colour filter pattern. The
// default crop, which stays as it was, leaves them out.
void widen(RawImage& image)
{
  const Rect crop = image.default_crop;
  if (crop.width == 0 || crop.height == 0 || crop.left + crop.width > image.width ||
      crop.top + crop.height > image.height)
  {
    image.default_crop = { 0, 0, image.height, image.width };
  }
  const uint32_t width = MIN_STORED_WIDTH + 2;
  const uint32_t spp = image.samples_per_pixel;
  std::vector<uint16_t> data(static_cast<size_t>(width) * image.height * spp);
  for (uint32_t row = 0; row < image.height; ++row)
  {
    for (uint32_t col = 0; col < width; ++col)
    {
      // Past the edge, the last column of the same parity: the same colour.
      const uint32_t from = col < image.width ? col : image.width - 1 - ((col - (image.width - 1)) & 1);
      for (uint32_t s = 0; s < spp; ++s)
      {
        data[(static_cast<size_t>(row) * width + col) * spp + s] = image.at(row, from, s);
      }
    }
  }
  image.data = std::move(data);
  image.width = width;
  image.sensor.clear();
  image.sensor_width = 0;
  image.sensor_height = 0;
  image.active_area = {};
}

struct ProcessedImageDeleter
{
  void operator()(libraw_processed_image_t* image) const
  {
    LibRaw::dcraw_clear_mem(image);
  }
};

}  // namespace

RenderedImage renderImage(const RawImage& image, ColorSpace color_space)
{
  // LibRaw renders files, not images in memory: give it the image as an
  // uncompressed DNG, which describes it completely, black and white levels,
  // colour matrix, white balance and crop included. The repairs a DNG reader
  // would make from the opcode, LibRaw does not: make them in the data.
  const RawImage* source = &image;
  RawImage changed;
  if (!image.opcode_bad_pixels.empty())
  {
    changed = image;
    repairBadPixels(changed, changed.opcode_bad_pixels);
    changed.opcode_bad_pixels.clear();
    source = &changed;
  }
  const uint32_t stored_width = image.sensor.empty() ? image.width : image.sensor_width;
  if (stored_width <= MIN_STORED_WIDTH)
  {
    if (source != &changed)
    {
      changed = image;
      source = &changed;
    }
    widen(changed);
  }
  DngWriteOptions dng;
  dng.compression = Compression::NONE;
  dng.embed_preview = false;
  const std::vector<uint8_t> file = encodeDng(*source, dng);

  // LibRaw is large (several hundred KB): keep it off the stack.
  auto raw = std::make_unique<LibRaw>();
  auto& params = raw->imgdata.params;
  params.output_bps = 16;
  params.gamm[0] = 1;  // Linear.
  params.gamm[1] = 1;
  params.no_auto_bright = 1;
  params.use_camera_wb = 1;
  params.use_camera_matrix = 1;
  params.output_color = librawColorSpace(color_space);
  params.user_flip = 0;
  params.user_qual = 3;  // AHD.
  params.highlight = 0;  // Clip.
  check(raw->open_buffer(file.data(), file.size()), "cannot open");
  check(raw->unpack(), "cannot decode");
  check(raw->dcraw_process(), "cannot process");
  int code = LIBRAW_SUCCESS;
  std::unique_ptr<libraw_processed_image_t, ProcessedImageDeleter> processed(raw->dcraw_make_mem_image(&code));
  check(code, "cannot make the image");
  if (!processed || processed->type != LIBRAW_IMAGE_BITMAP || processed->colors != 3 || processed->bits != 16)
  {
    throw Error("cannot render the image: LibRaw made no 16-bit RGB image");
  }
  // LibRaw renders the DNG's active area, the image, or nearly: it may leave
  // out a row or a column, e.g. the last row of an odd number of rows. Where
  // its rendering lies in the image, from the margins it read, counted in the
  // stored frame as the image's own position is.
  const auto& sizes = raw->imgdata.sizes;
  const int64_t top = static_cast<int64_t>(sizes.top_margin) - (source->sensor.empty() ? 0 : source->active_area.top);
  const int64_t left =
      static_cast<int64_t>(sizes.left_margin) - (source->sensor.empty() ? 0 : source->active_area.left);
  Rect crop = image.default_crop;
  if (crop.width == 0 || crop.height == 0 || crop.left + crop.width > image.width ||
      crop.top + crop.height > image.height)
  {
    crop = { 0, 0, image.height, image.width };
  }
  // The default crop, within what LibRaw rendered.
  const int64_t crop_top = std::max<int64_t>(crop.top, top);
  const int64_t crop_left = std::max<int64_t>(crop.left, left);
  const int64_t crop_bottom = std::min<int64_t>(crop.top + crop.height, top + processed->height);
  const int64_t crop_right = std::min<int64_t>(crop.left + crop.width, left + processed->width);
  if (crop_bottom <= crop_top || crop_right <= crop_left)
  {
    throw Error("cannot render the image: LibRaw rendered none of the default crop");
  }
  crop = { static_cast<uint32_t>(crop_top - top), static_cast<uint32_t>(crop_left - left),
           static_cast<uint32_t>(crop_bottom - crop_top), static_cast<uint32_t>(crop_right - crop_left) };

  RenderedImage out;
  out.width = crop.width;
  out.height = crop.height;
  out.color_space = color_space;
  out.orientation = image.orientation;
  out.metadata = image.metadata;
  out.data.resize(static_cast<size_t>(out.width) * out.height * 3);
  const auto* pixels = reinterpret_cast<const uint16_t*>(processed->data);
  for (uint32_t row = 0; row < out.height; ++row)
  {
    const uint16_t* src = pixels + ((static_cast<size_t>(row) + crop.top) * processed->width + crop.left) * 3;
    std::copy(src, src + static_cast<size_t>(out.width) * 3,
              out.data.begin() + static_cast<size_t>(row) * out.width * 3);
  }
  return out;
}

}  // namespace rawconv
