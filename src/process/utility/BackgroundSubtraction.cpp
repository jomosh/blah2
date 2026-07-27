/// @file BackgroundSubtraction.cpp
/// @class BackgroundSubtraction
/// @brief Implementation of EMA background gating for ambiguity maps.
/// @author 30hours

#include "BackgroundSubtraction.h"
#include <algorithm>
#include <cmath>

BackgroundSubtraction::BackgroundSubtraction(
    double _alpha, uint64_t _warmupCpis,
    uint16_t _nDopplerBins, uint16_t _nDelayBins,
    double _gateThreshold, double _suppressionFactor)
  : alpha(_alpha)
  , nDopplerBins(_nDopplerBins)
  , nDelayBins(_nDelayBins)
  , nCpi(0)
  , warmupCpis(_warmupCpis)
  , gateThreshold(_gateThreshold)
  , suppressionFactor(_suppressionFactor)
  , background(_nDopplerBins, std::vector<double>(_nDelayBins, 0.0))
{
}

void BackgroundSubtraction::process(Map<std::complex<double>> *map)
{
  nCpi++;

  for (uint16_t i = 0; i < nDopplerBins; i++)
  {
    for (uint16_t j = 0; j < nDelayBins; j++)
    {
      const double magSq = std::norm(map->data[i][j]);

      if (nCpi <= warmupCpis)
      {
        // Warmup: accumulate background only, leave map unchanged.
        background[i][j] = (1.0 - alpha) * background[i][j] + alpha * magSq;
      }
      else
      {
        // Snapshot the background model before updating it.
        const double oldBg = background[i][j];

        // Update EMA — model learns the current frame AFTER gating decision.
        background[i][j] = (1.0 - alpha) * background[i][j] + alpha * magSq;

        // Gate: if the current cell power is close to (or below) the expected
        // stationary background level, suppress it.  Cells significantly above
        // the background (moving targets, new interferers) pass through intact.
        // This preserves the noise statistics that CFAR relies on, unlike
        // continuous subtraction which destroys the exponential distribution.
        if (magSq < oldBg * gateThreshold)
        {
          map->data[i][j] *= suppressionFactor;
        }
      }
    }
  }
}