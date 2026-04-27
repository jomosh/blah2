/// @file NearestAxisIndex.h
/// @brief Shared helpers for locating the nearest delay or Doppler axis index.
/// @author GitHub Copilot

#ifndef NEAREST_AXIS_INDEX_H
#define NEAREST_AXIS_INDEX_H

#include <cmath>
#include <cstddef>
#include <deque>

namespace detection_utility
{
template <typename T>
inline size_t nearest_axis_index(const std::deque<T> &axisBins, double value)
{
  if (axisBins.empty())
  {
    return 0;
  }

  size_t bestIndex = 0;
  double bestDistance = std::abs(value - static_cast<double>(axisBins[0]));
  for (size_t index = 1; index < axisBins.size(); index++)
  {
    const double candidateDistance = std::abs(value - static_cast<double>(axisBins[index]));
    if (candidateDistance < bestDistance)
    {
      bestDistance = candidateDistance;
      bestIndex = index;
    }
  }

  return bestIndex;
}
}

#endif