/// @file TestDetectionSweep.cpp
/// @brief Sweep detection parameters over replay IQ data and print ranked summaries.
/// @author 30hours

#include "data/IqData.h"
#include "data/Map.h"
#include "data/Detection.h"
#include "process/ambiguity/Ambiguity.h"
#include "process/clutter/WienerHopf.h"
#include "process/detection/Centroid.h"
#include "process/detection/CfarDetector1D.h"
#include "process/detection/Interpolate.h"

#include <ryml/ryml.hpp>
#include <ryml/ryml_std.hpp>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
struct CliOptions
{
  std::string configPath;
  std::string replayFile;
  std::vector<double> pfas;
  std::vector<double> minDopplers;
  std::vector<int> minDelays;
  std::vector<CfarMode> modes;
  uint64_t maxCpis = 0;
};

struct RuntimeConfig
{
  std::string replayFile;
  uint32_t fs = 0;
  double tCpi = 0.0;
  uint32_t nSamples = 0;
  int32_t ambiguityDelayMin = 0;
  int32_t ambiguityDelayMax = 0;
  int32_t ambiguityDopplerMin = 0;
  int32_t ambiguityDopplerMax = 0;
  bool clutterEnable = false;
  int32_t clutterDelayMin = 0;
  int32_t clutterDelayMax = 0;
  int32_t nGuard = 0;
  int32_t nTrain = 0;
  int32_t minDelay = 0;
  double pfa = 0.0;
  double minDoppler = 0.0;
  CfarMode cfarMode = CfarMode::CAGO;
  uint16_t nCentroid = 0;
};

struct SweepCase
{
  double pfa;
  double minDoppler;
  int minDelay;
  CfarMode mode;
  CfarDetector1D detector;
  uint64_t cpisProcessed = 0;
  uint64_t cpisWithDetections = 0;
  uint64_t totalDetections = 0;
  double peakSnrSum = 0.0;

  SweepCase(double _pfa, int8_t nGuard, int8_t nTrain, int _minDelay,
    double _minDoppler, CfarMode _mode)
    : pfa(_pfa),
      minDoppler(_minDoppler),
      minDelay(_minDelay),
      mode(_mode),
      detector(_pfa, nGuard, nTrain, static_cast<int8_t>(_minDelay), _minDoppler, _mode)
  {
  }

  void record(const Detection &detection)
  {
    cpisProcessed++;
    const std::vector<double> &snr = detection.get_snr();
    totalDetections += detection.get_nDetections();

    if (!snr.empty())
    {
      cpisWithDetections++;
      peakSnrSum += *std::max_element(snr.begin(), snr.end());
    }
  }

  double detection_rate() const
  {
    if (cpisProcessed == 0)
    {
      return 0.0;
    }
    return static_cast<double>(cpisWithDetections) / static_cast<double>(cpisProcessed);
  }

  double mean_detections() const
  {
    if (cpisProcessed == 0)
    {
      return 0.0;
    }
    return static_cast<double>(totalDetections) / static_cast<double>(cpisProcessed);
  }

  double mean_peak_snr() const
  {
    if (cpisWithDetections == 0)
    {
      return std::numeric_limits<double>::quiet_NaN();
    }
    return peakSnrSum / static_cast<double>(cpisWithDetections);
  }
};

std::string trim(const std::string &value)
{
  const size_t first = value.find_first_not_of(" \t\n\r");
  if (first == std::string::npos)
  {
    return "";
  }
  const size_t last = value.find_last_not_of(" \t\n\r");
  return value.substr(first, last - first + 1);
}

std::string to_upper(std::string value)
{
  std::transform(value.begin(), value.end(), value.begin(),
    [](unsigned char character) {
      return static_cast<char>(std::toupper(character));
    });
  return value;
}

template <typename T, typename Parser>
std::vector<T> parse_csv_list(const std::string &csv, Parser parser)
{
  std::vector<T> values;
  std::stringstream stream(csv);
  std::string token;
  while (std::getline(stream, token, ','))
  {
    token = trim(token);
    if (token.empty())
    {
      continue;
    }
    values.push_back(parser(token));
  }
  return values;
}

template <typename T>
void dedupe_preserve_order(std::vector<T> &values)
{
  std::vector<T> deduped;
  for (const T &value : values)
  {
    if (std::find(deduped.begin(), deduped.end(), value) == deduped.end())
    {
      deduped.push_back(value);
    }
  }
  values.swap(deduped);
}

bool try_parse_cfar_mode(const std::string &value, CfarMode &mode)
{
  const std::string normalized = to_upper(trim(value));
  if (normalized == "CA")
  {
    mode = CfarMode::CA;
    return true;
  }
  if (normalized == "CAGO")
  {
    mode = CfarMode::CAGO;
    return true;
  }
  return false;
}

std::string format_mode(CfarMode mode)
{
  return (mode == CfarMode::CAGO) ? "CAGO" : "CA";
}

void print_help()
{
  std::cout
    << "Sweep detector parameters against a Blah2 replay IQ file.\n\n"
    << "Required:\n"
    << "  --config <file.yml>        Base blah2 config file.\n\n"
    << "Optional:\n"
    << "  --replay-file <file.iq>    Override capture.replay.file from config.\n"
    << "  --pfa <csv>                Comma-separated Pfa values, e.g. 1e-5,1e-4,1e-3.\n"
    << "  --min-doppler <csv>        Comma-separated minimum Doppler thresholds in Hz.\n"
    << "  --min-delay <csv>          Comma-separated minimum delay thresholds in bins.\n"
    << "  --cfar-modes <csv>         Comma-separated modes from CA,CAGO.\n"
    << "  --max-cpis <n>             Limit replay processing to the first n CPIs.\n"
    << "  --help                     Show this help text.\n\n"
    << "Example:\n"
    << "  testDetectionSweep --config config/config.yml --replay-file /tmp/capture.iq \\\n"
    << "    --pfa 1e-5,1e-4,1e-3 --min-doppler 5,10,15 --min-delay 0,5,10 --cfar-modes CAGO\n";
}

bool parse_arguments(int argc, char **argv, CliOptions &options, int &exitCode)
{
  exitCode = 0;
  for (int i = 1; i < argc; i++)
  {
    const std::string argument = argv[i];
    const auto require_value = [&](const std::string &name) -> std::string {
      if (i + 1 >= argc)
      {
        throw std::invalid_argument("Missing value for " + name);
      }
      i++;
      return argv[i];
    };

    if (argument == "--help")
    {
      print_help();
      exitCode = 0;
      return false;
    }
    if (argument == "--config")
    {
      options.configPath = require_value(argument);
      continue;
    }
    if (argument == "--replay-file")
    {
      options.replayFile = require_value(argument);
      continue;
    }
    if (argument == "--pfa")
    {
      options.pfas = parse_csv_list<double>(require_value(argument), [](const std::string &value) {
        return std::stod(value);
      });
      continue;
    }
    if (argument == "--min-doppler")
    {
      options.minDopplers = parse_csv_list<double>(require_value(argument), [](const std::string &value) {
        return std::stod(value);
      });
      continue;
    }
    if (argument == "--min-delay")
    {
      options.minDelays = parse_csv_list<int>(require_value(argument), [](const std::string &value) {
        return std::stoi(value);
      });
      continue;
    }
    if (argument == "--cfar-modes")
    {
      options.modes = parse_csv_list<CfarMode>(require_value(argument), [](const std::string &value) {
        CfarMode mode = CfarMode::CA;
        if (!try_parse_cfar_mode(value, mode))
        {
          throw std::invalid_argument("Unsupported CFAR mode '" + value + "'");
        }
        return mode;
      });
      continue;
    }
    if (argument == "--max-cpis")
    {
      options.maxCpis = static_cast<uint64_t>(std::stoull(require_value(argument)));
      continue;
    }

    throw std::invalid_argument("Unknown argument '" + argument + "'");
  }

  if (options.configPath.empty())
  {
    std::cerr << "Missing required --config argument" << std::endl;
    print_help();
    exitCode = 1;
    return false;
  }

  dedupe_preserve_order(options.pfas);
  dedupe_preserve_order(options.minDopplers);
  dedupe_preserve_order(options.minDelays);
  dedupe_preserve_order(options.modes);
  return true;
}

std::string read_text_file(const std::string &path)
{
  std::ifstream file(path);
  if (!file.is_open())
  {
    throw std::runtime_error("Unable to open config file '" + path + "'");
  }

  std::stringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

void validate_runtime_config(const RuntimeConfig &config)
{
  if (config.replayFile.empty())
  {
    throw std::runtime_error("No replay file configured. Set capture.replay.file or pass --replay-file");
  }
  if (config.fs == 0 || !std::isfinite(config.tCpi) || config.tCpi <= 0.0)
  {
    throw std::runtime_error("Invalid capture/process config: fs and CPI must be positive finite values");
  }
  if (config.nSamples == 0)
  {
    throw std::runtime_error("Derived CPI sample count is zero");
  }
  if (config.nGuard < 0 || config.nGuard > std::numeric_limits<int8_t>::max())
  {
    throw std::runtime_error("process.detection.nGuard must fit in int8 range");
  }
  if (config.nTrain < 0 || config.nTrain > std::numeric_limits<int8_t>::max())
  {
    throw std::runtime_error("process.detection.nTrain must fit in int8 range");
  }
  if (config.minDelay < std::numeric_limits<int8_t>::min() ||
    config.minDelay > std::numeric_limits<int8_t>::max())
  {
    throw std::runtime_error("process.detection.minDelay must fit in int8 range");
  }
  if (!(config.pfa > 0.0 && config.pfa < 1.0))
  {
    throw std::runtime_error("process.detection.pfa must be between 0 and 1");
  }
}

RuntimeConfig load_runtime_config(const CliOptions &options)
{
  RuntimeConfig config;

  std::string contents = read_text_file(options.configPath);
  ryml::Tree tree = ryml::parse_in_arena(ryml::to_csubstr(contents));

  tree["capture"]["fs"] >> config.fs;
  tree["capture"]["replay"]["file"] >> config.replayFile;
  tree["process"]["data"]["cpi"] >> config.tCpi;
  tree["process"]["ambiguity"]["delayMin"] >> config.ambiguityDelayMin;
  tree["process"]["ambiguity"]["delayMax"] >> config.ambiguityDelayMax;
  tree["process"]["ambiguity"]["dopplerMin"] >> config.ambiguityDopplerMin;
  tree["process"]["ambiguity"]["dopplerMax"] >> config.ambiguityDopplerMax;
  tree["process"]["clutter"]["enable"] >> config.clutterEnable;
  tree["process"]["clutter"]["delayMin"] >> config.clutterDelayMin;
  tree["process"]["clutter"]["delayMax"] >> config.clutterDelayMax;
  tree["process"]["detection"]["pfa"] >> config.pfa;
  tree["process"]["detection"]["nGuard"] >> config.nGuard;
  tree["process"]["detection"]["nTrain"] >> config.nTrain;
  tree["process"]["detection"]["minDelay"] >> config.minDelay;
  tree["process"]["detection"]["minDoppler"] >> config.minDoppler;
  tree["process"]["detection"]["nCentroid"] >> config.nCentroid;

  std::string modeString = "CAGO";
  auto cfarModeNode = tree["process"]["detection"]["cfarMode"];
  if (cfarModeNode.valid())
  {
    cfarModeNode >> modeString;
  }
  if (!try_parse_cfar_mode(modeString, config.cfarMode))
  {
    throw std::runtime_error("Unsupported process.detection.cfarMode '" + modeString + "'");
  }

  if (!options.replayFile.empty())
  {
    config.replayFile = options.replayFile;
  }

  const double samplesPerCpi = static_cast<double>(config.fs) * config.tCpi;
  if (!std::isfinite(samplesPerCpi) || samplesPerCpi <= 0.0 ||
    samplesPerCpi > static_cast<double>(std::numeric_limits<uint32_t>::max()))
  {
    throw std::runtime_error("Invalid derived CPI sample count");
  }
  config.nSamples = static_cast<uint32_t>(samplesPerCpi);

  validate_runtime_config(config);
  return config;
}

std::vector<SweepCase> build_sweep_cases(const CliOptions &options, const RuntimeConfig &config)
{
  std::vector<double> pfas = options.pfas;
  std::vector<double> minDopplers = options.minDopplers;
  std::vector<int> minDelays = options.minDelays;
  std::vector<CfarMode> modes = options.modes;

  if (pfas.empty())
  {
    pfas.push_back(config.pfa);
  }
  if (minDopplers.empty())
  {
    minDopplers.push_back(config.minDoppler);
  }
  if (minDelays.empty())
  {
    minDelays.push_back(config.minDelay);
  }
  if (modes.empty())
  {
    modes.push_back(config.cfarMode);
  }

  for (double pfa : pfas)
  {
    if (!(pfa > 0.0 && pfa < 1.0))
    {
      throw std::runtime_error("Sweep pfa values must be between 0 and 1");
    }
  }
  for (int minDelay : minDelays)
  {
    if (minDelay < std::numeric_limits<int8_t>::min() ||
      minDelay > std::numeric_limits<int8_t>::max())
    {
      throw std::runtime_error("Sweep minDelay values must fit in int8 range");
    }
  }

  std::vector<SweepCase> sweepCases;
  for (CfarMode mode : modes)
  {
    for (double pfa : pfas)
    {
      for (double minDoppler : minDopplers)
      {
        for (int minDelay : minDelays)
        {
          sweepCases.emplace_back(pfa, static_cast<int8_t>(config.nGuard),
            static_cast<int8_t>(config.nTrain), minDelay, minDoppler, mode);
        }
      }
    }
  }

  return sweepCases;
}

bool read_blah2_iq_cpi(std::ifstream &replay, uint32_t nSamples, IqData &x, IqData &y)
{
  std::vector<int16_t> raw(static_cast<size_t>(nSamples) * 4, 0);
  const std::streamsize expectedBytes = static_cast<std::streamsize>(raw.size() * sizeof(int16_t));
  replay.read(reinterpret_cast<char *>(raw.data()), expectedBytes);

  if (replay.gcount() == 0)
  {
    return false;
  }
  if (replay.gcount() != expectedBytes)
  {
    std::cerr << "Ignoring trailing partial CPI at end of replay file" << std::endl;
    return false;
  }

  x.clear();
  y.clear();
  for (size_t index = 0; index < raw.size(); index += 4)
  {
    x.push_back({static_cast<double>(raw[index]), static_cast<double>(raw[index + 1])});
    y.push_back({static_cast<double>(raw[index + 2]), static_cast<double>(raw[index + 3])});
  }
  return true;
}

std::string format_metric(double value)
{
  if (std::isnan(value))
  {
    return "NA";
  }

  std::ostringstream stream;
  stream << std::fixed << std::setprecision(3) << value;
  return stream.str();
}

void print_summary(const RuntimeConfig &config, const std::vector<SweepCase> &sweepCases,
  uint64_t rawCpis, uint64_t analysedCpis, uint64_t skippedCpis)
{
  std::vector<const SweepCase *> ranked;
  ranked.reserve(sweepCases.size());
  for (const SweepCase &sweepCase : sweepCases)
  {
    ranked.push_back(&sweepCase);
  }

  std::sort(ranked.begin(), ranked.end(), [](const SweepCase *left, const SweepCase *right) {
    if (left->detection_rate() != right->detection_rate())
    {
      return left->detection_rate() > right->detection_rate();
    }
    if (left->mean_peak_snr() != right->mean_peak_snr())
    {
      return left->mean_peak_snr() > right->mean_peak_snr();
    }
    return left->mean_detections() > right->mean_detections();
  });

  std::cout << "Replay file      : " << config.replayFile << "\n";
  std::cout << "Raw CPIs read    : " << rawCpis << "\n";
  std::cout << "Analysed CPIs    : " << analysedCpis << "\n";
  std::cout << "Skipped CPIs     : " << skippedCpis << "\n";
  std::cout << "Sweep cases      : " << sweepCases.size() << "\n\n";

  std::cout << std::left
    << std::setw(6) << "Rank"
    << std::setw(8) << "Mode"
    << std::setw(12) << "Pfa"
    << std::setw(14) << "MinDoppHz"
    << std::setw(12) << "MinDelay"
    << std::setw(12) << "HitRate"
    << std::setw(16) << "TotalDetect"
    << std::setw(16) << "Mean/CPI"
    << std::setw(16) << "MeanPeakSnr"
    << "\n";

  size_t rank = 1;
  for (const SweepCase *sweepCase : ranked)
  {
    std::cout << std::left
      << std::setw(6) << rank
      << std::setw(8) << format_mode(sweepCase->mode)
      << std::setw(12) << format_metric(sweepCase->pfa)
      << std::setw(14) << format_metric(sweepCase->minDoppler)
      << std::setw(12) << sweepCase->minDelay
      << std::setw(12) << format_metric(sweepCase->detection_rate())
      << std::setw(16) << sweepCase->totalDetections
      << std::setw(16) << format_metric(sweepCase->mean_detections())
      << std::setw(16) << format_metric(sweepCase->mean_peak_snr())
      << "\n";
    rank++;
  }
}
}

int main(int argc, char **argv)
{
  try
  {
    CliOptions options;
    int exitCode = 0;
    if (!parse_arguments(argc, argv, options, exitCode))
    {
      return exitCode;
    }

    RuntimeConfig config = load_runtime_config(options);
    std::vector<SweepCase> sweepCases = build_sweep_cases(options, config);

    std::ifstream replay(config.replayFile, std::ios::binary);
    if (!replay.is_open())
    {
      throw std::runtime_error("Unable to open replay file '" + config.replayFile + "'");
    }

    const bool roundHamming = true;
    Ambiguity ambiguity(config.ambiguityDelayMin, config.ambiguityDelayMax,
      config.ambiguityDopplerMin, config.ambiguityDopplerMax, config.fs,
      config.nSamples, roundHamming);
    std::unique_ptr<WienerHopf> filter;
    if (config.clutterEnable)
    {
      filter = std::make_unique<WienerHopf>(config.clutterDelayMin,
        config.clutterDelayMax, config.nSamples);
    }
    Centroid centroid(config.nCentroid, config.nCentroid, 1.0 / config.tCpi);
    Interpolate interpolate(true, true);

    uint64_t rawCpis = 0;
    uint64_t analysedCpis = 0;
    uint64_t skippedCpis = 0;

    while (options.maxCpis == 0 || rawCpis < options.maxCpis)
    {
      IqData x(config.nSamples);
      IqData y(config.nSamples);
      if (!read_blah2_iq_cpi(replay, config.nSamples, x, y))
      {
        break;
      }
      rawCpis++;

      if (config.clutterEnable && !filter->process(&x, &y))
      {
        skippedCpis++;
        continue;
      }

      Map<std::complex<double>> *map = ambiguity.process(&x, &y);
      map->set_metrics();
      analysedCpis++;

      for (SweepCase &sweepCase : sweepCases)
      {
        std::unique_ptr<Detection> detectionPhase1 = sweepCase.detector.process(map);
        std::unique_ptr<Detection> detectionPhase2 = centroid.process(detectionPhase1.get());
        std::unique_ptr<Detection> detection = interpolate.process(detectionPhase2.get(), map);
        sweepCase.record(*detection);
      }
    }

    if (rawCpis == 0)
    {
      throw std::runtime_error("Replay file did not contain a full CPI for the configured sample rate and CPI length");
    }
    if (analysedCpis == 0)
    {
      throw std::runtime_error("No CPIs were analysed. Check clutter filter stability or disable clutter for the sweep");
    }

    print_summary(config, sweepCases, rawCpis, analysedCpis, skippedCpis);
    return 0;
  }
  catch (const std::exception &error)
  {
    std::cerr << "testDetectionSweep: " << error.what() << std::endl;
    return 1;
  }
}