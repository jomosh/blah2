/// @file TestIqData.cpp
/// @brief Unit test for IqData.cpp
/// @author GitHub Copilot

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <thread>
#include <chrono>
#include <stdexcept>

#include "data/IqData.h"

TEST_CASE("Wait_For_Min_Length_Rejects_Impossible_Request", "[iqdata]")
{
  IqData iqData(8);

  CHECK_THROWS_AS(iqData.wait_for_min_length(9), std::invalid_argument);
}

TEST_CASE("Wait_For_Max_Length_Blocks_Until_Buffer_Has_Space", "[iqdata]")
{
  IqData iqData(2);
  iqData.lock();
  iqData.push_back({1.0, 0.0});
  iqData.push_back({2.0, 0.0});
  iqData.unlock_and_notify();

  std::atomic<bool> waiterStarted{false};
  std::atomic<bool> waiterReleased{false};
  std::thread waiter([&]() {
    waiterStarted.store(true, std::memory_order_release);
    iqData.wait_for_max_length(1);
    waiterReleased.store(true, std::memory_order_release);
  });

  const auto waitForState = [](const std::atomic<bool> &state,
    std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!state.load(std::memory_order_acquire)
      && std::chrono::steady_clock::now() < deadline)
    {
      std::this_thread::yield();
    }
    return state.load(std::memory_order_acquire);
  };

  const bool waiterStartedInTime = waitForState(waiterStarted,
    std::chrono::milliseconds(200));
  const bool waiterReleasedWhileFull = waiterReleased.load(
    std::memory_order_acquire);

  iqData.lock();
  (void)iqData.pop_front();
  iqData.unlock_and_notify();

  const bool waiterReleasedInTime = waitForState(waiterReleased,
    std::chrono::milliseconds(200));
  waiter.join();

  CHECK(waiterStartedInTime == true);
  CHECK(waiterReleasedWhileFull == false);
  CHECK(waiterReleasedInTime == true);
}