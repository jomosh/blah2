/// @file blah2.cpp
/// @brief A real-time radar.
/// @author 30hours

#include "capture/Capture.h"
#include "data/IqData.h"
#include "data/Map.h"
#include "data/Detection.h"
#include "data/meta/Timing.h"
#include "data/Track.h"
#include "process/ambiguity/Ambiguity.h"
#include "process/clutter/WienerHopf.h"
#include "process/detection/CfarDetector1D.h"
#include "process/detection/Centroid.h"
#include "process/detection/Interpolate.h"
#include "process/spectrum/SpectrumAnalyser.h"
#include "process/tracker/Tracker.h"
#include "process/utility/Socket.h"
#include "data/meta/Constants.h"

#include <httplib.h>
#include <ryml/ryml.hpp>
#include <ryml/ryml_std.hpp> // optional header, provided for std:: interop
#include <c4/format.hpp> // needed for the examples below
#include <sys/types.h>
#include <getopt.h>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <thread>
#include <chrono>
#include <sys/time.h>
#include <signal.h>
#include <atomic>
#include <memory>
#include <iostream>

Capture *CAPTURE_POINTER = NULL;
std::unique_ptr<Socket> socket_map;
std::unique_ptr<Socket> socket_detection;
std::unique_ptr<Socket> socket_track;
std::unique_ptr<Socket> socket_timestamp;
std::unique_ptr<Socket> socket_timing;
std::unique_ptr<Socket> socket_iqdata;
std::unique_ptr<Socket> socket_adsb;

void signal_callback_handler(int signum);
void getopt_print_help();
std::string getopt_process(int argc, char **argv);
std::string ryml_get_file(const char *filename);
uint64_t current_time_ms();
uint64_t current_time_us();
void timing_helper(std::vector<std::string>& timing_name, 
  std::vector<double>& timing_time, std::vector<uint64_t>& time_us, 
  std::string name);

struct MlModelConfig
{
  std::string type = "linear";
  double threshold = 0.0;
  double weight_delay = 0.0;
  double weight_doppler = 0.0;
  double weight_snr = 0.0;
  double bias = 0.0;
};

double score_detection_sample(double delay, double doppler, double snr, const MlModelConfig &model);
std::unique_ptr<Detection> filter_detection_by_model(std::unique_ptr<Detection> detection, const MlModelConfig &model);

double score_detection_sample(double delay, double doppler, double snr, const MlModelConfig &model)
{
  return model.weight_delay * delay + model.weight_doppler * doppler + model.weight_snr * snr + model.bias;
}

std::unique_ptr<Detection> filter_detection_by_model(std::unique_ptr<Detection> detection, const MlModelConfig &model)
{
  if (!detection)
  {
    return detection;
  }

  const std::vector<double> &delays = detection->get_delay_ref();
  const std::vector<double> &dopplers = detection->get_doppler_ref();
  const std::vector<double> &snrs = detection->get_snr_ref();
  size_t count = std::min({delays.size(), dopplers.size(), snrs.size()});

  std::vector<double> filteredDelay;
  std::vector<double> filteredDoppler;
  std::vector<double> filteredSnr;
  filteredDelay.reserve(count);
  filteredDoppler.reserve(count);
  filteredSnr.reserve(count);

  for (size_t i = 0; i < count; i++)
  {
    double score = score_detection_sample(delays[i], dopplers[i], snrs[i], model);
    if (score >= model.threshold)
    {
      filteredDelay.push_back(delays[i]);
      filteredDoppler.push_back(dopplers[i]);
      filteredSnr.push_back(snrs[i]);
    }
  }

  if (filteredDelay.size() == count)
  {
    return detection;
  }

  return std::make_unique<Detection>(filteredDelay, filteredDoppler, filteredSnr);
}

int main(int argc, char **argv)
{
  // input handling
  signal(SIGTERM, signal_callback_handler);
  std::string file = getopt_process(argc, argv);
  std::ifstream filePath(file);
  if (!filePath.is_open())
  {
    std::cout << "Error: Config file does not exist." << "\n";
    exit(1);
  }

  // config handling
  std::string contents = ryml_get_file(file.c_str());
  ryml::Tree tree = ryml::parse_in_arena(ryml::to_csubstr(contents));

  // set up capture
  uint32_t fs, fc;
  uint16_t port_capture;
  std::string type, path, replayFile, ip_capture;
  bool saveIq, state, loop;
  tree["capture"]["fs"] >> fs;
  tree["capture"]["fc"] >> fc;
  tree["capture"]["device"]["type"] >> type;
  tree["save"]["iq"] >> saveIq;
  tree["save"]["path"] >> path;
  tree["capture"]["replay"]["state"] >> state;
  tree["capture"]["replay"]["loop"] >> loop;
  tree["capture"]["replay"]["file"] >> replayFile;
  tree["network"]["ip"] >> ip_capture;
  tree["network"]["ports"]["api"] >> port_capture;

  // set up socket
  sleep(2);
  uint16_t port_map, port_detection, port_timestamp, 
    port_timing, port_iqdata, port_track, port_adsb;
  std::string ip;
  tree["network"]["ports"]["map"] >> port_map;
  tree["network"]["ports"]["detection"] >> port_detection;
  tree["network"]["ports"]["track"] >> port_track;
  tree["network"]["ports"]["timestamp"] >> port_timestamp;
  tree["network"]["ports"]["timing"] >> port_timing;
  tree["network"]["ports"]["iqdata"] >> port_iqdata;
  tree["network"]["ports"]["adsb"] >> port_adsb;
  tree["network"]["ip"] >> ip;
  
  try {
    socket_map = std::make_unique<Socket>(ip, port_map);
    socket_detection = std::make_unique<Socket>(ip, port_detection);
    socket_track = std::make_unique<Socket>(ip, port_track);
    socket_timestamp = std::make_unique<Socket>(ip, port_timestamp);
    socket_timing = std::make_unique<Socket>(ip, port_timing);
    socket_iqdata = std::make_unique<Socket>(ip, port_iqdata);
    socket_adsb = std::make_unique<Socket>(ip, port_adsb);
  } catch (const std::exception& e) {
    std::cerr << "Failed to initialize socket connections: " << e.what() << "\n";
    std::cerr << "Make sure the server at " << ip << " is reachable." << "\n";
    return 1;
  }

  // set up fftw multithread
  if (fftw_init_threads() == 0)
  {
    std::cout << "Error in FFTW multithreading." << "\n";
    return -1;
  }
  fftw_plan_with_nthreads(4);

  Capture *capture = new Capture(type, fs, fc, path);
  CAPTURE_POINTER = capture;
  if (state)
  {
    capture->set_replay(loop, replayFile);
  }

  // create shared queue
  double tCpi, tBuffer;
  tree["process"]["data"]["cpi"] >> tCpi;
  tree["process"]["data"]["buffer"] >> tBuffer;
  IqData *buffer1 = new IqData((int) (tCpi*tBuffer*fs));
  IqData *buffer2 = new IqData((int) (tCpi*tBuffer*fs));

  // run capture
  std::thread t1([&]{capture->process(buffer1, buffer2, 
    tree["capture"]["device"], ip_capture, port_capture);
  });

  // set up process CPI
  uint32_t nSamples = fs * tCpi;
  IqData *x = new IqData(nSamples);
  IqData *y = new IqData(nSamples);
  Map<std::complex<double>> *map;
  std::unique_ptr<Detection> detection;
  std::unique_ptr<Detection> detection1;
  std::unique_ptr<Detection> detection2;
  std::unique_ptr<Track> track;

  // set up process ambiguity
  int32_t delayMin, delayMax;
  int32_t dopplerMin, dopplerMax;
  bool roundHamming = true;
  tree["process"]["ambiguity"]["delayMin"] >> delayMin;
  tree["process"]["ambiguity"]["delayMax"] >> delayMax;
  tree["process"]["ambiguity"]["dopplerMin"] >> dopplerMin;
  tree["process"]["ambiguity"]["dopplerMax"] >> dopplerMax;
  Ambiguity *ambiguity = new Ambiguity(delayMin, delayMax, 
    dopplerMin, dopplerMax, fs, nSamples, roundHamming);

  // set up process clutter
  int32_t delayMinClutter, delayMaxClutter;
  tree["process"]["clutter"]["delayMin"] >> delayMinClutter;
  tree["process"]["clutter"]["delayMax"] >> delayMaxClutter;
  WienerHopf *filter = new WienerHopf(delayMinClutter, delayMaxClutter, nSamples);

  // set up process detection
  double pfa, minDoppler;
  int8_t nGuard, nTrain;
  int8_t minDelay;
  tree["process"]["detection"]["pfa"] >> pfa;
  tree["process"]["detection"]["nGuard"] >> nGuard;
  tree["process"]["detection"]["nTrain"] >> nTrain;
  tree["process"]["detection"]["minDelay"] >> minDelay;
  tree["process"]["detection"]["minDoppler"] >> minDoppler;
  CfarDetector1D *cfarDetector1D = new CfarDetector1D(pfa, nGuard, nTrain, minDelay, minDoppler);
  Interpolate *interpolate = new Interpolate(true, true);

  // set up process centroid
  uint16_t nCentroid;
  tree["process"]["detection"]["nCentroid"] >> nCentroid;
  Centroid *centroid = new Centroid(nCentroid, nCentroid, 1/tCpi);

  // set up process tracker
  uint8_t m, n, nDelete;
  double maxAcc, rangeRes, lambda;
  std::string smooth;
  tree["process"]["tracker"]["initiate"]["M"] >> m;
  tree["process"]["tracker"]["initiate"]["N"] >> n;
  tree["process"]["tracker"]["delete"] >> nDelete;
  tree["process"]["tracker"]["initiate"]["maxAcc"] >> maxAcc;
  rangeRes = (double)Constants::c/fs;
  lambda = (double)Constants::c/fc;
  Tracker *tracker = new Tracker(m, n, nDelete, ambiguity->get_cpi(), maxAcc, rangeRes, lambda);

  // set up ADS-B ingestion if available
  bool isAdsb = false;
  std::string adsbHost;
  std::string tar1090;
  double rxLatitude = 0.0, rxLongitude = 0.0, rxAltitude = 0.0;
  double txLatitude = 0.0, txLongitude = 0.0, txAltitude = 0.0;
  bool isLearning = false;
  std::string learningPath;
  bool useModel = false;
  std::string modelType;
  double modelThreshold = 0.5;
  double modelWeightDelay = 0.0;
  double modelWeightDoppler = 0.0;
  double modelWeightSnr = 1.0;
  double modelBias = 0.0;
  tree["truth"]["adsb"]["enabled"] >> isAdsb;
  tree["truth"]["adsb"]["adsb2dd"] >> adsbHost;
  tree["truth"]["adsb"]["tar1090"] >> tar1090;
  tree["learning"]["enabled"] >> isLearning;
  tree["learning"]["path"] >> learningPath;
  tree["learning"]["use_model"] >> useModel;
  tree["learning"]["model"]["type"] >> modelType;
  tree["learning"]["model"]["threshold"] >> modelThreshold;
  tree["learning"]["model"]["weights"]["delay"] >> modelWeightDelay;
  tree["learning"]["model"]["weights"]["doppler"] >> modelWeightDoppler;
  tree["learning"]["model"]["weights"]["snr"] >> modelWeightSnr;
  tree["learning"]["model"]["bias"] >> modelBias;
  tree["location"]["rx"]["latitude"] >> rxLatitude;
  tree["location"]["rx"]["longitude"] >> rxLongitude;
  tree["location"]["rx"]["altitude"] >> rxAltitude;
  tree["location"]["tx"]["latitude"] >> txLatitude;
  tree["location"]["tx"]["longitude"] >> txLongitude;
  tree["location"]["tx"]["altitude"] >> txAltitude;

  // set up process spectrum analyser
  double spectrumBandwidth = 2000;
  SpectrumAnalyser *spectrumAnalyser = new SpectrumAnalyser(nSamples, spectrumBandwidth);

  // process options
  bool isClutter, isDetection, isTracker;
  tree["process"]["clutter"]["enable"] >> isClutter;
  tree["process"]["detection"]["enable"] >> isDetection;
  tree["process"]["tracker"]["enable"] >> isTracker;
  if (!isDetection)
  {
    isTracker = false;
  }

  // set up output data
  bool saveMap, saveDetection;
  tree["save"]["map"] >> saveMap;
  tree["save"]["detection"] >> saveDetection;
  std::string savePath, saveMapPath, saveDetectionPath;
  if (saveIq || saveMap || saveDetection || isLearning)
  {
    char startTimeStr[16];
    struct timeval currentTime = {0, 0};
    gettimeofday(&currentTime, NULL);
    strftime(startTimeStr, 16, "%Y%m%d-%H%M%S", localtime(&currentTime.tv_sec));
    savePath = path + startTimeStr;
  }
  std::ofstream learningLog;
  if (saveMap)
  {
    saveMapPath = savePath + ".map";
  }
  if (saveDetection)
  {
    saveDetectionPath = savePath + ".detection";
  }
  if (isLearning)
  {
    if (learningPath.empty())
    {
      learningPath = savePath + ".learning.jsonl";
    }
    else if (learningPath.back() == '/')
    {
      learningPath += "learning.jsonl";
    }
    learningLog.open(learningPath, std::ios::out | std::ios::app);
    if (!learningLog.is_open())
    {
      std::cerr << "Warning: could not open learning log file " << learningPath << "\n";
    }
    else
    {
      std::cout << "Learning mode enabled. Logging examples to " << learningPath << "\n";
    }
  }

  // set up output timing
  uint64_t tStart = current_time_ms();
  Timing *timing = new Timing(tStart);
  std::vector<std::string> timing_name;
  std::vector<double> timing_time;
  std::string jsonTiming;
  std::vector<uint64_t> time;

  MlModelConfig mlModel;
  if (!isLearning && useModel)
  {
    mlModel.type = modelType.empty() ? "linear" : modelType;
    mlModel.threshold = modelThreshold;
    mlModel.weight_delay = modelWeightDelay;
    mlModel.weight_doppler = modelWeightDoppler;
    mlModel.weight_snr = modelWeightSnr;
    mlModel.bias = modelBias;
    std::cout << "Loaded inline ML model for production scoring (type=" << mlModel.type << ", threshold=" << mlModel.threshold << ")\n";
  }

  // set up output json
  std::string mapJson, detectionJson, jsonTracker, jsonIqData, trackJson;

  // run process
  std::thread t2([&]{
      while (true)
      {
        buffer1->lock();
        buffer2->lock();
        if ((buffer1->get_length() > nSamples) && (buffer2->get_length() > nSamples))
        {
          time.push_back(current_time_us());
          // extract data from buffer
          for (uint32_t i = 0; i < nSamples; i++)
          {
            x->push_back(buffer1->pop_front());
            y->push_back(buffer2->pop_front());      
          }
          buffer1->unlock();
          buffer2->unlock();
          timing_helper(timing_name, timing_time, time, "extract_buffer");
          
          // spectrum
          spectrumAnalyser->process(x);
          timing_helper(timing_name, timing_time, time, "spectrum");
          
          // clutter filter
          if (isClutter)
          {
            if (!filter->process(x, y))
            {
              continue;
            }
            timing_helper(timing_name, timing_time, time, "clutter_filter");
          }
          
          // ambiguity process
          map = ambiguity->process(x, y);
          map->set_metrics();
          timing_helper(timing_name, timing_time, time, "ambiguity_processing");
          
          // detection process
          if (isDetection)
          {
            detection1 = cfarDetector1D->process(map);
            detection2 = centroid->process(detection1.get());
            detection = interpolate->process(detection2.get(), map);
            timing_helper(timing_name, timing_time, time, "detector");
          }

          if (!isLearning && useModel)
          {
            detection = filter_detection_by_model(std::move(detection), mlModel);
          }

          // tracker process
          if (isTracker)
          {
            track = tracker->process(detection.get(), time[0]/1000);
            timing_helper(timing_name, timing_time, time, "tracker");
          }

          // output IqData meta data
          jsonIqData = x->to_json(time[0]/1000);
          socket_iqdata->sendData(jsonIqData);

          // output map data
          mapJson = map->to_json(time[0]/1000);
          mapJson = map->delay_bin_to_km(mapJson, fs);
          if (saveMap)
          {
            map->save(mapJson, saveMapPath);
          }
          socket_map->sendData(mapJson);

          // output detection data
          if (isDetection)
          {
            detectionJson = detection->to_json(time[0]/1000);
            detectionJson = detection->delay_bin_to_km(detectionJson, fs);
            socket_detection->sendData(detectionJson);
          }
          else
          {
            detectionJson = "{}";
          }
          if (saveDetection)
          {
            detection->save(detectionJson, saveDetectionPath);
          }

          // output tracker data
          trackJson = "{}";
          if (isTracker)
          {
            jsonTracker = track->to_json(time[0]/1000, fs);
            trackJson = jsonTracker;
            socket_track->sendData(jsonTracker);
          }

          // output ADS-B truth from external source through the C++ pipeline
          std::string jsonAdsb = "{}";
          if (isAdsb && !adsbHost.empty())
          {
            httplib::Client cli(("http://" + adsbHost).c_str());
            std::ostringstream query;
            query << "/api/dd?rx=" << rxLatitude << "," << rxLongitude << "," << rxAltitude;
            query << "&tx=" << txLatitude << "," << txLongitude << "," << txAltitude;
            query << "&fc=" << (fc / 1000000.0);
            query << "&server=" << "http://" << tar1090;
            if (auto res = cli.Get(query.str().c_str()))
            {
              if (res->status == 200 && !res->body.empty())
              {
                jsonAdsb = res->body;
              }
            }
          }
          socket_adsb->sendData(jsonAdsb);

          if (isLearning && learningLog.is_open())
          {
            learningLog << "{\"timestamp\":" << (time[0]/1000)
                        << ",\"detection\":" << detectionJson
                        << ",\"track\":" << trackJson
                        << ",\"adsb\":" << jsonAdsb << "}\n";
          }

          // output radar data timer
          timing_helper(timing_name, timing_time, time, "output_radar_data");

          // cpi timer
          time.push_back(current_time_us());
          double delta_ms = (double)(time.back()-time[0]) / 1000;
          timing_name.push_back("cpi");
          timing_time.push_back(delta_ms);
          std::cout << "CPI time (ms): " << delta_ms << "\n";

          // output timing data
          timing->update(time[0]/1000, timing_time, timing_name);
          jsonTiming = timing->to_json();
          socket_timing->sendData(jsonTiming);
          timing_time.clear();
          timing_name.clear();

          // output CPI timestamp for updating data
          std::string t0_string = std::to_string(time[0]/1000);
          socket_timestamp->sendData(t0_string);
          time.clear();

        }
        else
        {
          buffer1->unlock();
          buffer2->unlock();
          // short delay to prevent tight looping
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
      }
    });
  t2.join();
  t1.join();

  return 0;
}

void signal_callback_handler(int signum) {
  std::cout << "Caught signal " << signum << "\n";
  if (CAPTURE_POINTER != nullptr)
  {
    CAPTURE_POINTER->device->kill();
  }
  else
  {
    exit(0);
  }
}

void getopt_print_help()
{
  std::cout << "--config <file.yml>: 	Set number of program\n"
               "--help:              	Show help\n";
  exit(1);
}

std::string getopt_process(int argc, char **argv)
{
  const char *const short_opts = "c:h";
  const option long_opts[] = {
      {"config", required_argument, nullptr, 'c'},
      {"help", no_argument, nullptr, 'h'},
      {nullptr, no_argument, nullptr, 0}};

  if (argc == 1)
  {
    std::cout << "Error: No arguments provided." << "\n";
    exit(1);
  }

  std::string file;

  while (true)
  {
    const auto opt = getopt_long(argc, argv, short_opts, long_opts, nullptr);

    // handle input "-", ":", etc
    if ((argc == 2) && (-1 == opt))
    {
      std::cout << "Error: No arguments provided." << "\n";
      exit(1);
    }

    if (-1 == opt)
      break;

    switch (opt)
    {
    case 'c':
      file = std::string(optarg);
      break;

    case 'h':
      getopt_print_help();

    // unrecognised option
    case '?':
      exit(1);

    default:
      break;
    }
  }

  return file;
}

std::string ryml_get_file(const char *filename)
{
  std::ifstream in(filename, std::ios::in | std::ios::binary);
  if (!in)
  {
    std::cerr << "could not open " << filename << "\n";
    exit(1);
  }
  std::ostringstream contents;
  contents << in.rdbuf();
  return contents.str();
}

uint64_t current_time_ms()
{
  // current time in POSIX ms
  return std::chrono::duration_cast<std::chrono::milliseconds>
  (std::chrono::system_clock::now().time_since_epoch()).count();
}

uint64_t current_time_us()
{
  // current time in POSIX us
  return std::chrono::duration_cast<std::chrono::microseconds>
  (std::chrono::system_clock::now().time_since_epoch()).count();
}

void timing_helper(std::vector<std::string>& timing_name, 
  std::vector<double>& timing_time, std::vector<uint64_t>& time_us, 
  std::string name)
{
  time_us.push_back(current_time_us());
  double delta_ms = (double)(time_us.back()-time_us[time_us.size()-2]) / 1000;
  timing_name.push_back(name);
  timing_time.push_back(delta_ms);
}
