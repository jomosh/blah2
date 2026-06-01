#include "Tracker.h"
#include <iostream>
#include <algorithm>
#include <limits>
#include <cmath>
#include <vector>

namespace
{
/// @brief Find the minimum-cost global assignment between tracks (rows) and
///        detections (columns) using the O(n³) Hungarian (Kuhn-Munkres)
///        algorithm.
///
/// @param cost   n×m cost matrix.  Entries >= infCost are infeasible.
/// @param infCost Sentinel for infeasible pairs (e.g. outside gate).
/// @return Assignment vector of length n; result[i] = j means track i is
///         assigned to detection j, or -1 if track i has no feasible match.
///
/// @details The cost matrix is padded to a square sz×sz matrix (sz = max(n,m))
///          so the algorithm always terminates.  Phantom padded cells carry
///          infCost and are filtered out of the result.  For the typical
///          operational scenario (n,m < 100), the O(sz³) cost is negligible
///          relative to the DSP processing time.
static std::vector<int> hungarian_assign(
    const std::vector<std::vector<double>> &cost, double infCost)
{
  const int nRows = static_cast<int>(cost.size());
  if (nRows == 0) return {};
  const int nCols = (nRows > 0) ? static_cast<int>(cost[0].size()) : 0;
  if (nCols == 0) return std::vector<int>(nRows, -1);

  const int sz = std::max(nRows, nCols);

  // Build square matrix padded with infCost.
  std::vector<std::vector<double>> a(sz, std::vector<double>(sz, infCost));
  for (int i = 0; i < nRows; i++)
    for (int j = 0; j < nCols; j++)
      a[i][j] = cost[i][j];

  // Potential-reduction Hungarian (1-indexed internally).
  // u[i] = row potential, v[j] = column potential.
  // p[j] = row assigned to column j (0 = unassigned).
  std::vector<double> u(sz + 1, 0.0), v(sz + 1, 0.0);
  std::vector<int>    p(sz + 1, 0),   way(sz + 1, 0);

  for (int i = 1; i <= sz; i++)
  {
    p[0] = i;
    int j0 = 0;
    std::vector<double> minVal(sz + 1, std::numeric_limits<double>::max());
    std::vector<bool>   used(sz + 1, false);

    do
    {
      used[j0] = true;
      int    i0    = p[j0];
      double delta = std::numeric_limits<double>::max();
      int    j1    = -1;

      for (int j = 1; j <= sz; j++)
      {
        if (!used[j])
        {
          const double val = a[i0 - 1][j - 1] - u[i0] - v[j];
          if (val < minVal[j])
          {
            minVal[j] = val;
            way[j]    = j0;
          }
          if (minVal[j] < delta)
          {
            delta = minVal[j];
            j1    = j;
          }
        }
      }

      if (j1 < 0) break;  // no augmenting column (safety guard)

      for (int j = 0; j <= sz; j++)
      {
        if (used[j])
        {
          u[p[j]] += delta;
          v[j]    -= delta;
        }
        else
        {
          minVal[j] -= delta;
        }
      }
      j0 = j1;
    } while (p[j0] != 0);

    // Augment the path.
    do
    {
      const int j1 = way[j0];
      p[j0]        = p[j1];
      j0           = j1;
    } while (j0);
  }

  // Build result: p[j]=i means row i-1 assigned to column j-1.
  // Only include pairs whose original cost is below infCost.
  std::vector<int> result(nRows, -1);
  for (int j = 1; j <= nCols; j++)
  {
    if (p[j] >= 1 && p[j] <= nRows && a[p[j] - 1][j - 1] < infCost)
    {
      result[p[j] - 1] = j - 1;
    }
  }
  return result;
}
} // anonymous namespace

// constructor
Tracker::Tracker(uint32_t _m, uint32_t _n, uint32_t _nDelete, 
  double _cpi, double _maxAccInit, double _rangeRes, double _lambda)
{
  m = _m;
  n = _n;
  nDelete = _nDelete;
  cpi = _cpi;
  maxAccInit = _maxAccInit;
  timestamp = 0;
  rangeRes = _rangeRes;
  lambda = _lambda;

  double resolutionAcc = 1/(cpi*cpi);
  uint16_t nAcc = (int)maxAccInit/resolutionAcc;
  for (int i = 0; i < 2*nAcc+1; i++)
  {
    accInit.push_back(resolutionAcc*(i-nAcc));
  }

  Track track{};
}

Tracker::~Tracker()
{
}

std::unique_ptr<Track> Tracker::process(Detection *detection, uint64_t currentTime)
{
  doNotInitiate.clear();
  for (size_t i = 0; i < detection->get_nDetections(); i++)
  {
    doNotInitiate.push_back(false);
  }

  if (track.get_n() > 0)
  {
    update(detection, currentTime);
  }
  else
  {
    timestamp = currentTime;
  }
  initiate(detection);

  return std::make_unique<Track>(track);
}

void Tracker::update(Detection *detection, uint64_t current)
{
  const std::vector<double> &delay     = detection->get_delay();
  const std::vector<double> &doppler   = detection->get_doppler();
  const std::vector<double> &snr       = detection->get_snr();
  const size_t               nDetections = detection->get_nDetections();

  std::string state;

  // Time between CPIs (seconds).
  const double T = ((double)(current - timestamp)) / 1000;
  timestamp = current;

  const double delayGateBins = 3.0;
  const double dopplerGateHz = 3.0 * (1.0 / cpi);
  const double kInfCost      = 1.0e9;

  const uint64_t nTracks = track.get_n();

  // ---- Phase 1: pre-compute predictions for all tracks ------------------
  // Snapshot before any mutation so Hungarian indices stay stable.
  std::vector<Detection> currentPositions;
  std::vector<Detection> trackPredictions;
  currentPositions.reserve(nTracks);
  trackPredictions.reserve(nTracks);
  for (uint64_t i = 0; i < nTracks; i++)
  {
    currentPositions.push_back(track.get_current(i));
    const double a = track.get_acceleration(i);
    trackPredictions.push_back(predict(currentPositions.back(), a, T));
  }

  // ---- Phase 2: build gated cost matrix (nTracks × nDetections) ---------
  std::vector<std::vector<double>> costMatrix(
      nTracks, std::vector<double>(nDetections, kInfCost));
  for (uint64_t i = 0; i < nTracks; i++)
  {
    const double pDelay   = trackPredictions[i].get_delay().front();
    const double pDoppler = trackPredictions[i].get_doppler().front();
    for (size_t j = 0; j < nDetections; j++)
    {
      const double dDelay   = delay[j]   - pDelay;
      const double dDoppler = doppler[j] - pDoppler;
      if (std::abs(dDelay)   <= delayGateBins &&
          std::abs(dDoppler) <= dopplerGateHz)
      {
        const double nd  = dDelay   / delayGateBins;
        const double nf  = dDoppler / dopplerGateHz;
        costMatrix[i][j] = std::sqrt(nd * nd + nf * nf);
      }
    }
  }

  // ---- Phase 3: optimal global assignment (Hungarian) -------------------
  const std::vector<int> assignment =
      (nTracks > 0 && nDetections > 0)
          ? hungarian_assign(costMatrix, kInfCost)
          : std::vector<int>(nTracks, -1);

  // ---- Phase 4: apply associations and update track states --------------
  // Iterate over the stable snapshot count; no removals in this phase so
  // indices into currentPositions/trackPredictions stay consistent.
  for (uint64_t i = 0; i < nTracks; i++)
  {
    const int  j                  = assignment[i];
    const bool associatedThisCycle = (j >= 0);

    if (associatedThisCycle)
    {
      Detection associated(delay[j], doppler[j], snr[j]);
      track.set_current(i, associated);
      if (T > 0)
      {
        track.set_acceleration(
            i, (doppler[j] - currentPositions[i].get_doppler().front()) / T);
      }
      track.set_nInactive(i, 0);
      doNotInitiate[j] = true;
      state = "ASSOCIATED";
      track.set_state(i, state);
      track.promote(i, m, n);
    }
    else
    {
      track.set_current(i, trackPredictions[i]);
      const std::string &curState = track.get_state(i);
      if (curState == "ACTIVE")
      {
        state = "COASTING";
        track.set_state(i, state);
      }
      else if (curState == "ASSOCIATED")
      {
        state = "TENTATIVE";
        track.set_state(i, state);
      }
      else
      {
        track.set_state(i, curState);
      }
      track.set_nInactive(i, track.get_nInactive(i) + 1);
    }
  }

  // ---- Phase 5: remove over-inactive tracks (backwards to keep indices) -
  for (int64_t i = static_cast<int64_t>(nTracks) - 1; i >= 0; i--)
  {
    if (track.get_nInactive(static_cast<uint64_t>(i)) > nDelete)
    {
      track.remove(static_cast<uint64_t>(i));
    }
  }
}

Detection Tracker::predict(const Detection &current, double acc, double T)
{
  double delayTrack = current.get_delay().front();
  double dopplerTrack = current.get_doppler().front();
  double delayPredict = delayTrack+((dopplerTrack*T*lambda)+
    (0.5*acc*T*T*lambda))/rangeRes;
  double dopplerPredict = dopplerTrack+(acc*T);
  Detection prediction(delayPredict, dopplerPredict, 0);
  return prediction;
}

void Tracker::initiate(Detection *detection)
{  
  const std::vector<double> &delay = detection->get_delay();
  const std::vector<double> &doppler = detection->get_doppler();
  const std::vector<double> &snr = detection->get_snr();
  const size_t nDetections = detection->get_nDetections();
  uint64_t index;

  // loop over new detections
  for (size_t i = 0; i < nDetections; i++)
  {
    // skip if detection used in update
    if (doNotInitiate.at(i))
    {
      continue;
    }
    // add tentative detection for each acc
    for (size_t j = 0; j < accInit.size(); j++)
    {
      Detection detectionCurrent(delay[i], doppler[i], snr[i]);
      index = track.add(detectionCurrent);
      track.set_acceleration(index, accInit[j]);
    }
  }
}