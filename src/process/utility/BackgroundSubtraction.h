/// @file BackgroundSubtraction.h
/// @class BackgroundSubtraction
/// @brief Running exponential-moving-average background subtraction for
///        the ambiguity map surface.  Suppresses persistent stationary
///        features (terrain, SFN repeaters) before CFAR detection.
/// @author 30hours

#ifndef BACKGROUND_SUBTRACTION_H
#define BACKGROUND_SUBTRACTION_H

#include "data/Map.h"
#include <complex>
#include <vector>
#include <cstdint>

class BackgroundSubtraction
{
private:
  /// @brief EMA learning rate (0 < alpha <= 1).
  double alpha;

  /// @brief Number of Doppler rows.
  uint16_t nDopplerBins;

  /// @brief Number of delay columns.
  uint16_t nDelayBins;

  /// @brief CPI counter (incremented each call to process).
  uint64_t nCpi;

  /// @brief Number of CPIs to accumulate before subtraction activates.
  uint64_t warmupCpis;

  /// @brief Running EMA of magnitude-squared map.
  /// Dimensions: [nDopplerBins][nDelayBins].
  std::vector<std::vector<double>> background;

public:
  /// @brief Constructor.
  /// @param _alpha         EMA learning rate (0.001 to 0.1 typical).
  /// @param _warmupCpis    Number of CPIs to accumulate before subtracting.
  /// @param _nDopplerBins  Number of Doppler rows in the map.
  /// @param _nDelayBins    Number of delay columns in the map.
  BackgroundSubtraction(double _alpha, uint64_t _warmupCpis,
                        uint16_t _nDopplerBins, uint16_t _nDelayBins);

  /// @brief Destructor.
  ~BackgroundSubtraction() = default;

  /// @brief Apply background subtraction to a complex-valued ambiguity map.
  /// @param map  Map produced by Ambiguity::process().  Modified in-place:
  ///             complex magnitude is reduced by the running background.
  /// @return Void.
  void process(Map<std::complex<double>> *map);
};

#endif