/// @file TestWienerHopf.cpp
/// @brief Unit tests for WienerHopf constructor/runtime guards.
/// @author GitHub Copilot

#include <catch2/catch_test_macros.hpp>

#include "process/clutter/WienerHopf.h"
#include "data/IqData.h"

#include <limits>
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

TEST_CASE("WienerHopfConstructorRejectsBinCountOverflow", "[process][clutter]")
{
  CHECK_THROWS_AS(
    WienerHopf(std::numeric_limits<int32_t>::min(),
      std::numeric_limits<int32_t>::max(),
      std::numeric_limits<uint32_t>::max()),
    std::invalid_argument);
}

TEST_CASE("WienerHopfConstructorRejectsSamplesAboveFftwIntLimit", "[process][clutter]")
{
  const uint32_t tooLargeSamples =
    static_cast<uint32_t>(static_cast<uint64_t>(std::numeric_limits<int>::max()) + 1ULL);
  CHECK_THROWS_AS(WienerHopf(0, 0, tooLargeSamples), std::invalid_argument);
}

TEST_CASE("WienerHopfConstructorRejectsFilterLengthUint32Overflow", "[process][clutter]")
{
  CHECK_THROWS_AS(WienerHopf(0, 0, std::numeric_limits<uint32_t>::max()), std::invalid_argument);
}

TEST_CASE("WienerHopfProcessRejectsShortInputs", "[process][clutter]")
{
  WienerHopf filter(0, 0, 64);
  IqData reference(64);
  IqData surveillance(64);

  CHECK(filter.process(&reference, &surveillance) == false);
}
