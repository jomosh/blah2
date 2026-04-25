#include "HackRf.h"

#include <iostream>
#include <stdexcept>
#include <unordered_set>

// constructor
HackRf::HackRf(std::string _type, uint32_t _fc, uint32_t _fs, 
  std::string _path, bool *_saveIq, std::vector<std::string> _serial,
  std::vector<uint32_t> _gainLna, std::vector<uint32_t> _gainVga, 
  std::vector<bool> _ampEnable)
    : Source(_type, _fc, _fs, _path, _saveIq)
{
  serial = _serial;
  ampEnable = _ampEnable;

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
      throw std::invalid_argument("Invalid LNA gain value");
    }
  }
  gainVga = _gainVga;
}

void HackRf::check_status(uint8_t status, std::string message)
{
  if (status != HACKRF_SUCCESS)
  {
    throw std::runtime_error("[HackRF] " + message);
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
    check_status(-1, "Failed to find 2 HackRF devices.");
  }

  // surveillance config
  status = hackrf_open_by_serial(serial[1].c_str(), &dev[1]);
  check_status(status, "Failed to open device.");
  status = hackrf_set_freq(dev[1], fc);
  check_status(status, "Failed to set frequency.");
  status = hackrf_set_sample_rate(dev[1], fs);
  check_status(status, "Failed to set sample rate.");
  status = hackrf_set_amp_enable(dev[1], ampEnable[1] ? 1 : 0);
  check_status(status, "Failed to set AMP status.");
  status = hackrf_set_lna_gain(dev[1], gainLna[1]);
  check_status(status, "Failed to set LNA gain.");
  status = hackrf_set_vga_gain(dev[1], gainVga[1]);
  check_status(status, "Failed to set VGA gain.");
  status = hackrf_set_hw_sync_mode(dev[1], 1);
  check_status(status, "Failed to enable hardware synchronising.");
  status = hackrf_set_clkout_enable(dev[1], 1); 
  check_status(status, "Failed to set CLKOUT on survillance device");


  // reference config
  status = hackrf_open_by_serial(serial[0].c_str(), &dev[0]);
  check_status(status, "Failed to open device.");
  status = hackrf_set_freq(dev[0], fc);
  check_status(status, "Failed to set frequency.");
  status = hackrf_set_sample_rate(dev[0], fs);
  check_status(status, "Failed to set sample rate.");
  status = hackrf_set_amp_enable(dev[0], ampEnable[0] ? 1 : 0);
  check_status(status, "Failed to set AMP status.");
  status = hackrf_set_lna_gain(dev[0], gainLna[0]);
  check_status(status, "Failed to set LNA gain.");
  status = hackrf_set_vga_gain(dev[0], gainVga[0]);
  check_status(status, "Failed to set VGA gain.");
}

void HackRf::stop()
{
  hackrf_stop_rx(dev[0]);
  hackrf_stop_rx(dev[1]);
  hackrf_close(dev[0]);
  hackrf_close(dev[1]);
  hackrf_exit();
}

void HackRf::process(IqData *buffer1, IqData *buffer2)
{
    callbackContexts[0].device = this;
    callbackContexts[0].buffer = buffer1;
    callbackContexts[0].channelIndex = 0;
    callbackContexts[1].device = this;
    callbackContexts[1].buffer = buffer2;
    callbackContexts[1].channelIndex = 1;

    int status;
    status = hackrf_start_rx(dev[1], rx_callback, &callbackContexts[1]);
    check_status(status, "Failed to start RX streaming.");
    status = hackrf_start_rx(dev[0], rx_callback, &callbackContexts[0]);
    check_status(status, "Failed to start RX streaming.");
}

int HackRf::rx_callback(hackrf_transfer* transfer)
{
  CallbackContext *context = static_cast<CallbackContext *>(transfer->rx_ctx);
  IqData *buffer_blah2 = context->buffer;
  int8_t *buffer_hackrf = reinterpret_cast<int8_t *>(transfer->buffer);

  buffer_blah2->lock();

  for (int i = 0; i < transfer->buffer_length; i=i+2) 
  {
    double iqi = static_cast<double>(buffer_hackrf[i]);
    double iqq = static_cast<double>(buffer_hackrf[i+1]);
    buffer_blah2->push_back({iqi, iqq});
  }

  buffer_blah2->unlock_and_notify();

  context->device->append_save_samples(context->channelIndex, buffer_hackrf,
    static_cast<size_t>(transfer->buffer_length / 2));

  return 0;
}

void HackRf::append_save_samples(size_t channelIndex, const int8_t *samples,
  size_t nComplexSamples)
{
  append_blah2_paired_iq_samples(channelIndex, samples, nComplexSamples);
}

void HackRf::replay(IqData *buffer1, IqData *buffer2, std::string _file, bool _loop)
{
  replay_blah2_iq_file(buffer1, buffer2, _file, _loop);
}

