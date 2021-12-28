/**
 * @file banditpam.cpp
 * @date 2021-07-25
 *
 * This file contains the primary C++ implementation of the BanditPAM code.
 *
 */

#include "banditpam.hpp"

#include <armadillo>
#include <unordered_map>
#include <cmath>

namespace km {
void BanditPAM::fitBanditPAM(const arma::Mat<float>& inputData) {
  data = inputData;
  data = arma::trans(data);

  if (this->useCacheP) {
    size_t n = data.n_cols;
    size_t m = fmin(n, ceil(log10(data.n_cols) * cacheMultiplier));
    cache = new float[n * m];

    #pragma omp parallel for
    for (size_t idx = 0; idx < m*n; idx++) {
      cache[idx] = -1;
    }

    permutation = arma::randperm(n);
    permutationIdx = 0;
    reindex = {};
    // TODO(@motiwari): Can we parallelize this?
    for (size_t counter = 0; counter < m; counter++) {
      reindex[permutation[counter]] = counter;
    }
  }

  arma::Mat<float> medoids_mat(data.n_rows, nMedoids);
  arma::urowvec medoid_indices(nMedoids);
  BanditPAM::build(data, &medoid_indices, &medoids_mat);
  steps = 0;

  medoidIndicesBuild = medoid_indices;
  arma::urowvec assignments(data.n_cols);
  BanditPAM::swap(data, &medoid_indices, &medoids_mat, &assignments);
  medoidIndicesFinal = medoid_indices;
  labels = assignments;
}

arma::rowvec BanditPAM::buildSigma(
  const arma::Mat<float>& data,
  const arma::rowvec& bestDistances,
  const bool useAbsolute) {
  size_t N = data.n_cols;
  arma::uvec referencePoints;
  // TODO(@motiwari): Make this wraparound properly as
  // last batch_size elements are dropped
  if (usePerm) {
    if ((permutationIdx + batchSize - 1) >= N) {
      permutationIdx = 0;
    }
    // inclusive of both indices
    referencePoints = permutation.subvec(
      permutationIdx,
      permutationIdx + batchSize - 1);
    permutationIdx += batchSize;
  } else {
    referencePoints = arma::randperm(N, batchSize);
  }

  arma::Col<float> sample(batchSize);
  arma::rowvec updated_sigma(N);
  #pragma omp parallel for
  for (size_t i = 0; i < N; i++) {
    for (size_t j = 0; j < batchSize; j++) {
      float cost = KMedoids::cachedLoss(data, i, referencePoints(j));
      if (useAbsolute) {
        sample(j) = cost;
      } else {
        sample(j) = cost < bestDistances(referencePoints(j))
                          ? cost : bestDistances(referencePoints(j));
              sample(j) -= bestDistances(referencePoints(j));
      }
    }
    updated_sigma(i) = arma::stddev(sample);
  }
  return updated_sigma;
}

arma::rowvec BanditPAM::buildTarget(
  const arma::Mat<float>& data,
  const arma::uvec* target,
  const arma::rowvec* bestDistances,
  const bool useAbsolute,
  const size_t exact = 0) {
  size_t N = data.n_cols;
  size_t tmpBatchSize = batchSize;
  if (exact > 0) {
    tmpBatchSize = N;
  }
  arma::rowvec estimates(target->n_rows, arma::fill::zeros);
  arma::uvec referencePoints;
  // TODO(@motiwari): Make this wraparound properly
  // as last batch_size elements are dropped
  if (usePerm) {
    if ((permutationIdx + tmpBatchSize - 1) >= N) {
      permutationIdx = 0;
    }
    // inclusive of both indices
    referencePoints = permutation.subvec(
      permutationIdx,
      permutationIdx + tmpBatchSize - 1);
    permutationIdx += tmpBatchSize;
  } else {
    referencePoints = arma::randperm(N, tmpBatchSize);
  }

  #pragma omp parallel for
  for (size_t i = 0; i < target->n_rows; i++) {
    float total = 0;
    for (size_t j = 0; j < referencePoints.n_rows; j++) {
      float cost =
        KMedoids::cachedLoss(data, (*target)(i), referencePoints(j));
      if (useAbsolute) {
        total += cost;
      } else {
        total += cost < (*bestDistances)(referencePoints(j))
                      ? cost : (*bestDistances)(referencePoints(j));
        total -= (*bestDistances)(referencePoints(j));
      }
    }
     estimates(i) = total / tmpBatchSize;
  }
  return estimates;
}

void BanditPAM::build(
  const arma::Mat<float>& data,
  arma::urowvec* medoid_indices,
  arma::Mat<float>* medoids) {
  size_t N = data.n_cols;
  arma::rowvec N_mat(N);
  N_mat.fill(N);
  size_t p = (buildConfidence * N);
  bool useAbsolute = true;
  arma::rowvec estimates(N, arma::fill::zeros);
  arma::rowvec bestDistances(N);
  bestDistances.fill(std::numeric_limits<float>::infinity());
  arma::rowvec sigma(N);
  arma::urowvec candidates(N, arma::fill::ones);
  arma::rowvec lcbs(N);
  arma::rowvec ucbs(N);
  arma::rowvec numSamples(N, arma::fill::zeros);
  arma::rowvec exactMask(N, arma::fill::zeros);

  for (size_t k = 0; k < nMedoids; k++) {
    // instantiate medoids one-by-online
    permutationIdx = 0;
    size_t step_count = 0;
    candidates.fill(1);
    numSamples.fill(0);
    exactMask.fill(0);
    estimates.fill(0);
    // compute std dev amongst batch of reference points
    sigma = buildSigma(data, bestDistances, useAbsolute);

    while (arma::sum(candidates) > precision) {
      arma::umat compute_exactly =
        ((numSamples + batchSize) >= N_mat) != exactMask;
      if (arma::accu(compute_exactly) > 0) {
        arma::uvec targets = find(compute_exactly);
        arma::rowvec result = buildTarget(
          data,
          &targets,
          &bestDistances,
          useAbsolute,
          N);
        estimates.cols(targets) = result;
        ucbs.cols(targets) = result;
        lcbs.cols(targets) = result;
        exactMask.cols(targets).fill(1);
        numSamples.cols(targets) += N;
        candidates.cols(targets).fill(0);
      }
      if (arma::sum(candidates) < precision) {
        break;
      }
      arma::uvec targets = arma::find(candidates);
      arma::rowvec result = buildTarget(
        data,
        &targets,
        &bestDistances,
        useAbsolute,
        0);
      // update the running average
      estimates.cols(targets) =
        ((numSamples.cols(targets) % estimates.cols(targets)) +
        (result * batchSize)) /
        (batchSize + numSamples.cols(targets));
      numSamples.cols(targets) += batchSize;
      arma::rowvec adjust(targets.n_rows);
      adjust.fill(p);
      adjust = arma::log(adjust);
      arma::rowvec confBoundDelta =
        sigma.cols(targets) %
        arma::sqrt(adjust / numSamples.cols(targets));
      ucbs.cols(targets) = estimates.cols(targets) + confBoundDelta;
      lcbs.cols(targets) = estimates.cols(targets) - confBoundDelta;
      candidates = (lcbs < ucbs.min()) && (exactMask == 0);
      step_count++;
    }

    medoid_indices->at(k) = lcbs.index_min();
    medoids->unsafe_col(k) = data.unsafe_col((*medoid_indices)(k));

    // don't need to do this on final iteration
    #pragma omp parallel for
    for (size_t i = 0; i < N; i++) {
        float cost = KMedoids::cachedLoss(data, i, (*medoid_indices)(k));
        if (cost < bestDistances(i)) {
            bestDistances(i) = cost;
        }
    }
    // use difference of loss for sigma and sampling, not absolute
    useAbsolute = false;
  }
}

arma::Mat<float> BanditPAM::swapSigma(
  const arma::Mat<float>& data,
  const arma::rowvec* bestDistances,
  const arma::rowvec* secondBestDistances,
  const arma::urowvec* assignments) {
  size_t N = data.n_cols;
  size_t K = nMedoids;
  arma::Mat<float> updated_sigma(K, N, arma::fill::zeros);
  arma::uvec referencePoints;
  // TODO(@motiwari): Make this wraparound properly
  // as last batch_size elements are dropped
  if (usePerm) {
    if ((permutationIdx + batchSize - 1) >= N) {
      permutationIdx = 0;
    }
    // inclusive of both indices
    referencePoints = permutation.subvec(
      permutationIdx,
      permutationIdx + batchSize - 1);
    permutationIdx += batchSize;
  } else {
    referencePoints = arma::randperm(N, batchSize);
  }

  arma::Col<float> sample(batchSize);
  // for each considered swap
  #pragma omp parallel for
  for (size_t i = 0; i < K * N; i++) {
    // extract data point of swap
    size_t n = i / K;
    size_t k = i % K;

    // calculate change in loss for some subset of the data
    for (size_t j = 0; j < batchSize; j++) {
      float cost = KMedoids::cachedLoss(data, n, referencePoints(j));

      if (k == (*assignments)(referencePoints(j))) {
        if (cost < (*secondBestDistances)(referencePoints(j))) {
          sample(j) = cost;
        } else {
          sample(j) = (*secondBestDistances)(referencePoints(j));
        }
      } else {
        if (cost < (*bestDistances)(referencePoints(j))) {
          sample(j) = cost;
        } else {
          sample(j) = (*bestDistances)(referencePoints(j));
        }
      }
      sample(j) -= (*bestDistances)(referencePoints(j));
    }
    updated_sigma(k, n) = arma::stddev(sample);
  }
  return updated_sigma;
}

arma::Col<float> BanditPAM::swapTarget(
  const arma::Mat<float>& data,
  const arma::urowvec* medoid_indices,
  const arma::uvec* targets,
  const arma::rowvec* bestDistances,
  const arma::rowvec* secondBestDistances,
  const arma::urowvec* assignments,
  const size_t exact = 0) {
  size_t N = data.n_cols;
  arma::Col<float> estimates(targets->n_rows, arma::fill::zeros);

  size_t tmpBatchSize = batchSize;
  if (exact > 0) {
    tmpBatchSize = N;
  }

  arma::uvec referencePoints;
  // TODO(@motiwari): Make this wraparound properly
  // as last batch_size elements are dropped
  if (usePerm) {
    if ((permutationIdx + tmpBatchSize - 1) >= N) {
      permutationIdx = 0;
    }
    // inclusive of both indices
    referencePoints = permutation.subvec(
      permutationIdx,
      permutationIdx + tmpBatchSize - 1);
    permutationIdx += tmpBatchSize;
  } else {
    referencePoints = arma::randperm(N, tmpBatchSize);
  }

  // for each considered swap
  #pragma omp parallel for
  for (size_t i = 0; i < targets->n_rows; i++) {
    float total = 0;
    // extract data point of swap
    size_t n = (*targets)(i) / medoid_indices->n_cols;
    size_t k = (*targets)(i) % medoid_indices->n_cols;
    // calculate total loss for some subset of the data
    for (size_t j = 0; j < tmpBatchSize; j++) {
      float cost = KMedoids::cachedLoss(data, n, referencePoints(j));
      if (k == (*assignments)(referencePoints(j))) {
        if (cost < (*secondBestDistances)(referencePoints(j))) {
          total += cost;
        } else {
          total += (*secondBestDistances)(referencePoints(j));
        }
      } else {
        if (cost < (*bestDistances)(referencePoints(j))) {
          total += cost;
        } else {
          total += (*bestDistances)(referencePoints(j));
        }
      }
      total -= (*bestDistances)(referencePoints(j));
    }
    estimates(i) = total / referencePoints.n_rows;
  }
  return estimates;
}

void BanditPAM::swap(
  const arma::Mat<float>& data,
  arma::urowvec* medoid_indices,
  arma::Mat<float>* medoids,
  arma::urowvec* assignments) {
  size_t N = data.n_cols;
  size_t p = (N * nMedoids * swapConfidence);

  arma::Mat<float> sigma(nMedoids, N, arma::fill::zeros);

  arma::rowvec bestDistances(N);
  arma::rowvec secondBestDistances(N);
  size_t iter = 0;
  bool swap_performed = true;
  arma::umat candidates(nMedoids, N, arma::fill::ones);
  arma::umat exactMask(nMedoids, N, arma::fill::zeros);
  arma::Mat<float> estimates(nMedoids, N, arma::fill::zeros);
  arma::Mat<float> lcbs(nMedoids, N);
  arma::Mat<float> ucbs(nMedoids, N);
  arma::umat numSamples(nMedoids, N, arma::fill::zeros);

  // continue making swaps while loss is decreasing
  while (swap_performed && iter < maxIter) {
    iter++;
    permutationIdx = 0;

    // calculate quantities needed for swap, bestDistances and sigma
    calcBestDistancesSwap(
      data,
      medoid_indices,
      &bestDistances,
      &secondBestDistances,
      assignments);

    sigma = swapSigma(
      data,
      &bestDistances,
      &secondBestDistances,
      assignments);

    candidates.fill(1);
    exactMask.fill(0);
    estimates.fill(0);
    numSamples.fill(0);

    // while there is at least one candidate (float comparison issues)
    while (arma::accu(candidates) > 0.5) {
      calcBestDistancesSwap(
        data,
        medoid_indices,
        &bestDistances,
        &secondBestDistances,
        assignments);

      // compute exactly if it's been samples more than N times and
      // hasn't been computed exactly already
      arma::umat compute_exactly =
        ((numSamples + batchSize) >= N) != (exactMask);
      arma::uvec targets = arma::find(compute_exactly);

      if (targets.size() > 0) {
          arma::Col<float> result = swapTarget(
            data,
            medoid_indices,
            &targets,
            &bestDistances,
            &secondBestDistances,
            assignments,
            N);
          estimates.elem(targets) = result;
          ucbs.elem(targets) = result;
          lcbs.elem(targets) = result;
          exactMask.elem(targets).fill(1);
          numSamples.elem(targets) += N;
          candidates = (lcbs < ucbs.min()) && (exactMask == 0);
      }
      if (arma::accu(candidates) < precision) {
        break;
      }
      targets = arma::find(candidates);
      arma::Col<float> result = swapTarget(
        data,
        medoid_indices,
        &targets,
        &bestDistances,
        &secondBestDistances,
        assignments,
        0);
      estimates.elem(targets) =
        ((numSamples.elem(targets) % estimates.elem(targets)) +
        (result * batchSize)) /
        (batchSize + numSamples.elem(targets));
      numSamples.elem(targets) += batchSize;
      arma::Col<float> adjust(targets.n_rows);
      adjust.fill(p);
      adjust = arma::log(adjust);
      arma::Col<float> confBoundDelta = sigma.elem(targets) %
                          arma::sqrt(adjust / numSamples.elem(targets));

      ucbs.elem(targets) = estimates.elem(targets) + confBoundDelta;
      lcbs.elem(targets) = estimates.elem(targets) - confBoundDelta;
      candidates = (lcbs < ucbs.min()) && (exactMask == 0);
      targets = arma::find(candidates);
    }
    // now switch medoids
    arma::uword new_medoid = lcbs.index_min();
    // extract medoid of swap
    size_t k = new_medoid % medoids->n_cols;

    // extract data point of swap
    size_t n = new_medoid / medoids->n_cols;
    swap_performed = (*medoid_indices)(k) != n;
    steps++;

    (*medoid_indices)(k) = n;
    medoids->col(k) = data.col((*medoid_indices)(k));
    calcBestDistancesSwap(
      data,
      medoid_indices,
      &bestDistances,
      &secondBestDistances,
      assignments);
  }
}
}  // namespace km
