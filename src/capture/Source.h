/// @file Source.h
/// @class Source
/// @brief An abstract class for capture sources.
/// @author 30hours

#ifndef SOURCE_H
#define SOURCE_H

#include <string>
#include <stdint.h>
#include <cmath>
#include <complex>
#include <cstddef>
#include <deque>
#include <fstream>
#include <iostream>
#include <mutex>
#include <vector>
#include <atomic>
#include "data/IqData.h"

class Source
{
protected:

  /// @brief The capture device type.
  std::string type;

  /// @brief Center frequency (Hz).
  uint32_t fc;

  /// @brief Sampling frequency (Hz).
  uint32_t fs;

  /// @brief Absolute path to IQ save location.
  std::string path;

  /// @brief True if IQ data to be saved.
  bool *saveIq;

  /// @brief File stream to save IQ data.
  std::ofstream saveIqFile;

  /// @brief Protects IQ file open/close/write operations.
  std::mutex saveIqFileMutex;

  /// @brief Pending channel-aligned save samples waiting to be paired.
  std::deque<std::complex<float>> pendingSaveSamples[2];

  /// @brief Protects access to pending paired save samples.
  std::mutex pendingSaveMutex;

  /// @brief Maximum unpaired IQ samples retained per channel before dropping oldest.
  static constexpr size_t maxPendingBlah2PairedIqSamples = 1024 * 1024;

  /// @brief Clamp a numeric IQ component into the canonical Blah2 int16 file range.
  /// @param value Numeric sample component.
  /// @return Saturated int16 sample component.
  static int16_t clamp_blah2_iq_component(double value);

  /// @brief Replay a canonical Blah2 two-channel IQ file.
  /// @param buffer1 Pointer to reference buffer.
  /// @param buffer2 Pointer to surveillance buffer.
  /// @param file Path to file to replay data from.
  /// @param loop True if samples should loop at EOF.
  /// @return Void.
  void replay_blah2_iq_file(IqData *buffer1, IqData *buffer2,
    const std::string &file, bool loop);

  /// @brief Append paired int8 IQ samples awaiting canonical file writes.
  /// @param channelIndex Zero-based channel index.
  /// @param samples Pointer to interleaved IQ byte samples.
  /// @param nComplexSamples Number of IQ samples in this callback.
  /// @return Void.
  void append_blah2_paired_iq_samples(size_t channelIndex, const int8_t *samples,
    size_t nComplexSamples);

  /// @brief Flush paired callback samples into the canonical Blah2 IQ file.
  /// @return Void.
  void flush_blah2_paired_iq_samples_locked();

  /// @brief Clear any unpaired callback save samples.
  /// @return Void.
  void clear_blah2_paired_iq_samples_locked();

  /// @brief Write canonical Blah2 IQ samples to disk.
  /// @details Samples are stored as interleaved int16 tuples [refI, refQ, survI, survQ].
  /// @param reference Pointer to reference-channel samples.
  /// @param surveillance Pointer to surveillance-channel samples.
  /// @param nSamples Number of channel-paired samples to write.
  /// @param scale Scale factor applied before clamping to int16.
  /// @return Void.
  template <typename T>
  void write_blah2_iq_samples(const std::complex<T> *reference,
    const std::complex<T> *surveillance, size_t nSamples, double scale = 1.0)
  {
    if (reference == nullptr || surveillance == nullptr || nSamples == 0)
    {
      return;
    }

    thread_local std::vector<int16_t> interleavedScratch;
    interleavedScratch.resize(nSamples * 4);
    for (size_t i = 0; i < nSamples; i++)
    {
      const size_t offset = i * 4;
      interleavedScratch[offset] = clamp_blah2_iq_component(
        scale * static_cast<double>(reference[i].real()));
      interleavedScratch[offset + 1] = clamp_blah2_iq_component(
        scale * static_cast<double>(reference[i].imag()));
      interleavedScratch[offset + 2] = clamp_blah2_iq_component(
        scale * static_cast<double>(surveillance[i].real()));
      interleavedScratch[offset + 3] = clamp_blah2_iq_component(
        scale * static_cast<double>(surveillance[i].imag()));
    }

    std::lock_guard<std::mutex> lock(saveIqFileMutex);
    if (!saveIqFile.is_open())
    {
      return;
    }

    saveIqFile.write(reinterpret_cast<const char *>(interleavedScratch.data()),
      static_cast<std::streamsize>(interleavedScratch.size() * sizeof(int16_t)));
    if (!saveIqFile)
    {
      std::cerr << "Error: failed to write Blah2 IQ samples" << std::endl;
    }
  }

public:

  Source();

  /// @brief Constructor.
  /// @param type The capture device type.
  /// @param fs Sampling frequency (Hz).
  /// @param fc Center frequency (Hz).
  /// @param path Absolute path to IQ save location.
  /// @return The object.
  Source(std::string type, uint32_t fc, uint32_t fs,
    std::string path, bool *saveIq);

  /// @brief Implement the capture process.
  /// @param buffer1 Buffer for reference samples.
  /// @param buffer2 Buffer for surveillance samples.
  /// @return Void.
  virtual void process(IqData *buffer1, IqData *buffer2) = 0;

  /// @brief Call methods to start capture.
  /// @return Void.
  virtual void start() = 0;

  /// @brief Call methods to gracefully stop capture.
  /// @return Void.
  virtual void stop() = 0;

  /// @brief Replay a Blah2 two-channel IQ file.
  /// @param buffer1 Pointer to reference buffer.
  /// @param buffer2 Pointer to surveillance buffer.
  /// @param file Path to file to replay data from.
  /// @param loop True if samples should loop at EOF.
  /// @return Void.
  virtual void replay(IqData *buffer1, IqData *buffer2,
    std::string file, bool loop) = 0;

  /// @brief Open a new file to record IQ.
  /// @details First creates a new file from current timestamp.
  /// Files are of format <path><timestamp>.<type>.iq using Blah2's canonical
  /// interleaved two-channel int16 sample layout.
  /// @return String of full path to file.
  std::string open_file();

  /// @brief Close IQ file gracefully.
  /// @return Void.
  void close_file();

  /// @brief Graceful handler for SIGTERM.
  /// @return Void.
  void kill();

};

#endif
