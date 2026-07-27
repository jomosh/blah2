#include "SpectrumAnalyser.h"
#include <complex>
#include <iostream>
#include <vector>
#include <math.h>

// constructor
SpectrumAnalyser::SpectrumAnalyser(uint32_t _n, double _bandwidth, double _fc)
{
  // input
  n = _n;
  bandwidth = _bandwidth;
  fc = _fc;

  // compute nfft
  decimation = n/bandwidth;
  nSpectrum = n/decimation;
  nfft = nSpectrum*decimation;

  // compute FFTW plans in constructor (one for reference, one for surveillance)
  dataX = new std::complex<double>[nfft];
  fftX = fftw_plan_dft_1d(nfft, reinterpret_cast<fftw_complex *>(dataX),
                           reinterpret_cast<fftw_complex *>(dataX), FFTW_FORWARD, FFTW_ESTIMATE);

  // preallocate reusable output buffers
  spectrumBuffer.resize(nSpectrum);
  spectrumBufferSurv.resize(nSpectrum);
  frequencyBins.resize(nSpectrum);
  double offset = 0;
  if (decimation % 2 == 0)
  {
    offset = bandwidth/2;
  }
  for (uint32_t i = 0; i < nSpectrum; i++)
  {
    const int bin = static_cast<int>(i) - static_cast<int>(nSpectrum) / 2;
    frequencyBins[i] = ((bin * bandwidth) + offset + fc) / 1000;
  }

  // configure IQ decimation for constellation views
  // target ~2000 points per channel for the scatter plot
  iq_decimation = nfft / 2000;
  if (iq_decimation < 1) iq_decimation = 1;
}

SpectrumAnalyser::~SpectrumAnalyser()
{
  fftw_destroy_plan(fftX);
  delete[] dataX;
}

void SpectrumAnalyser::process(IqData *x, IqData *y)
{  
  if (x->get_length() < nfft || y->get_length() < nfft)
  {
    std::cerr << "SpectrumAnalyser requires at least " << nfft
      << " samples per channel, got " << x->get_length() << " / " << y->get_length() << std::endl;
    return;
  }

  // --- Reference channel (x) ---
  for (uint32_t i = 0; i < nfft; i++)
  {
    dataX[i] = x->at_unchecked(i);
  }
  fftw_execute(fftX);

  // fftshift + decimate in one pass for reference
  for (uint32_t i = 0; i < nSpectrum; i++)
  {
    spectrumBuffer[i] = dataX[(i * decimation + int(nfft / 2) + 1) % nfft];
  }
  x->update_spectrum(spectrumBuffer);
  x->update_frequency(frequencyBins);

  // --- Surveillance channel (y) ---
  for (uint32_t i = 0; i < nfft; i++)
  {
    dataX[i] = y->at_unchecked(i);
  }
  fftw_execute(fftX);

  // fftshift + decimate in one pass for surveillance
  for (uint32_t i = 0; i < nSpectrum; i++)
  {
    spectrumBufferSurv[i] = dataX[(i * decimation + int(nfft / 2) + 1) % nfft];
  }
  x->update_spectrum_surv(spectrumBufferSurv);

  // --- Decimated IQ samples for constellation views ---
  uint32_t nDecimated = nfft / iq_decimation;
  std::vector<double> iq_ref;
  std::vector<double> iq_surv;
  iq_ref.reserve(nDecimated * 2);
  iq_surv.reserve(nDecimated * 2);

  for (uint32_t i = 0; i < nfft; i += iq_decimation)
  {
    std::complex<double> s_ref = x->at_unchecked(i);
    std::complex<double> s_surv = y->at_unchecked(i);
    iq_ref.push_back(s_ref.real());
    iq_ref.push_back(s_ref.imag());
    iq_surv.push_back(s_surv.real());
    iq_surv.push_back(s_surv.imag());
  }

  x->update_iq_decimated(iq_ref, iq_surv);

  return;
}