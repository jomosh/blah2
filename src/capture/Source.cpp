#include "Source.h"

#include <iostream>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <limits>

Source::Source()
{
}

// constructor
Source::Source(std::string _type, uint32_t _fc, uint32_t _fs, 
    std::string _path, bool *_saveIq)
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
    std::cerr << "Error: Can not open replay file: " << file << std::endl;
    exit(1);
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

  saveIqFile.open(file, std::ios::binary);

  if (!saveIqFile.is_open())
  {
    std::cerr << "Error: Can not open file: " << file << std::endl;
    exit(1);
  }
  std::cout << "Ready to record IQ to file: " << file << std::endl;

  return file;
}

void Source::close_file()
{
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
