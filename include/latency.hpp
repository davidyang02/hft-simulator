#pragma once

#include <vector>
#include <algorithm>
#include <cstdint>
#include <chrono>
#include <iostream>
#include <iomanip>
#include <cmath>

namespace hft {

/// High-resolution timer for latency measurement
class Timer {
public:
    using Clock = std::chrono::high_resolution_clock;
    using TimePoint = Clock::time_point;
    using Duration = std::chrono::nanoseconds;

    void start() {
        start_ = Clock::now();
    }

    Duration stop() {
        auto end = Clock::now();
        return std::chrono::duration_cast<Duration>(end - start_);
    }

    /// Get elapsed nanoseconds
    int64_t stopNs() {
        return stop().count();
    }

private:
    TimePoint start_;
};

/// Histogram for tracking latency distributions
class LatencyHistogram {
public:
    explicit LatencyHistogram(const std::string& name = "Latency")
        : name_(name) {}

    /// Record a latency sample (in nanoseconds)
    void record(int64_t latencyNs) {
        samples_.push_back(latencyNs);
        totalNs_ += latencyNs;
        minNs_ = std::min(minNs_, latencyNs);
        maxNs_ = std::max(maxNs_, latencyNs);
    }

    /// Get number of samples
    [[nodiscard]] size_t count() const { return samples_.size(); }

    /// Get minimum latency (ns)
    [[nodiscard]] int64_t minNs() const { return samples_.empty() ? 0 : minNs_; }

    /// Get maximum latency (ns)
    [[nodiscard]] int64_t maxNs() const { return samples_.empty() ? 0 : maxNs_; }

    /// Get mean latency (ns)
    [[nodiscard]] double meanNs() const {
        return samples_.empty() ? 0.0 : static_cast<double>(totalNs_) / samples_.size();
    }

    /// Get percentile latency (ns)
    /// Note: This sorts the samples on first call
    [[nodiscard]] int64_t percentileNs(double p) {
        if (samples_.empty()) return 0;
        
        if (!sorted_) {
            std::sort(samples_.begin(), samples_.end());
            sorted_ = true;
        }
        
        size_t idx = static_cast<size_t>(p / 100.0 * (samples_.size() - 1));
        return samples_[idx];
    }

    /// Get p50 (median)
    [[nodiscard]] int64_t p50Ns() { return percentileNs(50); }

    /// Get p95
    [[nodiscard]] int64_t p95Ns() { return percentileNs(95); }

    /// Get p99
    [[nodiscard]] int64_t p99Ns() { return percentileNs(99); }

    /// Get p99.9
    [[nodiscard]] int64_t p999Ns() { return percentileNs(99.9); }

    /// Convert nanoseconds to microseconds
    static double toUs(int64_t ns) { return ns / 1000.0; }

    /// Convert nanoseconds to milliseconds
    static double toMs(int64_t ns) { return ns / 1'000'000.0; }

    /// Print summary
    void printSummary() const {
        if (samples_.empty()) {
            std::cout << name_ << ": No samples" << std::endl;
            return;
        }

        // Need to cast away const for percentile calculations
        auto& self = const_cast<LatencyHistogram&>(*this);

        std::cout << std::fixed << std::setprecision(3);
        std::cout << "\n=== " << name_ << " ===" << std::endl;
        std::cout << "Samples:  " << samples_.size() << std::endl;
        std::cout << "Min:      " << toUs(minNs_) << " µs" << std::endl;
        std::cout << "Max:      " << toUs(maxNs_) << " µs" << std::endl;
        std::cout << "Mean:     " << toUs(static_cast<int64_t>(meanNs())) << " µs" << std::endl;
        std::cout << "p50:      " << toUs(self.p50Ns()) << " µs" << std::endl;
        std::cout << "p95:      " << toUs(self.p95Ns()) << " µs" << std::endl;
        std::cout << "p99:      " << toUs(self.p99Ns()) << " µs" << std::endl;
        std::cout << "p99.9:    " << toUs(self.p999Ns()) << " µs" << std::endl;
    }

    /// Reset histogram
    void reset() {
        samples_.clear();
        totalNs_ = 0;
        minNs_ = INT64_MAX;
        maxNs_ = 0;
        sorted_ = false;
    }

    /// Get name
    [[nodiscard]] const std::string& name() const { return name_; }

private:
    std::string name_;
    std::vector<int64_t> samples_;
    int64_t totalNs_ = 0;
    int64_t minNs_ = INT64_MAX;
    int64_t maxNs_ = 0;
    bool sorted_ = false;
};

/// RAII timer that records to a histogram
class ScopedTimer {
public:
    ScopedTimer(LatencyHistogram& histogram) : histogram_(histogram) {
        timer_.start();
    }

    ~ScopedTimer() {
        histogram_.record(timer_.stopNs());
    }

private:
    LatencyHistogram& histogram_;
    Timer timer_;
};

} // namespace hft
