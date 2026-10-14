#pragma once

enum class HealthStatus {
    Unknown,
    Up,
    Down
};

inline void updateHealthStatus(
    HealthStatus& status,
    int& consecutiveSuccesses,
    int& consecutiveFailures,
    bool success,
    int recoveryThreshold = 2,
    int failureThreshold = 3
) {
    if (success) {
        consecutiveFailures = 0;
        ++consecutiveSuccesses;

        if (consecutiveSuccesses >= recoveryThreshold) {
            status = HealthStatus::Up;
        }
    } else {
        consecutiveSuccesses = 0;
        ++consecutiveFailures;

        if (consecutiveFailures >= failureThreshold) {
            status = HealthStatus::Down;
        }
    }
}