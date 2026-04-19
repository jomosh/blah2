/// @file TestDetectionPhaseB.cpp
/// @brief Unit tests for Phase B CAGO-CFAR behavior.
/// @author 30hours

#include <catch2/catch_test_macros.hpp>

#include "process/detection/CfarDetector1D.h"
#include "data/Map.h"
#include "data/Detection.h"

#include <complex>
#include <cmath>
#include <random>

namespace
{
bool has_delay(Detection* detection, double delayTarget)
{
  for (double delay : detection->get_delay())
  {
    if (std::abs(delay - delayTarget) < 1e-6)
    {
      return true;
    }
  }
  return false;
}
}

TEST_CASE("CFAR_CAGO_SuppressesClutterEdgeFalseAlarm", "[detection][cfar][phaseb]")
{
  Map<std::complex<double>> map(1, 5);
  map.delay = {0, 1, 2, 3, 4};
  map.doppler = {20.0};
  map.noisePower = 0.0;

  // CUT at delay=2 has moderate power. Leading side is cluttered, trailing
  // side is quiet.
  map.data[0][0] = std::complex<double>(0.1, 0.0);
  map.data[0][1] = std::complex<double>(10.0, 0.0);                 // power 100
  map.data[0][2] = std::complex<double>(7.745966692, 0.0);          // power 60
  map.data[0][3] = std::complex<double>(1.0, 0.0);                  // power 1
  map.data[0][4] = std::complex<double>(0.1, 0.0);

  CfarDetector1D cfarCa(0.5, 0, 1, 0, 0.0, CfarMode::CA);
  CfarDetector1D cfarCago(0.5, 0, 1, 0, 0.0, CfarMode::CAGO);

  std::unique_ptr<Detection> caResult = cfarCa.process(&map);
  std::unique_ptr<Detection> cagoResult = cfarCago.process(&map);

  CHECK(has_delay(caResult.get(), 2.0));
  CHECK(!has_delay(cagoResult.get(), 2.0));
}

TEST_CASE("CFAR_CAGO_MatchesCAInHomogeneousNoise", "[detection][cfar][phaseb]")
{
  Map<std::complex<double>> map(1, 7);
  map.delay = {0, 1, 2, 3, 4, 5, 6};
  map.doppler = {20.0};
  map.noisePower = 0.0;

  // Symmetric local background around CUT at delay=3.
  map.data[0][0] = std::complex<double>(0.1, 0.0);
  map.data[0][1] = std::complex<double>(1.0, 0.0);                  // power 1
  map.data[0][2] = std::complex<double>(1.0, 0.0);                  // power 1
  map.data[0][3] = std::complex<double>(5.0, 0.0);                  // power 25 (CUT)
  map.data[0][4] = std::complex<double>(1.0, 0.0);                  // power 1
  map.data[0][5] = std::complex<double>(1.0, 0.0);                  // power 1
  map.data[0][6] = std::complex<double>(0.1, 0.0);

  CfarDetector1D cfarCa(0.5, 0, 2, 0, 0.0, CfarMode::CA);
  CfarDetector1D cfarCago(0.5, 0, 2, 0, 0.0, CfarMode::CAGO);

  std::unique_ptr<Detection> caResult = cfarCa.process(&map);
  std::unique_ptr<Detection> cagoResult = cfarCago.process(&map);

  REQUIRE(has_delay(caResult.get(), 3.0));
  REQUIRE(has_delay(cagoResult.get(), 3.0));
  CHECK(caResult->get_nDetections() == cagoResult->get_nDetections());
}

TEST_CASE("CFAR_CAGO_EmpiricalPfaIsControlled", "[detection][cfar][phaseb][calibration]")
{
  const double pfaTarget = 0.05;
  const int nTrials = 8000;
  const int nCols = 33;
  const int cutIndex = 16;

  CfarDetector1D cfarCago(pfaTarget, 1, 6, 0, 0.0, CfarMode::CAGO);

  std::mt19937 rng(1337);
  std::exponential_distribution<double> expPower(1.0);

  int nFalseAtCut = 0;
  for (int trial = 0; trial < nTrials; trial++)
  {
    Map<std::complex<double>> map(1, nCols);
    map.noisePower = 0.0;
    map.doppler = {20.0};
    map.delay.clear();
    for (int j = 0; j < nCols; j++)
    {
      map.delay.push_back(j);
      const double power = expPower(rng);
      const double amplitude = std::sqrt(power);
      map.data[0][j] = std::complex<double>(amplitude, 0.0);
    }

    std::unique_ptr<Detection> detection = cfarCago.process(&map);
    if (has_delay(detection.get(), cutIndex))
    {
      nFalseAtCut++;
    }
  }

  const double empiricalPfa = (double)nFalseAtCut / nTrials;
  CHECK(empiricalPfa <= pfaTarget * 1.2);
  CHECK(empiricalPfa >= pfaTarget * 0.2);
}
