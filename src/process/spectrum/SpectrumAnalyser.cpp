#include "SpectrumAnalyser.h"
#include <complex>
#include <iostream>
#include <vector>
#include <math.h>

// constructor
SpectrumAnalyser::SpectrumAnalyser(uint32_t _n, double _bandwidth)
{
  // input
  n = _n;
  bandwidth = _bandwidth;

  // compute nfft
  decimation = n/bandwidth;
  nSpectrum = n/decimation;
  nfft = nSpectrum*decimation;

  // compute FFTW plans in constructor
  dataX = new std::complex<double>[nfft];
  fftX = fftw_plan_dft_1d(nfft, reinterpret_cast<fftw_complex *>(dataX),
                           reinterpret_cast<fftw_complex *>(dataX), FFTW_FORWARD, FFTW_ESTIMATE);

  // preallocate reusable output buffers
  spectrumBuffer.resize(nSpectrum);
  frequencyBins.resize(nSpectrum);
  double offset = 0;
  if (decimation % 2 == 0)
  {
    offset = bandwidth/2;
  }
  for (uint32_t i = 0; i < nSpectrum; i++)
  {
    const int bin = static_cast<int>(i) - static_cast<int>(nSpectrum) / 2;
    frequencyBins[i] = ((bin * bandwidth) + offset + 204640000) / 1000;
  }
}

SpectrumAnalyser::~SpectrumAnalyser()
{
  fftw_destroy_plan(fftX);
}

void SpectrumAnalyser::process(IqData *x)
{  
  if (x->get_length() < nfft)
  {
    std::cerr << "SpectrumAnalyser requires at least " << nfft
      << " samples, got " << x->get_length() << std::endl;
    return;
  }

  // load data and FFT
  uint32_t i;
  for (i = 0; i < nfft; i++)
  {
    dataX[i] = x->at_unchecked(i);
  }
  fftw_execute(fftX);

  // fftshift + decimate in one pass
  for (i = 0; i < nSpectrum; i++)
  {
    spectrumBuffer[i] = dataX[(i * decimation + int(nfft / 2) + 1) % nfft];
  }
  x->update_spectrum(spectrumBuffer);
  x->update_frequency(frequencyBins);

  return;
}
