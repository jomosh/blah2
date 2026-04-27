#include "Kraken.h"

#include <fftw3.h>

#include <algorithm>
#include <dlfcn.h>
#include <cmath>
#include <complex>
#include <cstdint>
#include <iostream>
#include <thread>

namespace
{
constexpr size_t kStartupAlignmentAttempts = 3;
constexpr size_t kEmitChunkSamples = 16384;
constexpr size_t kMinAlignmentWindowSamples = 65536;
constexpr size_t kMaxAlignmentWindowSamples = 262144;
constexpr int64_t kLagConsensusToleranceSamples = 16;
const std::chrono::minutes kDriftCheckInterval(10);
const std::chrono::minutes kDriftRetryInterval(1);

using RtlSdrSetDitheringFn = int (*)(rtlsdr_dev_t *, int);

RtlSdrSetDitheringFn lookup_rtlsdr_set_dithering()
{
    static RtlSdrSetDitheringFn function = reinterpret_cast<RtlSdrSetDitheringFn>(
      dlsym(RTLD_DEFAULT, "rtlsdr_set_dithering"));
    return function;
}

int set_rtlsdr_dithering_if_supported(rtlsdr_dev_t *device, int enabled)
{
    const RtlSdrSetDitheringFn function = lookup_rtlsdr_set_dithering();
    if (function != nullptr)
    {
        return function(device, enabled);
    }

    static bool warned = false;
    if (!warned)
    {
        std::cout << "[Kraken] Warning: librtlsdr does not expose rtlsdr_set_dithering(); continuing without disabling PLL dithering. In shared-clock RTL-SDR setups this can reduce phase coherence and make startup/drift alignment less stable." << std::endl;
        warned = true;
    }

    return 0;
}
}

void Kraken::SampleRingBuffer::set_capacity(size_t capacity)
{
    data.assign(capacity, std::complex<float>(0.0f, 0.0f));
    head = 0;
    length = 0;
}

void Kraken::SampleRingBuffer::clear()
{
    head = 0;
    length = 0;
}

size_t Kraken::SampleRingBuffer::size() const
{
    return length;
}

bool Kraken::SampleRingBuffer::empty() const
{
    return length == 0;
}

size_t Kraken::SampleRingBuffer::append(
    const std::vector<std::complex<float>> &samples)
{
    if (data.empty() || samples.empty())
    {
        return 0;
    }

    size_t overwritten = 0;
    const size_t capacity = data.size();
    for (size_t i = 0; i < samples.size(); i++)
    {
        if (length < capacity)
        {
            data[(head + length) % capacity] = samples[i];
            length++;
            continue;
        }

        data[head] = samples[i];
        head = (head + 1) % capacity;
        overwritten++;
    }

    return overwritten;
}

size_t Kraken::SampleRingBuffer::discard_front(size_t count)
{
    const size_t discarded = std::min(count, length);
    if (discarded == 0)
    {
        return 0;
    }

    head = (head + discarded) % data.size();
    length -= discarded;
    return discarded;
}

bool Kraken::SampleRingBuffer::copy_tail(std::vector<std::complex<float>> *out,
    size_t count) const
{
    if (out == nullptr || count > length)
    {
        return false;
    }

    out->clear();
    out->reserve(count);
    const size_t capacity = data.size();
    const size_t start = (head + length - count) % capacity;
    for (size_t i = 0; i < count; i++)
    {
        out->push_back(data[(start + i) % capacity]);
    }
    return true;
}

size_t Kraken::SampleRingBuffer::pop_front_into(
    std::vector<std::complex<float>> *out, size_t count)
{
    if (out == nullptr)
    {
        return 0;
    }

    const size_t nSamples = std::min(count, length);
    out->clear();
    out->reserve(nSamples);
    const size_t capacity = data.size();
    for (size_t i = 0; i < nSamples; i++)
    {
        out->push_back(data[(head + i) % capacity]);
    }

    head = (head + nSamples) % capacity;
    length -= nSamples;
    return nSamples;
}

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
            gain[i] = *it;
        } else {
            gain[i] = validGains.back();
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
        status = set_rtlsdr_dithering_if_supported(devs[i], 0); // disable dither when supported
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
    {
        std::lock_guard<std::mutex> lock(alignmentMutex);
        outputBuffers[0] = buffer1;
        outputBuffers[1] = buffer2;
        scheduledAlignmentDrops[0] = 0;
        scheduledAlignmentDrops[1] = 0;
        appliedAlignmentDrops[0] = 0;
        appliedAlignmentDrops[1] = 0;
        stopRequested = false;
        alignmentReady = false;
        initialLagSamples = 0;
        const size_t callbackBlockSamples = 16 * 16384 / 2;
        const size_t requestedWindow = next_power_of_two(std::max(
            callbackBlockSamples, static_cast<size_t>(std::max<uint32_t>(fs / 10, 1))));
        alignmentWindowSamples = std::min(kMaxAlignmentWindowSamples,
            std::max(kMinAlignmentWindowSamples, requestedWindow));
        historyCapacitySamples = kStartupAlignmentAttempts * alignmentWindowSamples;
        pendingOutputCapacitySamples = std::min(static_cast<size_t>(maxPendingBlah2PairedIqSamples),
            historyCapacitySamples + kEmitChunkSamples);
        for (size_t i = 0; i < nActiveChannels; i++)
        {
            pendingOutputSamples[i].set_capacity(pendingOutputCapacitySamples);
            historySamples[i].set_capacity(historyCapacitySamples);
        }
        nextDriftCheckTime = std::chrono::steady_clock::now() + kDriftCheckInterval;
    }

    std::thread alignmentThread(&Kraken::alignment_worker, this);
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

    {
        std::lock_guard<std::mutex> lock(alignmentMutex);
        stopRequested = true;
    }
    alignmentCv.notify_all();
    alignmentThread.join();
}

void Kraken::callback(unsigned char *buf, uint32_t len, void *ctx)
{
    CallbackContext *context = static_cast<CallbackContext *>(ctx);
    int8_t *buffer_kraken = reinterpret_cast<int8_t *>(buf);

    context->device->append_input_samples(context->channelIndex, buffer_kraken,
        static_cast<size_t>(len / 2));
}

void Kraken::append_save_samples(size_t channelIndex, const int8_t *samples,
    size_t nComplexSamples)
{
    append_blah2_paired_iq_samples(channelIndex, samples, nComplexSamples);
}

void Kraken::append_input_samples(size_t channelIndex, const int8_t *samples,
    size_t nComplexSamples)
{
    if (channelIndex >= nActiveChannels || samples == nullptr)
    {
        return;
    }

    thread_local std::vector<std::complex<float>> scratchSamples;
    scratchSamples.resize(nComplexSamples);
    for (size_t i = 0; i < nComplexSamples; i++)
    {
        scratchSamples[i] = {static_cast<float>(samples[2 * i]),
          static_cast<float>(samples[2 * i + 1])};
    }

    std::lock_guard<std::mutex> lock(alignmentMutex);
    const size_t overwrittenPending = pendingOutputSamples[channelIndex].append(scratchSamples);
    historySamples[channelIndex].append(scratchSamples);
    if (overwrittenPending > 0)
    {
        const size_t accountedDrops = std::min<size_t>(overwrittenPending,
          static_cast<size_t>(scheduledAlignmentDrops[channelIndex]));
        scheduledAlignmentDrops[channelIndex] -= static_cast<uint64_t>(accountedDrops);
        appliedAlignmentDrops[channelIndex] += static_cast<uint64_t>(accountedDrops);
    }

    alignmentCv.notify_all();
}

void Kraken::alignment_worker()
{
    while (true)
    {
        LagSnapshot snapshot;
        bool measureLag = false;
        std::vector<std::complex<float>> referenceChunk;
        std::vector<std::complex<float>> surveillanceChunk;

        {
            std::unique_lock<std::mutex> lock(alignmentMutex);
            alignmentCv.wait_for(lock, std::chrono::milliseconds(20), [&]() {
                return stopRequested
                    || !pendingOutputSamples[0].empty()
                    || !pendingOutputSamples[1].empty();
            });

            drain_scheduled_drops_locked();

            if (stopRequested && !alignmentReady)
            {
                pendingOutputSamples[0].clear();
                pendingOutputSamples[1].clear();
                break;
            }

            if (snapshot_recent_history_locked(&snapshot)
              && scheduledAlignmentDrops[0] == 0
              && scheduledAlignmentDrops[1] == 0)
            {
                if (!alignmentReady)
                {
                    measureLag = true;
                }
                else if (std::chrono::steady_clock::now() >= nextDriftCheckTime)
                {
                    measureLag = true;
                }
            }

            if (!measureLag && alignmentReady)
            {
                extract_output_chunk_locked(referenceChunk, surveillanceChunk,
                  kEmitChunkSamples);
            }

            if (stopRequested && pendingOutputSamples[0].empty()
              && pendingOutputSamples[1].empty()
              && referenceChunk.empty() && surveillanceChunk.empty())
            {
                break;
            }
        }

        if (measureLag)
        {
            const LagMeasurement measurement = measure_snapshot_lag(snapshot);
            std::lock_guard<std::mutex> lock(alignmentMutex);
            if (stopRequested)
            {
                continue;
            }

            if (!measurement.valid)
            {
                if (alignmentReady)
                {
                    std::cout << "[Kraken] Drift recheck could not establish a stable lag estimate; retrying in 1 minute." << std::endl;
                    nextDriftCheckTime = std::chrono::steady_clock::now() + kDriftRetryInterval;
                }
                continue;
            }

            const int64_t currentCorrection = static_cast<int64_t>(appliedAlignmentDrops[0])
              - static_cast<int64_t>(appliedAlignmentDrops[1]);
            const int64_t correctionDelta = measurement.lagSamples - currentCorrection;

            if (!alignmentReady)
            {
                initialLagSamples = measurement.lagSamples;
                schedule_alignment_delta_locked(correctionDelta);
                alignmentReady = true;
                nextDriftCheckTime = std::chrono::steady_clock::now() + kDriftCheckInterval;
                std::cout << "[Kraken] Startup sample-time alignment established at "
                  << measurement.lagSamples << " samples ("
                  << lag_samples_to_microseconds(measurement.lagSamples)
                  << " us)." << std::endl;
                alignmentCv.notify_all();
                continue;
            }

            nextDriftCheckTime = std::chrono::steady_clock::now() + kDriftCheckInterval;
            if (correctionDelta != 0)
            {
                const int64_t driftFromInitial = measurement.lagSamples - initialLagSamples;
                schedule_alignment_delta_locked(correctionDelta);
                std::cout << "[Kraken] Sample-time drift detected: current difference "
                  << measurement.lagSamples << " samples ("
                  << lag_samples_to_microseconds(measurement.lagSamples)
                  << " us), drift from initial " << driftFromInitial
                  << " samples (" << lag_samples_to_microseconds(driftFromInitial)
                  << " us)." << std::endl;
                alignmentCv.notify_all();
            }
            continue;
        }

        if (!referenceChunk.empty())
        {
            emit_output_chunk(referenceChunk, surveillanceChunk);
        }
    }
}

void Kraken::drain_scheduled_drops_locked()
{
    for (size_t i = 0; i < nActiveChannels; i++)
    {
                const size_t nDropped = pendingOutputSamples[i].discard_front(
                    static_cast<size_t>(scheduledAlignmentDrops[i]));
                scheduledAlignmentDrops[i] -= static_cast<uint64_t>(nDropped);
                appliedAlignmentDrops[i] += static_cast<uint64_t>(nDropped);
    }
}

void Kraken::schedule_alignment_delta_locked(int64_t deltaSamples)
{
    if (deltaSamples > 0)
    {
        scheduledAlignmentDrops[0] += static_cast<uint64_t>(deltaSamples);
    }
    else if (deltaSamples < 0)
    {
        scheduledAlignmentDrops[1] += static_cast<uint64_t>(-deltaSamples);
    }

    drain_scheduled_drops_locked();
}

bool Kraken::snapshot_recent_history_locked(LagSnapshot *snapshot) const
{
    if (snapshot == nullptr || alignmentWindowSamples == 0)
    {
        return false;
    }

    const size_t requiredSamples = kStartupAlignmentAttempts * alignmentWindowSamples;
    for (size_t i = 0; i < nActiveChannels; i++)
    {
        if (historySamples[i].size() < requiredSamples)
        {
            return false;
        }
    }

    for (size_t i = 0; i < nActiveChannels; i++)
    {
        if (!historySamples[i].copy_tail(&snapshot->channels[i], requiredSamples))
        {
            return false;
        }
    }

    return true;
}

Kraken::LagMeasurement Kraken::measure_snapshot_lag(const LagSnapshot &snapshot) const
{
    LagMeasurement measurement;
    const size_t requiredSamples = kStartupAlignmentAttempts * alignmentWindowSamples;
    if (snapshot.channels[0].size() != requiredSamples
      || snapshot.channels[1].size() != requiredSamples)
    {
        return measurement;
    }

    std::vector<int64_t> lags;
    lags.reserve(kStartupAlignmentAttempts);
    double peakMagnitude = 0.0;

    for (size_t attempt = 0; attempt < kStartupAlignmentAttempts; attempt++)
    {
        const size_t offset = attempt * alignmentWindowSamples;
        std::vector<std::complex<float>> referenceWindow(
          snapshot.channels[0].begin() + offset,
          snapshot.channels[0].begin() + offset + alignmentWindowSamples);
        std::vector<std::complex<float>> surveillanceWindow(
          snapshot.channels[1].begin() + offset,
          snapshot.channels[1].begin() + offset + alignmentWindowSamples);
        const LagEstimate windowLag = estimate_window_lag(referenceWindow,
          surveillanceWindow);
        if (!windowLag.valid)
        {
            return measurement;
        }
        lags.push_back(windowLag.lagSamples);
        peakMagnitude += windowLag.peakMagnitude;
    }

    std::vector<int64_t> sortedLags = lags;
    std::sort(sortedLags.begin(), sortedLags.end());
    const int64_t medianLag = sortedLags[sortedLags.size() / 2];
    for (size_t i = 0; i < lags.size(); i++)
    {
        if (std::llabs(lags[i] - medianLag) > kLagConsensusToleranceSamples)
        {
            return measurement;
        }
    }

    measurement.valid = true;
    measurement.lagSamples = medianLag;
    measurement.peakMagnitude = peakMagnitude / static_cast<double>(lags.size());
    measurement.attemptLags = lags;
    return measurement;
}

Kraken::LagEstimate Kraken::estimate_window_lag(
    const std::vector<std::complex<float>> &reference,
    const std::vector<std::complex<float>> &surveillance)
{
    LagEstimate estimate;
    if (reference.size() != surveillance.size() || reference.size() < 2)
    {
        return estimate;
    }

    const size_t nSamples = reference.size();
    const size_t fftSize = next_power_of_two(nSamples * 2);
    std::vector<std::complex<double>> referenceSpectrum(fftSize,
      std::complex<double>(0.0, 0.0));
    std::vector<std::complex<double>> surveillanceSpectrum(fftSize,
      std::complex<double>(0.0, 0.0));

    std::complex<double> referenceMean(0.0, 0.0);
    std::complex<double> surveillanceMean(0.0, 0.0);
    for (size_t i = 0; i < nSamples; i++)
    {
        referenceMean += static_cast<std::complex<double>>(reference[i]);
        surveillanceMean += static_cast<std::complex<double>>(surveillance[i]);
    }
    referenceMean /= static_cast<double>(nSamples);
    surveillanceMean /= static_cast<double>(nSamples);

    for (size_t i = 0; i < nSamples; i++)
    {
        referenceSpectrum[i] = static_cast<std::complex<double>>(reference[i])
          - referenceMean;
        surveillanceSpectrum[i] = static_cast<std::complex<double>>(surveillance[i])
          - surveillanceMean;
    }

    fftw_plan referencePlan = fftw_plan_dft_1d(static_cast<int>(fftSize),
      reinterpret_cast<fftw_complex *>(referenceSpectrum.data()),
      reinterpret_cast<fftw_complex *>(referenceSpectrum.data()),
      FFTW_FORWARD, FFTW_ESTIMATE);
    fftw_plan surveillancePlan = fftw_plan_dft_1d(static_cast<int>(fftSize),
      reinterpret_cast<fftw_complex *>(surveillanceSpectrum.data()),
      reinterpret_cast<fftw_complex *>(surveillanceSpectrum.data()),
      FFTW_FORWARD, FFTW_ESTIMATE);
    fftw_execute(referencePlan);
    fftw_execute(surveillancePlan);
    fftw_destroy_plan(referencePlan);
    fftw_destroy_plan(surveillancePlan);

    for (size_t i = 0; i < fftSize; i++)
    {
        referenceSpectrum[i] *= std::conj(surveillanceSpectrum[i]);
    }

    fftw_plan inversePlan = fftw_plan_dft_1d(static_cast<int>(fftSize),
      reinterpret_cast<fftw_complex *>(referenceSpectrum.data()),
      reinterpret_cast<fftw_complex *>(referenceSpectrum.data()),
      FFTW_BACKWARD, FFTW_ESTIMATE);
    fftw_execute(inversePlan);
    fftw_destroy_plan(inversePlan);

    size_t bestIndex = 0;
    double bestMagnitude = -1.0;
    for (size_t i = 0; i < nSamples; i++)
    {
        const double magnitude = std::norm(referenceSpectrum[i]);
        if (magnitude > bestMagnitude)
        {
            bestMagnitude = magnitude;
            bestIndex = i;
        }
    }
    for (size_t i = fftSize - (nSamples - 1); i < fftSize; i++)
    {
        const double magnitude = std::norm(referenceSpectrum[i]);
        if (magnitude > bestMagnitude)
        {
            bestMagnitude = magnitude;
            bestIndex = i;
        }
    }

    if (bestMagnitude <= 0.0)
    {
        return estimate;
    }

    estimate.valid = true;
    estimate.lagSamples = (bestIndex < nSamples)
      ? static_cast<int64_t>(bestIndex)
      : static_cast<int64_t>(bestIndex) - static_cast<int64_t>(fftSize);
    estimate.peakMagnitude = std::sqrt(bestMagnitude);
    return estimate;
}

void Kraken::extract_output_chunk_locked(
    std::vector<std::complex<float>> &referenceChunk,
    std::vector<std::complex<float>> &surveillanceChunk, size_t maxSamples)
{
    if (!alignmentReady)
    {
        return;
    }

    const size_t nPairs = std::min(pendingOutputSamples[0].size(),
      pendingOutputSamples[1].size());
    const size_t nEmit = std::min(nPairs, maxSamples);
    if (nEmit == 0)
    {
        return;
    }

    pendingOutputSamples[0].pop_front_into(&referenceChunk, nEmit);
    pendingOutputSamples[1].pop_front_into(&surveillanceChunk, nEmit);
}

void Kraken::emit_output_chunk(
    const std::vector<std::complex<float>> &referenceChunk,
    const std::vector<std::complex<float>> &surveillanceChunk)
{
    if (referenceChunk.empty() || referenceChunk.size() != surveillanceChunk.size()
      || outputBuffers[0] == nullptr || outputBuffers[1] == nullptr)
    {
        return;
    }

    outputBuffers[0]->lock();
    outputBuffers[1]->lock();
    for (size_t i = 0; i < referenceChunk.size(); i++)
    {
        outputBuffers[0]->push_back({static_cast<double>(referenceChunk[i].real()),
          static_cast<double>(referenceChunk[i].imag())});
        outputBuffers[1]->push_back({static_cast<double>(surveillanceChunk[i].real()),
          static_cast<double>(surveillanceChunk[i].imag())});
    }
    outputBuffers[0]->unlock_and_notify();
    outputBuffers[1]->unlock_and_notify();

    if (saveIq != nullptr && saveIq->load())
    {
        write_blah2_iq_samples(referenceChunk.data(), surveillanceChunk.data(),
          referenceChunk.size());
    }
}

size_t Kraken::next_power_of_two(size_t value)
{
    size_t power = 1;
    while (power < value)
    {
        power <<= 1;
    }
    return power;
}

double Kraken::lag_samples_to_microseconds(int64_t lagSamples) const
{
    if (fs == 0)
    {
        return 0.0;
    }
    return static_cast<double>(lagSamples) * 1000000.0 / static_cast<double>(fs);
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
