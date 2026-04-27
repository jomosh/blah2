#include "Centroid.h"
#include "NearestAxisIndex.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace
{
std::string normalize_centroid_mode(std::string modeString)
{
  std::transform(modeString.begin(), modeString.end(), modeString.begin(),
    [](unsigned char value) {
      if (value == '_')
      {
        return static_cast<char>('-');
      }
      return static_cast<char>(std::tolower(value));
    });
  return modeString;
}

double linear_weight_from_snr(double snr)
{
  if (!std::isfinite(snr))
  {
    return 0.0;
  }

  const double weight = std::pow(10.0, snr / 10.0);
  if (!std::isfinite(weight) || weight <= 0.0)
  {
    return 0.0;
  }
  return weight;
}

double detection_weight(const Map<std::complex<double>> *map,
  double delay, double doppler, double snr)
{
  if (map != nullptr && !map->delay.empty() && !map->doppler.empty() &&
    !map->data.empty())
  {
    const size_t delayIndex = detection_utility::nearest_axis_index(map->delay, delay);
    const size_t dopplerIndex = detection_utility::nearest_axis_index(map->doppler, doppler);
    const double weight = std::norm(map->data[dopplerIndex][delayIndex]);
    if (std::isfinite(weight) && weight > 0.0)
    {
      return weight;
    }
  }

  const double snrWeight = linear_weight_from_snr(snr);
  if (snrWeight > 0.0)
  {
    return snrWeight;
  }

  return 1.0;
}

bool are_cluster_neighbors(double leftDelay, double leftDoppler,
  double rightDelay, double rightDoppler, uint16_t nDelay,
  double dopplerWindow)
{
  return std::abs(leftDelay - rightDelay) <= static_cast<double>(nDelay) &&
    std::abs(leftDoppler - rightDoppler) <= dopplerWindow;
}
}

bool try_parse_centroid_mode(const std::string &modeString, CentroidMode &mode)
{
  const std::string normalized = normalize_centroid_mode(modeString);
  if (normalized == "local-peak")
  {
    mode = CentroidMode::LocalPeak;
    return true;
  }
  if (normalized == "cluster-centroid")
  {
    mode = CentroidMode::ClusterCentroid;
    return true;
  }
  return false;
}

std::string format_centroid_mode(CentroidMode mode)
{
  return (mode == CentroidMode::ClusterCentroid) ? "cluster-centroid" : "local-peak";
}

// constructor
Centroid::Centroid(uint16_t _nDelay, uint16_t _nDoppler, double _resolutionDoppler,
  CentroidMode _mode)
{
  // input
  nDelay = _nDelay;
  nDoppler = _nDoppler;
  resolutionDoppler = _resolutionDoppler;
  mode = _mode;
}

Centroid::~Centroid()
{
}

std::unique_ptr<Detection> Centroid::process(Detection *x, Map<std::complex<double>> *y)
{
  const std::vector<double> &delay = x->get_delay();
  const std::vector<double> &doppler = x->get_doppler();
  const std::vector<double> &snr = x->get_snr();
  std::vector<double> delay2, doppler2, snr2;

  if (mode == CentroidMode::LocalPeak)
  {
    const double delayWindow = static_cast<double>(nDelay);
    const double dopplerWindow = static_cast<double>(nDoppler) * resolutionDoppler;
    bool isCentroid;

    for (size_t i = 0; i < snr.size(); i++)
    {
      isCentroid = true;

      for (size_t j = 0; j < snr.size(); j++)
      {
        if (j == i)
        {
          continue;
        }
        if (std::abs(delay[j] - delay[i]) <= delayWindow &&
          std::abs(doppler[j] - doppler[i]) <= dopplerWindow)
        {
          if (snr[i] < snr[j])
          {
            isCentroid = false;
            break;
          }
        }
      }

      if (isCentroid)
      {
        delay2.push_back(delay[i]);
        doppler2.push_back(doppler[i]);
        snr2.push_back(snr[i]);
      }
    }

    return std::make_unique<Detection>(delay2, doppler2, snr2);
  }

  const double dopplerWindow = nDoppler * resolutionDoppler;
  std::vector<bool> clustered(snr.size(), false);
  for (size_t start = 0; start < snr.size(); start++)
  {
    if (clustered[start])
    {
      continue;
    }

    std::vector<size_t> cluster;
    std::vector<size_t> pending{start};
    clustered[start] = true;

    while (!pending.empty())
    {
      const size_t current = pending.back();
      pending.pop_back();
      cluster.push_back(current);

      for (size_t candidate = 0; candidate < snr.size(); candidate++)
      {
        if (clustered[candidate])
        {
          continue;
        }
        if (are_cluster_neighbors(delay[current], doppler[current], delay[candidate],
          doppler[candidate], nDelay, dopplerWindow))
        {
          clustered[candidate] = true;
          pending.push_back(candidate);
        }
      }
    }

    double totalWeight = 0.0;
    double weightedDelay = 0.0;
    double weightedDoppler = 0.0;
    double peakSnr = -std::numeric_limits<double>::infinity();
    for (size_t index : cluster)
    {
      const double weight = detection_weight(y, delay[index], doppler[index], snr[index]);
      totalWeight += weight;
      weightedDelay += weight * delay[index];
      weightedDoppler += weight * doppler[index];
      peakSnr = std::max(peakSnr, snr[index]);
    }

    if (!(totalWeight > 0.0))
    {
      totalWeight = 0.0;
      weightedDelay = 0.0;
      weightedDoppler = 0.0;
      for (size_t index : cluster)
      {
        totalWeight += 1.0;
        weightedDelay += delay[index];
        weightedDoppler += doppler[index];
      }
    }

    delay2.push_back(weightedDelay / totalWeight);
    doppler2.push_back(weightedDoppler / totalWeight);
    snr2.push_back(std::isfinite(peakSnr) ? peakSnr : 0.0);
  }

  return std::make_unique<Detection>(delay2, doppler2, snr2);
}
