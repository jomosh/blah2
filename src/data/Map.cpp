#include "Map.h"
#include "data/meta/Constants.h"
#include <iostream>
#include <cstdlib>
#include <chrono>

#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/filewritestream.h"

// constructor
template <class T>
Map<T>::Map(uint32_t _nRows, uint32_t _nCols)
{
  nRows = _nRows;
  nCols = _nCols;
  std::vector<std::vector<T>> tmp(nRows, std::vector<T>(nCols, {1}));
  data = tmp;
  dataDb = std::vector<std::vector<double>>(nRows, std::vector<double>(nCols, 0.0));
}

template <class T>
void Map<T>::set_row(uint32_t i, std::vector<T> row)
{
  //data[i].swap(row);
  for (uint32_t j = 0; j < nCols; j++)
  {
    data[i][j] = row[j];
  }
}

template <class T>
void Map<T>::set_col(uint32_t i, std::vector<T> col)
{
  for (uint32_t j = 0; j < nRows; j++)
  {
    data[j][i] = col[j];
  }
}

template <class T>
uint32_t Map<T>::get_nRows()
{
  return nRows;
}

template <class T>
uint32_t Map<T>::get_nCols()
{
  return nCols;
}

template <class T>
std::vector<T> Map<T>::get_row(uint32_t row)
{
  return data[row];
}

template <class T>
std::vector<T> Map<T>::get_col(uint32_t col)
{
  std::vector<T> colData;

  for (uint32_t i = 0; i < nRows; i++)
  {
    colData.push_back(data[i][col]);
  }

  return colData;
}

template <class T>
Map<double> *Map<T>::get_map_db()
{
  Map<double> *map = new Map<double>(nRows, nCols);

  for (uint32_t i = 0; i < nRows; i++)
  {
    for (uint32_t j = 0; j < nCols; j++)
    {
      map->data[i][j] = (double)10 * std::log10(std::abs(data[i][j]));
    }
  }

  return map;
}

template <class T>
void Map<T>::print()
{
  for (uint32_t i = 0; i < nRows; i++)
  {
    for (uint32_t j = 0; j < nCols; j++)
    {
      std::cout << data[i][j];
      std::cout << " ";
    }
    std::cout << std::endl;
  }
}

template <class T>
uint32_t Map<T>::doppler_hz_to_bin(double dopplerHz)
{
  for (size_t i = 0; i < doppler.size(); i++)
  {
    if (dopplerHz == doppler[i])
    {
      return (int) i;
    }
  }
  return 0;
}

template <class T>
std::string Map<T>::to_json(uint64_t timestamp)
{
  return to_json(timestamp, 1, false);
}

template <class T>
std::string Map<T>::to_json(uint64_t timestamp, uint32_t fs, bool delayInKm)
{
  rapidjson::Document document;
  document.SetObject();
  rapidjson::Document::AllocatorType &allocator = document.GetAllocator();
  const double delayScaleKm = (fs > 0) ? (Constants::c / (double)fs / 1000.0) : 0.0;

  // store data array
  rapidjson::Value array(rapidjson::kArrayType);
  for (size_t i = 0; i < data.size(); i++)
  {
    rapidjson::Value subarray(rapidjson::kArrayType);
    for (size_t j = 0; j < data[i].size(); j++)
    {
      subarray.PushBack(dataDb[i][j], document.GetAllocator());
    }
    array.PushBack(subarray, document.GetAllocator());
  }

  // store delay array
  rapidjson::Value arrayDelay(rapidjson::kArrayType);
  for (size_t i = 0; i < delay.size(); i++)
  {
    if (delayInKm)
    {
      arrayDelay.PushBack(delay[i] * delayScaleKm, allocator);
    }
    else
    {
      arrayDelay.PushBack(delay[i], allocator);
    }
  }

  // store Doppler array
  rapidjson::Value arrayDoppler(rapidjson::kArrayType);
  for (uint32_t i = 0; i < get_nRows(); i++)
  {
    arrayDoppler.PushBack(doppler[i], allocator);
  }

  document.AddMember("timestamp", timestamp, allocator);
  document.AddMember("nRows", nRows, allocator);
  document.AddMember("nCols", nCols, allocator);
  document.AddMember("noisePower", noisePower, allocator);
  document.AddMember("maxPower", maxPower, allocator);
  document.AddMember("delay", arrayDelay, allocator);
  document.AddMember("doppler", arrayDoppler, allocator);
  document.AddMember(rapidjson::Value("data", document.GetAllocator()).Move(), array, document.GetAllocator());

  thread_local static rapidjson::StringBuffer strbuf; strbuf.Clear();
  rapidjson::Writer<rapidjson::StringBuffer> writer(strbuf);
  writer.SetMaxDecimalPlaces(2);
  document.Accept(writer);

  return strbuf.GetString();
}

template <class T>
std::string Map<T>::delay_bin_to_km(std::string json, uint32_t fs)
{
  rapidjson::Document document;
  document.SetObject();
  rapidjson::Document::AllocatorType &allocator = document.GetAllocator();
  document.Parse(json.c_str());

  document["delay"].Clear();
  for (size_t i = 0; i < delay.size(); i++)
  {
    document["delay"].PushBack(1.0*delay[i]*(Constants::c/(double)fs)/1000, allocator);
  }

  thread_local static rapidjson::StringBuffer strbuf; strbuf.Clear();
  rapidjson::Writer<rapidjson::StringBuffer> writer(strbuf);
  writer.SetMaxDecimalPlaces(2);
  document.Accept(writer);

  return strbuf.GetString();
}

template <class T>
void Map<T>::set_metrics()
{
  // get map noise level and cache dB values for output
  double value;
  double noisePower = 0;
  double maxPower = 0;
  for (uint32_t i = 0; i < nRows; i++)
  {
    for (uint32_t j = 0; j < nCols; j++)
    {
      value = 10 * std::log10(std::abs(data[i][j]));
      dataDb[i][j] = value;
      noisePower = noisePower + value;
      maxPower = (maxPower < value) ? value : maxPower;
    }
  }
  noisePower = noisePower / (nRows * nCols);

  for (uint32_t i = 0; i < nRows; i++)
  {
    for (uint32_t j = 0; j < nCols; j++)
    {
      dataDb[i][j] -= noisePower;
    }
  }

  this->noisePower = noisePower;
  this->maxPower = maxPower - noisePower;
}

template <class T>
bool Map<T>::save(std::string _json, std::string filename)
{
  using namespace rapidjson;

  rapidjson::Document document;

  // create file if it doesn't exist
  if (FILE *fp = fopen(filename.c_str(), "r"); !fp)
  {
    if (fp = fopen(filename.c_str(), "w"); !fp)
      return false;
    fputs("[]", fp);
    fclose(fp);
  }

  // add the document to the file
  if (FILE *fp = fopen(filename.c_str(), "rb+"); fp)
  {
    // check if first is [
    std::fseek(fp, 0, SEEK_SET);
    if (getc(fp) != '[')
    {
      std::fclose(fp);
      return false;
    }

    // is array empty?
    bool isEmpty = false;
    if (getc(fp) == ']')
      isEmpty = true;

    // check if last is ]
    std::fseek(fp, -1, SEEK_END);
    if (getc(fp) != ']')
    {
      std::fclose(fp);
      return false;
    }

    // replace ] by ,
    fseek(fp, -1, SEEK_END);
    if (!isEmpty)
      fputc(',', fp);

    // add json element
    fwrite(_json.c_str(), sizeof(char), _json.length(), fp);

    // close the array
    std::fputc(']', fp);
    fclose(fp);
    return true;
  }
  return false;
}

// allowed types
template class Map<std::complex<double>>;
template class Map<double>;
