#include "Source.h"

#include <iostream>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <limits>
#include <stdexcept>

Source::Source()
{
}

// constructor
Source::Source(std::string _type, uint32_t _fc, uint32_t _fs,
  std::string _path, std::atomic<bool> *_saveIq)
{
  type = _type;
  fc = _fc;
  fs = _fs;
  path = _path;
  saveIq = _saveIq;
}

int16_t Source::clamp_blah2_iq_component(double value)
{
  if (!std::isfinite(value))
  {
    return 0;
  }
  if (value > static_cast<double>(std::numeric_limits<int16_t>::max()))
  {
    return std::numeric_limits<int16_t>::max();
  }
  if (value < static_cast<double>(std::numeric_limits<int16_t>::min()))
  {
    return std::numeric_limits<int16_t>::min();
  }
  return static_cast<int16_t>(std::lround(value));
}

void Source::replay_blah2_iq_file(IqData *buffer1, IqData *buffer2,
  const std::string &file, bool loop)
{
  std::ifstream replay(file, std::ios::binary);
  if (!replay.is_open())
  {
    throw std::runtime_error("Can not open replay file: " + file);
  }

  int16_t samples[4] = {0, 0, 0, 0};
  while (true)
  {
    if (!replay.read(reinterpret_cast<char *>(samples), sizeof(samples)))
    {
      if (!loop)
      {
        break;
      }

      replay.clear();
      replay.seekg(0, std::ios::beg);
      if (!replay.read(reinterpret_cast<char *>(samples), sizeof(samples)))
      {
        break;
      }
    }

    buffer1->wait_for_max_length(buffer1->get_n() - 1);
    buffer2->wait_for_max_length(buffer2->get_n() - 1);

    buffer1->lock();
    buffer2->lock();
    buffer1->push_back({static_cast<double>(samples[0]), static_cast<double>(samples[1])});
    buffer2->push_back({static_cast<double>(samples[2]), static_cast<double>(samples[3])});
    buffer1->unlock_and_notify();
    buffer2->unlock_and_notify();
  }
}

void Source::append_blah2_paired_iq_samples(size_t channelIndex, const int8_t *samples,
  size_t nComplexSamples)
{
  if (channelIndex > 1 || samples == nullptr)
  {
    return;
  }

  std::lock_guard<std::mutex> lock(pendingSaveMutex);
  if (saveIq == nullptr || !saveIq->load())
  {
    clear_blah2_paired_iq_samples_locked();
    return;
  }

  std::deque<std::complex<float>> &pending = pendingSaveSamples[channelIndex];
  for (size_t i = 0; i < nComplexSamples; i++)
  {
    pending.push_back({static_cast<float>(samples[2 * i]),
      static_cast<float>(samples[2 * i + 1])});
  }

  flush_blah2_paired_iq_samples_locked();

  // Keep unpaired backlog bounded if one device callback stream stalls.
  for (size_t i = 0; i < 2; i++)
  {
    std::deque<std::complex<float>> &queuedSamples = pendingSaveSamples[i];
    while (queuedSamples.size() > maxPendingBlah2PairedIqSamples)
    {
      queuedSamples.pop_front();
    }
  }
}

void Source::flush_blah2_paired_iq_samples_locked()
{
  {
    std::lock_guard<std::mutex> lock(saveIqFileMutex);
    if (!saveIqFile.is_open())
    {
      return;
    }
  }

  const size_t nPaired = std::min(pendingSaveSamples[0].size(), pendingSaveSamples[1].size());
  if (nPaired == 0)
  {
    return;
  }

  std::vector<std::complex<float>> reference(nPaired);
  std::vector<std::complex<float>> surveillance(nPaired);
  for (size_t i = 0; i < nPaired; i++)
  {
    reference[i] = pendingSaveSamples[0].front();
    pendingSaveSamples[0].pop_front();
    surveillance[i] = pendingSaveSamples[1].front();
    pendingSaveSamples[1].pop_front();
  }

  write_blah2_iq_samples(reference.data(), surveillance.data(), nPaired);
}

void Source::clear_blah2_paired_iq_samples_locked()
{
  pendingSaveSamples[0].clear();
  pendingSaveSamples[1].clear();
}

std::string Source::open_file()
{
  // get string of timestamp in YYYYmmdd-HHMMSS
  auto currentTime = std::chrono::system_clock::to_time_t(
    std::chrono::system_clock::now());
  std::tm* timeInfo = std::localtime(&currentTime);
  std::ostringstream oss;
  oss << std::put_time(timeInfo, "%Y%m%d-%H%M%S");
  std::string timestamp = oss.str();

  // create file path
  std::string typeLower = type;
  std::transform(typeLower.begin(), typeLower.end(),
    typeLower.begin(), ::tolower);
  std::string file = path + timestamp + "." + typeLower + ".iq";

  {
    std::lock_guard<std::mutex> lock(saveIqFileMutex);
    saveIqFile.open(file, std::ios::binary);

    if (!saveIqFile.is_open())
    {
      throw std::runtime_error("Can not open file: " + file);
    }
  }

  {
    std::lock_guard<std::mutex> lock(pendingSaveMutex);
    flush_blah2_paired_iq_samples_locked();
  }

  std::cout << "Ready to record IQ to file: " << file << std::endl;

  return file;
}

void Source::close_file()
{
  std::scoped_lock<std::mutex, std::mutex> lock(pendingSaveMutex, saveIqFileMutex);
  clear_blah2_paired_iq_samples_locked();
  if (saveIqFile.is_open())
  {
    saveIqFile.close();
  }

  // switch member with blank file stream
  std::ofstream blankFile;
  std::swap(saveIqFile, blankFile);
}

void Source::kill()
{
  if (type == "RspDuo")
  {
    stop();
  } else if (type == "HackRF")
  {
    stop();
  }
  exit(0);
}
