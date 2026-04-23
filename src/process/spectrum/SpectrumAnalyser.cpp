#include "SpectrumAnalyser.h"
#include <complex>
#include <iostream>
#include <deque>
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
  frequencyBins.reserve(nSpectrum);
  double offset = 0;
  if (decimation % 2 == 0)
  {
    offset = bandwidth/2;
  }
  for (int i = -((int)nSpectrum)/2; i < ((int)nSpectrum)/2; i++)
  {
    frequencyBins.push_back(((i*bandwidth)+offset+204640000)/1000);
  }
}

SpectrumAnalyser::~SpectrumAnalyser()
{
  fftw_destroy_plan(fftX);
}

void SpectrumAnalyser::process(IqData *x)
{  
  // load data and FFT
  uint32_t i;
  const std::deque<std::complex<double>> &data = x->get_data_ref();
  for (i = 0; i < nfft; i++)
  {
    dataX[i] = data[i];
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
