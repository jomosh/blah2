/// @file TestWienerHopf.cpp
/// @brief Unit tests for WienerHopf constructor/runtime guards.
/// @author GitHub Copilot

#include <catch2/catch_test_macros.hpp>

#include "process/clutter/WienerHopf.h"
#include "data/IqData.h"

#include <stdexcept>

TEST_CASE("WienerHopfConstructorRejectsInvalidDelayRange", "[process][clutter]")
{
  CHECK_THROWS_AS(WienerHopf(3, 2, 64), std::invalid_argument);
}

TEST_CASE("WienerHopfConstructorRejectsZeroSamples", "[process][clutter]")
{
  CHECK_THROWS_AS(WienerHopf(0, 0, 0), std::invalid_argument);
}

TEST_CASE("WienerHopfConstructorRejectsBinsLargerThanSamples", "[process][clutter]")
{
  CHECK_THROWS_AS(WienerHopf(0, 8, 4), std::invalid_argument);
}

TEST_CASE("WienerHopfProcessRejectsShortInputs", "[process][clutter]")
{
  WienerHopf filter(0, 0, 64);
  IqData reference(64);
  IqData surveillance(64);

  CHECK(filter.process(&reference, &surveillance) == false);
}
