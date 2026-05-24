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

const rapidjson::Value &require_array(const rapidjson::Value &value,
  const char *context,
  rapidjson::SizeType minimumSize = 0)
{
  INFO("Expected JSON array: " << context);
  REQUIRE(value.IsArray());
  INFO("JSON array shorter than expected: " << context);
  REQUIRE(value.Size() >= minimumSize);
  return value;
}

const rapidjson::Value &require_object(const rapidjson::Value &value,
  const char *context)
{
  INFO("Expected JSON object: " << context);
  REQUIRE(value.IsObject());
  return value;
}

const rapidjson::Value &require_index(const rapidjson::Value &array,
  rapidjson::SizeType index,
  const char *context)
{
  INFO("Missing JSON array element: " << context << "[" << index << "]");
  REQUIRE(array.IsArray());
  REQUIRE(index < array.Size());
  return array[index];
}

double require_double(const rapidjson::Value &value, const char *context)
{
  INFO("Expected JSON number: " << context);
  REQUIRE(value.IsNumber());
  return value.GetDouble();
}

uint64_t require_uint64(const rapidjson::Value &value, const char *context)
{
  INFO("Expected JSON uint64: " << context);
  REQUIRE(value.IsUint64());
  return value.GetUint64();
}

unsigned require_uint(const rapidjson::Value &value, const char *context)
{
  INFO("Expected JSON uint: " << context);
  REQUIRE(value.IsUint());
  return value.GetUint();
}

std::string require_string(const rapidjson::Value &value, const char *context)
{
  INFO("Expected JSON string: " << context);
  REQUIRE(value.IsString());
  return value.GetString();
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
  const rapidjson::Value &delay = require_array(require_member(document, "delay"), "delay", 2);
  const rapidjson::Value &doppler = require_array(require_member(document, "doppler"), "doppler", 2);
  const rapidjson::Value &snr = require_array(require_member(document, "snr"), "snr", 2);
  const rapidjson::Value &jsonTimestamp = require_member(document, "timestamp");
  CHECK(require_uint64(jsonTimestamp, "timestamp") == timestamp);
  CHECK_THAT(require_double(require_index(delay, 0, "delay"), "delay[0]"), Catch::Matchers::WithinAbs(2.0 * delayScaleKm, 0.01));
  CHECK_THAT(require_double(require_index(delay, 1, "delay"), "delay[1]"), Catch::Matchers::WithinAbs(4.0 * delayScaleKm, 0.01));
  CHECK_THAT(require_double(require_index(doppler, 0, "doppler"), "doppler[0]"), Catch::Matchers::WithinAbs(5.5, 0.01));
  CHECK_THAT(require_double(require_index(doppler, 1, "doppler"), "doppler[1]"), Catch::Matchers::WithinAbs(-3.25, 0.01));
  CHECK_THAT(require_double(require_index(snr, 0, "snr"), "snr[0]"), Catch::Matchers::WithinAbs(12.0, 0.01));
  CHECK_THAT(require_double(require_index(snr, 1, "snr"), "snr[1]"), Catch::Matchers::WithinAbs(9.5, 0.01));
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
  const rapidjson::Value &data = require_array(require_member(document, "data"), "data", 2);
  const rapidjson::Value &delay = require_array(require_member(document, "delay"), "delay", 2);
  const rapidjson::Value &doppler = require_array(require_member(document, "doppler"), "doppler", 2);
  const rapidjson::Value &jsonTimestamp = require_member(document, "timestamp");
  const rapidjson::Value &nRows = require_member(document, "nRows");
  const rapidjson::Value &nCols = require_member(document, "nCols");
  const rapidjson::Value &noisePower = require_member(document, "noisePower");
  const rapidjson::Value &maxPower = require_member(document, "maxPower");
  const rapidjson::Value &firstRow = require_array(require_index(data, 0, "data"), "data[0]", 2);
  const rapidjson::Value &secondRow = require_array(require_index(data, 1, "data"), "data[1]", 2);
  CHECK(require_uint64(jsonTimestamp, "timestamp") == timestamp);
  CHECK(require_uint(nRows, "nRows") == 2);
  CHECK(require_uint(nCols, "nCols") == 2);
  CHECK_THAT(require_double(noisePower, "noisePower"), Catch::Matchers::WithinAbs(15.0, 0.01));
  CHECK_THAT(require_double(maxPower, "maxPower"), Catch::Matchers::WithinAbs(15.0, 0.01));
  CHECK_THAT(require_double(require_index(delay, 0, "delay"), "delay[0]"), Catch::Matchers::WithinAbs(0.0, 0.01));
  CHECK_THAT(require_double(require_index(delay, 1, "delay"), "delay[1]"), Catch::Matchers::WithinAbs(2.0 * delayScaleKm, 0.01));
  CHECK_THAT(require_double(require_index(doppler, 0, "doppler"), "doppler[0]"), Catch::Matchers::WithinAbs(-12.5, 0.01));
  CHECK_THAT(require_double(require_index(doppler, 1, "doppler"), "doppler[1]"), Catch::Matchers::WithinAbs(12.5, 0.01));
  CHECK_THAT(require_double(require_index(firstRow, 0, "data[0]"), "data[0][0]"), Catch::Matchers::WithinAbs(-15.0, 0.01));
  CHECK_THAT(require_double(require_index(firstRow, 1, "data[0]"), "data[0][1]"), Catch::Matchers::WithinAbs(-5.0, 0.01));
  CHECK_THAT(require_double(require_index(secondRow, 0, "data[1]"), "data[1][0]"), Catch::Matchers::WithinAbs(5.0, 0.01));
  CHECK_THAT(require_double(require_index(secondRow, 1, "data[1]"), "data[1][1]"), Catch::Matchers::WithinAbs(15.0, 0.01));
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
  const rapidjson::Value &data = require_array(require_member(document, "data"), "data", 1);
  const rapidjson::Value &jsonTimestamp = require_member(document, "timestamp");
  const rapidjson::Value &count = require_member(document, "n");
  const rapidjson::Value &nTentative = require_member(document, "nTentative");
  const rapidjson::Value &nAssociated = require_member(document, "nAssociated");
  const rapidjson::Value &nActive = require_member(document, "nActive");
  const rapidjson::Value &nCoasting = require_member(document, "nCoasting");
  const rapidjson::Value &trackEntry = require_object(require_index(data, 0, "data"), "data[0]");
  const rapidjson::Value &trackId = require_member(trackEntry, "id");
  const rapidjson::Value &state = require_member(trackEntry, "state");
  const rapidjson::Value &delay = require_member(trackEntry, "delay");
  const rapidjson::Value &doppler = require_member(trackEntry, "doppler");
  const rapidjson::Value &acceleration = require_member(trackEntry, "acceleration");
  const rapidjson::Value &trackCount = require_member(trackEntry, "n");
  const rapidjson::Value &associatedDelay = require_array(require_member(trackEntry, "associated_delay"), "associated_delay", 2);
  const rapidjson::Value &associatedDoppler = require_array(require_member(trackEntry, "associated_doppler"), "associated_doppler", 2);
  const rapidjson::Value &associatedState = require_array(require_member(trackEntry, "associated_state"), "associated_state", 2);
  CHECK(require_uint64(jsonTimestamp, "timestamp") == timestamp);
  CHECK(require_uint64(count, "n") == 1);
  CHECK(require_uint64(nTentative, "nTentative") == 0);
  CHECK(require_uint64(nAssociated, "nAssociated") == 1);
  CHECK(require_uint64(nActive, "nActive") == 0);
  CHECK(require_uint64(nCoasting, "nCoasting") == 0);
  CHECK(require_string(trackId, "id") == "0000");
  CHECK(require_string(state, "state") == "ASSOCIATED");
  CHECK_THAT(require_double(delay, "delay"), Catch::Matchers::WithinAbs(4.0 * delayScaleKm, 0.01));
  CHECK_THAT(require_double(doppler, "doppler"), Catch::Matchers::WithinAbs(12.0, 0.01));
  CHECK_THAT(require_double(acceleration, "acceleration"), Catch::Matchers::WithinAbs(0.5, 0.001));
  CHECK(require_uint64(trackCount, "track.n") == 2);
  CHECK_THAT(require_double(require_index(associatedDelay, 0, "associated_delay"), "associated_delay[0]"), Catch::Matchers::WithinAbs(3.0 * delayScaleKm, 0.01));
  CHECK_THAT(require_double(require_index(associatedDelay, 1, "associated_delay"), "associated_delay[1]"), Catch::Matchers::WithinAbs(4.0 * delayScaleKm, 0.01));
  CHECK_THAT(require_double(require_index(associatedDoppler, 0, "associated_doppler"), "associated_doppler[0]"), Catch::Matchers::WithinAbs(9.0, 0.01));
  CHECK_THAT(require_double(require_index(associatedDoppler, 1, "associated_doppler"), "associated_doppler[1]"), Catch::Matchers::WithinAbs(12.0, 0.01));
  CHECK(require_string(require_index(associatedState, 0, "associated_state"), "associated_state[0]") == "TENTATIVE");
  CHECK(require_string(require_index(associatedState, 1, "associated_state"), "associated_state[1]") == "ASSOCIATED");
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
  CHECK(require_uint64(timestamp, "timestamp") == 61000);
  CHECK(require_uint64(nCpi, "nCpi") == 1);
  CHECK_THAT(require_double(uptimeSeconds, "uptime_s"), Catch::Matchers::WithinAbs(60.0, 0.01));
  CHECK_THAT(require_double(uptimeDays, "uptime_days"), Catch::Matchers::WithinAbs(60.0 / 86400.0, 0.01));
  CHECK_THAT(require_double(captureMs, "capture_ms"), Catch::Matchers::WithinAbs(1.25, 0.01));
  CHECK_THAT(require_double(processMs, "process_ms"), Catch::Matchers::WithinAbs(2.5, 0.01));
}
