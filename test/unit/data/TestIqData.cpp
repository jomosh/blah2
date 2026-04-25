/// @file TestIqData.cpp
/// @brief Unit test for IqData.cpp
/// @author GitHub Copilot

#include <catch2/catch_test_macros.hpp>

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

  bool waiterReleased = false;
  std::thread waiter([&]() {
    iqData.wait_for_max_length(1);
    waiterReleased = true;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  CHECK(waiterReleased == false);

  iqData.lock();
  (void)iqData.pop_front();
  iqData.unlock_and_notify();

  waiter.join();
  CHECK(waiterReleased == true);
}