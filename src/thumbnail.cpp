// Copyright (c) 2026 Giovanni Remigi
// SPDX-License-Identifier: MIT

#include "thumbnail.hpp"

#include <algorithm>
#include <cmath>

namespace dngconv
{

namespace
{

using Matrix = std::array<double, 9>;

// Linear sRGB (D65) to XYZ.
constexpr Matrix XYZ_FROM_SRGB = { 0.412453, 0.357580, 0.180423, 0.212671, 0.715160,
                                   0.072169, 0.019334, 0.119193, 0.950227 };

Matrix multiply(const Matrix& a, const Matrix& b)
{
  Matrix r{};
  for (int i = 0; i < 3; ++i)
  {
    for (int j = 0; j < 3; ++j)
    {
      for (int k = 0; k < 3; ++k)
      {
        r[i * 3 + j] += a[i * 3 + k] * b[k * 3 + j];
      }
    }
  }
  return r;
}

Matrix invert(const Matrix& m)
{
  const double det =
      m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) + m[2] * (m[3] * m[7] - m[4] * m[6]);
  if (std::abs(det) < 1e-12)
  {
    return { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
  }
  const double inv = 1.0 / det;
  return { (m[4] * m[8] - m[5] * m[7]) * inv, (m[2] * m[7] - m[1] * m[8]) * inv, (m[1] * m[5] - m[2] * m[4]) * inv,
           (m[5] * m[6] - m[3] * m[8]) * inv, (m[0] * m[8] - m[2] * m[6]) * inv, (m[2] * m[3] - m[0] * m[5]) * inv,
           (m[3] * m[7] - m[4] * m[6]) * inv, (m[1] * m[6] - m[0] * m[7]) * inv, (m[0] * m[4] - m[1] * m[3]) * inv };
}

uint8_t toSrgb8(double linear)
{
  linear = std::clamp(linear, 0.0, 1.0);
  const double v = linear <= 0.0031308 ? 12.92 * linear : 1.055 * std::pow(linear, 1 / 2.4) - 0.055;
  return static_cast<uint8_t>(std::lround(v * 255));
}

}  // namespace

std::array<double, 9> cameraToSrgb(const RawImage& image)
{
  // sRGB to camera, each row scaled so that sRGB white gives camera (1, 1, 1);
  // its inverse then maps white balanced camera values to sRGB.
  Matrix cam_from_srgb = multiply(image.color_matrix, XYZ_FROM_SRGB);
  for (int row = 0; row < 3; ++row)
  {
    const double sum = cam_from_srgb[row * 3] + cam_from_srgb[row * 3 + 1] + cam_from_srgb[row * 3 + 2];
    if (std::abs(sum) > 1e-9)
    {
      for (int col = 0; col < 3; ++col)
      {
        cam_from_srgb[row * 3 + col] /= sum;
      }
    }
  }
  return invert(cam_from_srgb);
}

RgbImage renderThumbnail(const RawImage& image, uint32_t max_side)
{
  const Rect crop = image.default_crop.width > 0 ? image.default_crop : Rect{ 0, 0, image.height, image.width };
  const bool cfa = image.layout == RawLayout::CFA;
  // A CFA image is read in 2x2 cells, each giving one RGB value.
  const uint32_t step = cfa ? 2 : 1;
  const uint32_t cells_w = std::max<uint32_t>(1, crop.width / step);
  const uint32_t cells_h = std::max<uint32_t>(1, crop.height / step);
  const double scale = std::max(1.0, static_cast<double>(std::max(cells_w, cells_h)) / max_side);

  RgbImage out;
  out.width = std::max<uint32_t>(1, static_cast<uint32_t>(std::lround(cells_w / scale)));
  out.height = std::max<uint32_t>(1, static_cast<uint32_t>(std::lround(cells_h / scale)));

  std::array<double, 3> range{};
  for (int c = 0; c < 3; ++c)
  {
    range[c] = std::max(1.0, image.white_level - (cfa ? image.black_level[0] : image.black_level[c]));
  }

  // Box filter each output pixel over its cells, in camera space, normalised
  // to the white level and white balanced.
  std::vector<std::array<double, 3>> linear(static_cast<size_t>(out.width) * out.height);
  for (uint32_t oy = 0; oy < out.height; ++oy)
  {
    const uint32_t y0 = oy * cells_h / out.height;
    const uint32_t y1 = std::max(y0 + 1, (oy + 1) * cells_h / out.height);
    for (uint32_t ox = 0; ox < out.width; ++ox)
    {
      const uint32_t x0 = ox * cells_w / out.width;
      const uint32_t x1 = std::max(x0 + 1, (ox + 1) * cells_w / out.width);
      std::array<double, 3> sum{};
      std::array<uint32_t, 3> count{};
      for (uint32_t cy = y0; cy < y1; ++cy)
      {
        for (uint32_t cx = x0; cx < x1; ++cx)
        {
          const uint32_t row = crop.top + cy * step;
          const uint32_t col = crop.left + cx * step;
          if (cfa)
          {
            for (uint32_t dy = 0; dy < 2 && row + dy < image.height; ++dy)
            {
              for (uint32_t dx = 0; dx < 2 && col + dx < image.width; ++dx)
              {
                const uint8_t c = image.cfaColor(row + dy, col + dx);
                sum[c] += image.at(row + dy, col + dx) - image.blackAt(row + dy, col + dx);
                ++count[c];
              }
            }
          }
          else
          {
            for (int c = 0; c < 3; ++c)
            {
              sum[c] += image.at(row, col, c) - image.black_level[c];
              ++count[c];
            }
          }
        }
      }
      auto& px = linear[static_cast<size_t>(oy) * out.width + ox];
      for (int c = 0; c < 3; ++c)
      {
        px[c] = count[c] ? sum[c] / count[c] / range[c] / std::max(1e-6, image.as_shot_neutral[c]) : 0;
      }
    }
  }

  const Matrix to_srgb = cameraToSrgb(image);
  std::vector<double> luminance;
  luminance.reserve(linear.size());
  for (auto& px : linear)
  {
    const std::array<double, 3> cam = px;
    for (int i = 0; i < 3; ++i)
    {
      px[i] = to_srgb[i * 3] * cam[0] + to_srgb[i * 3 + 1] * cam[1] + to_srgb[i * 3 + 2] * cam[2];
    }
    luminance.push_back(0.2126 * px[0] + 0.7152 * px[1] + 0.0722 * px[2]);
  }

  // A simple automatic exposure: the median at mid grey, within 0 and +3
  // stops, as raw data is usually exposed below what a JPEG shows.
  std::nth_element(luminance.begin(), luminance.begin() + luminance.size() / 2, luminance.end());
  const double median = luminance[luminance.size() / 2];
  const double gain = std::pow(2.0, image.camera_traits.baseline_exposure.value_or(0)) *
                      std::clamp(median > 0 ? 0.18 / median : 1.0, 1.0, 8.0);

  out.rgb.resize(linear.size() * 3);
  for (size_t i = 0; i < linear.size(); ++i)
  {
    for (int c = 0; c < 3; ++c)
    {
      // A soft shoulder instead of a hard clip for the highlights.
      const double v = linear[i][c] * gain;
      out.rgb[i * 3 + c] = toSrgb8(v / (1 + 0.15 * v) * 1.15);
    }
  }
  return out;
}

}  // namespace dngconv
