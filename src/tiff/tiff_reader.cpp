// Copyright (c) 2026 Giovanni Remigi
// SPDX-License-Identifier: MIT

#include "tiff/tiff_reader.hpp"

#include <algorithm>
#include <set>
#include <string>

namespace dngconv::tiff
{

namespace
{

constexpr uint16_t TAG_EXIF_IFD = 0x8769;
constexpr uint16_t TAG_GPS_IFD = 0x8825;
constexpr uint16_t TAG_MAKER_NOTE = 0x927c;

// EXIF entries that are not copied: pointers into the original file, and
// fields that describe the original file's JPEG rather than the raw data.
const std::set<uint16_t> EXIF_SKIPPED = {
  TAG_MAKER_NOTE,
  0xa005,  // InteroperabilityIFD, a pointer.
  0x9101,  // ComponentsConfiguration, of the JPEG.
  0x9102,  // CompressedBitsPerPixel, of the JPEG.
  0xa002,  // PixelXDimension, of the JPEG.
  0xa003,  // PixelYDimension, of the JPEG.
  0x927d,  // Not standard, but seen as a second maker note pointer.
};

uint32_t typeSizeOf(uint16_t type)
{
  switch (type)
  {
    case 1:
    case 2:
    case 6:
    case 7:
      return 1;
    case 3:
    case 8:
      return 2;
    case 4:
    case 9:
    case 11:
    case 13:
      return 4;
    case 5:
    case 10:
    case 12:
      return 8;
    default:
      return 0;
  }
}

}  // namespace

TiffReader::TiffReader(const uint8_t* data, size_t size, bool big_endian, size_t first_ifd)
  : data_(data), size_(size), big_endian_(big_endian), first_ifd_(first_ifd)
{
}

std::optional<TiffReader> TiffReader::open(const uint8_t* data, size_t size)
{
  if (size < 8)
  {
    return std::nullopt;
  }
  bool big_endian;
  if (data[0] == 'I' && data[1] == 'I')
  {
    big_endian = false;
  }
  else if (data[0] == 'M' && data[1] == 'M')
  {
    big_endian = true;
  }
  else
  {
    return std::nullopt;
  }
  TiffReader reader(data, size, big_endian, 0);
  // 42 for TIFF; Olympus (0x4f52, 0x5352) and Panasonic (0x55) use their own
  // magic numbers with an otherwise standard layout.
  const uint16_t magic = reader.get16(2);
  if (magic != 42 && magic != 0x4f52 && magic != 0x5352 && magic != 0x55)
  {
    return std::nullopt;
  }
  reader.first_ifd_ = reader.get32(4);
  if (reader.first_ifd_ < 8 || reader.first_ifd_ + 2 > size)
  {
    return std::nullopt;
  }
  return reader;
}

uint16_t TiffReader::get16(size_t pos) const
{
  if (pos + 2 > size_)
  {
    return 0;
  }
  return big_endian_ ? static_cast<uint16_t>(data_[pos] << 8 | data_[pos + 1]) :
                       static_cast<uint16_t>(data_[pos + 1] << 8 | data_[pos]);
}

uint32_t TiffReader::get32(size_t pos) const
{
  if (pos + 4 > size_)
  {
    return 0;
  }
  const uint32_t a = big_endian_ ? get16(pos) : get16(pos + 2);
  const uint32_t b = big_endian_ ? get16(pos + 2) : get16(pos);
  return a << 16 | b;
}

std::vector<TiffReader::RawEntry> TiffReader::readIfd(size_t offset) const
{
  std::vector<RawEntry> entries;
  if (offset == 0 || offset + 2 > size_)
  {
    return entries;
  }
  const uint16_t count = get16(offset);
  if (offset + 2 + 12ull * count > size_)
  {
    return entries;
  }
  for (uint16_t i = 0; i < count; ++i)
  {
    const size_t pos = offset + 2 + 12ull * i;
    RawEntry entry;
    entry.tag = get16(pos);
    entry.type = get16(pos + 2);
    entry.count = get32(pos + 4);
    const uint32_t type_size = typeSizeOf(entry.type);
    if (type_size == 0)
    {
      continue;
    }
    entry.value_size = static_cast<size_t>(type_size) * entry.count;
    entry.value_offset = entry.value_size <= 4 ? pos + 8 : get32(pos + 8);
    if (entry.value_offset + entry.value_size > size_ || entry.value_size > (64u << 20))
    {
      continue;
    }
    entries.push_back(entry);
  }
  return entries;
}

std::optional<TiffReader::RawEntry> TiffReader::find(size_t ifd_offset, uint16_t tag) const
{
  for (const auto& entry : readIfd(ifd_offset))
  {
    if (entry.tag == tag)
    {
      return entry;
    }
  }
  return std::nullopt;
}

std::optional<uint32_t> TiffReader::readUInt(const RawEntry& entry) const
{
  if (entry.count == 0)
  {
    return std::nullopt;
  }
  switch (entry.type)
  {
    case 1:
    case 7:
      return data_[entry.value_offset];
    case 3:
      return get16(entry.value_offset);
    case 4:
    case 13:
      return get32(entry.value_offset);
    default:
      return std::nullopt;
  }
}

ExifEntry TiffReader::toLittleEndian(const RawEntry& entry) const
{
  ExifEntry out;
  out.tag = entry.tag;
  out.type = entry.type;
  out.count = entry.count;
  out.data.assign(data_ + entry.value_offset, data_ + entry.value_offset + entry.value_size);
  if (big_endian_)
  {
    // Rationals are two 4-byte integers, each swapped on its own.
    uint32_t unit = typeSizeOf(entry.type);
    if (entry.type == 5 || entry.type == 10)
    {
      unit = 4;
    }
    if (unit > 1)
    {
      for (size_t i = 0; i + unit <= out.data.size(); i += unit)
      {
        std::reverse(out.data.begin() + i, out.data.begin() + i + unit);
      }
    }
  }
  return out;
}

void copyTiffMetadata(const uint8_t* data, size_t size, Metadata& metadata)
{
  auto reader = TiffReader::open(data, size);
  if (!reader)
  {
    return;
  }
  const size_t ifd0 = reader->firstIfdOffset();

  // The camera's own names, as they are; LibRaw normalises some of them.
  auto ascii = [&](uint16_t tag, std::string& field) {
    auto entry = reader->find(ifd0, tag);
    if (!entry || entry->type != 2)
    {
      return;
    }
    std::string text(reinterpret_cast<const char*>(data + entry->value_offset), entry->value_size);
    text = text.substr(0, text.find('\0'));
    while (!text.empty() && text.back() == ' ')
    {
      text.pop_back();
    }
    if (!text.empty())
    {
      field = text;
    }
  };
  ascii(0x010f, metadata.make);
  ascii(0x0110, metadata.model);
  ascii(0x013b, metadata.artist);
  ascii(0x8298, metadata.copyright);

  // The orientation the camera wrote, which follows its auto-rotate setting.
  // Canon's maker note records a physical one, which can differ, and which
  // LibRaw takes: this one is what the camera itself shows.
  if (auto entry = reader->find(ifd0, 0x0112))
  {
    const auto value = reader->readUInt(*entry);
    if (value && *value >= 1 && *value <= 8)
    {
      metadata.orientation = static_cast<uint16_t>(*value);
    }
  }

  if (auto exif_ptr = reader->find(ifd0, TAG_EXIF_IFD))
  {
    if (auto exif_offset = reader->readUInt(*exif_ptr))
    {
      for (const auto& entry : reader->readIfd(*exif_offset))
      {
        if (entry.tag == TAG_MAKER_NOTE)
        {
          metadata.maker_note.assign(data + entry.value_offset, data + entry.value_offset + entry.value_size);
          metadata.maker_note_big_endian = reader->bigEndian();
          metadata.maker_note_offset = static_cast<uint32_t>(entry.value_offset);
          continue;
        }
        if (EXIF_SKIPPED.count(entry.tag) != 0 || entry.type == 13)
        {
          continue;
        }
        metadata.exif.push_back(reader->toLittleEndian(entry));
        // The capture time as the camera wrote it, rather than LibRaw's
        // timestamp, which went through the local time zone and back.
        if (entry.tag == 0x9003 && entry.type == 2 && entry.value_size >= 19)
        {
          metadata.date_time.assign(reinterpret_cast<const char*>(data + entry.value_offset), 19);
        }
      }
    }
  }

  if (auto gps_ptr = reader->find(ifd0, TAG_GPS_IFD))
  {
    if (auto gps_offset = reader->readUInt(*gps_ptr))
    {
      for (const auto& entry : reader->readIfd(*gps_offset))
      {
        if (entry.type != 13)
        {
          metadata.gps.push_back(reader->toLittleEndian(entry));
        }
      }
    }
  }
}

}  // namespace dngconv::tiff
