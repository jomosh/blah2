/// @file TestWienerHopf.cpp
/// @brief Unit tests for WienerHopf clutter filter constructor contracts
///        and process() stability.
/// @details Covers the three branches/contracts added in the session-31-May-2026
///          pass: inverted delay range (clamped to 1 bin), equal-range (1 bin),
///          and zero-reference Cholesky failure path.
/// @author 30hours

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "process/clutter/WienerHopf.h"
#include "data/IqData.h"

#include <complex>
#include <cmath>

namespace
{
constexpr double kPi = 3.14159265358979323846;
}

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

/// Estimate complex-tone amplitude at a normalised frequency using coherent
/// projection (DFT at an arbitrary bin location).
double estimate_tone_amplitude(const IqData &buf, uint32_t nSamples, double freqNorm)
{
  std::complex<double> acc{0.0, 0.0};
  for (uint32_t i = 0; i < nSamples; i++)
  {
    const double phase = -2.0 * kPi * freqNorm * static_cast<double>(i);
    acc += buf.at_unchecked(i) * std::polar(1.0, phase);
  }
  return std::abs(acc) / static_cast<double>(nSamples);
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

TEST_CASE("WienerHopf_CustomDiagonalLoadScale_ProcessReturnsTrue", "[clutter]")
{
  // The YAML-controlled diagonal load scale must propagate into the filter
  // constructor without changing the basic ability to process a CPI.
  constexpr uint32_t nSamples = 512;
  WienerHopf filter(-8, 8, nSamples, 1e-4);

  IqData ref(nSamples);
  IqData sur(nSamples);
  fill_sinusoid(ref, nSamples, 0.09);
  fill_sinusoid(sur, nSamples, 0.11);

  CHECK(filter.process(&ref, &sur) == true);
}

TEST_CASE("WienerHopf_DelayWindowLargerThanCpi_ClampedProcessReturnsTrue", "[clutter]")
{
  // Guard against OOB in autocorrelation/cross-correlation indexing when
  // delayMax - delayMin + 1 > nSamples.
  constexpr uint32_t nSamples = 256;
  WienerHopf filter(-300, 300, nSamples);

  IqData ref(nSamples);
  IqData sur(nSamples);
  fill_sinusoid(ref, nSamples, 0.07);
  fill_sinusoid(sur, nSamples, 0.13);

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

// ---------------------------------------------------------------------------
// Quantitative DSP behavior
// ---------------------------------------------------------------------------

TEST_CASE("WienerHopf_ClutterSuppressionAndTargetRetention", "[clutter]")
{
  // Quantitative regression guard:
  // - reference contains clutter only
  // - surveillance contains delayed clutter + weaker target tone
  // We expect WienerHopf to significantly suppress the clutter component while
  // retaining most target energy.
  // Exercise both nfilt sizing paths introduced in WienerHopf:
  // - 3990: rawNfilt is FFTW-fast already (no round-up expected)
  // - 4000: rawNfilt requires round-up to a nearby FFTW-fast size
  const uint32_t nSamples = GENERATE(3990u, 4000u);
  constexpr int32_t delayMin = -20;
  constexpr int32_t delayMax = 20;

  // Bin-aligned normalised frequencies minimise leakage in projection.
  constexpr uint32_t clutterBin = 64;
  constexpr uint32_t targetBin = 257;
  const double clutterFreq = static_cast<double>(clutterBin) / static_cast<double>(nSamples);
  const double targetFreq = static_cast<double>(targetBin) / static_cast<double>(nSamples);

  constexpr double clutterAmp = 1.0;
  constexpr double targetAmp = 0.25;
  constexpr int32_t clutterDelay = 5;

  IqData ref(nSamples);
  IqData sur(nSamples);

  for (uint32_t i = 0; i < nSamples; i++)
  {
    const double n = static_cast<double>(i);
    const std::complex<double> clutterRef = std::polar(clutterAmp, 2.0 * kPi * clutterFreq * n);
    const std::complex<double> clutterSur =
      std::polar(clutterAmp, 2.0 * kPi * clutterFreq * (n - static_cast<double>(clutterDelay)));
    const std::complex<double> targetSur = std::polar(targetAmp, 2.0 * kPi * targetFreq * n);

    ref.push_back(clutterRef);
    sur.push_back(clutterSur + targetSur);
  }

  const double clutterBefore = estimate_tone_amplitude(sur, nSamples, clutterFreq);
  const double targetBefore = estimate_tone_amplitude(sur, nSamples, targetFreq);

  WienerHopf filter(delayMin, delayMax, nSamples);
  REQUIRE(filter.process(&ref, &sur) == true);

  const double clutterAfter = estimate_tone_amplitude(sur, nSamples, clutterFreq);
  const double targetAfter = estimate_tone_amplitude(sur, nSamples, targetFreq);

  INFO("nSamples=" << nSamples);

  // Require strong clutter suppression (at least ~14 dB).
  CHECK(clutterAfter < clutterBefore * 0.2);

  // Require target not to be excessively attenuated (<= ~6 dB loss).
  CHECK(targetAfter > targetBefore * 0.5);

  // Net SIR should improve substantially.
  const double sirBefore = targetBefore / clutterBefore;
  const double sirAfter = targetAfter / clutterAfter;
  CHECK(sirAfter > sirBefore * 3.0);
}
