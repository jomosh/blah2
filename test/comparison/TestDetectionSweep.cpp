/// @file TestDetectionSweep.cpp
/// @brief Sweep detection parameters over replay IQ data and print ranked summaries.
/// @author 30hours

#include "data/IqData.h"
#include "data/Map.h"
#include "data/Detection.h"
#include "data/meta/Constants.h"
#include "process/ambiguity/Ambiguity.h"
#include "process/clutter/WienerHopf.h"
#include "process/detection/Centroid.h"
#include "process/detection/CfarDetector1D.h"
#include "process/detection/Interpolate.h"

#include <rapidjson/document.h>

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
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <ctime>
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
  std::string adsbFile;
  double adsbDelayWindowKm = 1.0;
  double adsbDopplerWindowHz = 10.0;
  uint64_t adsbMaxAgeMs = 0;
  uint64_t captureStartMs = 0;
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
  double delayBinWidthKm = 0.0;
};

struct AdsbTarget
{
  double delayKm = 0.0;
  double dopplerHz = 0.0;
  std::string label;
};

struct AdsbSnapshot
{
  uint64_t timestampMs = 0;
  std::vector<AdsbTarget> targets;
};

struct AdsbMatchConfig
{
  double delayWindowKm = 1.0;
  double dopplerWindowHz = 10.0;
  uint64_t maxAgeMs = 0;
};

struct AdsbMatchSummary
{
  uint64_t cpisWithTruth = 0;
  uint64_t matchedDetections = 0;
  uint64_t falsePositives = 0;
  uint64_t missedTruthTargets = 0;

  uint64_t scoredDetections() const
  {
    return matchedDetections + falsePositives;
  }

  uint64_t totalTruthTargets() const
  {
    return matchedDetections + missedTruthTargets;
  }

  double match_rate() const
  {
    if (scoredDetections() == 0)
    {
      return std::numeric_limits<double>::quiet_NaN();
    }
    return static_cast<double>(matchedDetections) / static_cast<double>(scoredDetections());
  }

  double false_positive_rate() const
  {
    if (scoredDetections() == 0)
    {
      return std::numeric_limits<double>::quiet_NaN();
    }
    return static_cast<double>(falsePositives) / static_cast<double>(scoredDetections());
  }
};

struct AdsbRunContext
{
  std::string file;
  uint64_t captureStartMs = 0;
  size_t snapshotCount = 0;
  AdsbMatchConfig matchConfig;
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
  AdsbMatchSummary adsb;

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

  double adsb_match_rate() const
  {
    return adsb.match_rate();
  }

  double adsb_false_positive_rate() const
  {
    return adsb.false_positive_rate();
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
    << "  --adsb-file <file.adsb>    Optional timestamped ADS-B sidecar file for truth scoring.\n"
    << "  --adsb-delay-window-km <x> Match window in bistatic range km (default 1.0).\n"
    << "  --adsb-doppler-window-hz <x> Match window in Doppler Hz (default 10.0).\n"
    << "  --adsb-max-age-ms <n>      Max gap to nearest ADS-B snapshot before a CPI is unscored.\n"
    << "  --capture-start-ms <n>     Capture start time in POSIX ms; inferred from replay file name if omitted.\n"
    << "  --help                     Show this help text.\n\n"
    << "Example:\n"
    << "  testDetectionSweep --config config/config.yml --replay-file /tmp/capture.iq \\\n"
    << "    --pfa 1e-5,1e-4,1e-3 --min-doppler 5,10,15 --min-delay 0,5,10 --cfar-modes CAGO\n"
    << "  testDetectionSweep --config config/config.yml --replay-file /tmp/capture.iq \\\n"
    << "    --adsb-file /tmp/capture.adsb --adsb-delay-window-km 1.0 --adsb-doppler-window-hz 10\n";
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
    if (argument == "--adsb-file")
    {
      options.adsbFile = require_value(argument);
      continue;
    }
    if (argument == "--adsb-delay-window-km")
    {
      options.adsbDelayWindowKm = std::stod(require_value(argument));
      continue;
    }
    if (argument == "--adsb-doppler-window-hz")
    {
      options.adsbDopplerWindowHz = std::stod(require_value(argument));
      continue;
    }
    if (argument == "--adsb-max-age-ms")
    {
      options.adsbMaxAgeMs = static_cast<uint64_t>(std::stoull(require_value(argument)));
      continue;
    }
    if (argument == "--capture-start-ms")
    {
      options.captureStartMs = static_cast<uint64_t>(std::stoull(require_value(argument)));
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

std::string get_basename(const std::string &path)
{
  const size_t separator = path.find_last_of("/\\");
  if (separator == std::string::npos)
  {
    return path;
  }
  return path.substr(separator + 1);
}

uint64_t parse_capture_start_ms_from_replay_file(const std::string &replayFile)
{
  const std::string basename = get_basename(replayFile);
  if (basename.size() < 15)
  {
    throw std::runtime_error("Unable to infer capture start timestamp from replay file '" + replayFile + "'");
  }

  std::tm timeInfo = {};
  std::istringstream timestampStream(basename.substr(0, 15));
  timestampStream >> std::get_time(&timeInfo, "%Y%m%d-%H%M%S");
  if (timestampStream.fail())
  {
    throw std::runtime_error(
      "Unable to infer capture start timestamp from replay file '" + replayFile +
      "'. Pass --capture-start-ms explicitly.");
  }

  const std::time_t captureStart = std::mktime(&timeInfo);
  if (captureStart < 0)
  {
    throw std::runtime_error(
      "Unable to convert inferred replay timestamp for '" + replayFile + "'");
  }

  return static_cast<uint64_t>(captureStart) * 1000;
}

uint64_t absolute_diff_u64(uint64_t left, uint64_t right)
{
  return (left > right) ? (left - right) : (right - left);
}

std::vector<double> parse_adsb_axis_values(const rapidjson::Value &value, const std::string &fieldName)
{
  std::vector<double> values;
  if (value.IsNumber())
  {
    values.push_back(value.GetDouble());
    return values;
  }

  if (!value.IsArray())
  {
    throw std::runtime_error("ADS-B target field '" + fieldName + "' must be a number or array of numbers");
  }

  values.reserve(value.Size());
  for (const rapidjson::Value &entry : value.GetArray())
  {
    if (!entry.IsNumber())
    {
      throw std::runtime_error("ADS-B target field '" + fieldName + "' contains a non-numeric value");
    }
    values.push_back(entry.GetDouble());
  }
  return values;
}

void append_adsb_targets(const rapidjson::Value &target, const std::string &targetId,
  std::vector<AdsbTarget> &targets)
{
  if (!target.IsObject())
  {
    return;
  }

  const auto delayMember = target.FindMember("delay");
  const auto dopplerMember = target.FindMember("doppler");
  if (delayMember == target.MemberEnd() || dopplerMember == target.MemberEnd())
  {
    return;
  }

  std::vector<double> delays = parse_adsb_axis_values(delayMember->value, targetId + ".delay");
  std::vector<double> dopplers = parse_adsb_axis_values(dopplerMember->value, targetId + ".doppler");
  const size_t nPoints = std::min(delays.size(), dopplers.size());
  if (nPoints == 0)
  {
    return;
  }

  std::string label = targetId;
  const auto flightMember = target.FindMember("flight");
  if (flightMember != target.MemberEnd() && flightMember->value.IsString())
  {
    label = flightMember->value.GetString();
  }

  for (size_t i = 0; i < nPoints; i++)
  {
    targets.push_back({delays[i], dopplers[i], label});
  }
}

std::vector<AdsbSnapshot> load_adsb_snapshots(const std::string &path)
{
  rapidjson::Document document;
  const std::string contents = read_text_file(path);
  document.Parse(contents.c_str());
  if (document.HasParseError())
  {
    throw std::runtime_error("Unable to parse ADS-B sidecar file '" + path + "'");
  }
  if (!document.IsArray())
  {
    throw std::runtime_error("ADS-B sidecar file must be a JSON array: '" + path + "'");
  }

  std::vector<AdsbSnapshot> snapshots;
  snapshots.reserve(document.Size());
  for (const rapidjson::Value &entry : document.GetArray())
  {
    if (!entry.IsObject())
    {
      throw std::runtime_error("ADS-B sidecar entries must be objects");
    }

    const auto timestampMember = entry.FindMember("timestamp");
    const auto targetsMember = entry.FindMember("targets");
    if (timestampMember == entry.MemberEnd() || !timestampMember->value.IsUint64())
    {
      throw std::runtime_error("ADS-B sidecar entries must contain an unsigned integer timestamp");
    }
    if (targetsMember == entry.MemberEnd() || !targetsMember->value.IsObject())
    {
      throw std::runtime_error("ADS-B sidecar entries must contain a targets object");
    }

    AdsbSnapshot snapshot;
    snapshot.timestampMs = timestampMember->value.GetUint64();
    for (auto target = targetsMember->value.MemberBegin(); target != targetsMember->value.MemberEnd(); ++target)
    {
      append_adsb_targets(target->value, target->name.GetString(), snapshot.targets);
    }
    snapshots.push_back(std::move(snapshot));
  }

  std::sort(snapshots.begin(), snapshots.end(), [](const AdsbSnapshot &left, const AdsbSnapshot &right) {
    return left.timestampMs < right.timestampMs;
  });

  return snapshots;
}

const AdsbSnapshot *find_adsb_snapshot(const std::vector<AdsbSnapshot> &snapshots,
  uint64_t timestampMs, uint64_t maxAgeMs, size_t &cursor)
{
  if (snapshots.empty())
  {
    return nullptr;
  }

  if (cursor >= snapshots.size())
  {
    cursor = snapshots.size() - 1;
  }

  while (cursor + 1 < snapshots.size() && snapshots[cursor + 1].timestampMs <= timestampMs)
  {
    cursor++;
  }

  size_t bestIndex = cursor;
  uint64_t bestDistance = absolute_diff_u64(snapshots[bestIndex].timestampMs, timestampMs);
  if (cursor + 1 < snapshots.size())
  {
    const uint64_t nextDistance = absolute_diff_u64(snapshots[cursor + 1].timestampMs, timestampMs);
    if (nextDistance < bestDistance)
    {
      bestIndex = cursor + 1;
      bestDistance = nextDistance;
    }
  }

  if (bestDistance > maxAgeMs)
  {
    return nullptr;
  }

  return &snapshots[bestIndex];
}

void record_adsb_match(const Detection &detection, const AdsbSnapshot &snapshot,
  double delayBinWidthKm, const AdsbMatchConfig &config, AdsbMatchSummary &summary)
{
  summary.cpisWithTruth++;

  const std::vector<double> &detectionDelay = detection.get_delay();
  const std::vector<double> &detectionDoppler = detection.get_doppler();
  std::vector<bool> matchedTargets(snapshot.targets.size(), false);

  for (size_t i = 0; i < detection.get_nDetections(); i++)
  {
    const double delayKm = detectionDelay[i] * delayBinWidthKm;
    const double dopplerHz = detectionDoppler[i];
    int bestIndex = -1;
    double bestScore = std::numeric_limits<double>::infinity();

    for (size_t j = 0; j < snapshot.targets.size(); j++)
    {
      if (matchedTargets[j])
      {
        continue;
      }

      const double delayError = std::abs(delayKm - snapshot.targets[j].delayKm);
      const double dopplerError = std::abs(dopplerHz - snapshot.targets[j].dopplerHz);
      if (delayError > config.delayWindowKm || dopplerError > config.dopplerWindowHz)
      {
        continue;
      }

      const double delayScore = delayError / config.delayWindowKm;
      const double dopplerScore = dopplerError / config.dopplerWindowHz;
      const double score = (delayScore * delayScore) + (dopplerScore * dopplerScore);
      if (score < bestScore)
      {
        bestScore = score;
        bestIndex = static_cast<int>(j);
      }
    }

    if (bestIndex >= 0)
    {
      matchedTargets[static_cast<size_t>(bestIndex)] = true;
      summary.matchedDetections++;
    }
    else
    {
      summary.falsePositives++;
    }
  }

  for (bool matched : matchedTargets)
  {
    if (!matched)
    {
      summary.missedTruthTargets++;
    }
  }
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
  config.delayBinWidthKm = Constants::c / static_cast<double>(config.fs) / 1000.0;

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

double sortable_metric(double value)
{
  if (std::isnan(value))
  {
    return -std::numeric_limits<double>::infinity();
  }
  return value;
}

void print_summary(const RuntimeConfig &config, const std::vector<SweepCase> &sweepCases,
  uint64_t rawCpis, uint64_t analysedCpis, uint64_t skippedCpis,
  const std::optional<AdsbRunContext> &adsbContext)
{
  std::vector<const SweepCase *> ranked;
  ranked.reserve(sweepCases.size());
  for (const SweepCase &sweepCase : sweepCases)
  {
    ranked.push_back(&sweepCase);
  }

  const bool hasAdsb = adsbContext.has_value();
  std::sort(ranked.begin(), ranked.end(), [hasAdsb](const SweepCase *left, const SweepCase *right) {
    if (hasAdsb)
    {
      if (sortable_metric(left->adsb_match_rate()) != sortable_metric(right->adsb_match_rate()))
      {
        return sortable_metric(left->adsb_match_rate()) > sortable_metric(right->adsb_match_rate());
      }
      if (sortable_metric(left->adsb_false_positive_rate()) != sortable_metric(right->adsb_false_positive_rate()))
      {
        return sortable_metric(left->adsb_false_positive_rate()) < sortable_metric(right->adsb_false_positive_rate());
      }
      if (left->adsb.matchedDetections != right->adsb.matchedDetections)
      {
        return left->adsb.matchedDetections > right->adsb.matchedDetections;
      }
    }
    if (sortable_metric(left->detection_rate()) != sortable_metric(right->detection_rate()))
    {
      return sortable_metric(left->detection_rate()) > sortable_metric(right->detection_rate());
    }
    if (sortable_metric(left->mean_peak_snr()) != sortable_metric(right->mean_peak_snr()))
    {
      return sortable_metric(left->mean_peak_snr()) > sortable_metric(right->mean_peak_snr());
    }
    return left->mean_detections() > right->mean_detections();
  });

  std::cout << "Replay file      : " << config.replayFile << "\n";
  std::cout << "Raw CPIs read    : " << rawCpis << "\n";
  std::cout << "Analysed CPIs    : " << analysedCpis << "\n";
  std::cout << "Skipped CPIs     : " << skippedCpis << "\n";
  std::cout << "Sweep cases      : " << sweepCases.size() << "\n\n";

  if (hasAdsb)
  {
    std::cout << "ADS-B file       : " << adsbContext->file << "\n";
    std::cout << "ADS-B snapshots  : " << adsbContext->snapshotCount << "\n";
    std::cout << "Capture start ms : " << adsbContext->captureStartMs << "\n";
    std::cout << "ADS-B range win  : " << format_metric(adsbContext->matchConfig.delayWindowKm) << " km\n";
    std::cout << "ADS-B doppler win: " << format_metric(adsbContext->matchConfig.dopplerWindowHz) << " Hz\n";
    std::cout << "ADS-B max age ms : " << adsbContext->matchConfig.maxAgeMs << "\n\n";
  }

  std::cout << std::left
    << std::setw(6) << "Rank"
    << std::setw(8) << "Mode"
    << std::setw(12) << "Pfa"
    << std::setw(14) << "MinDoppHz"
    << std::setw(12) << "MinDelay"
    << std::setw(12) << "HitRate"
    << std::setw(16) << "TotalDetect"
    << std::setw(16) << "Mean/CPI"
    << std::setw(16) << "MeanPeakSnr";

  if (hasAdsb)
  {
    std::cout
      << std::setw(12) << "TruthCPI"
      << std::setw(12) << "MatchDet"
      << std::setw(12) << "FalsePos"
      << std::setw(12) << "MissedAdsb"
      << std::setw(12) << "MatchPct"
      << std::setw(12) << "FPRate";
  }

  std::cout
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
      << std::setw(16) << format_metric(sweepCase->mean_peak_snr());

    if (hasAdsb)
    {
      std::cout
        << std::setw(12) << sweepCase->adsb.cpisWithTruth
        << std::setw(12) << sweepCase->adsb.matchedDetections
        << std::setw(12) << sweepCase->adsb.falsePositives
        << std::setw(12) << sweepCase->adsb.missedTruthTargets
        << std::setw(12) << format_metric(sweepCase->adsb_match_rate())
        << std::setw(12) << format_metric(sweepCase->adsb_false_positive_rate());
    }

    std::cout << "\n";
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
    std::vector<AdsbSnapshot> adsbSnapshots;
    std::optional<AdsbRunContext> adsbContext;
    if (!options.adsbFile.empty())
    {
      if (!(options.adsbDelayWindowKm > 0.0) || !(options.adsbDopplerWindowHz > 0.0))
      {
        throw std::runtime_error("ADS-B match windows must be positive");
      }

      AdsbRunContext runContext;
      runContext.file = options.adsbFile;
      runContext.captureStartMs = options.captureStartMs;
      if (runContext.captureStartMs == 0)
      {
        runContext.captureStartMs = parse_capture_start_ms_from_replay_file(config.replayFile);
      }
      runContext.matchConfig.delayWindowKm = options.adsbDelayWindowKm;
      runContext.matchConfig.dopplerWindowHz = options.adsbDopplerWindowHz;
      if (options.adsbMaxAgeMs != 0)
      {
        runContext.matchConfig.maxAgeMs = options.adsbMaxAgeMs;
      }
      else
      {
        runContext.matchConfig.maxAgeMs = std::max<uint64_t>(
          2000, static_cast<uint64_t>(std::llround(config.tCpi * 1000.0)));
      }

      adsbSnapshots = load_adsb_snapshots(options.adsbFile);
      if (adsbSnapshots.empty())
      {
        throw std::runtime_error("ADS-B sidecar file did not contain any snapshots");
      }
      runContext.snapshotCount = adsbSnapshots.size();
      adsbContext = runContext;
    }

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
    size_t adsbSnapshotCursor = 0;

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

      const AdsbSnapshot *adsbSnapshot = nullptr;
      if (adsbContext.has_value())
      {
        const uint64_t cpiOffsetMs = static_cast<uint64_t>(
          std::llround(static_cast<double>(rawCpis - 1) * config.tCpi * 1000.0));
        const uint64_t cpiTimestampMs = adsbContext->captureStartMs + cpiOffsetMs;
        adsbSnapshot = find_adsb_snapshot(adsbSnapshots, cpiTimestampMs,
          adsbContext->matchConfig.maxAgeMs, adsbSnapshotCursor);
      }

      for (SweepCase &sweepCase : sweepCases)
      {
        std::unique_ptr<Detection> detectionPhase1 = sweepCase.detector.process(map);
        std::unique_ptr<Detection> detectionPhase2 = centroid.process(detectionPhase1.get());
        std::unique_ptr<Detection> detection = interpolate.process(detectionPhase2.get(), map);
        sweepCase.record(*detection);
        if (adsbSnapshot != nullptr)
        {
          record_adsb_match(*detection, *adsbSnapshot, config.delayBinWidthKm,
            adsbContext->matchConfig, sweepCase.adsb);
        }
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

    if (adsbContext.has_value() && !sweepCases.empty() && sweepCases.front().adsb.cpisWithTruth == 0)
    {
      std::cerr << "Warning: ADS-B sidecar was loaded but no analysed CPI had a nearby truth snapshot within "
        << adsbContext->matchConfig.maxAgeMs << " ms" << std::endl;
    }

    print_summary(config, sweepCases, rawCpis, analysedCpis, skippedCpis, adsbContext);
    return 0;
  }
  catch (const std::exception &error)
  {
    std::cerr << "testDetectionSweep: " << error.what() << std::endl;
    return 1;
  }
}