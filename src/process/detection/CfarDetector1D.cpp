#include "CfarDetector1D.h"
#include "data/Map.h"

#include <iostream>
#include <vector>
#include <cmath>
#include <unordered_map>
#include <algorithm>

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

  std::vector<std::complex<double>> mapRow;
  std::vector<double> mapRowSquare, mapRowSnr;

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
    mapRow = x->get_row(i);
    for (int j = 0; j < nDelayBins; j++)
    {
      mapRowSquare.push_back((double) std::abs(mapRow[j]*mapRow[j]));
      mapRowSnr.push_back((double)10 * std::log10(std::abs(mapRow[j])) - x->noisePower);
    }
    for (int j = 0; j < nDelayBins; j++)
    {
      // skip if less than min delay
      if (x->delay[j] < minDelay)
      {
        continue;
      } 
      // get train cell indices
      std::vector<int> iTrainLeading;
      std::vector<int> iTrainTrailing;
      for (int k = j-nGuard-nTrain; k < j-nGuard; k++)
      {
        if (k >= 0 && k < nDelayBins)
        {
          iTrainLeading.push_back(k);
        }
      }
      for (int k = j+nGuard+1; k < j+nGuard+nTrain+1; k++)
      {
        if (k >= 0 && k < nDelayBins)
        {
          iTrainTrailing.push_back(k);
        }
      }

      // compute threshold
      int nLeading = iTrainLeading.size();
      int nTrailing = iTrainTrailing.size();
      int nCells = 0;
      double trainNoise = 0.0;
      double alpha = 0.0;

      if (mode == CfarMode::CA)
      {
        nCells = nLeading + nTrailing;
        if (nCells > 0)
        {
          double leadingNoise = 0.0;
          double trailingNoise = 0.0;
          for (int k = 0; k < nLeading; k++)
          {
            leadingNoise += mapRowSquare[iTrainLeading[k]];
          }
          for (int k = 0; k < nTrailing; k++)
          {
            trailingNoise += mapRowSquare[iTrainTrailing[k]];
          }
          trainNoise = (leadingNoise + trailingNoise) / nCells;
          alpha = ca_alpha(pfa, nCells);
        }
      }
      else
      {
        double leadingNoise = 0.0;
        double trailingNoise = 0.0;
        double leadingMean = 0.0;
        double trailingMean = 0.0;

        for (int k = 0; k < nLeading; k++)
        {
          leadingNoise += mapRowSquare[iTrainLeading[k]];
        }
        for (int k = 0; k < nTrailing; k++)
        {
          trailingNoise += mapRowSquare[iTrainTrailing[k]];
        }

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
    mapRowSquare.clear();
    mapRowSnr.clear();
  }

  // create detection
  return std::make_unique<Detection>(delay, doppler, snr);
}
