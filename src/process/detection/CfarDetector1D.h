/// @file CfarDetector1D.h
/// @class CfarDetector1D
/// @brief A class to implement a 1D CFAR detector.
/// @details Converts an AmbiguityMap to DetectionData. 1D CFAR operates across delay, to minimise detections from the zero-Doppler line.
/// @author 30hours
/// @todo Actually implement the min delay and Doppler.

#ifndef CFARDETECTOR1D_H
#define CFARDETECTOR1D_H

#include "data/Map.h"
#include "data/Detection.h"
#include <stdint.h>
#include <complex>
#include <memory>
#include <unordered_map>

/// @brief CFAR thresholding mode.
enum class CfarMode
{
  /// @brief Cell-averaging CFAR (best for homogeneous background noise).
  CA,

  /// @brief Greatest-of cell-averaging CFAR (more robust to clutter edges).
  CAGO
};

class CfarDetector1D
{
private:
  /// @brief Probability of false alarm, numeric in [0,1]
  double pfa;

  /// @brief Number of single-sided guard cells.
  int8_t nGuard;

  /// @brief Number of single-sided training cells.
  int8_t nTrain;

  /// @brief Minimum delay to process detections (bins).
  int8_t minDelay;

  /// @brief Minimum absolute Doppler to process detections (Hz).
  double minDoppler;

  /// @brief CFAR mode.
  CfarMode mode;

  /// @brief Cache for CFAR scaling factors keyed by leading/trailing counts.
  std::unordered_map<uint32_t, double> alphaCache;

  /// @brief Pointer to detection data to store result.
  Detection *detection;

public:
  /// @brief Constructor.
  /// @param pfa Probability of false alarm, numeric in [0,1].
  /// @param nGuard Number of single-sided guard cells.
  /// @param nTrain Number of single-sided training cells.
  /// @param minDelay Minimum delay to process detections (bins).
  /// @param minDoppler Minimum absolute Doppler to process detections (Hz).
  /// @param mode CFAR mode.
  /// @return The object.
  CfarDetector1D(double pfa, int8_t nGuard, int8_t nTrain, int8_t minDelay, double minDoppler, CfarMode mode = CfarMode::CA);

  /// @brief Destructor.
  /// @return Void.
  ~CfarDetector1D();

  /// @brief Implement the 1D CFAR detector.
  /// @param x Ambiguity map data of IQ samples.
  /// @return Detections from the 1D CFAR detector.
  std::unique_ptr<Detection> process(Map<std::complex<double>> *x);
};

#endif
