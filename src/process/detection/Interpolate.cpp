#include "Interpolate.h"
#include "NearestAxisIndex.h"

#include <iostream>
#include <vector>
#include <cmath>
#include <stdint.h>
#include <algorithm>

// constructor
Interpolate::Interpolate(bool _doDelay, bool _doDoppler)
{
  // input
  doDelay = _doDelay;
  doDoppler = _doDoppler;
}

Interpolate::~Interpolate()
{
}

std::unique_ptr<Detection> Interpolate::process(Detection *x, Map<std::complex<double>> *y)
{ 
  // store detections temporarily
  std::vector<double> delay, doppler, snr;
  delay = x->get_delay();
  doppler = x->get_doppler();
  snr = x->get_snr();

  // interpolate data
  double intDelay, intDoppler, intSnrDelay, intSnrDoppler, intSnr[3];
  std::vector<double> delay2, doppler2, snr2;
  std::deque<int> indexDelay = y->delay;
  std::deque<double> indexDoppler = y->doppler;

  if (indexDelay.empty() || indexDoppler.empty())
  {
    return std::make_unique<Detection>(delay, doppler, snr);
  }

  // loop over every detection
  for (size_t i = 0; i < snr.size(); i++)
  {
    const size_t delayIndex = detection_utility::nearest_axis_index(indexDelay, delay[i]);
    const size_t dopplerIndex = detection_utility::nearest_axis_index(indexDoppler, doppler[i]);
    const double centerDelay = static_cast<double>(indexDelay[delayIndex]);
    const double centerDoppler = indexDoppler[dopplerIndex];
    bool useDelayInterpolation = doDelay;
    bool useDopplerInterpolation = doDoppler;
    // initialise interpolated values for bool flags
    intDelay = delay[i];
    intDoppler = doppler[i];
    intSnrDelay = snr[i];
    intSnrDoppler = snr[i];
    // interpolate in delay
    if (useDelayInterpolation)
    {
      // check not on boundary
      if (delayIndex == 0 || delayIndex + 1 >= indexDelay.size())
      {
        useDelayInterpolation = false;
      }
      if (useDelayInterpolation)
      {
        intSnr[0] = (double)10*std::log10(std::abs(y->data[dopplerIndex][delayIndex - 1]))-y->noisePower;
        intSnr[1] = (double)10*std::log10(std::abs(y->data[dopplerIndex][delayIndex]))-y->noisePower;
        intSnr[2] = (double)10*std::log10(std::abs(y->data[dopplerIndex][delayIndex + 1]))-y->noisePower;
        // check detection has peak SNR of neighbours
        if (intSnr[1] < intSnr[0] || intSnr[1] < intSnr[2])
        {
            std::cout << "Detection dropped (SNR of peak lower)" << std::endl;
            continue;
        }
        intDelay = (intSnr[0]-intSnr[2])/(2*(intSnr[0]-(2*intSnr[1])+intSnr[2]));
        intSnrDelay = intSnr[1] - (((intSnr[0]-intSnr[2])*intDelay)/4);
        intDelay = centerDelay + ((indexDelay[delayIndex + 1] - indexDelay[delayIndex]) * intDelay);
      }
    }
    // interpolate in Doppler
    if (useDopplerInterpolation)
    {
      // check not on boundary
      if (dopplerIndex == 0 || dopplerIndex + 1 >= indexDoppler.size())
      {
        useDopplerInterpolation = false;
      }
      if (useDopplerInterpolation)
      {
        intSnr[0] = (double)10*std::log10(std::abs(y->data[dopplerIndex - 1][delayIndex]))-y->noisePower;
        intSnr[1] = (double)10*std::log10(std::abs(y->data[dopplerIndex][delayIndex]))-y->noisePower;
        intSnr[2] = (double)10*std::log10(std::abs(y->data[dopplerIndex + 1][delayIndex]))-y->noisePower;
        // check detection has peak SNR of neighbours
        if (intSnr[1] < intSnr[0] || intSnr[1] < intSnr[2])
        {
            continue;
        }
        intDoppler = (intSnr[0]-intSnr[2])/(2*(intSnr[0]-(2*intSnr[1])+intSnr[2]));
        intSnrDoppler = intSnr[1] - (((intSnr[0]-intSnr[2])*intDoppler)/4);
        intDoppler = centerDoppler + ((indexDoppler[dopplerIndex + 1]-indexDoppler[dopplerIndex])*intDoppler);
      }
    }
    // store interpolated detections
    delay2.push_back(intDelay);
    doppler2.push_back(intDoppler);
    snr2.push_back(std::max(std::max(intSnrDelay, intSnrDoppler), snr[i]));
  }

  // create detection
  return std::make_unique<Detection>(delay2, doppler2, snr2);
}
