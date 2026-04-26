/// @file TestSource.cpp
/// @brief Unit tests for generic Blah2 IQ file capture/replay helpers.
/// @author 30hours

#include <catch2/catch_test_macros.hpp>

#include "capture/Source.h"
#include "data/IqData.h"

#include <atomic>
#include <chrono>
#include <complex>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <system_error>
#include <vector>

namespace
{
std::filesystem::path make_unique_test_path(const std::string &prefix)
{
  static std::atomic<uint64_t> uniqueId{0};
  return std::filesystem::temp_directory_path() /
    (prefix + "-" + std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count())
      + "-" + std::to_string(uniqueId.fetch_add(1, std::memory_order_relaxed))
      + ".iq");
}

class ScopedTempFile
{
private:
  std::filesystem::path filePath;

public:
  explicit ScopedTempFile(const std::string &prefix)
    : filePath(make_unique_test_path(prefix))
  {
  }

  ~ScopedTempFile()
  {
    std::error_code error;
    std::filesystem::remove(filePath, error);
  }

  const std::filesystem::path &path() const
  {
    return filePath;
  }
};

class TestSourceDevice : public Source
{
private:
  std::atomic<bool> saveIqEnabled{true};

public:
  explicit TestSourceDevice(const std::string &type = "Test",
    const std::string &path = "")
    : Source(type, 1, 1, path, &saveIqEnabled)
  {
  }

  void process(IqData *buffer1, IqData *buffer2) override
  {
    (void)buffer1;
    (void)buffer2;
  }

  void start() override
  {
  }

  void stop() override
  {
  }

  void replay(IqData *buffer1, IqData *buffer2, std::string file, bool loop) override
  {
    replay_blah2_iq_file(buffer1, buffer2, file, loop);
  }

  void open_test_file(const std::filesystem::path &path)
  {
    saveIqFile.open(path, std::ios::binary | std::ios::trunc);
  }

  void write_test_samples(const std::vector<std::complex<float>> &reference,
    const std::vector<std::complex<float>> &surveillance, double scale = 1.0)
  {
    write_blah2_iq_samples(reference.data(), surveillance.data(), reference.size(), scale);
  }

  void append_test_byte_samples(size_t channelIndex, const std::vector<int8_t> &samples)
  {
    append_blah2_paired_iq_samples(channelIndex, samples.data(), samples.size() / 2);
  }

  static size_t get_max_pending_test_samples()
  {
    return maxPendingBlah2PairedIqSamples;
  }

  void close_test_file()
  {
    close_file();
  }
};
}

TEST_CASE("Replay_Blah2IqFileLoadsCanonicalFrames", "[capture][source][replay]")
{
  const ScopedTempFile tempFile("blah2-test-replay");
  const std::filesystem::path &filePath = tempFile.path();
  const int16_t raw[] = {1, -2, 3, -4, 5, -6, 7, -8};

  {
    std::ofstream file(filePath, std::ios::binary | std::ios::trunc);
    REQUIRE(file.is_open());
    file.write(reinterpret_cast<const char *>(raw), sizeof(raw));
  }

  IqData reference(4);
  IqData surveillance(4);
  TestSourceDevice device("HackRF");
  device.replay(&reference, &surveillance, filePath.string(), false);

  REQUIRE(reference.get_length() == 2);
  REQUIRE(surveillance.get_length() == 2);
  CHECK(reference.at(0) == std::complex<double>(1.0, -2.0));
  CHECK(surveillance.at(0) == std::complex<double>(3.0, -4.0));
  CHECK(reference.at(1) == std::complex<double>(5.0, -6.0));
  CHECK(surveillance.at(1) == std::complex<double>(7.0, -8.0));
}

TEST_CASE("Replay_Blah2IqFileThrowsOnOpenFailure", "[capture][source][replay]")
{
  const ScopedTempFile tempFile("blah2-test-missing-replay");
  IqData reference(4);
  IqData surveillance(4);
  TestSourceDevice device("HackRF");

  CHECK_THROWS_AS(device.replay(&reference, &surveillance,
    tempFile.path().string(), false), std::runtime_error);
}

TEST_CASE("Open_FileThrowsOnCreateFailure", "[capture][source][save]")
{
  const std::string missingDirectoryPath =
    make_unique_test_path("blah2-test-missing-open").string() + "/";
  TestSourceDevice device("HackRF", missingDirectoryPath);

  CHECK_THROWS_AS(device.open_file(), std::runtime_error);
}

TEST_CASE("Write_Blah2IqSamplesUsesCanonicalInterleaving", "[capture][source][save]")
{
  const ScopedTempFile tempFile("blah2-test-write");
  const std::filesystem::path &filePath = tempFile.path();
  TestSourceDevice device("Usrp");
  const std::vector<std::complex<float>> reference = {
    {1.4f, -2.6f},
    {40000.0f, -40000.0f}
  };
  const std::vector<std::complex<float>> surveillance = {
    {3.5f, -4.5f},
    {9.2f, -10.2f}
  };

  device.open_test_file(filePath);
  device.write_test_samples(reference, surveillance);
  device.close_test_file();

  std::ifstream file(filePath, std::ios::binary);
  REQUIRE(file.is_open());
  std::vector<int16_t> raw(8, 0);
  file.read(reinterpret_cast<char *>(raw.data()), static_cast<std::streamsize>(raw.size() * sizeof(int16_t)));
  REQUIRE(file.gcount() == static_cast<std::streamsize>(raw.size() * sizeof(int16_t)));

  CHECK(raw[0] == 1);
  CHECK(raw[1] == -3);
  CHECK(raw[2] == 4);
  CHECK(raw[3] == -5);
  CHECK(raw[4] == 32767);
  CHECK(raw[5] == -32768);
  CHECK(raw[6] == 9);
  CHECK(raw[7] == -10);
}

TEST_CASE("Write_Blah2IqSamplesScalesNormalizedFloats", "[capture][source][save]")
{
  const ScopedTempFile tempFile("blah2-test-scaled-write");
  const std::filesystem::path &filePath = tempFile.path();
  TestSourceDevice device("Usrp");
  const std::vector<std::complex<float>> reference = {
    {0.5f, -0.5f}
  };
  const std::vector<std::complex<float>> surveillance = {
    {-1.0f, 1.0f}
  };

  device.open_test_file(filePath);
  device.write_test_samples(reference, surveillance,
    static_cast<double>(std::numeric_limits<int16_t>::max()));
  device.close_test_file();

  std::ifstream file(filePath, std::ios::binary);
  REQUIRE(file.is_open());
  std::vector<int16_t> raw(4, 0);
  file.read(reinterpret_cast<char *>(raw.data()), static_cast<std::streamsize>(raw.size() * sizeof(int16_t)));
  REQUIRE(file.gcount() == static_cast<std::streamsize>(raw.size() * sizeof(int16_t)));

  CHECK(raw[0] == 16384);
  CHECK(raw[1] == -16384);
  CHECK(raw[2] == -32767);
  CHECK(raw[3] == 32767);
}

TEST_CASE("PairedIqSamplesWriteOnlyAlignedChannelPairs", "[capture][source][save]")
{
  const ScopedTempFile tempFile("blah2-test-paired-write");
  const std::filesystem::path &filePath = tempFile.path();
  TestSourceDevice device("HackRF");

  device.open_test_file(filePath);
  device.append_test_byte_samples(0, {1, 2, 3, 4});
  device.append_test_byte_samples(1, {5, 6});
  device.append_test_byte_samples(1, {7, 8});
  device.close_test_file();

  std::ifstream file(filePath, std::ios::binary);
  REQUIRE(file.is_open());
  std::vector<int16_t> raw(8, 0);
  file.read(reinterpret_cast<char *>(raw.data()), static_cast<std::streamsize>(raw.size() * sizeof(int16_t)));
  REQUIRE(file.gcount() == static_cast<std::streamsize>(raw.size() * sizeof(int16_t)));

  CHECK(raw[0] == 1);
  CHECK(raw[1] == 2);
  CHECK(raw[2] == 5);
  CHECK(raw[3] == 6);
  CHECK(raw[4] == 3);
  CHECK(raw[5] == 4);
  CHECK(raw[6] == 7);
  CHECK(raw[7] == 8);
}

TEST_CASE("PairedIqSamplesDropOldestWhenChannelBacklogGrows", "[capture][source][save]")
{
  const ScopedTempFile tempFile("blah2-test-paired-cap");
  const std::filesystem::path &filePath = tempFile.path();
  TestSourceDevice device("Kraken");
  const size_t maxPendingSamples = TestSourceDevice::get_max_pending_test_samples();
  std::vector<int8_t> referenceSamples((maxPendingSamples + 2) * 2, 0);

  for (size_t i = 0; i < maxPendingSamples + 2; i++)
  {
    referenceSamples[2 * i] = 20;
    referenceSamples[2 * i + 1] = 21;
  }
  referenceSamples[0] = 10;
  referenceSamples[1] = 11;
  referenceSamples[2] = 12;
  referenceSamples[3] = 13;

  device.open_test_file(filePath);
  device.append_test_byte_samples(0, referenceSamples);
  device.append_test_byte_samples(1, {40, 41});
  device.close_test_file();

  std::ifstream file(filePath, std::ios::binary);
  REQUIRE(file.is_open());
  std::vector<int16_t> raw(4, 0);
  file.read(reinterpret_cast<char *>(raw.data()), static_cast<std::streamsize>(raw.size() * sizeof(int16_t)));
  REQUIRE(file.gcount() == static_cast<std::streamsize>(raw.size() * sizeof(int16_t)));

  CHECK(raw[0] == 20);
  CHECK(raw[1] == 21);
  CHECK(raw[2] == 40);
  CHECK(raw[3] == 41);
}

TEST_CASE("Close_FileClearsPendingPairedSamples", "[capture][source][save]")
{
  const ScopedTempFile firstTempFile("blah2-test-close-clear-1");
  const ScopedTempFile secondTempFile("blah2-test-close-clear-2");
  const std::filesystem::path &firstFilePath = firstTempFile.path();
  const std::filesystem::path &secondFilePath = secondTempFile.path();
  TestSourceDevice device("HackRF");

  device.open_test_file(firstFilePath);
  device.append_test_byte_samples(0, {1, 2});
  device.close_test_file();

  REQUIRE(std::filesystem::file_size(firstFilePath) == 0);

  device.open_test_file(secondFilePath);
  device.append_test_byte_samples(1, {5, 6});
  device.append_test_byte_samples(0, {7, 8});
  device.close_test_file();

  std::ifstream file(secondFilePath, std::ios::binary);
  REQUIRE(file.is_open());
  std::vector<int16_t> raw(4, 0);
  file.read(reinterpret_cast<char *>(raw.data()), static_cast<std::streamsize>(raw.size() * sizeof(int16_t)));
  REQUIRE(file.gcount() == static_cast<std::streamsize>(raw.size() * sizeof(int16_t)));

  CHECK(raw[0] == 7);
  CHECK(raw[1] == 8);
  CHECK(raw[2] == 5);
  CHECK(raw[3] == 6);
}
