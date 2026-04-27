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
