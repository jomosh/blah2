/// @file TestIqData.cpp
/// @brief Unit test for IqData.cpp
/// @author GitHub Copilot

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>

#include "data/IqData.h"

TEST_CASE("Wait_For_Min_Length_Rejects_Impossible_Request", "[iqdata]")
{
  IqData iqData(8);

  CHECK_THROWS_AS(iqData.wait_for_min_length(9), std::invalid_argument);
}