/// @file TestAmbiguity.cpp
/// @brief Unit test for Ambiguity.cpp
/// @author 30hours
/// @author Dan G
/// @todo Add golden data IqData file for testing.
/// @todo Declaration match to coding style?

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "process/ambiguity/Ambiguity.h"

#include <algorithm>
#include <random>
#include <iostream>
#include <filesystem>
#include <array>

namespace
{
std::vector<std::complex<double>> make_deterministic_qpsk_sequence(uint32_t nSamples)
{
  static const std::array<std::complex<double>, 4> kSymbols = {{
    {1.0, 0.0},
    {0.0, 1.0},
    {-1.0, 0.0},
    {0.0, -1.0}
  }};

  std::mt19937 generator(123456u);
  std::uniform_int_distribution<int> symbolIndex(0, 3);
  std::vector<std::complex<double>> sequence;
  sequence.reserve(nSamples);
  for (uint32_t i = 0; i < nSamples; i++)
  {
    sequence.push_back(kSymbols[static_cast<size_t>(symbolIndex(generator))]);
  }
  return sequence;
}

void append_samples(IqData &buffer,
  const std::vector<std::complex<double>> &samples,
  uint32_t startIndex, uint32_t endIndex)
{
  for (uint32_t index = startIndex; index < endIndex; index++)
  {
    buffer.push_back(samples[index]);
  }
}

void extract_cpi_window(IqData &input, IqData &output, uint32_t nSamples)
{
  for (uint32_t i = 0; i < nSamples; i++)
  {
    output.push_back(input.pop_front());
  }
}

struct MapPeak
{
  int delayBin;
  double power;
};

MapPeak find_peak_delay_bin(const Map<std::complex<double>> &map)
{
  MapPeak peak{0, -1.0};
  for (size_t dopplerIndex = 0; dopplerIndex < map.data.size(); dopplerIndex++)
  {
    for (size_t delayIndex = 0; delayIndex < map.data[dopplerIndex].size(); delayIndex++)
    {
      const double power = std::norm(map.data[dopplerIndex][delayIndex]);
      if (power > peak.power)
      {
        peak.delayBin = map.delay[delayIndex];
        peak.power = power;
      }
    }
  }
  return peak;
}

double get_delay_bin_power(const Map<std::complex<double>> &map, int delayBin)
{
  for (size_t delayIndex = 0; delayIndex < map.delay.size(); delayIndex++)
  {
    if (map.delay[delayIndex] == delayBin)
    {
      return std::norm(map.data[0][delayIndex]);
    }
  }
  return 0.0;
}
}

/// @brief Use random_device as RNG.
std::random_device g_rd;

/// @brief Generate random IQ data.
/// @param iqData Address of IqData object.
/// @details Have to use out ref parameter because there's no copy/move ctors.
/// @return Void.
void random_iq(IqData& iq_data) {
    std::mt19937 gen(g_rd());
    std::uniform_real_distribution<> dist(-100.0, 100.0);

    for (uint32_t i = 0; i < iq_data.get_n(); ++i) {
        iq_data.push_back({dist(gen), dist(gen)});
    }
}

/// @brief Read file to IqData buffer.
/// @param buffer1 IqData buffer reference.
/// @param buffer2 IqData buffer surveillance.
/// @param file String of file name.
/// @return Void.
void read_file(IqData& buffer1, IqData& buffer2, const std::string& file)
{
  short i1, q1, i2, q2;
  auto file_replay = fopen(file.c_str(), "rb");
  if (!file_replay) {
    return;
  }

  auto read_short = [](short& v, FILE* fid) {
    auto rv{fread(&v, 1, sizeof(short), fid)};
    return rv == sizeof(short);
  };

  while (!feof(file_replay))
  {
    if (!read_short(i1, file_replay)) break;
    if (!read_short(q1, file_replay)) break;
    if (!read_short(i2, file_replay)) break;
    if (!read_short(q2, file_replay)) break;

    buffer1.push_back({(double)i1, (double)q1});
    buffer2.push_back({(double)i2, (double)q2});

    // only read for the buffer length - this class is very poorly designed
    if (buffer1.get_length() == buffer1.get_n()) {
        break;
    }
  }

  fclose(file_replay);
}

/// @brief Test constructor.
/// @details Check constructor parameters created correctly.
TEST_CASE("Constructor", "[constructor]")
{
    int32_t delayMin{-10};
    int32_t delayMax{300};
    int32_t dopplerMin{-300};
    int32_t dopplerMax{300};

    uint32_t fs{2'000'000};
    float tCpi{0.5};
    uint32_t nSamples = tCpi * fs;    // narrow on purpose

    Ambiguity ambiguity(delayMin, delayMax, dopplerMin, 
      dopplerMax, fs, nSamples);

    CHECK_THAT(ambiguity.get_cpi(), Catch::Matchers::WithinAbs(tCpi, 0.02));
    CHECK(ambiguity.get_doppler_middle() == 0);
    CHECK(ambiguity.get_n_corr() == 3322);
    CHECK(ambiguity.get_n_delay_bins() == delayMax + std::abs(delayMin) + 1);
    CHECK(ambiguity.get_n_doppler_bins() == 301);
    CHECK(ambiguity.get_nfft() == 6643);
}

/// @brief Test constructor with rounded Hamming number FFT length.
TEST_CASE("Constructor_Round", "[constructor]")
{
    int32_t delayMin{-10};
    int32_t delayMax{300};
    int32_t dopplerMin{-300};
    int32_t dopplerMax{300};

    uint32_t fs{2'000'000};
    float tCpi{0.5};
    uint32_t nSamples = tCpi * fs;    // narrow on purpose

    Ambiguity ambiguity(delayMin, delayMax, dopplerMin, 
      dopplerMax, fs, nSamples, true);

    CHECK_THAT(ambiguity.get_cpi(), Catch::Matchers::WithinAbs(tCpi, 0.02));
    CHECK(ambiguity.get_doppler_middle() == 0);
    CHECK(ambiguity.get_n_corr() == 3322);
    CHECK(ambiguity.get_n_delay_bins() == delayMax + std::abs(delayMin) + 1);
    CHECK(ambiguity.get_n_doppler_bins() == 301);
    CHECK(ambiguity.get_nfft() == 6750);
}

/// @brief Test simple ambiguity processing.
TEST_CASE("Process_Simple", "[process]")
{
    auto round_hamming = GENERATE(true, false);

    int32_t delayMin{-10};
    int32_t delayMax{300};
    int32_t dopplerMin{-300};
    int32_t dopplerMax{300};

    uint32_t fs{2'000'000};
    float tCpi{0.5};
    uint32_t nSamples = tCpi * fs;    // narrow on purpose

    Ambiguity ambiguity(delayMin, delayMax, dopplerMin, 
      dopplerMax, fs, nSamples, round_hamming);

    IqData x{nSamples};
    IqData y{nSamples};

    random_iq(x);
    random_iq(y);
    auto map{ambiguity.process(&x, &y)};
    map->set_metrics();
    CHECK(map->maxPower > 0.0);
    CHECK(map->noisePower > 0.0);
}

/// @brief Test processing from a file.
TEST_CASE("Process_File", "[process]")
{
    std::filesystem::path test_input_file("20231214-230611.rspduo");
    // Bail if the test file doesn't exist
    if (!std::filesystem::exists(test_input_file)) {
      SKIP("Input test file does not exist.");
    }
    
    auto round_hamming = GENERATE(true, false);

    int32_t delayMin{-10};
    int32_t delayMax{300};
    int32_t dopplerMin{-300};
    int32_t dopplerMax{300};

    uint32_t fs{2'000'000};
    float tCpi{0.5};
    uint32_t nSamples = tCpi * fs;    // narrow on purpose

    Ambiguity ambiguity(delayMin, delayMax, dopplerMin, 
      dopplerMax, fs, nSamples, round_hamming);
    IqData x{nSamples};
    IqData y{nSamples};

    read_file(x, y, "20231214-230611.rspduo");
    REQUIRE(x.get_length() == x.get_n());

    auto map{ambiguity.process(&x ,&y)};
    map->set_metrics();
    CHECK_THAT(map->maxPower, Catch::Matchers::WithinAbs(30.2816, 0.001));
    CHECK_THAT(map->noisePower, Catch::Matchers::WithinAbs(76.918, 0.001));
}

/// @brief Test non-zero Doppler centering applies continuously across the CPI.
TEST_CASE("Process_NonZero_Doppler_Centering", "[process]")
{
    auto round_hamming = GENERATE(true, false);

  constexpr double pi = 3.14159265358979323846;

    int32_t delayMin{0};
    int32_t delayMax{0};
    int32_t dopplerMin{0};
    int32_t dopplerMax{4};

    uint32_t fs{1000};
    uint32_t nSamples{1000};

    Ambiguity ambiguity(delayMin, delayMax, dopplerMin,
      dopplerMax, fs, nSamples, round_hamming);

    IqData x{nSamples};
    IqData y{nSamples};

    for (uint32_t i = 0; i < nSamples; i++)
    {
      const double phase = 2.0 * pi * ambiguity.get_doppler_middle() * i / fs;
      x.push_back({1.0, 0.0});
      y.push_back(std::polar(1.0, phase));
    }

    auto map{ambiguity.process(&x, &y)};
    REQUIRE(map->doppler.size() == ambiguity.get_n_doppler_bins());

    size_t peakIndex = 0;
    double peakPower = 0.0;
    for (size_t i = 0; i < map->doppler.size(); i++)
    {
      const double power = std::norm(map->data[i][0]);
      if (power > peakPower)
      {
        peakPower = power;
        peakIndex = i;
      }
    }

    CHECK(peakIndex == map->doppler.size() / 2);
    CHECK_THAT(map->doppler[peakIndex], Catch::Matchers::WithinAbs(ambiguity.get_doppler_middle(), 1e-9));
}

TEST_CASE("Process_PairedBufferSkewReducesExpectedBinPower", "[process]")
{
    constexpr uint32_t nSamples = 256;
    constexpr uint32_t sharedBufferSamples = nSamples + 1;
    const std::vector<std::complex<double>> sourceSamples =
      make_deterministic_qpsk_sequence(nSamples + 2);

    IqData alignedReferenceBuffer(sharedBufferSamples);
    IqData alignedSurveillanceBuffer(sharedBufferSamples);
    append_samples(alignedReferenceBuffer, sourceSamples, 0, nSamples);
    append_samples(alignedSurveillanceBuffer, sourceSamples, 0, nSamples);

    IqData alignedReference(nSamples);
    IqData alignedSurveillance(nSamples);
    extract_cpi_window(alignedReferenceBuffer, alignedReference, nSamples);
    extract_cpi_window(alignedSurveillanceBuffer, alignedSurveillance, nSamples);

    Ambiguity alignedAmbiguity(0, 0, 0, 0, nSamples, nSamples, false);
    Map<std::complex<double>> *alignedMap = alignedAmbiguity.process(
      &alignedReference, &alignedSurveillance);
    const double alignedPower = std::norm(alignedMap->data[0][0]);

    IqData skewedReferenceBuffer(sharedBufferSamples);
    IqData skewedSurveillanceBuffer(sharedBufferSamples);
    append_samples(skewedReferenceBuffer, sourceSamples, 0, nSamples + 2);
    append_samples(skewedSurveillanceBuffer, sourceSamples, 0, nSamples);

    IqData skewedReference(nSamples);
    IqData skewedSurveillance(nSamples);
    extract_cpi_window(skewedReferenceBuffer, skewedReference, nSamples);
    extract_cpi_window(skewedSurveillanceBuffer, skewedSurveillance, nSamples);

    Ambiguity skewedAmbiguity(0, 0, 0, 0, nSamples, nSamples, false);
    Map<std::complex<double>> *skewedMap = skewedAmbiguity.process(
      &skewedReference, &skewedSurveillance);
    const double skewedPower = std::norm(skewedMap->data[0][0]);

    CHECK(alignedPower > 0.0);
    CHECK(skewedPower > 0.0);
    CHECK(skewedPower < alignedPower * 0.1);
}

TEST_CASE("Process_PairedBufferSkewMigratesPeakAcrossDelayBins", "[process]")
{
    auto round_hamming = GENERATE(true, false);

    constexpr uint32_t nSamples = 256;
    constexpr uint32_t sharedBufferSamples = nSamples + 1;
    const std::vector<std::complex<double>> sourceSamples =
      make_deterministic_qpsk_sequence(nSamples + 2);

    IqData alignedReferenceBuffer(sharedBufferSamples);
    IqData alignedSurveillanceBuffer(sharedBufferSamples);
    append_samples(alignedReferenceBuffer, sourceSamples, 0, nSamples);
    append_samples(alignedSurveillanceBuffer, sourceSamples, 0, nSamples);

    IqData alignedReference(nSamples);
    IqData alignedSurveillance(nSamples);
    extract_cpi_window(alignedReferenceBuffer, alignedReference, nSamples);
    extract_cpi_window(alignedSurveillanceBuffer, alignedSurveillance, nSamples);

    Ambiguity alignedAmbiguity(-2, 2, 0, 0, nSamples, nSamples, round_hamming);
    Map<std::complex<double>> *alignedMap = alignedAmbiguity.process(
      &alignedReference, &alignedSurveillance);
    const MapPeak alignedPeak = find_peak_delay_bin(*alignedMap);

    IqData skewedReferenceBuffer(sharedBufferSamples);
    IqData skewedSurveillanceBuffer(sharedBufferSamples);
    append_samples(skewedReferenceBuffer, sourceSamples, 0, nSamples + 2);
    append_samples(skewedSurveillanceBuffer, sourceSamples, 0, nSamples);

    IqData skewedReference(nSamples);
    IqData skewedSurveillance(nSamples);
    extract_cpi_window(skewedReferenceBuffer, skewedReference, nSamples);
    extract_cpi_window(skewedSurveillanceBuffer, skewedSurveillance, nSamples);

    Ambiguity skewedAmbiguity(-2, 2, 0, 0, nSamples, nSamples, round_hamming);
    Map<std::complex<double>> *skewedMap = skewedAmbiguity.process(
      &skewedReference, &skewedSurveillance);
    const MapPeak skewedPeak = find_peak_delay_bin(*skewedMap);
    const double skewedZeroDelayPower = get_delay_bin_power(*skewedMap, 0);

    CHECK(alignedPeak.delayBin == 0);
    CHECK(alignedPeak.power > 0.0);
    CHECK(std::abs(skewedPeak.delayBin) == 1);
    CHECK(skewedPeak.power > alignedPeak.power * 0.95);
    CHECK(skewedZeroDelayPower < skewedPeak.power * 0.1);
}

/// @brief Pin the delay-bin mapping: a surveillance signal that is delayed by
///        D samples relative to the reference must produce a peak at exactly
///        delay bin D, for both positive and negative D.
///        This test guards the index expression in Ambiguity::process() and
///        will catch any off-by-one or sign regression.
///
///        Construction for a given D:
///          refOffset = max(0,  D),  surOffset = max(0, -D)
///          ref[i] = source[i + refOffset],  sur[i] = source[i + surOffset]
///        This keeps all source indices non-negative while ensuring
///        sur is delayed by exactly D samples relative to ref.
TEST_CASE("Process_DelayBinPin", "[process]")
{
    auto round_hamming = GENERATE(true, false);

    // nSamples only needs to be much larger than the maximum tested |D| so the
    // correlation is unambiguous.  4096 keeps plan creation and FFT cost minimal.
    constexpr uint32_t fs = 4096;
    constexpr uint32_t nSamples = 4096;
    constexpr int32_t delayMin = -5;
    constexpr int32_t delayMax = 10;

    // Source length must cover nSamples + max(abs(delayMin), abs(delayMax))
    // so i + refOffset and i + surOffset are always in range even if bounds
    // are edited independently.
    const uint32_t absDelayMin =
      (delayMin < 0) ? static_cast<uint32_t>(-delayMin) : static_cast<uint32_t>(delayMin);
    const uint32_t absDelayMax =
      (delayMax < 0) ? static_cast<uint32_t>(-delayMax) : static_cast<uint32_t>(delayMax);
    const uint32_t maxDelayAbs = std::max(absDelayMin, absDelayMax);
    const std::vector<std::complex<double>> source =
      make_deterministic_qpsk_sequence(nSamples + maxDelayAbs + 1);

    // Covers both negative delays (within delayMin) and positive delays.
    for (int32_t D : {-5, -3, -1, 0, 1, 3, 5, 10})
    {
        IqData ref(nSamples);
        IqData sur(nSamples);
        const uint32_t refOffset = (D > 0) ? static_cast<uint32_t>(D) : 0u;
        const uint32_t surOffset = (D < 0) ? static_cast<uint32_t>(-D) : 0u;
        for (uint32_t i = 0; i < nSamples; i++)
        {
            ref.push_back(source[i + refOffset]);
            sur.push_back(source[i + surOffset]);
        }

        Ambiguity amb(delayMin, delayMax, 0, 0, fs, nSamples, round_hamming);
        auto *map = amb.process(&ref, &sur);

        const MapPeak peak = find_peak_delay_bin(*map);
        INFO("D=" << D << " round_hamming=" << round_hamming
             << " peak.delayBin=" << peak.delayBin);
        CHECK(peak.delayBin == D);
    }
}

/// @brief Verify that the Hann window suppresses Doppler sidelobes.
///
/// Inject a pure tone at one Doppler frequency (the centre bin) with zero
/// delay and check that the power 2 bins away is at least 15 dB below the
/// main-lobe peak.  With a rectangular window the first sidelobe is only
/// ~13 dB down; with a Hann window it is ~31.5 dB down, so 15 dB is a
/// conservative but meaningful threshold that definitively distinguishes the
/// two cases.
///
/// We use nDopplerBins >= 5 so that bin 0 (centre after fftshift) and
/// bin ±2 are all within the map.
TEST_CASE("Process_DopplerWindowReducesSidelobes", "[process]")
{
    constexpr double pi = 3.14159265358979323846;

    // Single delay bin (delayMin=delayMax=0) to keep the test simple.
    int32_t delayMin  = 0;
    int32_t delayMax  = 0;
    int32_t dopplerMin = -2;
    int32_t dopplerMax =  2;

    uint32_t fs      = 500;
    uint32_t nSamples = 500;

    Ambiguity amb(delayMin, delayMax, dopplerMin, dopplerMax, fs, nSamples, false);
    const uint16_t nDopplerBins = amb.get_n_doppler_bins();

    // Tone exactly at the centre Doppler (dopplerMiddle).
    const double fTone = amb.get_doppler_middle();
    IqData ref(nSamples);
    IqData sur(nSamples);
    for (uint32_t i = 0; i < nSamples; i++)
    {
        ref.push_back({1.0, 0.0});
        sur.push_back(std::polar(1.0, 2.0 * pi * fTone * i / fs));
    }

    auto *map = amb.process(&ref, &sur);
    REQUIRE(map->doppler.size() == nDopplerBins);

    // Find the index of the centre Doppler bin.
    size_t centreIdx = 0;
    double peakPower = 0.0;
    for (size_t k = 0; k < nDopplerBins; k++)
    {
        const double p = std::norm(map->data[k][0]);
        if (p > peakPower) { peakPower = p; centreIdx = k; }
    }
    REQUIRE(peakPower > 0.0);

    // Check that the bin 2 steps away from centre has sidelobe suppression
    // of at least 15 dB.  This passes for a Hann window (~31 dB) but fails
    // for a rectangular window (~8 dB at +2 bins for a 5-bin map).
    const size_t sideIdx = (centreIdx + 2) % nDopplerBins;
    const double sidePower = std::norm(map->data[sideIdx][0]);
    const double suppressionDb = (sidePower > 0.0)
        ? 10.0 * std::log10(peakPower / sidePower)
        : 100.0;

    INFO("peak bin=" << centreIdx << " peak power=" << peakPower
         << " side power=" << sidePower << " suppression=" << suppressionDb << " dB");
    CHECK(suppressionDb >= 15.0);
}

/// @brief A Hann window of size 1 must not zero the output (special case).
///
/// When nDopplerBins == 1 (e.g. dopplerMin == dopplerMax == 0) the window
/// coefficient must be 1.0 so the single-bin Doppler FFT is unaffected.
TEST_CASE("Process_DopplerWindow_SingleBinPassthrough", "[process]")
{
    constexpr uint32_t nSamples = 256;
    // Single delay and Doppler bin: the map has one cell.
    Ambiguity amb(0, 0, 0, 0, nSamples, nSamples, false);
    REQUIRE(amb.get_n_doppler_bins() == 1);

    IqData ref(nSamples);
    IqData sur(nSamples);
    for (uint32_t i = 0; i < nSamples; i++)
    {
        ref.push_back({1.0, 0.0});
        sur.push_back({1.0, 0.0});
    }

    auto *map = amb.process(&ref, &sur);
    // If the window were 0 for the single bin the map power would be 0.
    CHECK(std::norm(map->data[0][0]) > 0.0);
}
