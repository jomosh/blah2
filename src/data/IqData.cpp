#include "IqData.h"
#include <iostream>
#include <cstdlib>
#include <stdexcept>

#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/filewritestream.h"

// constructor
IqData::IqData(uint32_t _n)
{
  n = _n;
  data.resize(n, {0.0, 0.0});
  head = 0;
  length = 0;
}

uint32_t IqData::get_n()
{
  return n;
}

uint32_t IqData::get_length()
{
  return length;
}

void IqData::lock()
{
  mutex_lock.lock();
}

void IqData::unlock()
{
  mutex_lock.unlock();
}

void IqData::unlock_and_notify()
{
  mutex_lock.unlock();
  data_ready.notify_all();
}

void IqData::wait_for_min_length(uint32_t minLength)
{
  if (minLength > n)
  {
    throw std::invalid_argument("IqData::wait_for_min_length minLength exceeds buffer capacity");
  }

  std::unique_lock<std::mutex> lock(mutex_lock);
  data_ready.wait(lock, [&] { return length >= minLength; });
}

void IqData::wait_for_max_length(uint32_t maxLength)
{
  if (maxLength > n)
  {
    throw std::invalid_argument("IqData::wait_for_max_length maxLength exceeds buffer capacity");
  }

  std::unique_lock<std::mutex> lock(mutex_lock);
  data_ready.wait(lock, [&] { return length <= maxLength; });
}

std::deque<std::complex<double>> IqData::get_data()
{
  std::deque<std::complex<double>> out;
  for (uint32_t i = 0; i < length; i++)
  {
    out.push_back(data[(head + i) % n]);
  }
  return out;
}

std::complex<double> IqData::at(uint32_t index) const
{
  if (index >= length)
  {
    throw std::out_of_range("IqData::at index out of range");
  }
  return data[(head + index) % n];
}

std::complex<double> IqData::at_unchecked(uint32_t index) const
{
  return data[(head + index) % n];
}

void IqData::push_back(std::complex<double> sample)
{
  if (length < n)
  {
    data[(head + length) % n] = sample;
    length++;
  }
  else
  {
    data[head] = sample;
    head = (head + 1) % n;
  }
}

std::complex<double> IqData::pop_front()
{
  if (length == 0) {
    throw std::runtime_error("Attempting to pop from an empty IqData ring buffer");
  }
  std::complex<double> sample = data[head];
  head = (head + 1) % n;
  length--;
  return sample;
}
void IqData::print()
{
  std::cout << length << std::endl;
  for (uint32_t i = 0; i < length; i++)
  {
    std::cout << data[(head + i) % n] << std::endl;
  }
}

void IqData::clear()
{
  head = 0;
  length = 0;
}

void IqData::update_spectrum(const std::vector<std::complex<double>> &_spectrum)
{
  spectrum = _spectrum;
}

void IqData::update_frequency(const std::vector<double> &_frequency)
{
  frequency = _frequency;
}

std::string IqData::to_json(uint64_t timestamp)
{
  rapidjson::Document document;
  document.SetObject();
  rapidjson::Document::AllocatorType &allocator = document.GetAllocator();

  // store frequency array
  rapidjson::Value arrayFrequency(rapidjson::kArrayType);
  for (size_t i = 0; i < frequency.size(); i++)
  {
    arrayFrequency.PushBack(frequency[i], allocator);
  }

  // store spectrum array
  rapidjson::Value arraySpectrum(rapidjson::kArrayType);
  for (size_t i = 0; i < spectrum.size(); i++)
  {
    arraySpectrum.PushBack(10 * std::log10(std::abs(spectrum[i])), allocator);
  }

  document.AddMember("timestamp", timestamp, allocator);
  document.AddMember("min", min, allocator);
  document.AddMember("max", max, allocator);
  document.AddMember("mean", mean, allocator);
  document.AddMember("frequency", arrayFrequency, allocator);
  document.AddMember("spectrum", arraySpectrum, allocator);

  rapidjson::StringBuffer strbuf;
  rapidjson::Writer<rapidjson::StringBuffer> writer(strbuf);
  writer.SetMaxDecimalPlaces(2);
  document.Accept(writer);

  return strbuf.GetString();
}