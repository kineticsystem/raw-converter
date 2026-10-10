// Copyright (c) 2026 Giovanni Remigi
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace dngconv
{

// Where the converter reads raw files and writes DNG files. The seam between
// the converter and the file system: tests give it one in memory, and an
// application can give it one that reads from a camera or writes to a
// database. Every method must be safe to call from several threads at once.
class FileStore
{
public:
  virtual ~FileStore() = default;

  // Throws dngconv::Error when the file cannot be read.
  virtual std::vector<uint8_t> read(const std::filesystem::path& path) const = 0;
  // Creates the parent folders as needed. A reader never sees a partly written
  // file, and an existing file is replaced only by a complete one. Throws
  // dngconv::Error when the file cannot be written.
  virtual void write(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) = 0;
  virtual bool exists(const std::filesystem::path& path) const = 0;
};

// The local file system.
class LocalFileStore : public FileStore
{
public:
  std::vector<uint8_t> read(const std::filesystem::path& path) const override;
  void write(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) override;
  bool exists(const std::filesystem::path& path) const override;
};

}  // namespace dngconv
