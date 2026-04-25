/// @file TestSource.cpp
/// @brief Unit tests for generic Blah2 IQ file capture/replay helpers.
/// @author 30hours

#include <catch2/catch_test_macros.hpp>

#include "capture/Source.h"
#include "data/IqData.h"

#include <complex>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{
class TestSourceDevice : public Source
{
public:
  explicit TestSourceDevice(const std::string &type = "Test")
    : Source(type, 1, 1, "", nullptr)
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
    const std::vector<std::complex<float>> &surveillance)
  {
    write_blah2_iq_samples(reference.data(), surveillance.data(), reference.size());
  }

  void close_test_file()
  {
    close_file();
  }
};
}

TEST_CASE("Replay_Blah2IqFileLoadsCanonicalFrames", "[capture][source][replay]")
{
  const std::filesystem::path filePath = std::filesystem::temp_directory_path() / "blah2-test-replay.iq";
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

  std::filesystem::remove(filePath);
}

TEST_CASE("Write_Blah2IqSamplesUsesCanonicalInterleaving", "[capture][source][save]")
{
  const std::filesystem::path filePath = std::filesystem::temp_directory_path() / "blah2-test-write.iq";
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

  std::filesystem::remove(filePath);
}