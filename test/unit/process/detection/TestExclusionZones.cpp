/// @file TestExclusionZones.cpp
/// @brief Unit tests for exclusion zone suppression and track-based override.
/// @author 30hours

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "process/detection/CfarDetector1D.h"
#include "data/Map.h"
#include "data/Detection.h"

#include <complex>
#include <cmath>

namespace
{
bool has_delay(Detection *detection, double delayTarget)
{
  for (double d : detection->get_delay())
  {
    if (std::abs(d - delayTarget) < 1e-6)
    {
      return true;
    }
  }
  return false;
}
}

/// @brief A detection inside an exclusion zone is suppressed.
TEST_CASE("CFAR_ExclusionZoneSuppressesDetection", "[detection][cfar][exclusion]")
{
  // 5-bin map: delays 0..4, single Doppler row at 20 Hz.
  // Exclusion zone covers delay [1, 2], Doppler [-100, 100].
  // Target at delay=2 has high power — should be suppressed.
  Map<std::complex<double>> map(1, 5);
  map.delay      = {0, 1, 2, 3, 4};
  map.doppler    = {20.0};
  map.noisePower = 0.0;

  map.data[0][0] = std::complex<double>(1.0, 0.0);                   // power 1
  map.data[0][1] = std::complex<double>(1.0, 0.0);                   // power 1
  map.data[0][2] = std::complex<double>(100.0, 0.0);                 // power 10000 — target inside zone
  map.data[0][3] = std::complex<double>(1.0, 0.0);                   // power 1
  map.data[0][4] = std::complex<double>(1.0, 0.0);                   // power 1

  std::vector<ExclusionZone> zones;
  ExclusionZone zone;
  zone.delayMinBins = 1.0;
  zone.delayMaxBins = 2.0;
  zone.dopplerMinHz = -100.0;
  zone.dopplerMaxHz = 100.0;
  zones.push_back(zone);

  CfarDetector1D cfar(0.5, 0, 1, 0, 0.0, CfarMode::CA, true, zones);
  std::unique_ptr<Detection> result = cfar.process(&map);

  CHECK(!has_delay(result.get(), 2.0));
}

/// @brief An exclusion zone is overridden by an allowed zone (track gate).
TEST_CASE("CFAR_ExclusionZoneWithAllowedOverride", "[detection][cfar][exclusion]")
{
  // Same map and exclusion zone as previous test.
  // An allowed zone covers delay [1.5, 2.5] — target at delay=2 should
  // now pass through.
  Map<std::complex<double>> map(1, 5);
  map.delay      = {0, 1, 2, 3, 4};
  map.doppler    = {20.0};
  map.noisePower = 0.0;

  map.data[0][0] = std::complex<double>(1.0, 0.0);                   // power 1
  map.data[0][1] = std::complex<double>(1.0, 0.0);                   // power 1
  map.data[0][2] = std::complex<double>(100.0, 0.0);                 // power 10000 — target
  map.data[0][3] = std::complex<double>(1.0, 0.0);                   // power 1
  map.data[0][4] = std::complex<double>(1.0, 0.0);                   // power 1

  std::vector<ExclusionZone> zones;
  ExclusionZone zone;
  zone.delayMinBins = 1.0;
  zone.delayMaxBins = 2.0;
  zone.dopplerMinHz = -100.0;
  zone.dopplerMaxHz = 100.0;
  zones.push_back(zone);

  CfarDetector1D cfar(0.5, 0, 1, 0, 0.0, CfarMode::CA, true, zones);

  // Set an allowed zone that overlaps the target.
  std::vector<ExclusionZone> allowed;
  ExclusionZone allowedZone;
  allowedZone.delayMinBins = 1.5;
  allowedZone.delayMaxBins = 2.5;
  allowedZone.dopplerMinHz = -100.0;
  allowedZone.dopplerMaxHz = 100.0;
  allowed.push_back(allowedZone);
  cfar.set_allowed_zones(std::move(allowed));

  std::unique_ptr<Detection> result = cfar.process(&map);

  CHECK(has_delay(result.get(), 2.0));
}

/// @brief A target just outside an exclusion zone boundary is still detected.
///        Verifies that CFAR threshold statistics are unaffected by
///        the exclusion zone filter.
TEST_CASE("CFAR_ExclusionZoneEdgeBoundaryUnaffected", "[detection][cfar][exclusion]")
{
  // Exclusion zone covers delay [1, 2].  Target at delay=3 (just outside)
  // with high power should be detected.
  Map<std::complex<double>> map(1, 6);
  map.delay      = {0, 1, 2, 3, 4, 5};
  map.doppler    = {20.0};
  map.noisePower = 0.0;

  map.data[0][0] = std::complex<double>(1.0, 0.0);                   // power 1
  map.data[0][1] = std::complex<double>(1.0, 0.0);                   // power 1
  map.data[0][2] = std::complex<double>(1.0, 0.0);                   // power 1
  map.data[0][3] = std::complex<double>(100.0, 0.0);                 // power 10000 — target outside zone
  map.data[0][4] = std::complex<double>(1.0, 0.0);                   // power 1
  map.data[0][5] = std::complex<double>(1.0, 0.0);                   // power 1

  std::vector<ExclusionZone> zones;
  ExclusionZone zone;
  zone.delayMinBins = 1.0;
  zone.delayMaxBins = 2.0;
  zone.dopplerMinHz = -100.0;
  zone.dopplerMaxHz = 100.0;
  zones.push_back(zone);

  CfarDetector1D cfar(0.5, 0, 1, 0, 0.0, CfarMode::CA, true, zones);
  std::unique_ptr<Detection> result = cfar.process(&map);

  CHECK(has_delay(result.get(), 3.0));
}