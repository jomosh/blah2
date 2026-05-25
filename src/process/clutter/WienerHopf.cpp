#include "WienerHopf.h"
#include <algorithm>
#include <complex>
#include <iostream>
#include <limits>
#include <memory>
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
    + static_cast<uint64_t>(_nSamples);
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

  // Build allocation/plan state with local RAII buffers first. If any step
  // throws before ownership transfer, partially allocated buffers are freed.
  auto dataXLocal = std::make_unique<std::complex<double>[]>(nSamples);
  auto dataYLocal = std::make_unique<std::complex<double>[]>(nSamples);
  auto dataOutXLocal = std::make_unique<std::complex<double>[]>(nSamples);
  auto dataOutYLocal = std::make_unique<std::complex<double>[]>(nSamples);
  auto dataALocal = std::make_unique<std::complex<double>[]>(nSamples);
  auto dataBLocal = std::make_unique<std::complex<double>[]>(nSamples);
  auto filtXLocal = std::make_unique<std::complex<double>[]>(nFilterSamples);
  auto filtWLocal = std::make_unique<std::complex<double>[]>(nFilterSamples);
  auto filtLocal = std::make_unique<std::complex<double>[]>(nFilterSamples);

  fftw_plan fftXLocal = fftw_plan_dft_1d(static_cast<int>(nSamples),
    reinterpret_cast<fftw_complex *>(dataXLocal.get()),
    reinterpret_cast<fftw_complex *>(dataOutXLocal.get()), FFTW_FORWARD, FFTW_ESTIMATE);
  fftw_plan fftYLocal = fftw_plan_dft_1d(static_cast<int>(nSamples),
    reinterpret_cast<fftw_complex *>(dataYLocal.get()),
    reinterpret_cast<fftw_complex *>(dataOutYLocal.get()), FFTW_FORWARD, FFTW_ESTIMATE);
  fftw_plan fftALocal = fftw_plan_dft_1d(static_cast<int>(nSamples),
    reinterpret_cast<fftw_complex *>(dataALocal.get()),
    reinterpret_cast<fftw_complex *>(dataALocal.get()), FFTW_BACKWARD, FFTW_ESTIMATE);
  fftw_plan fftBLocal = fftw_plan_dft_1d(static_cast<int>(nSamples),
    reinterpret_cast<fftw_complex *>(dataBLocal.get()),
    reinterpret_cast<fftw_complex *>(dataBLocal.get()), FFTW_BACKWARD, FFTW_ESTIMATE);
  fftw_plan fftFiltXLocal = fftw_plan_dft_1d(static_cast<int>(nFilterSamples),
    reinterpret_cast<fftw_complex *>(filtXLocal.get()),
    reinterpret_cast<fftw_complex *>(filtXLocal.get()), FFTW_FORWARD, FFTW_ESTIMATE);
  fftw_plan fftFiltWLocal = fftw_plan_dft_1d(static_cast<int>(nFilterSamples),
    reinterpret_cast<fftw_complex *>(filtWLocal.get()),
    reinterpret_cast<fftw_complex *>(filtWLocal.get()), FFTW_FORWARD, FFTW_ESTIMATE);
  fftw_plan fftFiltLocal = fftw_plan_dft_1d(static_cast<int>(nFilterSamples),
    reinterpret_cast<fftw_complex *>(filtLocal.get()),
    reinterpret_cast<fftw_complex *>(filtLocal.get()), FFTW_BACKWARD, FFTW_ESTIMATE);

  if (fftXLocal == nullptr || fftYLocal == nullptr || fftALocal == nullptr || fftBLocal == nullptr
    || fftFiltXLocal == nullptr || fftFiltWLocal == nullptr || fftFiltLocal == nullptr)
  {
    if (fftXLocal != nullptr)
    {
      fftw_destroy_plan(fftXLocal);
    }
    if (fftYLocal != nullptr)
    {
      fftw_destroy_plan(fftYLocal);
    }
    if (fftALocal != nullptr)
    {
      fftw_destroy_plan(fftALocal);
    }
    if (fftBLocal != nullptr)
    {
      fftw_destroy_plan(fftBLocal);
    }
    if (fftFiltXLocal != nullptr)
    {
      fftw_destroy_plan(fftFiltXLocal);
    }
    if (fftFiltWLocal != nullptr)
    {
      fftw_destroy_plan(fftFiltWLocal);
    }
    if (fftFiltLocal != nullptr)
    {
      fftw_destroy_plan(fftFiltLocal);
    }

    throw std::runtime_error("WienerHopf failed to create FFTW plans.");
  }

  dataX = dataXLocal.release();
  dataY = dataYLocal.release();
  dataOutX = dataOutXLocal.release();
  dataOutY = dataOutYLocal.release();
  dataA = dataALocal.release();
  dataB = dataBLocal.release();
  filtX = filtXLocal.release();
  filtW = filtWLocal.release();
  filt = filtLocal.release();

  fftX = fftXLocal;
  fftY = fftYLocal;
  fftA = fftALocal;
  fftB = fftBLocal;
  fftFiltX = fftFiltXLocal;
  fftFiltW = fftFiltWLocal;
  fftFilt = fftFiltLocal;
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
  const int64_t startShiftedIndex = -static_cast<int64_t>(delayMin);
  int64_t wrappedIndex =
    ((startShiftedIndex % static_cast<int64_t>(nSamples))
    + static_cast<int64_t>(nSamples))
    % static_cast<int64_t>(nSamples);
  for (i = 0; i < nSamples; i++)
  {
    dataX[i] = x->at_unchecked(static_cast<uint32_t>(wrappedIndex));
    wrappedIndex++;
    if (wrappedIndex == static_cast<int64_t>(nSamples))
    {
      wrappedIndex = 0;
    }
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