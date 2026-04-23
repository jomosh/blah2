#include "CfarDetector1D.h"
#include "data/Map.h"

#include <iostream>
#include <vector>
#include <cmath>
#include <unordered_map>
#include <algorithm>
#include <limits>

namespace
{
double ca_alpha(double pfa, int nCells)
{
  if (nCells <= 0)
  {
    return 0.0;
  }
  return nCells * (std::pow(pfa, -1.0 / nCells) - 1.0);
}

double erlang_cdf_scaled_mean(double x, int n)
{
  if (n <= 0)
  {
    return 0.0;
  }
  if (x <= 0.0)
  {
    return 0.0;
  }

  const double nx = n * x;
  double sum = 0.0;
  double term = 1.0;
  for (int k = 0; k < n; k++)
  {
    if (k > 0)
    {
      term *= nx / k;
    }
    sum += term;
  }

  const double cdf = 1.0 - std::exp(-nx) * sum;
  if (cdf < 0.0)
  {
    return 0.0;
  }
  if (cdf > 1.0)
  {
    return 1.0;
  }
  return cdf;
}

double erlang_pdf_scaled_mean(double x, int n)
{
  if (n <= 0 || x < 0.0)
  {
    return 0.0;
  }
  if (x == 0.0)
  {
    return (n == 1) ? 1.0 : 0.0;
  }

  const double logPdf = n * std::log((double)n) + (n - 1) * std::log(x) - n * x - std::lgamma((double)n);
  return std::exp(logPdf);
}

double goca_pfa(double alpha, int nLeading, int nTrailing)
{
  // One-sided fallback becomes standard CA-CFAR with that side's cell count.
  if (nLeading <= 0 && nTrailing <= 0)
  {
    return 1.0;
  }
  if (nLeading <= 0)
  {
    return std::pow(1.0 + alpha / nTrailing, -nTrailing);
  }
  if (nTrailing <= 0)
  {
    return std::pow(1.0 + alpha / nLeading, -nLeading);
  }

  const int nSteps = 600;
  const double tMax = 12.0;
  const double dt = tMax / nSteps;

  auto integrand = [&](double t) {
    const double e = std::exp(-alpha * t);
    const double l = erlang_pdf_scaled_mean(t, nLeading) * erlang_cdf_scaled_mean(t, nTrailing);
    const double r = erlang_pdf_scaled_mean(t, nTrailing) * erlang_cdf_scaled_mean(t, nLeading);
    return e * (l + r);
  };

  double integral = 0.0;
  double prev = integrand(0.0);
  for (int i = 1; i <= nSteps; i++)
  {
    const double t = i * dt;
    const double cur = integrand(t);
    integral += 0.5 * (prev + cur) * dt;
    prev = cur;
  }
  return integral;
}

double goca_alpha(double pfa, int nLeading, int nTrailing)
{
  if (nLeading <= 0 && nTrailing <= 0)
  {
    return 0.0;
  }
  if (nLeading <= 0)
  {
    return ca_alpha(pfa, nTrailing);
  }
  if (nTrailing <= 0)
  {
    return ca_alpha(pfa, nLeading);
  }

  double low = 0.0;
  double high = 1.0;
  while (goca_pfa(high, nLeading, nTrailing) > pfa && high < 1e6)
  {
    high *= 2.0;
  }

  for (int i = 0; i < 40; i++)
  {
    const double mid = 0.5 * (low + high);
    const double pfaMid = goca_pfa(mid, nLeading, nTrailing);
    if (pfaMid > pfa)
    {
      low = mid;
    }
    else
    {
      high = mid;
    }
  }
  return 0.5 * (low + high);
}
}

// constructor
CfarDetector1D::CfarDetector1D(double _pfa, int8_t _nGuard, int8_t _nTrain, int8_t _minDelay, double _minDoppler, CfarMode _mode)
{
  // input
  pfa = _pfa;
  nGuard = _nGuard;
  nTrain = _nTrain;
  minDelay = _minDelay;
  minDoppler = _minDoppler;
  mode = _mode;
  alphaCache.clear();
}

CfarDetector1D::~CfarDetector1D()
{
}

std::unique_ptr<Detection> CfarDetector1D::process(Map<std::complex<double>> *x)
{ 
  int32_t nDelayBins = x->get_nCols();
  int32_t nDopplerBins = x->get_nRows();
  const int guard = static_cast<int>(nGuard);
  const int train = static_cast<int>(nTrain);

  std::vector<double> mapRowSquare(nDelayBins, 0.0);
  std::vector<double> mapRowSnr(nDelayBins, 0.0);
  std::vector<double> prefixEnergy(nDelayBins + 1, 0.0);

  // store detections temporarily
  std::vector<double> delay;
  std::vector<double> doppler;
  std::vector<double> snr;

  // loop over every cell
  for (int i = 0; i < nDopplerBins; i++)
  { 
    // skip if less than min Doppler
    if (std::abs(x->doppler[i]) < minDoppler)
    {
      continue;
    }

    prefixEnergy[0] = 0.0;
    for (int j = 0; j < nDelayBins; j++)
    {
      mapRowSquare[j] = std::norm(x->data[i][j]);
      if (mapRowSquare[j] > 0.0)
      {
        mapRowSnr[j] = (double)5 * std::log10(mapRowSquare[j]) - x->noisePower;
      }
      else
      {
        mapRowSnr[j] = -std::numeric_limits<double>::infinity();
      }
      prefixEnergy[j + 1] = prefixEnergy[j] + mapRowSquare[j];
    }

    for (int j = 0; j < nDelayBins; j++)
    {
      // skip if less than min delay
      if (x->delay[j] < minDelay)
      {
        continue;
      }

      const int leadingStart = std::max(0, j - guard - train);
      const int leadingEnd = std::max(0, std::min(nDelayBins, j - guard));
      const int trailingStart = std::min(nDelayBins, j + guard + 1);
      const int trailingEnd = std::min(nDelayBins, j + guard + train + 1);

      // compute threshold
      const int nLeading = std::max(0, leadingEnd - leadingStart);
      const int nTrailing = std::max(0, trailingEnd - trailingStart);
      int nCells = 0;
      double trainNoise = 0.0;
      double alpha = 0.0;
      const double leadingNoise = (nLeading > 0) ? (prefixEnergy[leadingEnd] - prefixEnergy[leadingStart]) : 0.0;
      const double trailingNoise = (nTrailing > 0) ? (prefixEnergy[trailingEnd] - prefixEnergy[trailingStart]) : 0.0;

      if (mode == CfarMode::CA)
      {
        nCells = nLeading + nTrailing;
        if (nCells > 0)
        {
          trainNoise = (leadingNoise + trailingNoise) / nCells;
          alpha = ca_alpha(pfa, nCells);
        }
      }
      else
      {
        double leadingMean = 0.0;
        double trailingMean = 0.0;

        if (nLeading > 0)
        {
          leadingMean = leadingNoise / nLeading;
        }
        if (nTrailing > 0)
        {
          trailingMean = trailingNoise / nTrailing;
        }

        if (nLeading == 0 && nTrailing == 0)
        {
          nCells = 0;
        }
        else if (nLeading == 0)
        {
          nCells = nTrailing;
          trainNoise = trailingMean;
        }
        else if (nTrailing == 0)
        {
          nCells = nLeading;
          trainNoise = leadingMean;
        }
        else if (leadingMean >= trailingMean)
        {
          nCells = nLeading;
          trainNoise = leadingMean;
        }
        else
        {
          nCells = nTrailing;
          trainNoise = trailingMean;
        }

        if (nCells > 0)
        {
          const uint32_t key = (((uint32_t)nLeading) << 16) | (uint32_t)nTrailing;
          auto it = alphaCache.find(key);
          if (it == alphaCache.end())
          {
            alpha = goca_alpha(pfa, nLeading, nTrailing);
            alphaCache[key] = alpha;
          }
          else
          {
            alpha = it->second;
          }

          // Keep CAGO robust in the field by enforcing a conservative floor.
          // This avoids under-thresholding if numeric GOCA calibration drifts low.
          alpha = std::max(alpha, ca_alpha(pfa, nCells));
        }
      }

      if (nCells <= 0)
      {
        continue;
      }
      double threshold = alpha * trainNoise;

      // detection if over threshold
      if (mapRowSquare[j] > threshold)
      {
        delay.push_back(j + x->delay[0]);
        doppler.push_back(x->doppler[i]);
        snr.push_back(mapRowSnr[j]);
      }
    }
  }

  // create detection
  return std::make_unique<Detection>(delay, doppler, snr);
}
