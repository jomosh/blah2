#include "Ambiguity.h"
#include <complex>
#include <iostream>
#include <deque>
#include <vector>
#include <numeric>
#include <numbers>
#include <chrono>
#include <cmath>

// constructor
Ambiguity::Ambiguity(int32_t _delayMin, int32_t _delayMax, 
  int32_t _dopplerMin, int32_t _dopplerMax, uint32_t _fs, 
  uint32_t _n, bool _roundHamming)
{
  // init
  delayMin = _delayMin;
  delayMax = _delayMax;
  dopplerMin = _dopplerMin;
  dopplerMax = _dopplerMax;
  fs = _fs;
  nSamples = _n;
  nDelayBins = static_cast<uint16_t>(_delayMax - _delayMin + 1);
  dopplerMiddle = (_dopplerMin + _dopplerMax) / 2.0;
  
  // doppler calculations
  std::deque<double> doppler;
  double resolutionDoppler = 1.0 / (static_cast<double>(_n) / static_cast<double>(_fs));
  doppler.push_back(dopplerMiddle);
  int i = 1;
  while (dopplerMiddle + (i * resolutionDoppler) <= dopplerMax)
  {
    doppler.push_back(dopplerMiddle + (i * resolutionDoppler));
    doppler.push_front(dopplerMiddle - (i * resolutionDoppler));
    i++;
  }
  nDopplerBins = doppler.size();

  // batches constants
  nCorr = _n / nDopplerBins;
  cpi = (static_cast<double>(nCorr) * nDopplerBins) / fs;

  // update doppler bins to true cpi time
  resolutionDoppler = 1.0 / cpi;

  // create ambiguity map
  map = std::make_unique<Map<Complex>>(nDopplerBins, nDelayBins);

  // delay calculations
  map->delay.resize(nDelayBins);
  std::iota(map->delay.begin(), map->delay.end(), delayMin);

  map->doppler.push_front(dopplerMiddle);
  i = 1;
  while (map->doppler.size() < nDopplerBins)
  {
    map->doppler.push_back(dopplerMiddle + (i * resolutionDoppler));
    map->doppler.push_front(dopplerMiddle - (i * resolutionDoppler));
    i++;
  }

  // other setup
  nfft = 2 * nCorr - 1;
  if (_roundHamming) {
    nfft = next_hamming(nfft);
  }
  dataCorr.resize(2 * nDelayBins + 1);

  // compute FFTW plans in constructor
  dataXi.resize(nfft);
  dataYi.resize(nfft);
  dataZi.resize(nfft);
  dataDoppler.resize(nfft);
  dopplerPhase.resize(static_cast<size_t>(nCorr) * nDopplerBins);

  if (dopplerMiddle != 0)
  {
    const double phaseStep = 2.0 * std::numbers::pi * dopplerMiddle / fs;
    const std::complex<double> phaseInc = std::polar(1.0, phaseStep);
    std::complex<double> phasor = {1.0, 0.0};
    for (size_t k = 0; k < dopplerPhase.size(); k++)
    {
      dopplerPhase[k] = phasor;
      phasor *= phaseInc;
    }
  }
  else
  {
    for (size_t k = 0; k < dopplerPhase.size(); k++)
    {
      dopplerPhase[k] = {1.0, 0.0};
    }
  }

  fftXi = fftw_plan_dft_1d(nfft, reinterpret_cast<fftw_complex *>(dataXi.data()),
                           reinterpret_cast<fftw_complex *>(dataXi.data()), FFTW_FORWARD, FFTW_ESTIMATE);
  fftYi = fftw_plan_dft_1d(nfft, reinterpret_cast<fftw_complex *>(dataYi.data()),
                           reinterpret_cast<fftw_complex *>(dataYi.data()), FFTW_FORWARD, FFTW_ESTIMATE);
  fftZi = fftw_plan_dft_1d(nfft, reinterpret_cast<fftw_complex *>(dataZi.data()),
                           reinterpret_cast<fftw_complex *>(dataZi.data()), FFTW_BACKWARD, FFTW_ESTIMATE);
  fftDoppler = fftw_plan_dft_1d(nDopplerBins, reinterpret_cast<fftw_complex *>(dataDoppler.data()),
                                reinterpret_cast<fftw_complex *>(dataDoppler.data()), FFTW_FORWARD, FFTW_ESTIMATE);

  // Pre-compute symmetric Hann window for the Doppler FFT (one allocation,
  // zero per-CPI overhead).  For nDopplerBins==1 the window is trivially 1.
  dopplerWindow.resize(nDopplerBins);
  if (nDopplerBins == 1)
  {
    dopplerWindow[0] = 1.0;
  }
  else
  {
    for (uint16_t j = 0; j < nDopplerBins; j++)
    {
      dopplerWindow[j] = 0.5 * (1.0 - std::cos(2.0 * std::numbers::pi * j / (nDopplerBins - 1)));
    }
  }

}

Ambiguity::~Ambiguity()
{
  fftw_destroy_plan(fftXi);
  fftw_destroy_plan(fftYi);
  fftw_destroy_plan(fftZi);
  fftw_destroy_plan(fftDoppler);
}

Map<std::complex<double>> *Ambiguity::process(IqData *x, IqData *y)
{
  // range processing
  nSamples = nDopplerBins * nCorr;
  for (uint16_t i = 0; i < nDopplerBins; i++)
  {
    for (uint16_t j = 0; j < nCorr; j++)
    {
      const size_t phaseIndex = static_cast<size_t>(i) * nCorr + j;
      dataXi[j] = x->pop_front() * dopplerPhase[phaseIndex];
      dataYi[j] = y->pop_front();
    }

    for (uint16_t j = nCorr; j < nfft; j++)
    {
      dataXi[j] = {0, 0};
      dataYi[j] = {0, 0};
    }

    fftw_execute(fftXi);
    fftw_execute(fftYi);

    // compute correlation
    for (uint32_t j = 0; j < nfft; j++)
    {
      dataZi[j] = (dataYi[j] * std::conj(dataXi[j])) / (double)nfft;
    }

    fftw_execute(fftZi);

    // extract center of corr
    for (uint16_t j = 0; j < nDelayBins; j++)
    {
      dataCorr[j] = dataZi[nfft - nDelayBins + j];
    }
    for (uint16_t j = 0; j < nDelayBins + 1; j++)
    {
      dataCorr[j + nDelayBins] = dataZi[j];
    }

    // write current correlation slice directly to map row.
    // Index derivation: dataCorr[nDelayBins + lag] holds the correlation at
    // delay `lag` samples.  For bin j, lag = delayMin + j, so the correct
    // index is nDelayBins + delayMin + j.  The previous expression contained
    // a no-op "- 1 + 1" left from an unresolved debugging attempt; that has
    // been removed.  A deterministic delay-pin test in TestAmbiguity.cpp
    // guards this mapping.
    for (uint16_t j = 0; j < nDelayBins; j++)
    {
      map->data[i][j] = dataCorr[nDelayBins + delayMin + j];
    }
  }

  // doppler processing — apply pre-computed Hann window before the Doppler
  // FFT to suppress −13 dB rectangular-window sidelobes down to −31.5 dB.
  // The window is multiplied element-wise into dataDoppler (one scalar
  // multiply per bin, same cost as the assignment that was here before).
  for (uint16_t i = 0; i < nDelayBins; i++)
  {
    for (uint16_t j = 0; j < nDopplerBins; j++)
    {
      dataDoppler[j] = map->data[j][i] * dopplerWindow[j];
    }

    fftw_execute(fftDoppler);

    // write fftshifted Doppler response directly to map column
    for (uint16_t j = 0; j < nDopplerBins; j++)
    {
      map->data[j][i] = dataDoppler[(j + int(nDopplerBins / 2) + 1) % nDopplerBins];
    }
  }

  return map.get();
}

double Ambiguity::get_doppler_middle() const {
  return dopplerMiddle;
}

uint16_t Ambiguity::get_n_delay_bins() const {
  return nDelayBins;
}

uint16_t Ambiguity::get_n_doppler_bins() const {
  return nDopplerBins;
}

uint16_t Ambiguity::get_n_corr() const {
  return nCorr;
}

double Ambiguity::get_cpi() const {
  return cpi;
}

uint32_t Ambiguity::get_nfft() const {
  return nfft;
}

uint32_t Ambiguity::get_n_samples() const {
  return nSamples;
}