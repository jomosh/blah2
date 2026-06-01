/// @file TestTracker.cpp
/// @brief Unit test for Tracker.cpp
/// @author 30hours

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "data/Detection.h"
#include "data/Track.h"
#include "process/tracker/Tracker.h"
#include "data/meta/Constants.h"

#include <string>
#include <vector>
#include <random>
#include <iostream>

/// @brief Test constructor.
/// @details Check constructor parameters created correctly.
TEST_CASE("Constructor", "[constructor]")
{
  uint32_t m = 3;
  uint32_t n = 5;
  uint32_t nDelete = 5;
  double cpi = 1;
  double maxAccInit = 10;
  double fs = 2000000;
  double rangeRes = (double)Constants::c/fs;
  double fc = 204640000;
  double lambda = (double)Constants::c/fc;
  Tracker tracker = Tracker(m, n, nDelete, 
    cpi, maxAccInit, rangeRes, lambda);
}

/// @brief Test process for an ACTIVE track.
TEST_CASE("Process ACTIVE track constant acc", "[process]")
{
  uint32_t m = 3;
  uint32_t n = 5;
  uint32_t nDelete = 5;
  double cpi = 1;
  double maxAccInit = 10;
  double fs = 2000000;
  double rangeRes = (double)Constants::c/fs;
  double fc = 204640000;
  double lambda = (double)Constants::c/fc;
  Tracker tracker = Tracker(m, n, nDelete, 
    cpi, maxAccInit, rangeRes, lambda);
  
  
  // create detections with constant acc 5 Hz/s
  std::vector<uint64_t> timestamp = {0,1,2,3,4,5,6,7,8,9,10};
  std::vector<double> delay = {10};
  std::vector<double> doppler = {-20,-15,-10,-5,0,5,10,15,20,25};

  std::string state = "ACTIVE";
}

/// @brief Test predict for kinematics equations.
TEST_CASE("Test predict", "[predict]")
{
  uint32_t m = 3;
  uint32_t n = 5;
  uint32_t nDelete = 5;
  double cpi = 1;
  double maxAccInit = 10;
  double fs = 2000000;
  double rangeRes = (double)Constants::c/fs;
  double fc = 204640000;
  double lambda = (double)Constants::c/fc;
  Tracker tracker = Tracker(m, n, nDelete, 
    cpi, maxAccInit, rangeRes, lambda);

  Detection input = Detection(10, -20, 0);
  double acc = 5;
  double T = 1;
  Detection prediction = tracker.predict(input, acc, T);
  Detection prediction_truth = Detection(9.821, -15, 0);

  CHECK_THAT(prediction.get_delay().front(), 
    Catch::Matchers::WithinAbs(prediction_truth.get_delay().front(), 0.01));
  CHECK_THAT(prediction.get_doppler().front(), 
    Catch::Matchers::WithinAbs(prediction_truth.get_doppler().front(), 0.01));
}

/// @brief Test update keeps previous Doppler for acceleration calculation.
TEST_CASE("Process update preserves previous Doppler for acceleration", "[process]")
{
  uint32_t m = 1;
  uint32_t n = 1;
  uint32_t nDelete = 5;
  double cpi = 1;
  double maxAccInit = 0;
  double fs = 2000000;
  double rangeRes = (double)Constants::c/fs;
  double fc = 204640000;
  double lambda = (double)Constants::c/fc;
  Tracker tracker = Tracker(m, n, nDelete,
    cpi, maxAccInit, rangeRes, lambda);

  Detection first(10, 10, 20);
  auto trackAfterFirst = tracker.process(&first, 0);
  REQUIRE(trackAfterFirst->get_n() == 1);
  CHECK_THAT(trackAfterFirst->get_acceleration(0), Catch::Matchers::WithinAbs(0.0, 0.001));

  Detection second(10, 12, 20);
  auto trackAfterSecond = tracker.process(&second, 1000);
  REQUIRE(trackAfterSecond->get_n() == 1);
  CHECK_THAT(trackAfterSecond->get_acceleration(0), Catch::Matchers::WithinAbs(2.0, 0.001));
}
/// @brief Hungarian assignment must produce the globally optimal matching.
///
/// Scenario: two tracks and two detections where a greedy nearest-neighbour
/// search produces a sub-optimal result that differs from the Hungarian
/// optimum.
///
///   Track 0 predicted at delay=10, doppler=5.0.
///   Track 1 predicted at delay=10, doppler=5.2.
///   Detection A: delay=10.5, doppler=5.0.  (cost T0→A≈0.167, T1→A≈0.179)
///   Detection B: delay=10.1, doppler=5.2.  (cost T0→B≈0.075, T1→B≈0.033)
///
/// Note: costs above are approximate; the Doppler-driven delay prediction
/// shifts predicted positions slightly from the initial values, so the actual
/// runtime costs differ by a small amount.  The optimal pairing T0→A, T1→B
/// is unchanged under either cost set.
///
/// Both detections are inside both tracks' gates.
///
/// Greedy (T0 first, nearest-unassigned): T0 picks B (cost 0.075), T1 is
/// forced to take A (cost 0.179).  Total = 0.254.
///
/// Hungarian optimal: T0→A (cost 0.167) + T1→B (cost 0.033) = 0.200.
///
/// The per-track Doppler assertions verify the globally optimal assignment;
/// a greedy algorithm would produce the wrong pairing and the checks would
/// fail.
TEST_CASE("Hungarian_BothTracksAssignedOptimally", "[process][tracker][hungarian]")
{
  // m=1, n=1 so each detection immediately promotes a track.
  uint32_t m = 1;
  uint32_t n = 1;
  uint32_t nDelete = 5;
  double cpi = 1.0;
  double maxAccInit = 0.0;  // no acceleration hypotheses, one track per init
  double fs = 2000000;
  double rangeRes = (double)Constants::c / fs;
  double fc = 204640000;
  double lambda = (double)Constants::c / fc;

  Tracker tracker(m, n, nDelete, cpi, maxAccInit, rangeRes, lambda);

  // CPI 0: initiate two tracks.
  // Track index 0: delay=10, doppler=5.0.  Track index 1: delay=10, doppler=5.2.
  Detection initDetections({10.0, 10.0}, {5.0, 5.2}, {0.0, 0.0});
  auto trackAfterInit = tracker.process(&initDetections, 0);
  REQUIRE(trackAfterInit->get_n() == 2);

  // CPI 1: Detection A (index 0) at (10.5, 5.0), Detection B (index 1) at (10.1, 5.2).
  // Both fall inside both tracks' gates.
  Detection cpi1({10.5, 10.1}, {5.0, 5.2}, {1.0, 1.0});
  auto trackAfterCpi1 = tracker.process(&cpi1, 1000);

  // Both tracks must have been associated (nInactive == 0).
  REQUIRE(trackAfterCpi1->get_n() == 2);
  CHECK(trackAfterCpi1->get_nInactive(0) == 0);
  CHECK(trackAfterCpi1->get_nInactive(1) == 0);

  // Verify the globally optimal pairing: T0→A and T1→B.
  // A greedy algorithm would assign T0→B and T1→A, swapping the Dopplers.
  CHECK_THAT(trackAfterCpi1->get_current(0).get_doppler().front(),
             Catch::Matchers::WithinAbs(5.0, 0.01));   // T0 updated to A (doppler=5.0)
  CHECK_THAT(trackAfterCpi1->get_current(1).get_doppler().front(),
             Catch::Matchers::WithinAbs(5.2, 0.01));   // T1 updated to B (doppler=5.2)
}

/// @brief With one track and two in-gate detections, the closer detection is
///        chosen (Hungarian on a 1x2 matrix minimises over columns).
TEST_CASE("Hungarian_SingleTrackSelectsNearestDetection", "[process][tracker][hungarian]")
{
  uint32_t m = 1;
  uint32_t n = 1;
  uint32_t nDelete = 5;
  double cpi = 1.0;
  double maxAccInit = 0.0;
  double fs = 2000000;
  double rangeRes = (double)Constants::c / fs;
  double fc = 204640000;
  double lambda = (double)Constants::c / fc;

  Tracker tracker(m, n, nDelete, cpi, maxAccInit, rangeRes, lambda);

  // Initiate a single track at delay=10, doppler=0.
  Detection init(10.0, 0.0, 0.0);
  tracker.process(&init, 0);

  // Two detections: A is 0.5 bins away, B is 2.0 bins away (both in gate).
  Detection cpi1({10.5, 12.0}, {0.0, 0.0}, {1.0, 1.0});
  auto result = tracker.process(&cpi1, 1000);

  REQUIRE(result->get_n() >= 1);
  // The track must be updated to the closer detection (delay~10.5, not 12.0).
  const double updatedDelay = result->get_current(0).get_delay().front();
  CHECK_THAT(updatedDelay, Catch::Matchers::WithinAbs(10.5, 0.01));
}
