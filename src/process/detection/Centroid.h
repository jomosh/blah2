/// @file Centroid.h
/// @class Centroid
/// @brief A class to post-process nearby target detections.
/// @details Supports legacy local-peak suppression and cluster-based weighted centroiding.
/// @author 30hours

#ifndef CENTROID_H
#define CENTROID_H

#include "data/Map.h"
#include "data/Detection.h"
#include <stdint.h>
#include <complex>
#include <memory>
#include <string>

enum class CentroidMode
{
  LocalPeak,
  ClusterCentroid
};

/// @brief Parse a centroid post-process mode string.
/// @param modeString Mode string from config or CLI.
/// @param mode Parsed mode when the string is supported.
/// @return True when the mode string is supported.
bool try_parse_centroid_mode(const std::string &modeString, CentroidMode &mode);

/// @brief Format a centroid post-process mode for display.
/// @param mode Mode to format.
/// @return User-facing mode string.
std::string format_centroid_mode(CentroidMode mode);

class Centroid
{
private:
  /// @brief Number of delay bins to check.
  uint16_t nDelay;

  /// @brief Number of Doppler bins to check.
  uint16_t nDoppler;

  /// @brief Doppler resolution to convert Hz to bins (Hz).
  double resolutionDoppler;

  /// @brief Detection post-process mode.
  CentroidMode mode;

public:
  /// @brief Constructor.
  /// @param nDelay Number of delay bins to check.
  /// @param nDoppler Number of Doppler bins to check.
  /// @param resolutionDoppler Doppler resolution to convert Hz to bins (Hz).
  /// @param mode Detection post-process mode.
  /// @return The object.
  Centroid(uint16_t nDelay, uint16_t nDoppler, double resolutionDoppler,
    CentroidMode mode = CentroidMode::LocalPeak);

  /// @brief Destructor.
  /// @return Void.
  ~Centroid();

  /// @brief Post-process detections from the 1D CFAR detector.
  /// @param x Detections from the 1D CFAR detector.
  /// @param y Ambiguity map used for weight estimation when available.
  /// @return Post-processed detections.
  std::unique_ptr<Detection> process(Detection *x, Map<std::complex<double>> *y = nullptr);
};

#endif
