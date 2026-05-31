/// @file TestSpectrumAnalyser.cpp
/// @brief Unit tests for SpectrumAnalyser frequency-bin axis.
/// @details Verifies that the frequency axis produced by SpectrumAnalyser
///          is centred on fc and spaced by bandwidth for multiple fc values.
///          Catches regression of the hardcoded-204.64 MHz literal removed in
///          the session-31-May-2026 pass.
/// @author 30hours

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "process/spectrum/SpectrumAnalyser.h"
#include "data/IqData.h"

#include <complex>

namespace
{

/// Choose parameters that give decimation=1 (no aliasing artefacts) so the
/// frequency bin formula is easiest to reason about in tests.
/// With n==bandwidth, decimation = n/bandwidth = 1, nSpectrum = n,
/// nfft = n, offset = 0 (decimation odd).
constexpr uint32_t kN = 1024;
constexpr double   kBandwidth = 1024.0; // Hz

/// Index of the DC bin: nSpectrum/2 = 512.
constexpr uint32_t kCenterIdx = kN / 2;

/// Expected bin spacing (kHz): bandwidth/1000 = 1.024 kHz.
constexpr double kBinSpacingKHz = kBandwidth / 1000.0;

/// Fill an IqData buffer with kN zero samples so process() does not early-out.
IqData* make_zero_iqdata()
{
  auto* buf = new IqData(kN);
  for (uint32_t i = 0; i < kN; i++)
  {
    buf->push_back({0.0, 0.0});
  }
  return buf;
}

} // namespace

TEST_CASE("SpectrumAnalyser_FrequencyBins_CenterBinAtFc", "[spectrum]")
{
  // The DC bin (index nSpectrum/2) must equal fc/1000 kHz.
  // Tested at three fc values to catch any hardcoded-literal regression.
  const double fc = GENERATE(100e6, 204640000.0, 433920000.0);

    SpectrumAnalyser analyser(kN, kBandwidth, fc);
    IqData* buf = make_zero_iqdata();
    analyser.process(buf);

    const auto &freqs = buf->get_frequency();
    REQUIRE(freqs.size() == kN);

    const double expectedKHz = fc / 1000.0;
    INFO("fc=" << fc << " center_bin=" << freqs[kCenterIdx]
      << " expected=" << expectedKHz);
    CHECK_THAT(freqs[kCenterIdx],
         Catch::Matchers::WithinRel(expectedKHz, 1e-9));
    delete buf;
}

TEST_CASE("SpectrumAnalyser_FrequencyBins_SpacingEqualsBandwidth", "[spectrum]")
{
  // Consecutive bins must be spaced by bandwidth/1000 kHz throughout the axis.
  constexpr double fc = 100e6;
    SpectrumAnalyser analyser(kN, kBandwidth, fc);
    IqData* buf = make_zero_iqdata();
    analyser.process(buf);

    const auto &freqs = buf->get_frequency();
    REQUIRE(freqs.size() == kN);

    for (uint32_t i = 1; i < kN; i++)
    {
      const double spacing = freqs[i] - freqs[i - 1];
      INFO("bin " << i << " spacing=" << spacing
        << " expected=" << kBinSpacingKHz);
      CHECK_THAT(spacing,
        Catch::Matchers::WithinRel(kBinSpacingKHz, 1e-9));
    }
    delete buf;
}

TEST_CASE("SpectrumAnalyser_FrequencyBins_AxisSymmetricAroundFc", "[spectrum]")
{
  // The bin just above DC and the bin just below DC must be equidistant from
  // fc (within floating-point tolerance), confirming the fftshift centering.
  constexpr double fc = 204640000.0;
  SpectrumAnalyser analyser(kN, kBandwidth, fc);
  IqData* buf = make_zero_iqdata();
  analyser.process(buf);

  const auto &freqs = buf->get_frequency();
  REQUIRE(freqs.size() >= kCenterIdx + 1);

  const double fcKHz = fc / 1000.0;
  const double upperOffset = freqs[kCenterIdx + 1] - fcKHz;
  const double lowerOffset = fcKHz - freqs[kCenterIdx - 1];
  INFO("upperOffset=" << upperOffset << " lowerOffset=" << lowerOffset);
  CHECK_THAT(upperOffset, Catch::Matchers::WithinRel(lowerOffset, 1e-9));
  delete buf;
}
