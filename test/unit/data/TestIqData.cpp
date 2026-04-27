/// @file TestIqData.cpp
/// @brief Unit test for IqData.cpp
/// @author GitHub Copilot

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <thread>
#include <chrono>
#include <stdexcept>
#include <vector>

#include "data/IqData.h"

namespace
{
std::vector<int> pop_real_samples(IqData &iqData, uint32_t nSamples)
{
  std::vector<int> out;
  out.reserve(nSamples);
  for (uint32_t i = 0; i < nSamples; i++)
  {
    out.push_back(static_cast<int>(iqData.pop_front().real()));
  }
  return out;
}
}

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

TEST_CASE("Overwrite_PreservesNewestSamplesInFifoOrder", "[iqdata]")
{
  IqData iqData(3);

  for (int sample = 0; sample < 5; sample++)
  {
    iqData.push_back({static_cast<double>(sample), static_cast<double>(-sample)});
  }

  REQUIRE(iqData.get_length() == 3);

  const std::deque<std::complex<double>> snapshot = iqData.get_data();
  REQUIRE(snapshot.size() == 3);
  CHECK(snapshot[0] == std::complex<double>(2.0, -2.0));
  CHECK(snapshot[1] == std::complex<double>(3.0, -3.0));
  CHECK(snapshot[2] == std::complex<double>(4.0, -4.0));

  CHECK(iqData.at(0) == std::complex<double>(2.0, -2.0));
  CHECK(iqData.at(1) == std::complex<double>(3.0, -3.0));
  CHECK(iqData.at_unchecked(2) == std::complex<double>(4.0, -4.0));

  CHECK(pop_real_samples(iqData, 3) == std::vector<int>{2, 3, 4});
  CHECK(iqData.get_length() == 0);
}

TEST_CASE("Paired_BuffersSkewWhenOneChannelOverwritesBeforePeerCatchesUp", "[iqdata]")
{
  constexpr uint32_t nSamples = 4;
  IqData reference(nSamples + 1);
  IqData surveillance(nSamples + 1);

  for (int sample = 0; sample < static_cast<int>(nSamples) + 2; sample++)
  {
    reference.push_back({static_cast<double>(sample), 0.0});
  }
  for (int sample = 0; sample < static_cast<int>(nSamples); sample++)
  {
    surveillance.push_back({static_cast<double>(sample), 0.0});
  }

  REQUIRE(reference.get_length() == nSamples + 1);
  REQUIRE(surveillance.get_length() == nSamples);

  reference.lock();
  surveillance.lock();
  const std::vector<int> extractedReference = pop_real_samples(reference, nSamples);
  const std::vector<int> extractedSurveillance = pop_real_samples(surveillance, nSamples);
  reference.unlock();
  surveillance.unlock();

  CHECK(extractedReference == std::vector<int>{1, 2, 3, 4});
  CHECK(extractedSurveillance == std::vector<int>{0, 1, 2, 3});
  CHECK(extractedReference != extractedSurveillance);
}
