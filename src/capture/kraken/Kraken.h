/// @file Kraken.h
/// @class Kraken
/// @brief A class to capture data on the Kraken SDR.
/// @details Uses a custom librtlsdr API to extract samples.
/// Uses 2 channels of the Kraken to capture IQ data.
/// The noise source phase synchronisation is not required for 2 channel operation.
/// Future work is to replicate the Heimdall DAQ phase syncronisation.
/// This will enable a surveillance array of up to 4 antenna elements.
/// Requires a custom librtlsdr which includes method rtlsdr_set_dithering().
/// The original steve-m/librtlsdr does not include this method.
/// This is included in librtlsdr/librtlsdr or krakenrf/librtlsdr.
/// Also works using 2 RTL-SDRs which have been clock synchronised.
/// @author 30hours, Michael Brock, sdn-ninja
/// @todo Add support for multiple surveillance channels.
#ifndef KRAKEN_H
#define KRAKEN_H

#include "capture/Source.h"
#include "data/IqData.h"

#include <chrono>
#include <condition_variable>
#include <complex>
#include <cstddef>
#include <deque>
#include <mutex>
#include <stdint.h>
#include <string>
#include <vector>
#include <rtl-sdr.h>

struct KrakenTestAccess;

class Kraken : public Source
{
private:

  static constexpr size_t nActiveChannels = 2;

  /// @brief Individual RTL-SDR devices.
  rtlsdr_dev_t* devs[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};

  /// @brief Device indices for Kraken.
  std::vector<int> channelIndex;

  /// @brief Gain for each channel.
  std::vector<int> gain;

  /// @brief Callback context for each Kraken receive stream.
  struct CallbackContext
  {
    Kraken *device;
    IqData *buffer;
    size_t channelIndex;
  };

  /// @brief Context data passed into each Kraken callback.
  CallbackContext callbackContexts[2];

  /// @brief Shared output buffers for the aligned reference and surveillance streams.
  IqData *outputBuffers[nActiveChannels] = {nullptr, nullptr};

  /// @brief Protects alignment state shared across callback and worker threads.
  std::mutex alignmentMutex;

  /// @brief Wakes the alignment worker when fresh raw samples arrive.
  std::condition_variable alignmentCv;

  /// @brief Pending raw samples awaiting startup alignment or later drift correction.
  std::deque<std::complex<float>> pendingOutputSamples[nActiveChannels];

  /// @brief Recent raw sample history used for startup alignment and drift rechecks.
  std::deque<std::complex<float>> historySamples[nActiveChannels];

  /// @brief Alignment drops queued for future callbacks when not enough samples are buffered yet.
  uint64_t scheduledAlignmentDrops[nActiveChannels] = {0, 0};

  /// @brief Total alignment drops already applied to each channel.
  uint64_t appliedAlignmentDrops[nActiveChannels] = {0, 0};

  /// @brief True once the capture worker should stop processing.
  bool stopRequested = false;

  /// @brief True once startup alignment has completed and aligned samples may flow downstream.
  bool alignmentReady = false;

  /// @brief Number of samples per lag-estimation window.
  size_t alignmentWindowSamples = 0;

  /// @brief Number of raw history samples retained per channel.
  size_t historyCapacitySamples = 0;

  /// @brief Raw startup lag estimate retained as the drift baseline.
  int64_t initialLagSamples = 0;

  /// @brief Deadline for the next drift recheck.
  std::chrono::steady_clock::time_point nextDriftCheckTime;

  /// @brief Single-window lag estimate.
  struct LagEstimate
  {
    bool valid = false;
    int64_t lagSamples = 0;
    double peakMagnitude = 0.0;
  };

  /// @brief Multi-window lag estimate used to establish startup certainty.
  struct LagMeasurement
  {
    bool valid = false;
    int64_t lagSamples = 0;
    double peakMagnitude = 0.0;
    std::vector<int64_t> attemptLags;
  };

  /// @brief Snapshot of recent raw IQ windows for lag estimation.
  struct LagSnapshot
  {
    std::vector<std::complex<float>> channels[nActiveChannels];
  };

  /// @brief Check status of API returns.
  /// @param status Return code of API call.
  /// @param message Message if API call error.
  /// @return Void.
  void check_status(int status, std::string message);

  /// @brief Append callback samples into the paired IQ save queues.
  /// @param channelIndex Zero-based channel index.
  /// @param samples Pointer to interleaved IQ byte samples.
  /// @param nComplexSamples Number of IQ samples in this callback.
  void append_save_samples(size_t channelIndex, const int8_t *samples,
    size_t nComplexSamples);

  /// @brief Append raw callback samples into the alignment and history queues.
  /// @param channelIndex Zero-based channel index.
  /// @param samples Pointer to interleaved IQ byte samples.
  /// @param nComplexSamples Number of IQ samples in this callback.
  /// @return Void.
  void append_input_samples(size_t channelIndex, const int8_t *samples,
    size_t nComplexSamples);

  /// @brief Run startup alignment and periodic drift rechecks.
  /// @return Void.
  void alignment_worker();

  /// @brief Drop any queued correction samples that now have backing data.
  /// @return Void.
  void drain_scheduled_drops_locked();

  /// @brief Queue a signed alignment correction on the earlier channel.
  /// @param deltaSamples Positive when channel 1 lags channel 0.
  /// @return Void.
  void schedule_alignment_delta_locked(int64_t deltaSamples);

  /// @brief Copy a recent raw history snapshot for lag estimation.
  /// @param snapshot Destination snapshot.
  /// @return True when enough history was available.
  bool snapshot_recent_history_locked(LagSnapshot *snapshot) const;

  /// @brief Estimate lag across several recent windows.
  /// @param snapshot Raw IQ windows.
  /// @return Consensus lag measurement.
  LagMeasurement measure_snapshot_lag(const LagSnapshot &snapshot) const;

  /// @brief Estimate lag from a single pair of IQ windows.
  /// @param reference Reference channel IQ window.
  /// @param surveillance Surveillance channel IQ window.
  /// @return Single-window lag estimate.
  static LagEstimate estimate_window_lag(
    const std::vector<std::complex<float>> &reference,
    const std::vector<std::complex<float>> &surveillance);

  /// @brief Copy one aligned output chunk out of the pending queues.
  /// @param referenceChunk Destination reference chunk.
  /// @param surveillanceChunk Destination surveillance chunk.
  /// @param maxSamples Maximum pairs to extract in one chunk.
  /// @return Void.
  void extract_output_chunk_locked(std::vector<std::complex<float>> &referenceChunk,
    std::vector<std::complex<float>> &surveillanceChunk, size_t maxSamples);

  /// @brief Emit an aligned output chunk into live buffers and optional IQ save.
  /// @param referenceChunk Reference samples.
  /// @param surveillanceChunk Surveillance samples.
  /// @return Void.
  void emit_output_chunk(const std::vector<std::complex<float>> &referenceChunk,
    const std::vector<std::complex<float>> &surveillanceChunk);

  /// @brief Compute the next power of two greater than or equal to value.
  /// @param value Input value.
  /// @return Next power of two.
  static size_t next_power_of_two(size_t value);

  /// @brief Convert signed lag samples into microseconds.
  /// @param lagSamples Signed lag in samples.
  /// @return Signed lag in microseconds.
  double lag_samples_to_microseconds(int64_t lagSamples) const;

  friend struct KrakenTestAccess;

  /// @brief Callback function when buffer is filled.
  /// @param buf Pointer to buffer of IQ data.
  /// @param len Length of buffer.
  /// @param ctx Context data for callback.
  /// @return Void.
  static void callback(unsigned char *buf, uint32_t len, void *ctx);

public:

  /// @brief Constructor.
  /// @param fc Center frequency (Hz).
  /// @param path Path to save IQ data.
  /// @return The object.
  Kraken(std::string type, uint32_t fc, uint32_t fs, std::string path, 
    std::atomic<bool> *saveIq, std::vector<double> gain);

  /// @brief Implement capture function on KrakenSDR.
  /// @param buffer Pointers to buffers for each channel.
  /// @return Void.
  void process(IqData *buffer1, IqData *buffer2);

  /// @brief Call methods to start capture.
  /// @return Void.
  void start();

  /// @brief Call methods to gracefully stop capture.
  /// @return Void.
  void stop();

  /// @brief Implement replay function on the Kraken.
  /// @param buffers Pointers to buffers for each channel.
  /// @param file Path to file to replay data from.
  /// @param loop True if samples should loop at EOF.
  /// @return Void.
  void replay(IqData *buffer1, IqData *buffer2, std::string file, bool loop);

};

#endif
