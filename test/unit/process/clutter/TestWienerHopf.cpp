/// @file TestWienerHopf.cpp
/// @brief Unit tests for WienerHopf clutter filter constructor contracts
///        and process() stability.
/// @details Covers the three branches/contracts added in the session-31-May-2026
///          pass: inverted delay range (clamped to 1 bin), equal-range (1 bin),
///          and zero-reference Cholesky failure path.
/// @author 30hours

#include <catch2/catch_test_macros.hpp>

#include "process/clutter/WienerHopf.h"
#include "data/IqData.h"

#include <complex>
#include <cmath>

namespace
{

/// Fill IqData with a unit-amplitude complex sinusoid at normalised frequency freqNorm.
void fill_sinusoid(IqData &buf, uint32_t nSamples, double freqNorm)
{
  for (uint32_t i = 0; i < nSamples; i++)
  {
    buf.push_back({std::cos(2.0 * M_PI * freqNorm * static_cast<double>(i)),
                   std::sin(2.0 * M_PI * freqNorm * static_cast<double>(i))});
  }
}

} // namespace

// ---------------------------------------------------------------------------
// Constructor contracts
// ---------------------------------------------------------------------------

TEST_CASE("WienerHopf_Constructor_InvertedRange_NoCrash", "[clutter]")
{
  // delayMax < delayMin must be clamped to delayMax=delayMin without throwing
  // or crashing.  Pre-fix this would underflow nBins (uint32_t) and attempt
  // to allocate ~4 GB of FFTW buffers.
  REQUIRE_NOTHROW(WienerHopf(-10, -20, 256));
}

TEST_CASE("WienerHopf_InvertedRange_ProcessReturnsTrue", "[clutter]")
{
  // After clamping, nBins=1.  A 1×1 positive-definite autocorrelation matrix
  // always passes Cholesky; process() must return true on a normal input.
  constexpr uint32_t nSamples = 256;
  WienerHopf filter(-10, -20, nSamples);

  IqData ref(nSamples);
  IqData sur(nSamples);
  fill_sinusoid(ref, nSamples, 0.1);
  fill_sinusoid(sur, nSamples, 0.15);

  CHECK(filter.process(&ref, &sur) == true);
}

TEST_CASE("WienerHopf_EqualRange_OneBin_ProcessReturnsTrue", "[clutter]")
{
  // delayMin == delayMax → nBins=1 inclusive.  Single-tap filter must succeed.
  constexpr uint32_t nSamples = 256;
  WienerHopf filter(5, 5, nSamples);

  IqData ref(nSamples);
  IqData sur(nSamples);
  fill_sinusoid(ref, nSamples, 0.1);
  fill_sinusoid(sur, nSamples, 0.15);

  CHECK(filter.process(&ref, &sur) == true);
}

// ---------------------------------------------------------------------------
// Stability: near-singular reference
// ---------------------------------------------------------------------------

TEST_CASE("WienerHopf_ZeroReference_ReturnsFalseNoCrash", "[clutter]")
{
  // Exactly-zero reference → autocorrelation matrix A=0, Frobenius norm=0,
  // eps=0, Cholesky fails.  process() must return false gracefully without
  // crashing or invoking UB.  This is the correct outcome: no clutter model
  // can be formed from a silent reference channel.
  constexpr uint32_t nSamples = 256;
  WienerHopf filter(-5, 5, nSamples);

  IqData ref(nSamples);
  IqData sur(nSamples);
  for (uint32_t i = 0; i < nSamples; i++)
  {
    ref.push_back({0.0, 0.0});
  }
  fill_sinusoid(sur, nSamples, 0.1);

  CHECK(filter.process(&ref, &sur) == false);
}
