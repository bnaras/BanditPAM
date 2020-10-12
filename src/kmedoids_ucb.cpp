/**
 * @file kmedoids_ucb.cpp
 * @date 2020-06-10
 *
 * This file contains the main C++ implementation of the BanditPAM code.
 *
 */
#include "kmedoids_ucb.hpp"
#include <armadillo>
#include <unordered_map>

/**
 *  KMedoids class. Creates a KMedoids object that can be used to find the medoids
 *  for a particular set of input data.
 *
 *  @param n_medoids Number of medoids to identify
 *  @param algorithm Algorithm to use to find medoids; options are "BanditPAM" for
 *  this paper's iplementation, or "naive" to use the naive method
 *  @param verbosity Verbosity of the algorithm, 0 will have no log file emitted, 1 will emit a log file
 *  @param max_iter The maximum number of iterations to run the algorithm for
 *  @param logFilename The name of the output log file
 */
KMedoids::KMedoids(int n_medoids, std::string algorithm, int verbosity, int max_iter, std::string logFilename): n_medoids(n_medoids), algorithm(algorithm), max_iter(max_iter), verbosity(verbosity), logFilename(logFilename) {
  KMedoids::checkAlgorithm(algorithm);
}

/**
 * This function is the destructor for the KMedoids class.
 */
KMedoids::~KMedoids() {
  logFile.close();
}

/**
 * This function sets the algorithm the KMedoids object will use
 */
void KMedoids::checkAlgorithm(std::string algorithm) {
  if (algorithm == "BanditPAM") {
    fitFn = &KMedoids::fit_bpam;
  } else if (algorithm == "naive") {
    fitFn = &KMedoids::fit_naive;
  } else {
    throw "unrecognized algorithm";
  }
}

/**
 * This function returns the final medoids for a KMedoids object.
 */
arma::rowvec KMedoids::getMedoidsFinal() {
  return medoid_indices_final;
}

/**
 * This function returns the build medoids for a KMedoids object.
 */
arma::rowvec KMedoids::getMedoidsBuild() {
  return medoid_indices_build;
}

/**
 * This function returns the labels/medoids assignments for each datapoint after the final
 * medoids are identified.
 */
arma::rowvec KMedoids::getLabels() {
  return labels;
}

/**
 * This function returns the number of swap steps that took place during the computation
 */
int KMedoids::getSteps() {
  return steps;
}

/**
 * This function sets the loss function the KMedoids object will use
 */
void KMedoids::setLossFn(std::string loss) {
  if (loss == "manhattan") {
      lossFn = &KMedoids::manhattan;
  } else if (loss == "cos") {
      lossFn = &KMedoids::cos;
  } else if (loss == "L1") {
      lossFn = &KMedoids::L1;
  } else if (loss == "L2"){
      lossFn = &KMedoids::L2;
  } else {
      throw "unrecognized loss function";
  }
}

/**
 * This function gets the number of clusters for the KMedoids object
 */
int KMedoids::getNMedoids() {
  return n_medoids;
}

/**
 * This function sets the number of clusters for the KMedoids object
 */
void KMedoids::setNMedoids(int new_num) {
  n_medoids = new_num;
}

/**
 * This function gets the algorithm for the KMedoids object
 */
std::string KMedoids::getAlgorithm() {
  return algorithm;
}

/**
 * This function sets the algorithm for the KMedoids object
 */
void KMedoids::setAlgorithm(std::string new_alg) {
  algorithm = new_alg;
}

/**
 * This function gets the verbosity for the KMedoids object
 */
int KMedoids::getVerbosity() {
  return verbosity;
}

/**
 * This function sets the verbosity for the KMedoids object
 */
void KMedoids::setVerbosity(int new_ver) {
  verbosity = new_ver;
}

/**
 * This function gets the maximum number of iterations for the KMedoids object
 */
int KMedoids::getMaxIter() {
  return max_iter;
}

/**
 * This function sets the maximum number of iterations for the KMedoids object
 */
void KMedoids::setMaxIter(int new_max) {
  max_iter = new_max;
}

/**
 * This function gets the log filename for the KMedoids object
 */
std::string KMedoids::getLogfileName() {
  return logFilename;
}

/**
 * This function sets the log filename for the KMedoids object
 */
void KMedoids::setLogFilename(std::string new_lname) {
  logFilename = new_lname;
}

/**
 * This is the main function of the KMedoids object: this finds the build and swap
 * medoids for the desired data
 *
 * @param input_data Input data to find the medoids of
 * @param loss The loss function used during medoid computation
 */
void KMedoids::fit(arma::mat input_data, std::string loss) {
  logHelper.init(n_medoids, logFilename);
  KMedoids::setLossFn(loss);
  (this->*fitFn)(input_data);
  logHelper.writeProfile(medoid_indices_build, medoid_indices_final, 4, 7.44);
  logHelper.close();
}


/**
 * This function will run the naive PAM algorithm to identify a dataset's medoids.
 *
 * @param input_data Input data to find the medoids of
 * @param loss The loss function used during medoid computation
 */
void KMedoids::fit_naive(arma::mat input_data) {
  data = input_data;
  data = arma::trans(data);
  arma::rowvec medoid_indices(n_medoids);
  KMedoids::build_naive(medoid_indices);
  steps = 0;

  medoid_indices_build = medoid_indices;
  // TODO: make assignments in naive code too!
  size_t i = 0;
  bool medoidChange = true;
  while (i < max_iter && medoidChange) {
    auto previous(medoid_indices);
    KMedoids::swap_naive(medoid_indices);
    medoidChange = arma::any(medoid_indices != previous);
    i++;
  }
  medoid_indices_final = medoid_indices;
}

/**
 * Build step for the naive algorithm
 */
void KMedoids::build_naive(
  arma::rowvec& medoid_indices)
{
  for (size_t k = 0; k < n_medoids; k++) {
    double minDistance = std::numeric_limits<double>::infinity();
    int best = 0;
    for (int i = 0; i < data.n_cols; i++) {
      double total = 0;
      for (size_t j = 0; j < data.n_cols; j++) {
        double cost = (this->*lossFn)(i, j);
        for (size_t medoid = 0; medoid < k; medoid++) {
          double current = (this->*lossFn)(medoid_indices(medoid), j);
          if (current < cost) {
            cost = current;
          }
        }
        total += cost;
      }
      if (total < minDistance) {
        minDistance = total;
        best = i;
      }
    }
    medoid_indices(k) = best;
  }
}

/**
 * Swap step for the naive algorithm
 */
void KMedoids::swap_naive(
  arma::rowvec& medoid_indices)
{
  double minDistance = std::numeric_limits<double>::infinity();
  size_t best = 0;
  size_t medoid_to_swap = 0;
  for (size_t k = 0; k < n_medoids; k++) {
    for (size_t i = 0; i < data.n_cols; i++) {
      double total = 0;
      for (size_t j = 0; j < data.n_cols; j++) {
        double cost = (this->*lossFn)(i, j);
        for (size_t medoid = 0; medoid < n_medoids; medoid++) {
          if (medoid == k) {
            continue;
          }
          double current = (this->*lossFn)(medoid_indices(medoid), j);
          if (current < cost) {
            cost = current;
          }
        }
        total += cost;
      }
      if (total < minDistance) {
        minDistance = total;
        best = i;
        medoid_to_swap = k;
      }
    }
  }
  medoid_indices(medoid_to_swap) = best;
}

/**
 * This function will run the BanditPAM algorithm to identify a dataset's medoids.
 *
 * @param input_data Input data to find the medoids of
 */
void KMedoids::fit_bpam(arma::mat input_data) {
  // logFile.open(logFilename);
  data = input_data;
  data = arma::trans(data);
  arma::mat medoids_mat(data.n_rows, n_medoids);
  arma::rowvec medoid_indices(n_medoids);
  KMedoids::build(medoid_indices, medoids_mat);
  steps = 0;

  medoid_indices_build = medoid_indices;
  arma::rowvec assignments(data.n_cols);
  KMedoids::swap(medoid_indices, medoids_mat, assignments);
  medoid_indices_final = medoid_indices;
  labels = assignments;
}

void KMedoids::build(
  arma::rowvec& medoid_indices,
  arma::mat& medoids)
{
    // Parameters
    size_t N = data.n_cols;
    arma::rowvec N_mat(N);
    N_mat.fill(N);
    int p = (buildConfidence * N); // reciprocal of
    bool use_absolute = true;
    arma::rowvec num_samples(N, arma::fill::zeros);
    arma::rowvec estimates(N, arma::fill::zeros);
    arma::rowvec best_distances(N);
    best_distances.fill(std::numeric_limits<double>::infinity());
    arma::rowvec sigma(N);
    arma::urowvec candidates(
      N,
      arma::fill::ones); // one hot encoding of candidates;
    arma::rowvec lcbs(N);
    arma::rowvec ucbs(N);
    arma::rowvec T_samples(N, arma::fill::zeros);
    arma::rowvec exact_mask(N, arma::fill::zeros);

    for (size_t k = 0; k < n_medoids; k++) {
        size_t step_count = 0;
        candidates.fill(1);
        T_samples.fill(0);
        exact_mask.fill(0);
        estimates.fill(0);
        KMedoids::build_sigma(
           best_distances, sigma, batchSize, use_absolute);

        while (arma::sum(candidates) > precision) {
            arma::umat compute_exactly =
              ((T_samples + batchSize) >= N_mat) != exact_mask;
            if (arma::accu(compute_exactly) > 0) {
                arma::uvec targets = find(compute_exactly);
                logBuffer << "Computing exactly for " << targets.n_rows
                          << " out of " << data.n_cols << '\n';
                // log(2);
                logHelper.comp_exact_build.push_back(targets.n_rows);
                arma::rowvec result =
                  build_target(targets, N, best_distances, use_absolute);
                estimates.cols(targets) = result;
                ucbs.cols(targets) = result;
                lcbs.cols(targets) = result;
                exact_mask.cols(targets).fill(1);
                T_samples.cols(targets) += N;
                candidates.cols(targets).fill(0);
            }
            if (arma::sum(candidates) < precision) {
                break;
            }
            arma::uvec targets = arma::find(candidates);
            arma::rowvec result = build_target(
              targets, batchSize, best_distances, use_absolute);
            estimates.cols(targets) =
              ((T_samples.cols(targets) % estimates.cols(targets)) +
               (result * batchSize)) /
              (batchSize + T_samples.cols(targets));
            T_samples.cols(targets) += batchSize;
            arma::rowvec adjust(targets.n_rows);
            adjust.fill(p);
            adjust = arma::log(adjust);
            arma::rowvec cb_delta =
              sigma.cols(targets) %
              arma::sqrt(adjust / T_samples.cols(targets));
            ucbs.cols(targets) = estimates.cols(targets) + cb_delta;
            lcbs.cols(targets) = estimates.cols(targets) - cb_delta;
            candidates = (lcbs < ucbs.min()) && (exact_mask == 0);
            step_count++;
        }

        medoid_indices.at(k) = lcbs.index_min();
        medoids.unsafe_col(k) = data.unsafe_col(medoid_indices(k));

        // don't need to do this on final iteration
        for (size_t i = 0; i < N; i++) {
            double cost = (this->*lossFn)(i, medoid_indices(k));
            if (cost < best_distances(i)) {
                best_distances(i) = cost;
            }
        }
        use_absolute = false; // use difference of loss for sigma and sampling,
                              // not absolute
        logHelper.loss_build.push_back(arma::mean(arma::mean(best_distances)));
        logHelper.p_build.push_back((float)1/(float)p);
        // logBuffer << "Loss: " << arma::mean(arma::mean(best_distances)) << '\n';
        // log(2);
        // logBuffer << "p: " << (float)1/(float)p << '\n';
        // log(2);
    }
}

void KMedoids::build_sigma(
  arma::rowvec& best_distances,
  arma::rowvec& sigma,
  arma::uword batch_size,
  bool use_absolute)
{
    size_t N = data.n_cols;
    // without replacement, requires updated version of armadillo
    arma::uvec tmp_refs = arma::randperm(N, batch_size);
    arma::vec sample(batch_size);
// for each possible swap
#pragma omp parallel for
    for (size_t i = 0; i < N; i++) {
        // gather a sample of points
        for (size_t j = 0; j < batch_size; j++) {
            double cost = (this->*lossFn)(i,tmp_refs(j));
            if (use_absolute) {
                sample(j) = cost;
            } else {
                sample(j) = cost < best_distances(tmp_refs(j))
                              ? cost
                              : best_distances(tmp_refs(j));
                sample(j) -= best_distances(tmp_refs(j));
            }
        }
        sigma(i) = arma::stddev(sample);
    }
    arma::rowvec P = {0.25, 0.5, 0.75};
    arma::rowvec Q = arma::quantile(sigma, P);
    std::ostringstream sigma_out;
    sigma_out << "min: " << arma::min(sigma)
              << ", 25th: " << Q(0)
              << ", median: " << Q(1)
              << ", 75th: " << Q(2)
              << ", max: " << arma::max(sigma)
              << ", mean: " << arma::mean(sigma);
    logHelper.sigma_build.push_back(sigma_out.str());
    log(2);
}

arma::rowvec KMedoids::build_target(
  arma::uvec& target,
  size_t batch_size,
  arma::rowvec& best_distances,
  bool use_absolute)
{
    size_t N = data.n_cols;
    arma::rowvec estimates(target.n_rows, arma::fill::zeros);
    arma::uvec tmp_refs = arma::randperm(N,
                                   batch_size); // without replacement, requires
                                                // updated version of armadillo
#pragma omp parallel for
    for (size_t i = 0; i < target.n_rows; i++) {
        double total = 0;
        for (size_t j = 0; j < tmp_refs.n_rows; j++) {
            double cost =
              (this->*lossFn)(tmp_refs(j),target(i));
            if (use_absolute) {
                total += cost;
            } else {
                total += cost < best_distances(tmp_refs(j))
                           ? cost
                           : best_distances(tmp_refs(j));
                total -= best_distances(tmp_refs(j));
            }
        }
        estimates(i) = total / batch_size;
    }
    return estimates;
}

void KMedoids::swap(
  arma::rowvec& medoid_indices,
  arma::mat& medoids,
  arma::rowvec& assignments)
{
    size_t N = data.n_cols;
    int p = (N * n_medoids * swapConfidence); // reciprocal

    arma::mat sigma(n_medoids, N, arma::fill::zeros);

    arma::rowvec best_distances(N);
    arma::rowvec second_distances(N);
    size_t iter = 0;
    bool swap_performed = true;
    arma::umat candidates(n_medoids, N, arma::fill::ones);
    arma::umat exact_mask(n_medoids, N, arma::fill::zeros);
    arma::mat estimates(n_medoids, N, arma::fill::zeros);
    arma::mat lcbs(n_medoids, N);
    arma::mat ucbs(n_medoids, N);
    arma::umat T_samples(n_medoids, N, arma::fill::zeros);

    // continue making swaps while loss is decreasing
    while (swap_performed && iter < max_iter) {
        iter++;

        // calculate quantities needed for swap, best_distances and sigma
        calc_best_distances_swap(
          medoid_indices, best_distances, second_distances, assignments);

        swap_sigma(sigma,
                   batchSize,
                   best_distances,
                   second_distances,
                   assignments);

        candidates.fill(1);
        exact_mask.fill(0);
        estimates.fill(0);
        T_samples.fill(0);

        // while there is at least one candidate (double comparison issues)
        while (arma::accu(candidates) > 0.5) {
            calc_best_distances_swap(
              medoid_indices, best_distances, second_distances, assignments);

            // compute exactly if it's been samples more than N times and hasn't
            // been computed exactly already
            arma::umat compute_exactly =
              ((T_samples + batchSize) >= N) != (exact_mask);
            arma::uvec targets = arma::find(compute_exactly);

            if (targets.size() > 0) {
                // logBuffer << "COMPUTING EXACTLY " << targets.size()
                //           << " out of " << candidates.size() << '\n';
                // log(2);
                logHelper.comp_exact_swap.push_back(targets.size());
                arma::vec result = swap_target(medoid_indices,
                                               targets,
                                               N,
                                               best_distances,
                                               second_distances,
                                               assignments);
                estimates.elem(targets) = result;
                ucbs.elem(targets) = result;
                lcbs.elem(targets) = result;
                exact_mask.elem(targets).fill(1);
                T_samples.elem(targets) += N;

                candidates = (lcbs < ucbs.min()) && (exact_mask == 0);
            }
            if (arma::accu(candidates) < precision) {
                break;
            }
            targets = arma::find(candidates);
            arma::vec result = swap_target(medoid_indices,
                                           targets,
                                           batchSize,
                                           best_distances,
                                           second_distances,
                                           assignments);
            estimates.elem(targets) =
              ((T_samples.elem(targets) % estimates.elem(targets)) +
               (result * batchSize)) /
              (batchSize + T_samples.elem(targets));
            T_samples.elem(targets) += batchSize;
            arma::vec adjust(targets.n_rows);
            adjust.fill(p);
            adjust = arma::log(adjust);
            arma::vec cb_delta = sigma.elem(targets) %
                                 arma::sqrt(adjust / T_samples.elem(targets));

            ucbs.elem(targets) = estimates.elem(targets) + cb_delta;
            lcbs.elem(targets) = estimates.elem(targets) - cb_delta;
            candidates = (lcbs < ucbs.min()) && (exact_mask == 0);
            targets = arma::find(candidates);
        }
        // now switch medoids
        arma::uword new_medoid = lcbs.index_min();
        // extract medoid of swap
        size_t k = new_medoid % medoids.n_cols;

        // extract data point of swap
        size_t n = new_medoid / medoids.n_cols;
        swap_performed = medoid_indices(k) != n;
        steps++;

        // logBuffer << (swap_performed ? ("swap performed")
        //                              : ("no swap performed"))
        //                              << " " << medoid_indices(k) << "to" << n
        //                              << '\n';
        // log(2);
        medoid_indices(k) = n;
        medoids.col(k) = data.col(medoid_indices(k));
        calc_best_distances_swap(
          medoid_indices, best_distances, second_distances, assignments);
        // arma::rowvec P = {0.25, 0.5, 0.75};
        // arma::rowvec Q = arma::quantile(sigma.elem(targets), P);
        std::ostringstream sigma_out;
        sigma_out << "Sigma: min: " << sigma.min()
        // << ", 25th: " << Q(0)
        // << ", median: " << Q(1)
        // << ", 75th: " << Q(2)
        << ", max: " << sigma.max()
        << ", mean: " << arma::mean(arma::mean(sigma));
        logHelper.sigma_swap.push_back(sigma_out.str());
        logHelper.loss_swap.push_back(arma::mean(arma::mean(best_distances)));
        logHelper.p_swap.push_back((float)1/(float)p);
    }
}

void KMedoids::calc_best_distances_swap(
  arma::rowvec& medoid_indices,
  arma::rowvec& best_distances,
  arma::rowvec& second_distances,
  arma::rowvec& assignments)
{
#pragma omp parallel for
    for (size_t i = 0; i < data.n_cols; i++) {
        double best = std::numeric_limits<double>::infinity();
        double second = std::numeric_limits<double>::infinity();
        for (size_t k = 0; k < medoid_indices.n_cols; k++) {
            double cost = (this->*lossFn)(medoid_indices(k), i);
            if (cost < best) {
                assignments(i) = k;
                second = best;
                best = cost;
            } else if (cost < second) {
                second = cost;
            }
        }
        best_distances(i) = best;
        second_distances(i) = second;
    }
}

arma::vec KMedoids::swap_target(
  arma::rowvec& medoid_indices,
  arma::uvec& targets,
  size_t batch_size,
  arma::rowvec& best_distances,
  arma::rowvec& second_best_distances,
  arma::rowvec& assignments)
{
    size_t N = data.n_cols;
    arma::vec estimates(targets.n_rows, arma::fill::zeros);
    arma::uvec tmp_refs = arma::randperm(N,
                                   batch_size); // without replacement, requires
                                                // updated version of armadillo

// for each considered swap
#pragma omp parallel for
    for (size_t i = 0; i < targets.n_rows; i++) {
        double total = 0;
        // extract data point of swap
        size_t n = targets(i) / medoid_indices.n_cols;
        size_t k = targets(i) % medoid_indices.n_cols;
        // calculate total loss for some subset of the data
        for (size_t j = 0; j < batch_size; j++) {
            double cost = (this->*lossFn)(n, tmp_refs(j));
            if (k == assignments(tmp_refs(j))) {
                if (cost < second_best_distances(tmp_refs(j))) {
                    total += cost;
                } else {
                    total += second_best_distances(tmp_refs(j));
                }
            } else {
                if (cost < best_distances(tmp_refs(j))) {
                    total += cost;
                } else {
                    total += best_distances(tmp_refs(j));
                }
            }
            total -= best_distances(tmp_refs(j));
        }
        estimates(i) = total / tmp_refs.n_rows;
    }
    return estimates;
}

void KMedoids::swap_sigma(
  arma::mat& sigma,
  size_t batch_size,
  arma::rowvec& best_distances,
  arma::rowvec& second_best_distances,
  arma::rowvec& assignments)
{
    size_t N = data.n_cols;
    size_t K = sigma.n_rows;
    arma::uvec tmp_refs = arma::randperm(N,
                                   batch_size); // without replacement, requires
                                                // updated version of armadillo

    arma::vec sample(batch_size);
// for each considered swap
#pragma omp parallel for
    for (size_t i = 0; i < K * N; i++) {
        // extract data point of swap
        size_t n = i / K;
        size_t k = i % K;

        // calculate change in loss for some subset of the data
        for (size_t j = 0; j < batch_size; j++) {
            double cost = (this->*lossFn)(n,tmp_refs(j));

            if (k == assignments(tmp_refs(j))) {
                if (cost < second_best_distances(tmp_refs(j))) {
                    sample(j) = cost;
                } else {
                    sample(j) = second_best_distances(tmp_refs(j));
                }
            } else {
                if (cost < best_distances(tmp_refs(j))) {
                    sample(j) = cost;
                } else {
                    sample(j) = best_distances(tmp_refs(j));
                }
            }
            sample(j) -= best_distances(tmp_refs(j));
        }
        sigma(k, n) = arma::stddev(sample);
    }
}

double KMedoids::calc_loss(
  arma::rowvec& medoid_indices)
{
    double total = 0;

    for (size_t i = 0; i < data.n_cols; i++) {
        double cost = std::numeric_limits<double>::infinity();
        for (size_t k = 0; k < n_medoids; k++) {
            double currCost = (this->*lossFn)(medoid_indices(k), i);
            if (currCost < cost) {
                cost = currCost;
            }
        }
        total += cost;
    }
    return total;
}

// Loss and miscellaneous functions

void KMedoids::log(int priority) {
  logFile << logBuffer.rdbuf();
  logFile.clear();
}

double KMedoids::L1(int i, int j) const {
    return arma::norm(data.col(i) - data.col(j), 1);
}

double KMedoids::L2(int i, int j) const {
    return arma::norm(data.col(i) - data.col(j), 2);
}

double KMedoids::cos(int i, int j) const {
    return arma::dot(data.col(i), data.col(j)) / (arma::norm(data.col(i)) * arma::norm(data.col(j)));
}

double KMedoids::manhattan(int i, int j) const {
    return arma::accu(arma::abs(data.col(i) - data.col(j)));
}
