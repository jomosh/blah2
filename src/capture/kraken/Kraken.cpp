#include "Kraken.h"

#include <iostream>
#include <complex>
#include <thread>
#include <algorithm>

// constructor
Kraken::Kraken(std::string _type, uint32_t _fc, uint32_t _fs, 
    std::string _path, std::atomic<bool> *_saveIq, std::vector<double> _gain)
    : Source(_type, _fc, _fs, _path, _saveIq)
{
    // convert gain to tenths of dB
    for (size_t i = 0; i < _gain.size(); i++)
    {
        gain.push_back(static_cast<int>(_gain[i]*10));
        channelIndex.push_back(i);
    }
    std::vector<rtlsdr_dev_t*> devs(channelIndex.size());

    // store all valid gains
    std::vector<int> validGains;
    int nGains, status;
    status = rtlsdr_open(&devs[0], 0);
    check_status(status, "Failed to open device for available gains.");
    nGains = rtlsdr_get_tuner_gains(devs[0], nullptr);
    check_status(nGains, "Failed to get number of gains.");
    std::unique_ptr<int[]> _validGains(new int[nGains]);
    status = rtlsdr_get_tuner_gains(devs[0], _validGains.get());
    check_status(status, "Failed to get number of gains.");
    validGains.assign(_validGains.get(), _validGains.get() + nGains);
    status = rtlsdr_close(devs[0]);
    check_status(status, "Failed to close device for available gains.");

    // update gains to next value if invalid
    for (size_t i = 0; i < _gain.size(); i++)
    {
        int adjustedGain = static_cast<int>(_gain[i] * 10);
        auto it = std::lower_bound(validGains.begin(), 
            validGains.end(), adjustedGain);
        if (it != validGains.end()) {
            gain.push_back(*it);
        } else {
            gain.push_back(validGains.back());
        }
        std::cout << "[Kraken] Gain update on channel " << i << " from " << 
            adjustedGain << " to " << gain[i] << "." << std::endl;
    }
}

void Kraken::start()
{
    int status;
    for (size_t i = 0; i < channelIndex.size(); i++) 
    {
        std::cout << "[Kraken] Setting up channel " << i << "." << std::endl;

        status = rtlsdr_open(&devs[i], i);
        check_status(status, "Failed to open device.");

        status = rtlsdr_set_center_freq(devs[i], fc);
        check_status(status, "Failed to set center frequency.");
        status = rtlsdr_set_sample_rate(devs[i], fs);
        check_status(status, "Failed to set sample rate.");
        status = rtlsdr_set_dithering(devs[i], 0); // disable dither
        check_status(status, "Failed to disable dithering.");
        status = rtlsdr_set_tuner_gain_mode(devs[i], 1); // disable AGC
        check_status(status, "Failed to disable AGC.");
        status = rtlsdr_set_tuner_gain(devs[i], gain[i]);
        check_status(status, "Failed to set gain.");
        status = rtlsdr_reset_buffer(devs[i]);
        check_status(status, "Failed to reset buffer.");
    }
}

void Kraken::stop()
{
    int status;
    for (size_t i = 0; i < channelIndex.size(); i++) 
    {
        status = rtlsdr_cancel_async(devs[i]);
        check_status(status, "Failed to stop async read.");
    }
}

void Kraken::process(IqData *buffer1, IqData *buffer2)
{
    std::vector<std::thread> threads;
    callbackContexts[0].device = this;
    callbackContexts[0].buffer = buffer1;
    callbackContexts[0].channelIndex = 0;
    callbackContexts[1].device = this;
    callbackContexts[1].buffer = buffer2;
    callbackContexts[1].channelIndex = 1;
    threads.emplace_back(rtlsdr_read_async, devs[0], callback, &callbackContexts[0], 0, 16 * 16384);
    threads.emplace_back(rtlsdr_read_async, devs[1], callback, &callbackContexts[1], 0, 16 * 16384);
    // join threads
    for (auto& thread : threads) {
        thread.join();
    }
}

void Kraken::callback(unsigned char *buf, uint32_t len, void *ctx) 
{
    CallbackContext *context = static_cast<CallbackContext *>(ctx);
    IqData *buffer_blah2 = context->buffer;
    int8_t *buffer_kraken = reinterpret_cast<int8_t *>(buf);

    buffer_blah2->lock();

    for (size_t i = 0; i < len; i += 2) {
        double iqi = static_cast<double>(buffer_kraken[i]);
        double iqq = static_cast<double>(buffer_kraken[i + 1]);

        buffer_blah2->push_back({iqi, iqq});
    }

    buffer_blah2->unlock_and_notify();

        context->device->append_save_samples(context->channelIndex, buffer_kraken,
            static_cast<size_t>(len / 2));
}

void Kraken::append_save_samples(size_t channelIndex, const int8_t *samples,
    size_t nComplexSamples)
{
    append_blah2_paired_iq_samples(channelIndex, samples, nComplexSamples);
}

void Kraken::replay(IqData *buffer1, IqData *buffer2, std::string _file, bool _loop)
{
    replay_blah2_iq_file(buffer1, buffer2, _file, _loop);
}

void Kraken::check_status(int status, std::string message)
{
  if (status < 0)
  {
    throw std::runtime_error("[Kraken] " + message);
  }
}
