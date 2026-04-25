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
#include <fstream>
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

  /// @brief Write canonical Blah2 IQ samples to disk.
  /// @details Samples are stored as interleaved int16 tuples [refI, refQ, survI, survQ].
  /// @param reference Pointer to reference-channel samples.
  /// @param surveillance Pointer to surveillance-channel samples.
  /// @param nSamples Number of channel-paired samples to write.
  /// @return Void.
  template <typename T>
  void write_blah2_iq_samples(const std::complex<T> *reference,
    const std::complex<T> *surveillance, size_t nSamples)
  {
    if (!saveIqFile.is_open() || reference == nullptr || surveillance == nullptr || nSamples == 0)
    {
      return;
    }

    std::vector<int16_t> interleaved;
    interleaved.reserve(nSamples * 4);
    for (size_t i = 0; i < nSamples; i++)
    {
      interleaved.push_back(clamp_blah2_iq_component(reference[i].real()));
      interleaved.push_back(clamp_blah2_iq_component(reference[i].imag()));
      interleaved.push_back(clamp_blah2_iq_component(surveillance[i].real()));
      interleaved.push_back(clamp_blah2_iq_component(surveillance[i].imag()));
    }

    saveIqFile.write(reinterpret_cast<const char *>(interleaved.data()),
      static_cast<std::streamsize>(interleaved.size() * sizeof(int16_t)));
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