// Copyright (c) 2026 Giovanni Remigi
// SPDX-License-Identifier: MIT

#pragma once

#include <stdexcept>
#include <string>

namespace rawconv
{

// Thrown by the library when a file cannot be read, decoded or written.
class Error : public std::runtime_error
{
public:
  explicit Error(const std::string& message) : std::runtime_error(message)
  {
  }
};

}  // namespace rawconv
