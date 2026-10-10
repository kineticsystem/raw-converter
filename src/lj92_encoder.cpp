// Copyright (c) 2026 Giovanni Remigi
// SPDX-License-Identifier: MIT

#include "lj92_encoder.hpp"

#include <array>
#include <stdexcept>

namespace dngconv
{

namespace
{

constexpr int PRECISION = 16;
constexpr int SYMBOLS = 17;  // Difference categories 0 to 16.

// The number of bits of |diff|, the JPEG difference category. With 16-bit
// precision, a difference of -32768 is category 16 with no extra bits, as
// the DNG specification and every DNG reader expect.
inline int category(int diff)
{
  unsigned magnitude = static_cast<unsigned>(diff < 0 ? -diff : diff);
  int bits = 0;
  while (magnitude != 0)
  {
    ++bits;
    magnitude >>= 1;
  }
  return bits;
}

// The difference between a sample and its prediction, modulo 2^16, as a
// signed value.
inline int difference(uint16_t sample, uint16_t prediction)
{
  return static_cast<int16_t>(static_cast<uint16_t>(sample - prediction));
}

struct HuffmanTable
{
  std::array<uint8_t, 17> bits{};  // bits[n]: number of codes of length n.
  std::vector<uint8_t> values;     // Symbols in order of increasing code length.
  std::array<uint16_t, SYMBOLS> code{};
  std::array<uint8_t, SYMBOLS> size{};
};

// Builds the optimal table for the given symbol frequencies, limited to codes
// of 16 bits at most: the procedure of ITU T.81 Annex K.2, as libjpeg's
// jpeg_gen_optimal_table implements it.
HuffmanTable buildTable(const std::array<long, SYMBOLS>& counts)
{
  // One extra symbol, with frequency 1, reserves the code of all ones, which
  // JPEG forbids.
  constexpr int N = SYMBOLS + 1;
  std::array<long, N> freq{};
  for (int i = 0; i < SYMBOLS; ++i)
  {
    freq[i] = counts[i];
  }
  freq[SYMBOLS] = 1;

  std::array<int, N> codesize{};
  std::array<int, N> others;
  others.fill(-1);

  for (;;)
  {
    // The two least frequent symbols, ties going to the larger index.
    int c1 = -1;
    long v = 0x7fffffffL;
    for (int i = 0; i < N; ++i)
    {
      if (freq[i] != 0 && freq[i] <= v)
      {
        v = freq[i];
        c1 = i;
      }
    }
    int c2 = -1;
    v = 0x7fffffffL;
    for (int i = 0; i < N; ++i)
    {
      if (freq[i] != 0 && freq[i] <= v && i != c1)
      {
        v = freq[i];
        c2 = i;
      }
    }
    if (c2 < 0)
    {
      break;
    }
    freq[c1] += freq[c2];
    freq[c2] = 0;
    ++codesize[c1];
    while (others[c1] >= 0)
    {
      c1 = others[c1];
      ++codesize[c1];
    }
    others[c1] = c2;
    ++codesize[c2];
    while (others[c2] >= 0)
    {
      c2 = others[c2];
      ++codesize[c2];
    }
  }

  std::array<int, 33> bits{};
  for (int i = 0; i < N; ++i)
  {
    if (codesize[i] != 0)
    {
      if (codesize[i] > 32)
      {
        throw std::logic_error("Huffman code length overflow");
      }
      ++bits[codesize[i]];
    }
  }
  // Limit the code lengths to 16 bits (Annex K.3).
  for (int i = 32; i > 16; --i)
  {
    while (bits[i] > 0)
    {
      int j = i - 2;
      while (bits[j] == 0)
      {
        --j;
      }
      bits[i] -= 2;
      ++bits[i - 1];
      bits[j + 1] += 2;
      --bits[j];
    }
  }
  // Drop the reserved symbol: it has the longest code.
  int longest = 16;
  while (bits[longest] == 0)
  {
    --longest;
  }
  --bits[longest];

  HuffmanTable table;
  for (int i = 1; i <= 16; ++i)
  {
    table.bits[i] = static_cast<uint8_t>(bits[i]);
  }
  for (int length = 1; length <= 32; ++length)
  {
    for (int symbol = 0; symbol < SYMBOLS; ++symbol)
    {
      if (codesize[symbol] == length)
      {
        table.values.push_back(static_cast<uint8_t>(symbol));
      }
    }
  }

  // The codes themselves (Annex C).
  uint16_t code = 0;
  size_t k = 0;
  for (int length = 1; length <= 16; ++length)
  {
    for (int i = 0; i < table.bits[length]; ++i)
    {
      const uint8_t symbol = table.values[k++];
      table.code[symbol] = code++;
      table.size[symbol] = static_cast<uint8_t>(length);
    }
    code = static_cast<uint16_t>(code << 1);
  }
  return table;
}

class BitWriter
{
public:
  explicit BitWriter(std::vector<uint8_t>& out) : out_(out)
  {
  }

  // Appends the `count` low bits of `value`, most significant first.
  void put(uint32_t value, int count)
  {
    acc_ = acc_ << count | (value & ((1u << count) - 1));
    filled_ += count;
    while (filled_ >= 8)
    {
      filled_ -= 8;
      emit(static_cast<uint8_t>(acc_ >> filled_));
    }
  }

  // Pads the last byte with ones, as T.81 requires.
  void flush()
  {
    if (filled_ > 0)
    {
      put(0xff, 8 - filled_);
    }
  }

private:
  void emit(uint8_t byte)
  {
    out_.push_back(byte);
    if (byte == 0xff)
    {
      out_.push_back(0x00);  // Byte stuffing.
    }
  }

  std::vector<uint8_t>& out_;
  uint64_t acc_ = 0;  // Only the `filled_` low bits are pending.
  int filled_ = 0;
};

void putMarker(std::vector<uint8_t>& out, uint8_t marker)
{
  out.push_back(0xff);
  out.push_back(marker);
}

void put16(std::vector<uint8_t>& out, uint32_t v)
{
  out.push_back(static_cast<uint8_t>(v >> 8));
  out.push_back(static_cast<uint8_t>(v));
}

}  // namespace

std::vector<uint8_t> encodeLosslessJpeg(const uint16_t* samples, uint32_t width, uint32_t height, uint32_t components)
{
  if (components == 0 || components > 4 || width == 0 || height == 0 || width % components != 0 || height > 65535 ||
      width / components > 65535)
  {
    throw std::invalid_argument("Unsupported lossless JPEG dimensions");
  }

  // The prediction of each sample (T.81 H.1.2.1, predictor 1): the first row
  // takes the sample to its left, the first column the sample above, and the
  // very first sample of each component 2^(P-1).
  auto predict = [&](uint32_t row, uint32_t col) -> uint16_t {
    if (col >= components)
    {
      return samples[static_cast<size_t>(row) * width + col - components];
    }
    if (row > 0)
    {
      return samples[static_cast<size_t>(row - 1) * width + col];
    }
    return static_cast<uint16_t>(1u << (PRECISION - 1));
  };

  std::array<long, SYMBOLS> counts{};
  for (uint32_t row = 0; row < height; ++row)
  {
    const uint16_t* line = samples + static_cast<size_t>(row) * width;
    for (uint32_t col = 0; col < width; ++col)
    {
      ++counts[category(difference(line[col], predict(row, col)))];
    }
  }
  const HuffmanTable table = buildTable(counts);

  std::vector<uint8_t> out;
  out.reserve(static_cast<size_t>(width) * height + 1024);

  putMarker(out, 0xd8);  // SOI

  putMarker(out, 0xc4);  // DHT
  put16(out, static_cast<uint32_t>(2 + 1 + 16 + table.values.size()));
  out.push_back(0x00);  // DC table 0.
  for (int i = 1; i <= 16; ++i)
  {
    out.push_back(table.bits[i]);
  }
  out.insert(out.end(), table.values.begin(), table.values.end());

  putMarker(out, 0xc3);  // SOF3, lossless.
  put16(out, 8 + 3 * components);
  out.push_back(PRECISION);
  put16(out, height);
  put16(out, width / components);
  out.push_back(static_cast<uint8_t>(components));
  for (uint32_t c = 0; c < components; ++c)
  {
    out.push_back(static_cast<uint8_t>(c + 1));  // Component id.
    out.push_back(0x11);                         // No subsampling.
    out.push_back(0x00);                         // Quantisation table, unused.
  }

  putMarker(out, 0xda);  // SOS
  put16(out, 6 + 2 * components);
  out.push_back(static_cast<uint8_t>(components));
  for (uint32_t c = 0; c < components; ++c)
  {
    out.push_back(static_cast<uint8_t>(c + 1));
    out.push_back(0x00);  // Huffman table 0.
  }
  out.push_back(1);  // Ss: predictor 1.
  out.push_back(0);  // Se
  out.push_back(0);  // Ah/Al: no point transform.

  BitWriter writer(out);
  for (uint32_t row = 0; row < height; ++row)
  {
    const uint16_t* line = samples + static_cast<size_t>(row) * width;
    for (uint32_t col = 0; col < width; ++col)
    {
      const int diff = difference(line[col], predict(row, col));
      const int ssss = category(diff);
      writer.put(table.code[ssss], table.size[ssss]);
      if (ssss != 0 && ssss != 16)
      {
        const int extra = diff < 0 ? diff - 1 : diff;
        writer.put(static_cast<uint32_t>(extra) & ((1u << ssss) - 1), ssss);
      }
    }
  }
  writer.flush();

  putMarker(out, 0xd9);  // EOI
  return out;
}

}  // namespace dngconv
