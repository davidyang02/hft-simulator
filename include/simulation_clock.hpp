#pragma once

#include <cstdint>
#include <chrono>
#include <functional>
#include <queue>
#include <vector>

namespace hft {

/// Simulation clock for discrete event simulation
class SimulationClock {
public:
    using TimePoint = uint64_t;  // Nanoseconds from simulation start
    using Duration = uint64_t;   // Duration in nanoseconds
    
    SimulationClock() : currentTime_(0) {}

    /// Get current simulation time
    [[nodiscard]] TimePoint now() const { return currentTime_; }

    /// Advance time by duration
    void advance(Duration duration) { currentTime_ += duration; }

    /// Set current time
    void setTime(TimePoint time) { currentTime_ = time; }

    /// Reset clock to zero
    void reset() { currentTime_ = 0; }

    /// Convert milliseconds to simulation time
    static constexpr Duration fromMs(uint64_t ms) { return ms * 1'000'000; }
    
    /// Convert seconds to simulation time
    static constexpr Duration fromSeconds(double seconds) { 
        return static_cast<Duration>(seconds * 1'000'000'000); 
    }

    /// Convert simulation time to seconds
    static constexpr double toSeconds(Duration duration) {
        return static_cast<double>(duration) / 1'000'000'000.0;
    }

private:
    TimePoint currentTime_;
};

/// Event in the simulation
struct SimEvent {
    SimulationClock::TimePoint time;
    std::function<void()> action;
    uint64_t priority;  // Lower = higher priority for same time

    bool operator>(const SimEvent& other) const {
        if (time != other.time) return time > other.time;
        return priority > other.priority;
    }
};

/// Event-driven simulation scheduler
class EventScheduler {
public:
    explicit EventScheduler(SimulationClock& clock) : clock_(clock) {}

    /// Schedule an event at a specific time
    void scheduleAt(SimulationClock::TimePoint time, std::function<void()> action, 
                    uint64_t priority = 0) {
        events_.push({time, std::move(action), priority});
    }

    /// Schedule an event after a delay from current time
    void scheduleAfter(SimulationClock::Duration delay, std::function<void()> action,
                       uint64_t priority = 0) {
        scheduleAt(clock_.now() + delay, std::move(action), priority);
    }

    /// Process next event
    /// Returns false if no more events
    bool processNext() {
        if (events_.empty()) return false;
        
        SimEvent event = events_.top();
        events_.pop();
        
        clock_.setTime(event.time);
        event.action();
        
        return true;
    }

    /// Process all events until end time
    void runUntil(SimulationClock::TimePoint endTime) {
        while (!events_.empty() && events_.top().time <= endTime) {
            processNext();
        }
        clock_.setTime(endTime);
    }

    /// Check if there are pending events
    [[nodiscard]] bool hasPendingEvents() const { return !events_.empty(); }

    /// Get number of pending events
    [[nodiscard]] size_t pendingEventCount() const { return events_.size(); }

    /// Clear all pending events
    void clear() {
        while (!events_.empty()) events_.pop();
    }

private:
    SimulationClock& clock_;
    std::priority_queue<SimEvent, std::vector<SimEvent>, std::greater<SimEvent>> events_;
};

} // namespace hft
