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

// Default gating parameters: 3 dB gate threshold (~2.0 linear), -30 dB suppression (0.001)
constexpr double kGateThreshold = 2.0;
constexpr double kSuppressionFactor = 0.001;
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
  BackgroundSubtraction bs(alpha, warmupCpis, nDoppler, nDelay,
                           kGateThreshold, kSuppressionFactor);

  // Run warmup CPIs — map should remain unchanged
  for (uint64_t k = 0; k < warmupCpis; k++)
  {
    bs.process(&map);
  }

  // After warmup, map still has the original peak
  REQUIRE(magSq(map.data[1][2]) == Catch::Approx(100.0));
}

TEST_CASE("BackgroundSubtraction: stationary peak is gated after warmup", "[BackgroundSubtraction]")
{
  const uint16_t nDoppler = 3;
  const uint16_t nDelay = 4;
  auto map = make_map(nDoppler, nDelay);

  // Place a stationary peak at (1, 2)
  map.data[1][2] = Complex(10.0, 0.0);

  const double alpha = 1.0; // jump straight to full value
  const uint64_t warmupCpis = 1;
  BackgroundSubtraction bs(alpha, warmupCpis, nDoppler, nDelay,
                           kGateThreshold, kSuppressionFactor);

  // Warmup pass — accumulate background, no gating
  bs.process(&map);
  REQUIRE(magSq(map.data[1][2]) == Catch::Approx(100.0));

  // Second CPI — oldBg=100, magSq=100, 100 < 2.0*100 → GATED
  // suppressed to 100 * (0.001)^2 = 0.0001
  bs.process(&map);
  REQUIRE(magSq(map.data[1][2]) == Catch::Approx(0.0001));
}

TEST_CASE("BackgroundSubtraction: moving peak survives gating", "[BackgroundSubtraction]")
{
  const uint16_t nDoppler = 4;
  const uint16_t nDelay = 5;
  const double alpha = 0.5;
  const uint64_t warmupCpis = 2;
  BackgroundSubtraction bs(alpha, warmupCpis, nDoppler, nDelay,
                           kGateThreshold, kSuppressionFactor);

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

  // After warmup CPI 2: bg(0,0)=50, bg(2,3)=0
  // CPI 3: peak MOVES to (2, 3) — new position
  auto map3 = make_map(nDoppler, nDelay);
  map3.data[2][3] = Complex(10.0, 0.0);
  bs.process(&map3); // active: gate check

  // (2,3): magSq=100, oldBg=0 → 100 < 2*0 is false → NOT gated, passes through at full power
  // (0,0): magSq=0, oldBg=50 → 0 < 2*50 is true → gated, but 0*suppression = 0
  REQUIRE(magSq(map3.data[2][3]) == Catch::Approx(100.0));
  REQUIRE(magSq(map3.data[0][0]) == Catch::Approx(0.0));

  // CPI 4: peak stays at (2, 3) — becomes stationary
  auto map4 = make_map(nDoppler, nDelay);
  map4.data[2][3] = Complex(10.0, 0.0);
  bs.process(&map4);
  // (2,3): magSq=100, oldBg=50 (from CPI 3 update) → 100 < 2*50 is false → NOT gated
  // This is the expected trade-off: a target that lands and stays in a bin
  // will take multiple CPIs before the background converges enough to gate it.
  // With alpha=0.5 and gateThreshold=2.0, after CPI 3 bg=50, so 100 < 100 is false.
  REQUIRE(magSq(map4.data[2][3]) == Catch::Approx(100.0));

  // CPI 5: peak STILL at (2, 3) — background continues converging
  auto map5 = make_map(nDoppler, nDelay);
  map5.data[2][3] = Complex(10.0, 0.0);
  bs.process(&map5);
  // (2,3): oldBg=75 (0.5*50+0.5*100=75) → 100 < 2*75=150 → true → GATED
  // suppressed to 100 * (0.001)^2 = 0.0001
  REQUIRE(magSq(map5.data[2][3]) == Catch::Approx(0.0001));
}

TEST_CASE("BackgroundSubtraction: noise cell below background is gated", "[BackgroundSubtraction]")
{
  const uint16_t nDoppler = 1;
  const uint16_t nDelay = 1;
  const double alpha = 1.0; // instant convergence
  const uint64_t warmupCpis = 1;
  BackgroundSubtraction bs(alpha, warmupCpis, nDoppler, nDelay,
                           kGateThreshold, kSuppressionFactor);

  // CPI 1: warmup — background converges to noise level (10.0 → magSq=100)
  auto map1 = make_map(nDoppler, nDelay);
  map1.data[0][0] = Complex(10.0, 0.0);
  bs.process(&map1);
  REQUIRE(magSq(map1.data[0][0]) == Catch::Approx(100.0));

  // CPI 2: same noise level — matches background → gated
  auto map2 = make_map(nDoppler, nDelay);
  map2.data[0][0] = Complex(10.0, 0.0);
  bs.process(&map2);
  // oldBg=100, magSq=100, 100 < 2*100 → true → GATED
  REQUIRE(magSq(map2.data[0][0]) == Catch::Approx(0.0001).margin(1e-6));
}

TEST_CASE("BackgroundSubtraction: transient signal above threshold passes through", "[BackgroundSubtraction]")
{
  const uint16_t nDoppler = 1;
  const uint16_t nDelay = 1;
  const double alpha = 1.0;
  const uint64_t warmupCpis = 1;
  BackgroundSubtraction bs(alpha, warmupCpis, nDoppler, nDelay,
                           4.0 /* 6 dB gate */, kSuppressionFactor);

  // Warmup with noise level (5.0 → magSq=25)
  auto map1 = make_map(nDoppler, nDelay);
  map1.data[0][0] = Complex(5.0, 0.0);
  bs.process(&map1);

  // Strong signal arrives: 15.0 → magSq=225
  auto map2 = make_map(nDoppler, nDelay);
  map2.data[0][0] = Complex(15.0, 0.0);
  bs.process(&map2);
  // oldBg=25, magSq=225, 225 < 4*25=100 is FALSE → passes through
  REQUIRE(magSq(map2.data[0][0]) == Catch::Approx(225.0));
}

TEST_CASE("BackgroundSubtraction: phase is preserved when not gated", "[BackgroundSubtraction]")
{
  const uint16_t nDoppler = 1;
  const uint16_t nDelay = 1;
  const double alpha = 0.5;
  const uint64_t warmupCpis = 1;
  BackgroundSubtraction bs(alpha, warmupCpis, nDoppler, nDelay,
                           kGateThreshold, kSuppressionFactor);

  auto map = make_map(nDoppler, nDelay);
  const double angle = std::acos(-1.0) / 4.0; // 45°
  map.data[0][0] = Complex(10.0 * std::cos(angle), 10.0 * std::sin(angle));

  // Warmup — no change
  bs.process(&map);
  double phaseAfterWarmup = std::arg(map.data[0][0]);
  REQUIRE(phaseAfterWarmup == Catch::Approx(angle).margin(1e-9));

  // Active: oldBg=50, magSq=100, 100 < 2*50=100 → false (not gated, passes through)
  bs.process(&map);
  double phaseAfterActive = std::arg(map.data[0][0]);
  REQUIRE(phaseAfterActive == Catch::Approx(angle).margin(1e-9));
}