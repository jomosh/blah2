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

const rapidjson::Value &require_member(const rapidjson::Value &object,
  const char *memberName)
{
  REQUIRE(object.IsObject());
  const auto member = object.FindMember(memberName);
  INFO("Missing JSON member: " << memberName);
  REQUIRE(member != object.MemberEnd());
  return member->value;
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
  const rapidjson::Value &delay = require_member(document, "delay");
  const rapidjson::Value &doppler = require_member(document, "doppler");
  const rapidjson::Value &snr = require_member(document, "snr");
  const rapidjson::Value &jsonTimestamp = require_member(document, "timestamp");
  REQUIRE(delay.IsArray());
  REQUIRE(doppler.IsArray());
  REQUIRE(snr.IsArray());
  REQUIRE(delay.Size() == 2);
  CHECK(jsonTimestamp.GetUint64() == timestamp);
  CHECK_THAT(delay[0].GetDouble(), Catch::Matchers::WithinAbs(2.0 * delayScaleKm, 0.01));
  CHECK_THAT(delay[1].GetDouble(), Catch::Matchers::WithinAbs(4.0 * delayScaleKm, 0.01));
  CHECK_THAT(doppler[0].GetDouble(), Catch::Matchers::WithinAbs(5.5, 0.01));
  CHECK_THAT(doppler[1].GetDouble(), Catch::Matchers::WithinAbs(-3.25, 0.01));
  CHECK_THAT(snr[0].GetDouble(), Catch::Matchers::WithinAbs(12.0, 0.01));
  CHECK_THAT(snr[1].GetDouble(), Catch::Matchers::WithinAbs(9.5, 0.01));
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
  const rapidjson::Value &data = require_member(document, "data");
  const rapidjson::Value &delay = require_member(document, "delay");
  const rapidjson::Value &doppler = require_member(document, "doppler");
  const rapidjson::Value &jsonTimestamp = require_member(document, "timestamp");
  const rapidjson::Value &nRows = require_member(document, "nRows");
  const rapidjson::Value &nCols = require_member(document, "nCols");
  const rapidjson::Value &noisePower = require_member(document, "noisePower");
  const rapidjson::Value &maxPower = require_member(document, "maxPower");
  REQUIRE(data.IsArray());
  REQUIRE(data.Size() == 2);
  REQUIRE(data[0].IsArray());
  REQUIRE(data[0].Size() == 2);
  REQUIRE(delay.IsArray());
  REQUIRE(doppler.IsArray());
  CHECK(jsonTimestamp.GetUint64() == timestamp);
  CHECK(nRows.GetUint() == 2);
  CHECK(nCols.GetUint() == 2);
  CHECK_THAT(noisePower.GetDouble(), Catch::Matchers::WithinAbs(15.0, 0.01));
  CHECK_THAT(maxPower.GetDouble(), Catch::Matchers::WithinAbs(15.0, 0.01));
  CHECK_THAT(delay[0].GetDouble(), Catch::Matchers::WithinAbs(0.0, 0.01));
  CHECK_THAT(delay[1].GetDouble(), Catch::Matchers::WithinAbs(2.0 * delayScaleKm, 0.01));
  CHECK_THAT(doppler[0].GetDouble(), Catch::Matchers::WithinAbs(-12.5, 0.01));
  CHECK_THAT(doppler[1].GetDouble(), Catch::Matchers::WithinAbs(12.5, 0.01));
  CHECK_THAT(data[0][0].GetDouble(), Catch::Matchers::WithinAbs(-15.0, 0.01));
  CHECK_THAT(data[0][1].GetDouble(), Catch::Matchers::WithinAbs(-5.0, 0.01));
  CHECK_THAT(data[1][0].GetDouble(), Catch::Matchers::WithinAbs(5.0, 0.01));
  CHECK_THAT(data[1][1].GetDouble(), Catch::Matchers::WithinAbs(15.0, 0.01));
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
  const rapidjson::Value &data = require_member(document, "data");
  const rapidjson::Value &jsonTimestamp = require_member(document, "timestamp");
  const rapidjson::Value &count = require_member(document, "n");
  const rapidjson::Value &nTentative = require_member(document, "nTentative");
  const rapidjson::Value &nAssociated = require_member(document, "nAssociated");
  const rapidjson::Value &nActive = require_member(document, "nActive");
  const rapidjson::Value &nCoasting = require_member(document, "nCoasting");
  REQUIRE(data.IsArray());
  REQUIRE(data.Size() == 1);
  REQUIRE(data[0].IsObject());
  const rapidjson::Value &trackEntry = data[0];
  const rapidjson::Value &trackId = require_member(trackEntry, "id");
  const rapidjson::Value &state = require_member(trackEntry, "state");
  const rapidjson::Value &delay = require_member(trackEntry, "delay");
  const rapidjson::Value &doppler = require_member(trackEntry, "doppler");
  const rapidjson::Value &acceleration = require_member(trackEntry, "acceleration");
  const rapidjson::Value &trackCount = require_member(trackEntry, "n");
  const rapidjson::Value &associatedDelay = require_member(trackEntry, "associated_delay");
  const rapidjson::Value &associatedDoppler = require_member(trackEntry, "associated_doppler");
  const rapidjson::Value &associatedState = require_member(trackEntry, "associated_state");
  REQUIRE(associatedDelay.IsArray());
  REQUIRE(associatedState.IsArray());
  CHECK(jsonTimestamp.GetUint64() == timestamp);
  CHECK(count.GetUint64() == 1);
  CHECK(nTentative.GetUint64() == 0);
  CHECK(nAssociated.GetUint64() == 1);
  CHECK(nActive.GetUint64() == 0);
  CHECK(nCoasting.GetUint64() == 0);
  CHECK(std::string(trackId.GetString()) == "0000");
  CHECK(std::string(state.GetString()) == "ASSOCIATED");
  CHECK_THAT(delay.GetDouble(), Catch::Matchers::WithinAbs(4.0 * delayScaleKm, 0.01));
  CHECK_THAT(doppler.GetDouble(), Catch::Matchers::WithinAbs(12.0, 0.01));
  CHECK_THAT(acceleration.GetDouble(), Catch::Matchers::WithinAbs(0.5, 0.001));
  CHECK(trackCount.GetUint64() == 2);
  CHECK_THAT(associatedDelay[0].GetDouble(), Catch::Matchers::WithinAbs(3.0 * delayScaleKm, 0.01));
  CHECK_THAT(associatedDelay[1].GetDouble(), Catch::Matchers::WithinAbs(4.0 * delayScaleKm, 0.01));
  CHECK_THAT(associatedDoppler[0].GetDouble(), Catch::Matchers::WithinAbs(9.0, 0.01));
  CHECK_THAT(associatedDoppler[1].GetDouble(), Catch::Matchers::WithinAbs(12.0, 0.01));
  CHECK(std::string(associatedState[0].GetString()) == "TENTATIVE");
  CHECK(std::string(associatedState[1].GetString()) == "ASSOCIATED");
}

TEST_CASE("TimingJsonReportsElapsedAndStageDurations", "[functional][data][timing]")
{
  Timing timing(1000);
  timing.update(61000, {1.25, 2.5}, {"capture_ms", "process_ms"});

  rapidjson::Document document = parse_document(timing.to_json());

  REQUIRE_FALSE(document.HasParseError());
  REQUIRE(document.IsObject());
  const rapidjson::Value &timestamp = require_member(document, "timestamp");
  const rapidjson::Value &nCpi = require_member(document, "nCpi");
  const rapidjson::Value &uptimeSeconds = require_member(document, "uptime_s");
  const rapidjson::Value &uptimeDays = require_member(document, "uptime_days");
  const rapidjson::Value &captureMs = require_member(document, "capture_ms");
  const rapidjson::Value &processMs = require_member(document, "process_ms");
  CHECK(timestamp.GetUint64() == 61000);
  CHECK(nCpi.GetUint64() == 1);
  CHECK_THAT(uptimeSeconds.GetDouble(), Catch::Matchers::WithinAbs(60.0, 0.01));
  CHECK_THAT(uptimeDays.GetDouble(), Catch::Matchers::WithinAbs(60.0 / 86400.0, 0.01));
  CHECK_THAT(captureMs.GetDouble(), Catch::Matchers::WithinAbs(1.25, 0.01));
  CHECK_THAT(processMs.GetDouble(), Catch::Matchers::WithinAbs(2.5, 0.01));
}