// Copyright (c) 2026 Giovanni Remigi
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "dngconv/raw_image.hpp"

namespace dngconv::tiff
{

// A read-only view of a TIFF structured file -- CR2, NEF, ARW, DNG, ... --
// enough to copy its metadata directories. Nothing is copied: the view refers
// to the caller's buffer, which must outlive it. Every offset is bounds
// checked, so a damaged file yields missing entries rather than a crash.
class TiffReader
{
public:
  struct RawEntry
  {
    uint16_t tag = 0;
    uint16_t type = 0;
    uint32_t count = 0;
    size_t value_offset = 0;  // Where the value is in the file.
    size_t value_size = 0;
  };

  // Returns nothing when the buffer is not a TIFF file.
  static std::optional<TiffReader> open(const uint8_t* data, size_t size);

  bool bigEndian() const
  {
    return big_endian_;
  }
  size_t firstIfdOffset() const
  {
    return first_ifd_;
  }

  std::vector<RawEntry> readIfd(size_t offset) const;
  std::optional<RawEntry> find(size_t ifd_offset, uint16_t tag) const;
  // The first value of an integer entry.
  std::optional<uint32_t> readUInt(const RawEntry& entry) const;

  // The entry's value, converted to little-endian, the byte order of the DNG
  // files written by this library.
  ExifEntry toLittleEndian(const RawEntry& entry) const;

  const uint8_t* data() const
  {
    return data_;
  }

private:
  TiffReader(const uint8_t* data, size_t size, bool big_endian, size_t first_ifd);

  uint16_t get16(size_t pos) const;
  uint32_t get32(size_t pos) const;

  const uint8_t* data_;
  size_t size_;
  bool big_endian_;
  size_t first_ifd_;
};

// The metadata of the file that is worth carrying over to the DNG: the EXIF and
// GPS directories, and the maker note. Leaves `metadata` untouched when the
// file is not TIFF based (e.g. CR3, RAF).
void copyTiffMetadata(const uint8_t* data, size_t size, Metadata& metadata);

}  // namespace dngconv::tiff
