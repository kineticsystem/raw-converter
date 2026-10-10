// Copyright (c) 2026 Giovanni Remigi
// SPDX-License-Identifier: MIT

#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <exception>
#include <mutex>
#include <thread>
#include <vector>

namespace dngconv
{

// Calls fn(i) for i in [0, count), on up to `threads` threads; 0 means one per
// CPU core. Each index is handed out once, in increasing order, to the next
// free thread. If fn throws, no further index is handed out, every thread is
// joined, and the first exception is rethrown here: an exception never
// escapes a thread, which would end the whole program.
template <typename Fn>
void parallelFor(size_t count, unsigned threads, Fn&& fn)
{
  if (threads == 0)
  {
    threads = std::max(1u, std::thread::hardware_concurrency());
  }
  threads = static_cast<unsigned>(std::min<size_t>(threads, count));
  if (threads <= 1)
  {
    for (size_t i = 0; i < count; ++i)
    {
      fn(i);
    }
    return;
  }

  std::atomic<size_t> next{ 0 };
  std::exception_ptr error;
  std::mutex error_mutex;
  auto work = [&] {
    for (size_t i = next++; i < count; i = next++)
    {
      try
      {
        fn(i);
      }
      catch (...)
      {
        std::lock_guard<std::mutex> lock(error_mutex);
        if (!error)
        {
          error = std::current_exception();
        }
        next = count;  // Stop handing out work.
      }
    }
  };

  std::vector<std::thread> workers;
  workers.reserve(threads);
  try
  {
    for (unsigned t = 1; t < threads; ++t)
    {
      workers.emplace_back(work);
    }
  }
  catch (...)
  {
    // Fewer threads than asked for: carry on with those that started.
  }
  work();  // This thread works too.
  for (auto& worker : workers)
  {
    worker.join();
  }
  if (error)
  {
    std::rethrow_exception(error);
  }
}

}  // namespace dngconv
