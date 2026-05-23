/// @file TestDataContracts.cpp
/// @brief Functional tests for runtime JSON data contracts.
/// @author GitHub Copilot

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "data/Detection.h"
#include "data/Map.h"
#include "data/Track.h"
#include "data/meta/Constants.h"
#include "data/meta/Timing.h"

#include <rapidjson/document.h>

#include <cstdint>
#include <string>
#include <vector>

namespace
{
rapidjson::Document parse_document(const std::string &json)
{
  rapidjson::Document document;
  document.Parse(json.c_str());
  return document;
}
}

TEST_CASE("DetectionJsonConvertsDelayBinsToKm", "[functional][data][detection]")
{
  const uint64_t timestamp = 123456;
  const uint32_t fs = 1000000;
  const double delayScaleKm = static_cast<double>(Constants::c) / static_cast<double>(fs) / 1000.0;
  Detection detection({2.0, 4.0}, {5.5, -3.25}, {12.0, 9.5});

  rapidjson::Document document = parse_document(detection.to_json(timestamp, fs, true));

  REQUIRE_FALSE(document.HasParseError());
  REQUIRE(document.IsObject());
  REQUIRE(document["delay"].IsArray());
  REQUIRE(document["doppler"].IsArray());
  REQUIRE(document["snr"].IsArray());
  REQUIRE(document["delay"].Size() == 2);
  CHECK(document["timestamp"].GetUint64() == timestamp);
  CHECK_THAT(document["delay"][0].GetDouble(), Catch::Matchers::WithinAbs(2.0 * delayScaleKm, 0.01));
  CHECK_THAT(document["delay"][1].GetDouble(), Catch::Matchers::WithinAbs(4.0 * delayScaleKm, 0.01));
  CHECK_THAT(document["doppler"][0].GetDouble(), Catch::Matchers::WithinAbs(5.5, 0.01));
  CHECK_THAT(document["doppler"][1].GetDouble(), Catch::Matchers::WithinAbs(-3.25, 0.01));
  CHECK_THAT(document["snr"][0].GetDouble(), Catch::Matchers::WithinAbs(12.0, 0.01));
  CHECK_THAT(document["snr"][1].GetDouble(), Catch::Matchers::WithinAbs(9.5, 0.01));
}

TEST_CASE("MapJsonCarriesNormalizedMetricsAndAxes", "[functional][data][map]")
{
  const uint64_t timestamp = 77;
  const uint32_t fs = 1000000;
  const double delayScaleKm = static_cast<double>(Constants::c) / static_cast<double>(fs) / 1000.0;
  Map<double> map(2, 2);
  map.set_row(0, {1.0, 10.0});
  map.set_row(1, {100.0, 1000.0});
  map.delay = {0, 2};
  map.doppler = {-12.5, 12.5};
  map.set_metrics();

  rapidjson::Document document = parse_document(map.to_json(timestamp, fs, true));

  REQUIRE_FALSE(document.HasParseError());
  REQUIRE(document.IsObject());
  REQUIRE(document["data"].IsArray());
  REQUIRE(document["data"].Size() == 2);
  REQUIRE(document["data"][0].IsArray());
  REQUIRE(document["data"][0].Size() == 2);
  REQUIRE(document["delay"].IsArray());
  REQUIRE(document["doppler"].IsArray());
  CHECK(document["timestamp"].GetUint64() == timestamp);
  CHECK(document["nRows"].GetUint() == 2);
  CHECK(document["nCols"].GetUint() == 2);
  CHECK_THAT(document["noisePower"].GetDouble(), Catch::Matchers::WithinAbs(15.0, 0.01));
  CHECK_THAT(document["maxPower"].GetDouble(), Catch::Matchers::WithinAbs(15.0, 0.01));
  CHECK_THAT(document["delay"][0].GetDouble(), Catch::Matchers::WithinAbs(0.0, 0.01));
  CHECK_THAT(document["delay"][1].GetDouble(), Catch::Matchers::WithinAbs(2.0 * delayScaleKm, 0.01));
  CHECK_THAT(document["doppler"][0].GetDouble(), Catch::Matchers::WithinAbs(-12.5, 0.01));
  CHECK_THAT(document["doppler"][1].GetDouble(), Catch::Matchers::WithinAbs(12.5, 0.01));
  CHECK_THAT(document["data"][0][0].GetDouble(), Catch::Matchers::WithinAbs(-15.0, 0.01));
  CHECK_THAT(document["data"][0][1].GetDouble(), Catch::Matchers::WithinAbs(-5.0, 0.01));
  CHECK_THAT(document["data"][1][0].GetDouble(), Catch::Matchers::WithinAbs(5.0, 0.01));
  CHECK_THAT(document["data"][1][1].GetDouble(), Catch::Matchers::WithinAbs(15.0, 0.01));
}

TEST_CASE("TrackJsonEmitsPromotedTrackHistory", "[functional][data][track]")
{
  const uint64_t timestamp = 9999;
  const uint32_t fs = 1000000;
  const double delayScaleKm = static_cast<double>(Constants::c) / static_cast<double>(fs) / 1000.0;
  Track track;
  const uint64_t index = track.add(Detection(3.0, 9.0, 15.0));
  track.set_state(index, "ASSOCIATED");
  track.set_current(index, Detection(4.0, 12.0, 18.0));
  track.set_acceleration(index, 0.5);

  rapidjson::Document document = parse_document(track.to_json(timestamp, fs, true));

  REQUIRE_FALSE(document.HasParseError());
  REQUIRE(document.IsObject());
  REQUIRE(document["data"].IsArray());
  REQUIRE(document["data"].Size() == 1);
  REQUIRE(document["data"][0].IsObject());
  REQUIRE(document["data"][0]["associated_delay"].IsArray());
  REQUIRE(document["data"][0]["associated_state"].IsArray());
  CHECK(document["timestamp"].GetUint64() == timestamp);
  CHECK(document["n"].GetUint64() == 1);
  CHECK(document["nTentative"].GetUint64() == 0);
  CHECK(document["nAssociated"].GetUint64() == 1);
  CHECK(document["nActive"].GetUint64() == 0);
  CHECK(document["nCoasting"].GetUint64() == 0);
  CHECK(std::string(document["data"][0]["id"].GetString()) == "0000");
  CHECK(std::string(document["data"][0]["state"].GetString()) == "ASSOCIATED");
  CHECK_THAT(document["data"][0]["delay"].GetDouble(), Catch::Matchers::WithinAbs(4.0 * delayScaleKm, 0.01));
  CHECK_THAT(document["data"][0]["doppler"].GetDouble(), Catch::Matchers::WithinAbs(12.0, 0.01));
  CHECK_THAT(document["data"][0]["acceleration"].GetDouble(), Catch::Matchers::WithinAbs(0.5, 0.001));
  CHECK(document["data"][0]["n"].GetUint64() == 2);
  CHECK_THAT(document["data"][0]["associated_delay"][0].GetDouble(), Catch::Matchers::WithinAbs(3.0 * delayScaleKm, 0.01));
  CHECK_THAT(document["data"][0]["associated_delay"][1].GetDouble(), Catch::Matchers::WithinAbs(4.0 * delayScaleKm, 0.01));
  CHECK_THAT(document["data"][0]["associated_doppler"][0].GetDouble(), Catch::Matchers::WithinAbs(9.0, 0.01));
  CHECK_THAT(document["data"][0]["associated_doppler"][1].GetDouble(), Catch::Matchers::WithinAbs(12.0, 0.01));
  CHECK(std::string(document["data"][0]["associated_state"][0].GetString()) == "TENTATIVE");
  CHECK(std::string(document["data"][0]["associated_state"][1].GetString()) == "ASSOCIATED");
}

TEST_CASE("TimingJsonReportsElapsedAndStageDurations", "[functional][data][timing]")
{
  Timing timing(1000);
  timing.update(61000, {1.25, 2.5}, {"capture_ms", "process_ms"});

  rapidjson::Document document = parse_document(timing.to_json());

  REQUIRE_FALSE(document.HasParseError());
  REQUIRE(document.IsObject());
  CHECK(document["timestamp"].GetUint64() == 61000);
  CHECK(document["nCpi"].GetUint64() == 1);
  CHECK_THAT(document["uptime_s"].GetDouble(), Catch::Matchers::WithinAbs(60.0, 0.01));
  CHECK_THAT(document["uptime_days"].GetDouble(), Catch::Matchers::WithinAbs(60.0 / 86400.0, 0.01));
  CHECK_THAT(document["capture_ms"].GetDouble(), Catch::Matchers::WithinAbs(1.25, 0.01));
  CHECK_THAT(document["process_ms"].GetDouble(), Catch::Matchers::WithinAbs(2.5, 0.01));
}