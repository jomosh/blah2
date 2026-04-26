/// @file TestCentroid.cpp
/// @brief Unit test for Centroid.cpp
/// @author GitHub Copilot

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "process/detection/Centroid.h"

TEST_CASE("Local_Peak_Mode_Keeps_Highest_Snr_Detection", "[centroid]")
{
  Detection detections({10.0, 11.0}, {0.0, 0.0}, {6.0, 12.0});
  Centroid centroid(1, 1, 1.0, CentroidMode::LocalPeak);

  std::unique_ptr<Detection> result = centroid.process(&detections);

  REQUIRE(result->get_nDetections() == 1);
  CHECK(result->get_delay()[0] == 11.0);
  CHECK(result->get_doppler()[0] == 0.0);
  CHECK(result->get_snr()[0] == 12.0);
}

TEST_CASE("Local_Peak_Mode_Handles_Low_Delay_Edge_Without_Wrapping", "[centroid]")
{
  Detection detections({0.0, 1.0}, {0.0, 0.0}, {4.0, 9.0});
  Centroid centroid(1, 1, 1.0, CentroidMode::LocalPeak);

  std::unique_ptr<Detection> result = centroid.process(&detections);

  REQUIRE(result->get_nDetections() == 1);
  CHECK(result->get_delay()[0] == 1.0);
  CHECK(result->get_doppler()[0] == 0.0);
  CHECK(result->get_snr()[0] == 9.0);
}

TEST_CASE("Cluster_Centroid_Mode_Averages_Cluster_With_Weighted_Centroid", "[centroid]")
{
  Detection detections({10.0, 11.0, 30.0}, {0.0, 0.0, 40.0}, {10.0, 20.0, 5.0});
  Centroid centroid(1, 1, 1.0, CentroidMode::ClusterCentroid);

  std::unique_ptr<Detection> result = centroid.process(&detections);

  REQUIRE(result->get_nDetections() == 2);
  CHECK_THAT(result->get_delay()[0], Catch::Matchers::WithinAbs(10.909, 0.001));
  CHECK_THAT(result->get_doppler()[0], Catch::Matchers::WithinAbs(0.0, 0.001));
  CHECK(result->get_snr()[0] == 20.0);
  CHECK_THAT(result->get_delay()[1], Catch::Matchers::WithinAbs(30.0, 0.001));
  CHECK_THAT(result->get_doppler()[1], Catch::Matchers::WithinAbs(40.0, 0.001));
  CHECK(result->get_snr()[1] == 5.0);
}