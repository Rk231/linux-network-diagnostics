#include "health_state.h"

#include <cassert>
#include <iostream>

int main() {
    HealthStatus status = HealthStatus::Unknown;
    int successes = 0;
    int failures = 0;

    // Two successes should transition UNKNOWN -> UP.
    updateHealthStatus(status, successes, failures, true);
    assert(status == HealthStatus::Unknown);
    updateHealthStatus(status, successes, failures, true);
    assert(status == HealthStatus::Up);
    assert(failures == 0);

    // Three failures should transition UP -> DOWN.
    updateHealthStatus(status, successes, failures, false);
    assert(status == HealthStatus::Up);
    updateHealthStatus(status, successes, failures, false);
    assert(status == HealthStatus::Up);
    updateHealthStatus(status, successes, failures, false);
    assert(status == HealthStatus::Down);
    assert(successes == 0);

    // Two successes should recover DOWN -> UP.
    updateHealthStatus(status, successes, failures, true);
    assert(status == HealthStatus::Down);
    updateHealthStatus(status, successes, failures, true);
    assert(status == HealthStatus::Up);
    assert(failures == 0);

    // A failure resets the consecutive-success counter.
    status = HealthStatus::Unknown;
    successes = 0;
    failures = 0;

    updateHealthStatus(status, successes, failures, true);
    updateHealthStatus(status, successes, failures, false);
    assert(successes == 0);
    assert(failures == 1);
    assert(status == HealthStatus::Unknown);

    // Test 5: A success resets consecutive failures.
    status = HealthStatus::Unknown;
    successes = 0;
    failures = 0;

    updateHealthStatus(status, successes, failures, false);
    updateHealthStatus(status, successes, failures, false);
    assert(failures == 2);

    updateHealthStatus(status, successes, failures, true);
    assert(failures == 0);
    assert(successes == 1);
    assert(status == HealthStatus::Unknown);

    // Test 6: Custom thresholds work correctly.
    status = HealthStatus::Unknown;
    successes = 0;
    failures = 0;

    updateHealthStatus(status, successes, failures, true, 3, 2);
    assert(status == HealthStatus::Unknown);

    updateHealthStatus(status, successes, failures, true, 3, 2);
    assert(status == HealthStatus::Unknown);

    updateHealthStatus(status, successes, failures, true, 3, 2);
    assert(status == HealthStatus::Up);

    std::cout << "All health-state tests passed!\n";
    return 0;
}