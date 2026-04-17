#include "Track.h"
#include "data/meta/Constants.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <cstdlib>
#include <stdexcept>

#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/filewritestream.h"

const uint64_t Track::MAX_INDEX = 65535;
const std::string Track::STATE_ACTIVE = "ACTIVE";
const std::string Track::STATE_TENTATIVE = "TENTATIVE";
const std::string Track::STATE_COASTING = "COASTING";
const std::string Track::STATE_ASSOCIATED = "ASSOCIATED";

// constructor
Track::Track()
{
  iNext = 0;
}

Track::~Track()
{
}

std::string Track::uint2hex(uint64_t number)
{
    std::ostringstream oss;
    oss << std::setw(4) << std::setfill('0') << std::uppercase << std::hex << number;
    return oss.str();
}

void Track::set_state(uint64_t index, std::string _state)
{
  state.at(index).push_back(_state);
}

void Track::set_current(uint64_t index, Detection smoothed)
{
  current.at(index) = smoothed;
  associated.at(index).push_back(smoothed);
}

void Track::set_acceleration(uint64_t index, double _acceleration)
{
  acceleration.at(index) = _acceleration;
}

void Track::set_nInactive(uint64_t index, uint64_t n)
{
  nInactive.at(index) = n;
}

uint64_t Track::get_nState(std::string _state)
{
  uint64_t n = 0;
  for (size_t i = 0; i < id.size(); i++)
  {
    if (get_state(i) == _state)
    {
      n++;
    }
  }
  return n;
}

uint64_t Track::get_n()
{
  return id.size();
}

Detection Track::get_current(uint64_t index)
{
  return current.at(index);
}

double Track::get_acceleration(uint64_t index)
{
  return acceleration.at(index);
}

std::string Track::get_state(uint64_t index)
{
  return state.at(index).at(state.at(index).size()-1);
}

uint64_t Track::get_nInactive(uint64_t index)
{
  return nInactive.at(index);
}

uint64_t Track::add(Detection initial)
{
  id.push_back(uint2hex(iNext));
  std::vector<std::string> _state;
  _state.push_back(STATE_TENTATIVE);
  state.push_back(_state);
  current.push_back(initial);
  acceleration.push_back(0);
  std::vector<Detection> _associated;
  _associated.push_back(initial);
  associated.push_back(_associated);
  nInactive.push_back(0);
  iNext++;
  if (iNext >= MAX_INDEX)
  {
    iNext = 0;
  }
  return id.size()-1;
}

void Track::promote(uint64_t index, uint32_t m, uint32_t n)
{
  if (state.at(index).size() >= n)
  {
    uint32_t _m = 0;
    for (size_t i = state.at(index).size()-n; i < state.at(index).size(); i++) 
    {
      if (state.at(index).at(i) == STATE_ACTIVE || 
        state.at(index).at(i) == STATE_ASSOCIATED)
      {
        _m++;
      }
    }

    // promote track to ACTIVE if passes threshold
    if (_m >= m)
    {
      state.at(index).at(state.at(index).size()-1) = STATE_ACTIVE;
    }
  }
}

void Track::remove(uint64_t index)
{
  // Check if the index is within bounds for each vector
  if (index < id.size()) {
    id.erase(id.begin() + index);
  } else {
    // Throw an exception if the index is out of bounds
    throw std::out_of_range("Index out of bounds for 'id' vector");
  }

  if (index < state.size()) {
    state.erase(state.begin() + index);
  } else {
    throw std::out_of_range("Index out of bounds for 'state' vector");
  }

  if (index < current.size()) {
    current.erase(current.begin() + index);
  } else {
    throw std::out_of_range("Index out of bounds for 'current' vector");
  }

  if (index < acceleration.size()) {
    acceleration.erase(acceleration.begin() + index);
  } else {
    throw std::out_of_range("Index out of bounds for 'acceleration' vector");
  }

  if (index < associated.size()) {
    associated.erase(associated.begin() + index);
  } else {
    throw std::out_of_range("Index out of bounds for 'associated' vector");
  }
}

std::string Track::to_json(uint64_t timestamp)
{
  return to_json_impl(timestamp, false, 0);
}

std::string Track::to_json(uint64_t timestamp, uint32_t fs)
{
  return to_json_impl(timestamp, true, fs);
}

std::string Track::to_json_impl(uint64_t timestamp, bool convertDelayToKm, uint32_t fs)
{
  rapidjson::Document document;
  document.SetObject();
  rapidjson::Document::AllocatorType &allocator = document.GetAllocator();
  const uint64_t trackCount = get_n();
  uint64_t nTentative = 0;
  uint64_t nAssociated = 0;
  uint64_t nActive = 0;
  uint64_t nCoasting = 0;
  const double delayScale = convertDelayToKm ? (Constants::c / static_cast<double>(fs)) / 1000.0 : 1.0;

  // store track data
  rapidjson::Value dataArray(rapidjson::kArrayType);
  dataArray.Reserve(trackCount, allocator);
  for (uint64_t i = 0; i < trackCount; i++)
  {
    const std::string &trackState = state.at(i).back();
    if (trackState == STATE_TENTATIVE)
    {
      nTentative++;
      continue;
    }
    if (trackState == STATE_ASSOCIATED)
    {
      nAssociated++;
    }
    else if (trackState == STATE_ACTIVE)
    {
      nActive++;
    }
    else if (trackState == STATE_COASTING)
    {
      nCoasting++;
    }

    const Detection &currentDetection = current.at(i);
    const std::vector<double> &currentDelay = currentDetection.get_delay_ref();
    const std::vector<double> &currentDoppler = currentDetection.get_doppler_ref();
    const std::vector<Detection> &associatedTrack = associated.at(i);
    const std::vector<std::string> &trackStateHistory = state.at(i);

    rapidjson::Value object1(rapidjson::kObjectType);
    object1.AddMember("id", rapidjson::Value(id.at(i).c_str(), 
      allocator).Move(), allocator);
    object1.AddMember("state", rapidjson::Value(
      trackState.c_str(), 
      allocator).Move(), allocator);
    object1.AddMember("delay", 
      currentDelay.at(0) * delayScale,
      allocator);
    object1.AddMember("doppler", 
      currentDoppler.at(0), 
      allocator);
    object1.AddMember("acceleration", 
      acceleration.at(i), allocator);
    object1.AddMember("n", associatedTrack.size(), 
      allocator);
    rapidjson::Value associatedDelay(rapidjson::kArrayType);
    rapidjson::Value associatedDoppler(rapidjson::kArrayType);
    rapidjson::Value associatedState(rapidjson::kArrayType);
    associatedDelay.Reserve(associatedTrack.size(), allocator);
    associatedDoppler.Reserve(associatedTrack.size(), allocator);
    associatedState.Reserve(associatedTrack.size(), allocator);
    for (size_t j = 0; j < associatedTrack.size(); j++)
    {
      const Detection &associatedDetection = associatedTrack.at(j);
      associatedDelay.PushBack(associatedDetection.get_delay_ref().at(0) * delayScale, 
        allocator);
      associatedDoppler.PushBack(associatedDetection.get_doppler_ref().at(0), 
        allocator);
      associatedState.PushBack(rapidjson::Value(trackStateHistory.at(j).c_str(), 
        allocator).Move(), allocator);
    }
    object1.AddMember("associated_delay", 
      associatedDelay, allocator);
    object1.AddMember("associated_doppler", 
      associatedDoppler, allocator);
    object1.AddMember("associated_state", 
      associatedState, allocator);
    dataArray.PushBack(object1, allocator);
  }

  document.AddMember("timestamp", timestamp, allocator);
  document.AddMember("n", trackCount, allocator);
  document.AddMember("nTentative", nTentative, allocator);
  document.AddMember("nAssociated", nAssociated, allocator);
  document.AddMember("nActive", nActive, allocator);
  document.AddMember("nCoasting", nCoasting, allocator);
  document.AddMember("data", dataArray, allocator);
  
  rapidjson::StringBuffer strbuf;
  rapidjson::Writer<rapidjson::StringBuffer> writer(strbuf);
  writer.SetMaxDecimalPlaces(2);
  document.Accept(writer);

  return strbuf.GetString();
}
