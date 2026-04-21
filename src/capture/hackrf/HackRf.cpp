#include "HackRf.h"

#include <iostream>
#include <complex>
#include <stdexcept>
#include <unordered_set>
#include <sstream>
#include <thread>
#include <chrono>

namespace {

bool has_serial(const hackrf_device_list_t *list, const std::string &target)
{
  if (!list || !list->serial_numbers)
  {
    return false;
  }
  for (int i = 0; i < list->devicecount; ++i)
  {
    const char *serial = list->serial_numbers[i];
    if (serial != nullptr && target == serial)
    {
      return true;
    }
  }
  return false;
}

int open_by_serial_with_retry(const std::string &serial, hackrf_device **device, int maxAttempts, int delayMs)
{
  int status = HACKRF_ERROR_NOT_FOUND;
  for (int attempt = 1; attempt <= maxAttempts; ++attempt)
  {
    status = hackrf_open_by_serial(serial.c_str(), device);
    if (status == HACKRF_SUCCESS)
    {
      return status;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
  }
  return status;
}

} // namespace

// constructor
HackRf::HackRf(std::string _type, uint32_t _fc, uint32_t _fs, 
  std::string _path, bool *_saveIq, std::vector<std::string> _serial,
  std::vector<uint32_t> _gainLna, std::vector<uint32_t> _gainVga, 
  std::vector<bool> _ampEnable)
    : Source(_type, _fc, _fs, _path, _saveIq)
{
  dev[0] = nullptr;
  dev[1] = nullptr;

  serial = _serial;
  ampEnable = _ampEnable;

  if (serial.size() != 2)
  {
    throw std::invalid_argument("HackRF requires exactly 2 serial numbers per node.");
  }
  if (serial[0].empty() || serial[1].empty())
  {
    throw std::invalid_argument("HackRF serial numbers must not be empty.");
  }
  if (serial[0] == serial[1])
  {
    throw std::invalid_argument("HackRF serial numbers must be unique within a node.");
  }
  if (_gainLna.size() != 2 || _gainVga.size() != 2 || _ampEnable.size() != 2)
  {
    throw std::invalid_argument("HackRF gain and amp arrays must each contain 2 values.");
  }

  // validate LNA gain
  std::unordered_set<uint32_t> validLna;
  for (uint32_t gain = 0; gain <= 40; gain += 8) {
    validLna.insert(gain);
  }
  for (uint32_t gain : _gainLna) {
    if (validLna.find(gain) == validLna.end()) {
      throw std::invalid_argument("Invalid LNA gain value");
    }
  }
  gainLna = _gainLna;

  // validate VGA gain
  std::unordered_set<uint32_t> validVga;
  for (uint32_t gain = 0; gain <= 62; gain += 2) {
    validVga.insert(gain);
  }
  for (uint32_t gain : _gainVga) {
    if (validVga.find(gain) == validVga.end()) {
      throw std::invalid_argument("Invalid VGA gain value");
    }
  }
  gainVga = _gainVga;
}

void HackRf::check_status(int status, std::string message)
{
  if (status != HACKRF_SUCCESS)
  {
    std::ostringstream oss;
    oss << "[HackRF] " << message << " (status=" << status << ")";
    throw std::runtime_error(oss.str());
  }
}

void HackRf::start()
{
  // global hackrf config
  int status;
  status = hackrf_init();
  check_status(status, "Failed to initialise HackRF");
  hackrf_device_list_t *list;
  list = hackrf_device_list();
  if (!list || list->devicecount < 2)
  {
    if (list)
    {
      hackrf_device_list_free(list);
    }
    check_status(-1, "Failed to find 2 HackRF devices.");
  }
  if (!has_serial(list, serial[0]) || !has_serial(list, serial[1]))
  {
    std::ostringstream oss;
    oss << "Configured serials not found on host. Expected: ["
        << serial[0] << ", " << serial[1] << "]";
    hackrf_device_list_free(list);
    check_status(-1, oss.str());
  }
  hackrf_device_list_free(list);

  // surveillance config
  status = open_by_serial_with_retry(serial[1], &dev[1], 20, 250);
  check_status(status, "Failed to open surveillance device with serial " + serial[1]);
  status = hackrf_set_freq(dev[1], fc);
  check_status(status, "Failed to set frequency on surveillance device.");
  status = hackrf_set_sample_rate(dev[1], fs);
  check_status(status, "Failed to set sample rate on surveillance device.");
  status = hackrf_set_amp_enable(dev[1], ampEnable[1] ? 1 : 0);
  check_status(status, "Failed to set AMP status on surveillance device.");
  status = hackrf_set_lna_gain(dev[1], gainLna[1]);
  check_status(status, "Failed to set LNA gain on surveillance device.");
  status = hackrf_set_vga_gain(dev[1], gainVga[1]);
  check_status(status, "Failed to set VGA gain on surveillance device.");
  status = hackrf_set_hw_sync_mode(dev[1], 1);
  check_status(status, "Failed to enable hardware synchronising on surveillance device.");
  status = hackrf_set_clkout_enable(dev[1], 1); 
  check_status(status, "Failed to set CLKOUT on surveillance device");


  // reference config
  status = open_by_serial_with_retry(serial[0], &dev[0], 20, 250);
  check_status(status, "Failed to open reference device with serial " + serial[0]);
  status = hackrf_set_freq(dev[0], fc);
  check_status(status, "Failed to set frequency on reference device.");
  status = hackrf_set_sample_rate(dev[0], fs);
  check_status(status, "Failed to set sample rate on reference device.");
  status = hackrf_set_amp_enable(dev[0], ampEnable[0] ? 1 : 0);
  check_status(status, "Failed to set AMP status on reference device.");
  status = hackrf_set_lna_gain(dev[0], gainLna[0]);
  check_status(status, "Failed to set LNA gain on reference device.");
  status = hackrf_set_vga_gain(dev[0], gainVga[0]);
  check_status(status, "Failed to set VGA gain on reference device.");

  std::cout << "HackRF configured. Reference=" << serial[0]
            << ", Surveillance=" << serial[1] << std::endl;
}

void HackRf::stop()
{
  if (dev[0] != nullptr)
  {
    hackrf_stop_rx(dev[0]);
    hackrf_close(dev[0]);
    dev[0] = nullptr;
  }
  if (dev[1] != nullptr)
  {
    hackrf_stop_rx(dev[1]);
    hackrf_close(dev[1]);
    dev[1] = nullptr;
  }
  hackrf_exit();
}

void HackRf::process(IqData *buffer1, IqData *buffer2)
{
    int status;
    status = hackrf_start_rx(dev[1], rx_callback, buffer2);
    check_status(status, "Failed to start RX streaming.");
    status = hackrf_start_rx(dev[0], rx_callback, buffer1);
    check_status(status, "Failed to start RX streaming.");
}

int HackRf::rx_callback(hackrf_transfer* transfer)
{
  IqData* buffer_blah2 = (IqData*)transfer->rx_ctx;
  int8_t* buffer_hackrf = (int8_t*) transfer->buffer;

  buffer_blah2->lock();

  for (int i = 0; i < transfer->buffer_length; i=i+2) 
  {
    double iqi = static_cast<double>(buffer_hackrf[i]);
    double iqq = static_cast<double>(buffer_hackrf[i+1]);
    buffer_blah2->push_back({iqi, iqq});
  }

  buffer_blah2->unlock();

  return 0;
}

void HackRf::replay(IqData *buffer1, IqData *buffer2, std::string _file, bool _loop)
{
  return;
}

