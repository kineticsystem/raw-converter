// Copyright (c) 2026 Giovanni Remigi
// SPDX-License-Identifier: MIT

#include "tiff/tiff_writer.hpp"

#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <unordered_map>

namespace rawconv::tiff
{

namespace
{

void put16(std::vector<uint8_t>& out, uint16_t v)
{
  out.push_back(static_cast<uint8_t>(v));
  out.push_back(static_cast<uint8_t>(v >> 8));
}

void put32(std::vector<uint8_t>& out, uint32_t v)
{
  for (int i = 0; i < 4; ++i)
  {
    out.push_back(static_cast<uint8_t>(v >> (8 * i)));
  }
}

void set32(std::vector<uint8_t>& out, size_t pos, uint32_t v)
{
  for (int i = 0; i < 4; ++i)
  {
    out[pos + i] = static_cast<uint8_t>(v >> (8 * i));
  }
}

uint32_t checkedOffset(size_t offset)
{
  if (offset > std::numeric_limits<uint32_t>::max())
  {
    throw std::runtime_error("The DNG file would be larger than 4 GB");
  }
  return static_cast<uint32_t>(offset);
}

}  // namespace

uint32_t typeSize(uint16_t type)
{
  switch (type)
  {
    case BYTE:
    case ASCII:
    case SBYTE:
    case UNDEFINED:
      return 1;
    case SHORT:
    case SSHORT:
      return 2;
    case LONG:
    case SLONG:
    case FLOAT:
    case 13:  // IFD
      return 4;
    case RATIONAL:
    case SRATIONAL:
    case DOUBLE:
      return 8;
    default:
      return 0;
  }
}

void Ifd::addRaw(uint16_t tag, uint16_t type, uint32_t count, std::vector<uint8_t> bytes)
{
  if (bytes.size() != static_cast<size_t>(count) * typeSize(type))
  {
    throw std::logic_error("TIFF tag " + std::to_string(tag) + ": value size does not match its type and count");
  }
  Entry entry;
  entry.type = type;
  entry.count = count;
  entry.bytes = std::move(bytes);
  entries_[tag] = std::move(entry);
}

void Ifd::addAscii(uint16_t tag, const std::string& text)
{
  std::vector<uint8_t> bytes(text.begin(), text.end());
  bytes.push_back(0);
  const auto count = static_cast<uint32_t>(bytes.size());
  addRaw(tag, ASCII, count, std::move(bytes));
}

void Ifd::addBytes(uint16_t tag, uint16_t type, const std::vector<uint8_t>& bytes)
{
  addRaw(tag, type, static_cast<uint32_t>(bytes.size()), bytes);
}

void Ifd::addShort(uint16_t tag, std::vector<uint16_t> values)
{
  std::vector<uint8_t> bytes;
  for (uint16_t v : values)
  {
    put16(bytes, v);
  }
  addRaw(tag, SHORT, static_cast<uint32_t>(values.size()), std::move(bytes));
}

void Ifd::addLong(uint16_t tag, std::vector<uint32_t> values)
{
  std::vector<uint8_t> bytes;
  for (uint32_t v : values)
  {
    put32(bytes, v);
  }
  addRaw(tag, LONG, static_cast<uint32_t>(values.size()), std::move(bytes));
}

void Ifd::addSLong(uint16_t tag, std::vector<int32_t> values)
{
  std::vector<uint8_t> bytes;
  for (int32_t v : values)
  {
    put32(bytes, static_cast<uint32_t>(v));
  }
  addRaw(tag, SLONG, static_cast<uint32_t>(values.size()), std::move(bytes));
}

void Ifd::addRational(uint16_t tag, const std::vector<double>& values, uint32_t denominator)
{
  std::vector<uint8_t> bytes;
  for (double v : values)
  {
    put32(bytes, static_cast<uint32_t>(std::llround(std::max(0.0, v) * denominator)));
    put32(bytes, denominator);
  }
  addRaw(tag, RATIONAL, static_cast<uint32_t>(values.size()), std::move(bytes));
}

void Ifd::addSRational(uint16_t tag, const std::vector<double>& values, int32_t denominator)
{
  std::vector<uint8_t> bytes;
  for (double v : values)
  {
    put32(bytes, static_cast<uint32_t>(static_cast<int32_t>(std::lround(v * denominator))));
    put32(bytes, static_cast<uint32_t>(denominator));
  }
  addRaw(tag, SRATIONAL, static_cast<uint32_t>(values.size()), std::move(bytes));
}

void Ifd::addRationalPairs(uint16_t tag, const std::vector<uint32_t>& pairs)
{
  std::vector<uint8_t> bytes;
  for (uint32_t v : pairs)
  {
    put32(bytes, v);
  }
  addRaw(tag, RATIONAL, static_cast<uint32_t>(pairs.size() / 2), std::move(bytes));
}

void Ifd::addChild(uint16_t tag, std::shared_ptr<Ifd> child)
{
  addChildren(tag, { std::move(child) });
}

void Ifd::addChildren(uint16_t tag, std::vector<std::shared_ptr<Ifd>> children)
{
  Entry entry;
  entry.type = LONG;
  entry.count = static_cast<uint32_t>(children.size());
  entry.bytes.assign(4 * children.size(), 0);  // Patched by TiffWriter.
  entry.children = std::move(children);
  entries_[tag] = std::move(entry);
}

void Ifd::addDataOffsets(uint16_t offsets_tag, uint16_t counts_tag, std::vector<std::vector<uint8_t>> blocks)
{
  std::vector<uint32_t> sizes;
  sizes.reserve(blocks.size());
  for (const auto& block : blocks)
  {
    sizes.push_back(checkedOffset(block.size()));
  }
  addLong(counts_tag, sizes);

  Entry entry;
  entry.type = LONG;
  entry.count = static_cast<uint32_t>(blocks.size());
  entry.bytes.assign(4 * blocks.size(), 0);  // Patched by TiffWriter.
  entry.data_set = static_cast<int>(data_sets_.size());
  entries_[offsets_tag] = std::move(entry);
  data_sets_.push_back(std::move(blocks));
}

bool Ifd::has(uint16_t tag) const
{
  return entries_.count(tag) != 0;
}

std::vector<uint8_t> TiffWriter::write(const Ifd& ifd0)
{
  // Every directory, depth first, so that a directory comes before its
  // children.
  std::vector<const Ifd*> ifds;
  std::vector<const Ifd*> stack = { &ifd0 };
  while (!stack.empty())
  {
    const Ifd* ifd = stack.back();
    stack.pop_back();
    ifds.push_back(ifd);
    // Push in reverse so that children are visited in order.
    std::vector<const Ifd*> children;
    for (const auto& [tag, entry] : ifd->entries_)
    {
      for (const auto& child : entry.children)
      {
        children.push_back(child.get());
      }
    }
    for (auto it = children.rbegin(); it != children.rend(); ++it)
    {
      stack.push_back(*it);
    }
  }

  // Lay out: the header, then each directory followed by its out-of-line
  // values, then the data blocks. Everything starts on a 4-byte boundary.
  auto align = [](size_t v) { return (v + 3) & ~size_t(3); };
  std::unordered_map<const Ifd*, size_t> ifd_offsets;
  std::unordered_map<const void*, size_t> value_offsets;  // Keyed by &Entry.
  size_t pos = 8;
  for (const Ifd* ifd : ifds)
  {
    pos = align(pos);
    ifd_offsets[ifd] = pos;
    pos += 2 + 12 * ifd->entries_.size() + 4;
    for (const auto& [tag, entry] : ifd->entries_)
    {
      if (entry.bytes.size() > 4)
      {
        pos = align(pos);
        value_offsets[&entry] = pos;
        pos += entry.bytes.size();
      }
    }
  }
  std::unordered_map<const void*, std::vector<size_t>> block_offsets;  // Keyed by data set.
  for (const Ifd* ifd : ifds)
  {
    for (const auto& blocks : ifd->data_sets_)
    {
      auto& offsets = block_offsets[&blocks];
      for (const auto& block : blocks)
      {
        pos = align(pos);
        offsets.push_back(pos);
        pos += block.size();
      }
    }
  }
  checkedOffset(pos);

  std::vector<uint8_t> out;
  out.reserve(pos);
  out.push_back('I');
  out.push_back('I');
  put16(out, 42);
  put32(out, checkedOffset(ifd_offsets[&ifd0]));

  for (const Ifd* ifd : ifds)
  {
    out.resize(ifd_offsets[ifd], 0);
    put16(out, static_cast<uint16_t>(ifd->entries_.size()));
    std::vector<std::pair<const Ifd::Entry*, std::vector<uint8_t>>> out_of_line;
    for (const auto& [tag, entry] : ifd->entries_)
    {
      std::vector<uint8_t> bytes = entry.bytes;
      for (size_t i = 0; i < entry.children.size(); ++i)
      {
        set32(bytes, 4 * i, checkedOffset(ifd_offsets[entry.children[i].get()]));
      }
      if (entry.data_set >= 0)
      {
        const auto& offsets = block_offsets[&ifd->data_sets_[entry.data_set]];
        for (size_t i = 0; i < offsets.size(); ++i)
        {
          set32(bytes, 4 * i, checkedOffset(offsets[i]));
        }
      }
      put16(out, tag);
      put16(out, entry.type);
      put32(out, entry.count);
      if (bytes.size() <= 4)
      {
        bytes.resize(4, 0);
        out.insert(out.end(), bytes.begin(), bytes.end());
      }
      else
      {
        put32(out, checkedOffset(value_offsets[&entry]));
        out_of_line.emplace_back(&entry, std::move(bytes));
      }
    }
    put32(out, 0);  // No next IFD: the other directories are children.
    for (auto& [entry, bytes] : out_of_line)
    {
      out.resize(value_offsets[entry], 0);
      out.insert(out.end(), bytes.begin(), bytes.end());
    }
  }

  for (const Ifd* ifd : ifds)
  {
    for (const auto& blocks : ifd->data_sets_)
    {
      const auto& offsets = block_offsets[&blocks];
      for (size_t i = 0; i < blocks.size(); ++i)
      {
        out.resize(offsets[i], 0);
        out.insert(out.end(), blocks[i].begin(), blocks[i].end());
      }
    }
  }
  return out;
}

}  // namespace rawconv::tiff
