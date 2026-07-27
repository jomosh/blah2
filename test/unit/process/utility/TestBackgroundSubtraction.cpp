/// @file TestBackgroundSubtraction.cpp
/// @brief Catch2 unit tests for BackgroundSubtraction.
/// @author 30hours

#include "process/utility/BackgroundSubtraction.h"
#include "data/Map.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/catch_approx.hpp>
#include <complex>
#include <vector>
#include <cmath>

using Complex = std::complex<double>;

namespace
{
/// @brief Build a Map with nDoppler × nDelay cells, all initialised to zero.
Map<Complex> make_map(uint16_t nDopplerBins, uint16_t nDelayBins)
{
  Map<Complex> map(nDopplerBins, nDelayBins);
  map.delay.resize(nDelayBins);
  map.doppler.resize(nDopplerBins);
  for (uint16_t i = 0; i < nDopplerBins; i++)
  {
    map.doppler[i] = i - static_cast<double>(nDopplerBins) / 2.0;
  }
  for (uint16_t j = 0; j < nDelayBins; j++)
  {
    map.delay[j] = j;
  }
  return map;
}

/// @brief Approximate magnitude-squared of a complex value.
double magSq(Complex c)
{
  return std::norm(c);
}
}

TEST_CASE("BackgroundSubtraction: warmup phase does not modify map", "[BackgroundSubtraction]")
{
  const uint16_t nDoppler = 3;
  const uint16_t nDelay = 4;
  auto map = make_map(nDoppler, nDelay);

  // Place a stationary peak at (1, 2)
  map.data[1][2] = Complex(10.0, 0.0);

  const double alpha = 0.1;
  const uint64_t warmupCpis = 3;
  BackgroundSubtraction bs(alpha, warmupCpis, nDoppler, nDelay);

  // Run warmup CPIs — map should remain unchanged
  for (uint64_t k = 0; k < warmupCpis; k++)
  {
    bs.process(&map);
  }

  // After warmup, map still has the original peak
  REQUIRE(magSq(map.data[1][2]) == Catch::Approx(100.0));
}

TEST_CASE("BackgroundSubtraction: stationary peak is suppressed after warmup", "[BackgroundSubtraction]")
{
  const uint16_t nDoppler = 3;
  const uint16_t nDelay = 4;
  auto map = make_map(nDoppler, nDelay);

  // Place a stationary peak at (1, 2)
  map.data[1][2] = Complex(10.0, 0.0);

  const double alpha = 1.0; // jump straight to full value
  const uint64_t warmupCpis = 1;
  BackgroundSubtraction bs(alpha, warmupCpis, nDoppler, nDelay);

  // Warmup pass — accumulate but no subtraction
  bs.process(&map);
  REQUIRE(magSq(map.data[1][2]) == Catch::Approx(100.0));

  // Second CPI — alpha=1 means background = current, so residual = 0
  bs.process(&map);
  REQUIRE(magSq(map.data[1][2]) == Catch::Approx(0.0));
}

TEST_CASE("BackgroundSubtraction: moving peak survives background subtraction", "[BackgroundSubtraction]")
{
  const uint16_t nDoppler = 4;
  const uint16_t nDelay = 5;
  const double alpha = 0.5;
  const uint64_t warmupCpis = 2;
  BackgroundSubtraction bs(alpha, warmupCpis, nDoppler, nDelay);

  // CPI 1: peak at (0, 0)
  auto map1 = make_map(nDoppler, nDelay);
  map1.data[0][0] = Complex(10.0, 0.0);
  bs.process(&map1); // warmup 1 — no change
  REQUIRE(magSq(map1.data[0][0]) == Catch::Approx(100.0));

  // CPI 2: peak at (0, 0) — same position
  auto map2 = make_map(nDoppler, nDelay);
  map2.data[0][0] = Complex(10.0, 0.0);
  bs.process(&map2); // warmup 2 — no change
  REQUIRE(magSq(map2.data[0][0]) == Catch::Approx(100.0));

  // CPI 3: peak MOVES to (2, 3) — new position
  auto map3 = make_map(nDoppler, nDelay);
  map3.data[2][3] = Complex(10.0, 0.0);
  bs.process(&map3); // active: subtract background

  // The old peak at (0,0) had 100 accumulated, new peak at (2,3) has 0 background.
  // alpha=0.5 → background = 0.5*100 + 0.5*0 = 50 at (0,0),
  // new peak at (2,3) → background = 0.5*0 + 0.5*100 = 50
  const double step3OldBg = 50.0;
  const double step3Residual = std::max(0.0, 100.0 - step3OldBg);
  REQUIRE(magSq(map3.data[2][3]) == Catch::Approx(step3Residual));
  // The surviving signal at (2,3) should be 50 (half of original 100)
  REQUIRE(magSq(map3.data[2][3]) == Catch::Approx(50.0));

  // The old position (0,0) has no signal this CPI, so residual = max(0, 0 - 50) = 0
  REQUIRE(magSq(map3.data[0][0]) == Catch::Approx(0.0));

  // CPI 4: peak stays at (2, 3) — becomes stationary
  auto map4 = make_map(nDoppler, nDelay);
  map4.data[2][3] = Complex(10.0, 0.0);
  bs.process(&map4);
  // Background at (2,3): 0.5*50 + 0.5*100 = 75
  // Residual: max(0, 100 - 75) = 25
  REQUIRE(magSq(map4.data[2][3]) == Catch::Approx(25.0));
}

TEST_CASE("BackgroundSubtraction: default-constructed background is all zeros", "[BackgroundSubtraction]")
{
  const uint16_t nDoppler = 2;
  const uint16_t nDelay = 2;
  const double alpha = 0.1;
  const uint64_t warmupCpis = 0; // no warmup

  BackgroundSubtraction bs(alpha, warmupCpis, nDoppler, nDelay);

  auto map = make_map(nDoppler, nDelay);
  map.data[0][0] = Complex(5.0, 0.0);

  // With warmupCpis=0 and background=0:
  // background = 0.9*0 + 0.1*25 = 2.5
  // residual = max(0, 25 - 2.5) = 22.5
  // newMag = sqrt(22.5) ≈ 4.7434
  // scale = 4.7434 / 5.0 = 0.94868
  bs.process(&map);

  const double expectedMagSq = 22.5;
  REQUIRE(magSq(map.data[0][0]) == Catch::Approx(expectedMagSq));
}

TEST_CASE("BackgroundSubtraction: phase is preserved", "[BackgroundSubtraction]")
{
  const uint16_t nDoppler = 1;
  const uint16_t nDelay = 1;
  const double alpha = 0.5;
  const uint64_t warmupCpis = 1;

  BackgroundSubtraction bs(alpha, warmupCpis, nDoppler, nDelay);

  auto map = make_map(nDoppler, nDelay);
  const double angle = std::acos(-1.0) / 4.0; // 45°
  map.data[0][0] = Complex(10.0 * std::cos(angle), 10.0 * std::sin(angle));

  // Warmup
  bs.process(&map);
  double phaseAfterWarmup = std::arg(map.data[0][0]);
  REQUIRE(phaseAfterWarmup == Catch::Approx(angle).margin(1e-9));

  // Active: alpha=0.5, background=50, residual=50, newMag=sqrt(50)≈7.07
  bs.process(&map);
  double phaseAfterActive = std::arg(map.data[0][0]);
  REQUIRE(phaseAfterActive == Catch::Approx(angle).margin(1e-9));
}