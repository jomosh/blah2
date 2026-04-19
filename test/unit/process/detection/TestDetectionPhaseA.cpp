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
