/// @file BackgroundSubtraction.cpp
/// @class BackgroundSubtraction
/// @brief Implementation of EMA background subtraction for ambiguity maps.
/// @author 30hours

#include "BackgroundSubtraction.h"
#include <algorithm>
#include <cmath>

BackgroundSubtraction::BackgroundSubtraction(
    double _alpha, uint64_t _warmupCpis,
    uint16_t _nDopplerBins, uint16_t _nDelayBins)
  : alpha(_alpha)
  , nDopplerBins(_nDopplerBins)
  , nDelayBins(_nDelayBins)
  , nCpi(0)
  , warmupCpis(_warmupCpis)
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
        // Update background EMA
        background[i][j] = (1.0 - alpha) * background[i][j] + alpha * magSq;

        // Subtract background, clamp at zero
        const double residual = std::max(0.0, magSq - background[i][j]);

        // Preserve phase of original complex value, scale magnitude
        const double oldMag = std::sqrt(magSq);
        if (oldMag > 0.0)
        {
          const double newMag = std::sqrt(residual);
          const double scale = newMag / oldMag;
          map->data[i][j] *= scale;
        }
        else
        {
          map->data[i][j] = {0.0, 0.0};
        }
      }
    }
  }
}