// Copyright (c) 2026 Giovanni Remigi
// SPDX-License-Identifier: MIT

// Reads the tags of a DNG file in the tests, with the library's own TIFF
// reader.

#pragma once

#include <string>
#include <vector>

#include "tiff/tiff_reader.hpp"
#include "tiff/tiff_writer.hpp"

namespace dngconv::test
{

class DngTags
{
public:
  // `bytes` must outlive the object.
  explicit DngTags(const std::vector<uint8_t>& bytes)
    : reader(*tiff::TiffReader::open(bytes.data(), bytes.size())), bytes_(bytes)
  {
    sub_ifd = *reader.readUInt(*reader.find(reader.firstIfdOffset(), 330));
  }

  size_t ifd0() const
  {
    return reader.firstIfdOffset();
  }

  bool has(size_t ifd, uint16_t tag) const
  {
    return reader.find(ifd, tag).has_value();
  }

  std::string ascii(size_t ifd, uint16_t tag) const
  {
    auto e = reader.find(ifd, tag);
    return e ? std::string(reinterpret_cast<const char*>(bytes_.data() + e->value_offset), e->value_size - 1) : "";
  }

  // The integers of an entry; the numerators and denominators, in turn, of
  // a rational one.
  std::vector<uint32_t> values(size_t ifd, uint16_t tag) const
  {
    std::vector<uint32_t> out;
    auto e = reader.find(ifd, tag);
    if (!e)
    {
      return out;
    }
    const auto le = reader.toLittleEndian(*e);
    const size_t unit = tiff::typeSize(e->type) == 8 ? 4 : tiff::typeSize(e->type);
    for (size_t i = 0; i + unit <= le.data.size(); i += unit)
    {
      uint32_t v = 0;
      for (size_t b = 0; b < unit; ++b)
      {
        v |= static_cast<uint32_t>(le.data[i + b]) << (8 * b);
      }
      out.push_back(v);
    }
    return out;
  }

  // The values of an entry as numbers, whatever its type.
  std::vector<double> numbers(size_t ifd, uint16_t tag) const
  {
    std::vector<double> out;
    auto e = reader.find(ifd, tag);
    if (!e)
    {
      return out;
    }
    const auto v = values(ifd, tag);
    const bool rational = e->type == tiff::RATIONAL || e->type == tiff::SRATIONAL;
    const bool is_signed = e->type == tiff::SRATIONAL || e->type == tiff::SLONG || e->type == tiff::SSHORT;
    auto number = [&](uint32_t x) { return is_signed ? static_cast<double>(static_cast<int32_t>(x)) : x; };
    for (size_t i = 0; i < v.size(); i += rational ? 2 : 1)
    {
      out.push_back(rational ? number(v[i]) / number(v[i + 1]) : number(v[i]));
    }
    return out;
  }

  tiff::TiffReader reader;
  size_t sub_ifd = 0;

private:
  const std::vector<uint8_t>& bytes_;
};

}  // namespace dngconv::test
