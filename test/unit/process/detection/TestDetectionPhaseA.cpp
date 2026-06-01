/// @file TestDetectionPhaseA.cpp
/// @brief Unit tests for Phase A detection robustness fixes.
/// @author 30hours

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "process/detection/CfarDetector1D.h"
#include "process/detection/Interpolate.h"
#include "data/Map.h"
#include "data/Detection.h"

#include <complex>
#include <vector>

TEST_CASE("CFAR_LeftEdgeTrainingBinIncluded", "[detection][cfar]")
{
  Map<std::complex<double>> map(1, 6);
  map.delay = {0, 1, 2, 3, 4, 5};
  map.doppler = {20.0};
  map.noisePower = 0.0;

  // Power profile crafted so CUT at delay=2 is detectable only when k=0
  // is included in training cells.
  map.data[0][0] = std::complex<double>(1.0, 0.0);                    // power 1
  map.data[0][1] = std::complex<double>(0.0, 0.0);                    // guard
  map.data[0][2] = std::complex<double>(44.72135955, 0.0);            // CUT power ~2000
  map.data[0][3] = std::complex<double>(0.0, 0.0);                    // guard
  map.data[0][4] = std::complex<double>(3.16227766, 0.0);             // power 10
  map.data[0][5] = std::complex<double>(0.0, 0.0);

  CfarDetector1D cfar(1e-3, 1, 1, 0, 0.0);
  std::unique_ptr<Detection> result = cfar.process(&map);

  REQUIRE(result->get_nDetections() >= 1);
  CHECK_THAT(result->get_delay().front(), Catch::Matchers::WithinAbs(2.0, 1e-6));
  CHECK_THAT(result->get_doppler().front(), Catch::Matchers::WithinAbs(20.0, 1e-6));
}

TEST_CASE("CFAR_EmptyTrainingWindowReturnsNoDetections", "[detection][cfar]")
{
  Map<std::complex<double>> map(1, 1);
  map.delay = {0};
  map.doppler = {30.0};
  map.noisePower = 0.0;
  map.data[0][0] = std::complex<double>(100.0, 0.0);

  // Guard/train sizes intentionally force empty training-cell set.
  CfarDetector1D cfar(1e-3, 4, 4, 0, 0.0);
  std::unique_ptr<Detection> result = cfar.process(&map);

  CHECK(result->get_nDetections() == 0);
}

TEST_CASE("Interpolate_DelayBoundaryDetectionRetained", "[detection][interpolate]")
{
  Map<std::complex<double>> map(3, 3);
  map.delay = {0, 1, 2};
  map.doppler = {-1.0, 0.0, 1.0};
  map.noisePower = 0.0;

  // Make doppler interpolation valid at delay index 0 for doppler=0.
  map.data[0][0] = std::complex<double>(2.0, 0.0);
  map.data[1][0] = std::complex<double>(3.0, 0.0);
  map.data[2][0] = std::complex<double>(1.0, 0.0);

  Detection input(0.0, 0.0, 0.0);
  Interpolate interpolate(true, true);

  std::unique_ptr<Detection> result = interpolate.process(&input, &map);

  REQUIRE(result->get_nDetections() == 1);
  CHECK_THAT(result->get_delay().front(), Catch::Matchers::WithinAbs(0.0, 1e-6));
}

TEST_CASE("Interpolate_DopplerBoundaryDetectionRetained", "[detection][interpolate]")
{
  Map<std::complex<double>> map(3, 3);
  map.delay = {0, 1, 2};
  map.doppler = {-1.0, 0.0, 1.0};
  map.noisePower = 0.0;

  // Make delay interpolation valid at doppler index 0 for delay=1.
  map.data[0][0] = std::complex<double>(2.0, 0.0);
  map.data[0][1] = std::complex<double>(3.0, 0.0);
  map.data[0][2] = std::complex<double>(1.0, 0.0);

  Detection input(1.0, -1.0, 0.0);
  Interpolate interpolate(true, true);

  std::unique_ptr<Detection> result = interpolate.process(&input, &map);

  REQUIRE(result->get_nDetections() == 1);
  CHECK_THAT(result->get_doppler().front(), Catch::Matchers::WithinAbs(-1.0, 1e-6));
}

/// @brief Cells inside the delay exclusion zone must NOT raise the CFAR
///        threshold for bins just outside the zone.
///
/// Layout: delays = {0, 1, 2, 3, 4, 5}, minDelay = 2.
/// Bins 0 and 1 (delays 0 and 1) are inside the exclusion zone and carry
/// very high power (strong clutter / sidelobe).  The target at bin 2 (delay 2)
/// has moderate power.  Background (bins 3-5) is low.
///
/// Old behaviour: clutter at delays 0-1 contributed to the training sum for
/// delay 2's leading window, inflating the threshold so the target was missed.
/// New behaviour: excluded cells contribute 0 to the prefix sum, so the
/// threshold at delay 2 reflects only the clean trailing background and the
/// target is detected.
TEST_CASE("CFAR_ExcludedDelayZoneDoesNotRaiseThreshold", "[detection][cfar][improvement5]")
{
  //  delay:  0     1      2        3    4    5
  //  power:  1e6   1e6    64       1    1    1   (amplitude²)
  //  amp:    1000  1000   8        1    1    1
  Map<std::complex<double>> map(1, 6);
  map.delay      = {0, 1, 2, 3, 4, 5};
  map.doppler    = {20.0};
  map.noisePower = 0.0;

  map.data[0][0] = std::complex<double>(1000.0, 0.0);  // power 1e6 — excluded clutter
  map.data[0][1] = std::complex<double>(1000.0, 0.0);  // power 1e6 — excluded clutter
  map.data[0][2] = std::complex<double>(8.0,    0.0);  // power 64  — target (just outside zone)
  map.data[0][3] = std::complex<double>(1.0,    0.0);  // power 1   — background
  map.data[0][4] = std::complex<double>(1.0,    0.0);  // power 1   — background
  map.data[0][5] = std::complex<double>(1.0,    0.0);  // power 1   — background

  // nGuard=1, nTrain=1, minDelay=2: only delay>=2 produces detections.
  // CA-CFAR: threshold = alpha * mean(trailing training cell).
  // Trailing cell for delay=2: bin 4 (j+guard+1=4), power = 1.
  // With pfa=0.01, nCells=1, alpha ~ 100.  threshold = 100*1 = 100 < 64? No.
  // Let's use pfa=0.1 → alpha~10, threshold=10*1=10 < 64 → detected.
  // With old code, training includes bins 0 and 1 (both power 1e6), so
  // leadingMean=1e6 >> trailingMean=1 → CA threshold = (1e6+1)/1 ≈ 5e5 >> 64.
  CfarDetector1D cfar(0.1, 1, 1, 2, 0.0, CfarMode::CA);
  std::unique_ptr<Detection> result = cfar.process(&map);

  // Target at delay=2 must be detected with the exclusion fix in place.
  bool foundTarget = false;
  for (double d : result->get_delay())
  {
    if (std::abs(d - 2.0) < 1e-6) { foundTarget = true; break; }
  }
  CHECK(foundTarget);
}

/// @brief Cells inside the delay exclusion zone must never appear in the
///        detection output regardless of their power.
TEST_CASE("CFAR_ExcludedDelayZoneProducesNoDetections", "[detection][cfar][improvement5]")
{
  Map<std::complex<double>> map(1, 4);
  map.delay      = {0, 1, 2, 3};
  map.doppler    = {20.0};
  map.noisePower = 0.0;

  // Extremely high power in excluded bins, modest power in valid bins.
  map.data[0][0] = std::complex<double>(1e6, 0.0);  // delay 0 — excluded
  map.data[0][1] = std::complex<double>(1e6, 0.0);  // delay 1 — excluded
  map.data[0][2] = std::complex<double>(1.0, 0.0);  // delay 2 — background
  map.data[0][3] = std::complex<double>(1.0, 0.0);  // delay 3 — background

  CfarDetector1D cfar(0.1, 0, 1, 2, 0.0, CfarMode::CA);
  std::unique_ptr<Detection> result = cfar.process(&map);

  // No detection should have delay < minDelay=2.
  for (double d : result->get_delay())
  {
    CHECK(d >= 2.0);
  }
}
