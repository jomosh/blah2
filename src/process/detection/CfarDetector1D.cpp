#include "CfarDetector1D.h"
#include "data/Map.h"

#include <iostream>
#include <vector>
#include <cmath>

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
      }

      if (nCells <= 0)
      {
        continue;
      }
      double alpha = nCells * (pow(pfa, -1.0 / nCells) - 1);
      double threshold = alpha * trainNoise;

      // detection if over threshold
      if (mapRowSquare[j] > threshold)
      {
        delay.push_back(j + x->delay[0]);
        doppler.push_back(x->doppler[i]);
        snr.push_back(mapRowSnr[j]);
      }
      iTrainLeading.clear();
      iTrainTrailing.clear();
    }
    mapRowSquare.clear();
    mapRowSnr.clear();
  }

  // create detection
  return std::make_unique<Detection>(delay, doppler, snr);
}
