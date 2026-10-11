// Copyright (c) 2026 Giovanni Remigi
// SPDX-License-Identifier: MIT

#include "rawconv/file_store.hpp"

#include <atomic>
#include <fstream>
#include <string>
#include <system_error>

#include <unistd.h>

#include "rawconv/error.hpp"

namespace rawconv
{

std::vector<uint8_t> LocalFileStore::read(const std::filesystem::path& path) const
{
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  if (!in)
  {
    throw Error(path.string() + ": cannot open");
  }
  const auto size = static_cast<size_t>(in.tellg());
  std::vector<uint8_t> data(size);
  in.seekg(0);
  if (!in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size)))
  {
    throw Error(path.string() + ": cannot read");
  }
  return data;
}

void LocalFileStore::write(const std::filesystem::path& path, const std::vector<uint8_t>& bytes)
{
  std::error_code error;
  if (path.has_parent_path())
  {
    std::filesystem::create_directories(path.parent_path(), error);
    if (error)
    {
      throw Error(path.parent_path().string() + ": cannot create the folder: " + error.message());
    }
  }

  // Write next to the target under a name of its own, then rename: a reader
  // never sees a partial file, and two writers never share a temporary file.
  static std::atomic<unsigned> counter{ 0 };
  std::filesystem::path temp = path;
  temp += ".partial-" + std::to_string(::getpid()) + "-" + std::to_string(counter++);
  {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    if (!out)
    {
      throw Error(path.string() + ": cannot create");
    }
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    out.close();
    if (!out)
    {
      std::filesystem::remove(temp, error);
      throw Error(path.string() + ": cannot write");
    }
  }
  std::filesystem::rename(temp, path, error);
  if (error)
  {
    const std::string message = error.message();
    std::filesystem::remove(temp, error);
    throw Error(path.string() + ": cannot write: " + message);
  }
}

bool LocalFileStore::exists(const std::filesystem::path& path) const
{
  std::error_code error;
  return std::filesystem::exists(path, error);
}

}  // namespace rawconv
