// Copyright (c) 2026 Giovanni Remigi
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace dngconv::tiff
{

// The TIFF field types used in DNG.
enum Type : uint16_t
{
  BYTE = 1,
  ASCII = 2,
  SHORT = 3,
  LONG = 4,
  RATIONAL = 5,
  SBYTE = 6,
  UNDEFINED = 7,
  SSHORT = 8,
  SLONG = 9,
  SRATIONAL = 10,
  FLOAT = 11,
  DOUBLE = 12,
};

// The size in bytes of one value of the given type, 0 for an unknown type.
uint32_t typeSize(uint16_t type);

// An image file directory under construction. Values are stored little-endian,
// the byte order of the files TiffWriter writes. Tags that point somewhere else
// in the file -- to a child directory or to a block of image data -- are added
// with addChild() and addDataOffsets() and resolved when the file is laid out.
class Ifd
{
public:
  void addRaw(uint16_t tag, uint16_t type, uint32_t count, std::vector<uint8_t> bytes);

  void addAscii(uint16_t tag, const std::string& text);
  void addBytes(uint16_t tag, uint16_t type, const std::vector<uint8_t>& bytes);
  void addShort(uint16_t tag, std::vector<uint16_t> values);
  void addLong(uint16_t tag, std::vector<uint32_t> values);
  void addSLong(uint16_t tag, std::vector<int32_t> values);
  // Each value is approximated by a fraction with the given denominator.
  void addRational(uint16_t tag, const std::vector<double>& values, uint32_t denominator = 10000);
  void addSRational(uint16_t tag, const std::vector<double>& values, int32_t denominator = 10000);
  // An exact fraction, numerator/denominator pairs.
  void addRationalPairs(uint16_t tag, const std::vector<uint32_t>& pairs);

  // A tag holding the offsets of child directories, e.g. SubIFDs or ExifIFD.
  void addChild(uint16_t tag, std::shared_ptr<Ifd> child);
  void addChildren(uint16_t tag, std::vector<std::shared_ptr<Ifd>> children);

  // A pair of tags holding the offsets and the sizes of blocks of data, e.g.
  // TileOffsets and TileByteCounts. The blocks are owned by the directory.
  void addDataOffsets(uint16_t offsets_tag, uint16_t counts_tag, std::vector<std::vector<uint8_t>> blocks);

  bool has(uint16_t tag) const;

private:
  friend class TiffWriter;

  struct Entry
  {
    uint16_t type = 0;
    uint32_t count = 0;
    std::vector<uint8_t> bytes;
    std::vector<std::shared_ptr<Ifd>> children;  // For a pointer to child IFDs.
    int data_set = -1;                           // For an offsets tag: index in data_sets_.
  };

  std::map<uint16_t, Entry> entries_;  // Sorted by tag, as TIFF requires.
  std::vector<std::vector<std::vector<uint8_t>>> data_sets_;
};

// Lays out and serialises a little-endian TIFF file from its first directory.
class TiffWriter
{
public:
  static std::vector<uint8_t> write(const Ifd& ifd0);
};

}  // namespace dngconv::tiff
