#include "capture/kraken/Kraken.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <complex>
#include <vector>

struct KrakenTestAccess
{
  static auto estimate(const std::vector<std::complex<float>> &reference,
    const std::vector<std::complex<float>> &surveillance)
  {
    return Kraken::estimate_window_lag(reference, surveillance);
  }
};

namespace
{
std::vector<std::complex<float>> shift_signal(
  const std::vector<std::complex<float>> &input, int lagSamples)
{
  std::vector<std::complex<float>> shifted(input.size(), {0.0f, 0.0f});
  for (size_t i = 0; i < input.size(); i++)
  {
    const int sourceIndex = static_cast<int>(i) - lagSamples;
    if (sourceIndex >= 0 && sourceIndex < static_cast<int>(input.size()))
    {
      shifted[i] = input[static_cast<size_t>(sourceIndex)];
    }
  }
  return shifted;
}

std::vector<std::complex<float>> make_signal(size_t nSamples)
{
  std::vector<std::complex<float>> signal(nSamples, {0.0f, 0.0f});
  for (size_t i = 0; i < nSamples; i++)
  {
    const float sample = static_cast<float>(i);
    signal[i] = {
      std::sin(0.017f * sample) + 0.31f * std::cos(0.071f * sample),
      std::cos(0.023f * sample) - 0.19f * std::sin(0.053f * sample)
    };
  }
  return signal;
}
}

TEST_CASE("KrakenLagEstimatorReportsPositiveLagWhenSecondChannelStartsLater", "[capture][kraken]")
{
  const int lagSamples = 37;
  const std::vector<std::complex<float>> reference = make_signal(4096);
  const std::vector<std::complex<float>> surveillance = shift_signal(reference,
    lagSamples);

  const auto estimate = KrakenTestAccess::estimate(reference, surveillance);
  REQUIRE(estimate.valid);
  CHECK(estimate.lagSamples == lagSamples);
}

TEST_CASE("KrakenLagEstimatorReportsNegativeLagWhenSecondChannelStartsEarlier", "[capture][kraken]")
{
  const int lagSamples = -23;
  const std::vector<std::complex<float>> reference = make_signal(4096);
  const std::vector<std::complex<float>> surveillance = shift_signal(reference,
    lagSamples);

  const auto estimate = KrakenTestAccess::estimate(reference, surveillance);
  REQUIRE(estimate.valid);
  CHECK(estimate.lagSamples == lagSamples);
}