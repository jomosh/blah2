#include "WienerHopf.h"
#include <algorithm>
#include <complex>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

// constructor
WienerHopf::WienerHopf(int32_t _delayMin, int32_t _delayMax, uint32_t _nSamples)
{
  if (_delayMax < _delayMin)
  {
    throw std::invalid_argument("WienerHopf delayMax must be >= delayMin.");
  }
  if (_nSamples == 0)
  {
    throw std::invalid_argument("WienerHopf nSamples must be > 0.");
  }

  const int64_t binCount = static_cast<int64_t>(_delayMax)
    - static_cast<int64_t>(_delayMin) + 1;
  if (binCount <= 0
    || binCount > static_cast<int64_t>(std::numeric_limits<uint32_t>::max()))
  {
    throw std::invalid_argument("WienerHopf delay range produces invalid bin count.");
  }
  if (static_cast<uint64_t>(binCount) > static_cast<uint64_t>(_nSamples))
  {
    throw std::invalid_argument("WienerHopf requires nBins <= nSamples.");
  }

  if (_nSamples > static_cast<uint32_t>(std::numeric_limits<int>::max()))
  {
    throw std::invalid_argument("WienerHopf nSamples exceeds FFTW int length limit.");
  }

  const uint64_t filterLen64 = static_cast<uint64_t>(binCount)
    + static_cast<uint64_t>(_nSamples) + 1ULL;
  if (filterLen64 > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()))
  {
    throw std::invalid_argument("WienerHopf filter length overflows uint32_t.");
  }
  if (filterLen64 > static_cast<uint64_t>(std::numeric_limits<int>::max()))
  {
    throw std::invalid_argument("WienerHopf filter length exceeds FFTW int length limit.");
  }

  // input
  delayMin = _delayMin;
  delayMax = _delayMax;
  nBins = static_cast<uint32_t>(binCount);
  nSamples = _nSamples;
  nFilterSamples = static_cast<uint32_t>(filterLen64);

  // initialise data
  A = arma::cx_mat(nBins, nBins);
  a = arma::cx_vec(nBins);
  b = arma::cx_vec(nBins);
  w = arma::cx_vec(nBins);

  // compute FFTW plans in constructor
  dataX = new std::complex<double>[nSamples];
  dataY = new std::complex<double>[nSamples];
  dataOutX = new std::complex<double>[nSamples];
  dataOutY = new std::complex<double>[nSamples];
  dataA = new std::complex<double>[nSamples];
  dataB = new std::complex<double>[nSamples];
  filtX = new std::complex<double>[nFilterSamples];
  filtW = new std::complex<double>[nFilterSamples];
  filt = new std::complex<double>[nFilterSamples];
  fftX = fftw_plan_dft_1d(static_cast<int>(nSamples), reinterpret_cast<fftw_complex *>(dataX),
                          reinterpret_cast<fftw_complex *>(dataOutX), FFTW_FORWARD, FFTW_ESTIMATE);
  fftY = fftw_plan_dft_1d(static_cast<int>(nSamples), reinterpret_cast<fftw_complex *>(dataY),
                          reinterpret_cast<fftw_complex *>(dataOutY), FFTW_FORWARD, FFTW_ESTIMATE);
  fftA = fftw_plan_dft_1d(static_cast<int>(nSamples), reinterpret_cast<fftw_complex *>(dataA),
                          reinterpret_cast<fftw_complex *>(dataA), FFTW_BACKWARD, FFTW_ESTIMATE);
  fftB = fftw_plan_dft_1d(static_cast<int>(nSamples), reinterpret_cast<fftw_complex *>(dataB),
                          reinterpret_cast<fftw_complex *>(dataB), FFTW_BACKWARD, FFTW_ESTIMATE);
  fftFiltX = fftw_plan_dft_1d(static_cast<int>(nFilterSamples), reinterpret_cast<fftw_complex *>(filtX),
                              reinterpret_cast<fftw_complex *>(filtX), FFTW_FORWARD, FFTW_ESTIMATE);
  fftFiltW = fftw_plan_dft_1d(static_cast<int>(nFilterSamples), reinterpret_cast<fftw_complex *>(filtW),
                              reinterpret_cast<fftw_complex *>(filtW), FFTW_FORWARD, FFTW_ESTIMATE);
  fftFilt = fftw_plan_dft_1d(static_cast<int>(nFilterSamples), reinterpret_cast<fftw_complex *>(filt),
                             reinterpret_cast<fftw_complex *>(filt), FFTW_BACKWARD, FFTW_ESTIMATE);

  if (fftX == nullptr || fftY == nullptr || fftA == nullptr || fftB == nullptr
    || fftFiltX == nullptr || fftFiltW == nullptr || fftFilt == nullptr)
  {
    if (fftX != nullptr)
    {
      fftw_destroy_plan(fftX);
      fftX = nullptr;
    }
    if (fftY != nullptr)
    {
      fftw_destroy_plan(fftY);
      fftY = nullptr;
    }
    if (fftA != nullptr)
    {
      fftw_destroy_plan(fftA);
      fftA = nullptr;
    }
    if (fftB != nullptr)
    {
      fftw_destroy_plan(fftB);
      fftB = nullptr;
    }
    if (fftFiltX != nullptr)
    {
      fftw_destroy_plan(fftFiltX);
      fftFiltX = nullptr;
    }
    if (fftFiltW != nullptr)
    {
      fftw_destroy_plan(fftFiltW);
      fftFiltW = nullptr;
    }
    if (fftFilt != nullptr)
    {
      fftw_destroy_plan(fftFilt);
      fftFilt = nullptr;
    }

    delete[] dataX;
    dataX = nullptr;
    delete[] dataY;
    dataY = nullptr;
    delete[] dataOutX;
    dataOutX = nullptr;
    delete[] dataOutY;
    dataOutY = nullptr;
    delete[] dataA;
    dataA = nullptr;
    delete[] dataB;
    dataB = nullptr;
    delete[] filtX;
    filtX = nullptr;
    delete[] filtW;
    filtW = nullptr;
    delete[] filt;
    filt = nullptr;

    throw std::runtime_error("WienerHopf failed to create FFTW plans.");
  }
}

WienerHopf::~WienerHopf()
{
  if (fftX != nullptr)
  {
    fftw_destroy_plan(fftX);
  }
  if (fftY != nullptr)
  {
    fftw_destroy_plan(fftY);
  }
  if (fftA != nullptr)
  {
    fftw_destroy_plan(fftA);
  }
  if (fftB != nullptr)
  {
    fftw_destroy_plan(fftB);
  }
  if (fftFiltX != nullptr)
  {
    fftw_destroy_plan(fftFiltX);
  }
  if (fftFiltW != nullptr)
  {
    fftw_destroy_plan(fftFiltW);
  }
  if (fftFilt != nullptr)
  {
    fftw_destroy_plan(fftFilt);
  }

  delete[] dataX;
  delete[] dataY;
  delete[] dataOutX;
  delete[] dataOutY;
  delete[] dataA;
  delete[] dataB;
  delete[] filtX;
  delete[] filtW;
  delete[] filt;
}

bool WienerHopf::process(IqData *x, IqData *y)
{
  uint32_t i, j;

  if (x == nullptr || y == nullptr)
  {
    std::cerr << "WienerHopf requires non-null input buffers." << std::endl;
    return false;
  }

  if (x->get_length() < nSamples || y->get_length() < nSamples)
  {
    std::cerr << "WienerHopf requires at least " << nSamples
      << " samples in both inputs, got " << x->get_length()
      << " and " << y->get_length() << std::endl;
    return false;
  }

  // load data from ring-buffers
  for (i = 0; i < nSamples; i++)
  {
    const int64_t shiftedIndex = static_cast<int64_t>(i)
      - static_cast<int64_t>(delayMin);
    const int64_t wrappedIndex =
      ((shiftedIndex % static_cast<int64_t>(nSamples))
      + static_cast<int64_t>(nSamples))
      % static_cast<int64_t>(nSamples);
    dataX[i] = x->at_unchecked(static_cast<uint32_t>(wrappedIndex));
    dataY[i] = y->at_unchecked(i);
  }

  // pre-compute FFT of signals
  fftw_execute(fftX);
  fftw_execute(fftY);

  // auto-correlation matrix A
  for (i = 0; i < nSamples; i++)
  {
    dataA[i] = (dataOutX[i] * std::conj(dataOutX[i]));
  }
  fftw_execute(fftA);
  for (i = 0; i < nBins; i++)
  {
    a[i] = std::conj(dataA[i]) / (double)nSamples;
  }
  A = arma::toeplitz(a);

  const double diagonalLoading = std::max(1e-12,
    1e-6 * (nBins > 0 ? std::abs(a[0]) : 0.0));
  for (i = 0; i < nBins; i++)
  {
    A(i, i) += diagonalLoading;
  }

  // conjugate upper diagonal as arma does not
  for (i = 0; i < nBins; i++)
  {
    for (j = 0; j < nBins; j++)
    {
      if (i > j)
      {
        A(i, j) = std::conj(A(i, j));
      }
    }
  }

  // cross-correlation vector b
  for (i = 0; i < nSamples; i++)
  {
    dataB[i] = (dataOutY[i] * std::conj(dataOutX[i]));
  }
  fftw_execute(fftB);
  for (i = 0; i < nBins; i++)
  {
    b[i] = dataB[i] / (double)nSamples;
  }

  // compute weights
  success = arma::chol(A, A);
  if (!success)
  {
    std::cerr << "Chol decomposition failed, skip clutter filter" << std::endl;
    return false;
  }
  success = arma::solve(w, arma::trimatu(A), arma::solve(arma::trimatl(arma::trans(A)), b));
  if (!success)
  {
    std::cerr << "Solve failed, skip clutter filter" << std::endl;
    return false;
  }

  // assign and pad x
  for (i = 0; i < nSamples; i++)
  {
    filtX[i] = dataX[i];
  }
  for (i = nSamples; i < nFilterSamples; i++)
  {
    filtX[i] = {0, 0};
  }

  // assign and pad w
  for (i = 0; i < nBins; i++)
  {
    filtW[i] = w[i];
  }
  for (i = nBins; i < nFilterSamples; i++)
  {
    filtW[i] = {0, 0};
  }

  // compute fft
  fftw_execute(fftFiltX);
  fftw_execute(fftFiltW);

  // compute convolution/filter
  for (i = 0; i < nFilterSamples; i++)
  {
    filt[i] = (filtW[i] * filtX[i]);
  }
  fftw_execute(fftFilt);

  // update surveillance signal
  y->clear();
  for (i = 0; i < nSamples; i++)
  {
    y->push_back(dataY[i] - (filt[i] / (double)nFilterSamples));
  }

  return true;
}