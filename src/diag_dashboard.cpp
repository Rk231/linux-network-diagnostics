#include "health_state.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>
#include <stdexcept>
#include <ctime>
#include <sstream>
#include <csignal>
#include <atomic>
#include <future>
#include <exception>

#include <nlohmann/json.hpp>
#include <map>
#include <limits>
#include <algorithm>
#include <iomanip>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <set>
#include <ncurses.h>
#include <deque>

std::atomic<bool> shutdownRequested{false};

void handleSignal(int signal){
    if (signal == SIGINT) {
        shutdownRequested.store(true);
    }
}

using Clock = std::chrono::steady_clock;
std::string currentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);

    std::tm localTime{};

    if (localtime_r(&time, &localTime) == nullptr) {
        return "unknown-time";
    }

    std::ostringstream output;
    output << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S");

    return output.str();
}

constexpr int CONNECT_TIMEOUT_MS = 2000;
constexpr int RESPONSE_TIMEOUT_SEC = 2;
constexpr int ATTEMPTS = 5;

struct Result {
    bool applicationSuccess = false;
    bool tcpConnected = false;
    double connectMs = 0.0;
    std::string error;
    double checkMs = 0.0;
};

Result finishCheck(
    Result result,
    Clock::time_point checkStart
) {
    result.checkMs =
        std::chrono::duration<double, std::milli>(
            Clock::now() - checkStart
        ).count();

    return result;
}

Result testConnection(addrinfo* addresses, int timeoutMs) {

    const auto checkStart = std::chrono::steady_clock::now();
    std::string lastError = 
        "Could not create or configure a usable socket";

    for (addrinfo* p = addresses; p != nullptr; p = p->ai_next) {

        int fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);

        if (fd < 0) {

            lastError = std::string("socket() failed: ")+
                        std::strerror(errno);
            continue;
        } 


        int oldFlags = fcntl(fd, F_GETFL, 0);

        if (oldFlags < 0 ) {

            lastError = std::string("fcntl(F_GETFL) failed") +
                        std::strerror(errno);
            close(fd);
            continue;
        }

        if (fcntl(fd, F_SETFL, oldFlags | O_NONBLOCK) < 0) {

            lastError = std::string("fcntl(F_SETFL) failed: ") +
                        std::strerror(errno);
            close(fd);
            continue;
        }

        auto start = Clock::now();
        int rc = connect(fd, p->ai_addr, p->ai_addrlen);

        if (rc < 0 && errno != EINPROGRESS) {
            int error = errno;
            close(fd);
            
            if (error == ECONNREFUSED) {
                lastError = "Connection refused: port is closed";
            } else if (error == ENETUNREACH) {
                lastError = "Network is unreacheable";
            } else if (error == EHOSTUNREACH) {
                lastError = "Host is unreachable";
            } else {
                lastError = std::string("Connection error: ") + 
                            std::strerror(error);
            }

            continue;
        }

        if (rc < 0) {
            pollfd event{};
            event.fd = fd;
            event.events = POLLOUT;

            
        auto deadline = Clock::now() +
            std::chrono::milliseconds(timeoutMs);

        do {
            auto remaining =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    deadline - Clock::now()
                ).count();

            if (remaining <= 0) {
                close(fd);
                return finishCheck(
                    {false, false, static_cast<double>(timeoutMs),
                    "Connection timed out"},
                    checkStart
                );
            }

            rc = poll(&event, 1, static_cast<int>(remaining));

        } while (rc < 0 && errno == EINTR);

        if (rc == 0) {
            close(fd);
            return finishCheck(
                {false, false, static_cast<double>(timeoutMs),
                "Connection timed out"},
                checkStart
            );
        }

        if (rc < 0) {
            int error = errno;
            close(fd);

            return {false, false, 0.0,
                std::string("poll error: ") +
                std::strerror(error)};
        }


            int error = 0;
            socklen_t len = sizeof(error);

            if (getsockopt(fd, SOL_SOCKET, SO_ERROR,
                           &error, &len) < 0) {
                error = errno;
            }

            if (error != 0) {
                close(fd);

                if (error == ECONNREFUSED) {
                    return {false, false, 0.0,
                            "Connection refused: port is closed"};
                }

                if (error == ENETUNREACH) {
                    return {false, false, 0.0, 
                            "Network is unreachable"};
                }

                if (error == EHOSTUNREACH) {
                    return {false, false, 0.0, 
                            "Host is unreachable"};
                } 

                return {false, false, 0.0,
                        std::string("Connection error: ") + 
                        std::strerror(error)};
            }

        }

        double elapsed = 
            std::chrono::duration<double, std::milli>(
                Clock::now() - start).count();

        if (fcntl(fd, F_SETFL, oldFlags) < 0) {
            close(fd);
            return {true, false, elapsed,
                    "Could not restore socket flags"};
        }

        timeval timeout{};
        timeout.tv_sec = timeoutMs / 1000;
        timeout.tv_usec = (timeoutMs % 1000) * 1000;

        if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO,
                       &timeout, sizeof(timeout)) < 0) {
            int error = errno;
            close(fd);
            
            return {true, false, elapsed,
                    std::string("Could not set receive timeout: ") + 
                    std::strerror(error)};
        }

        const std::string request = "diagnostic request\n";
        size_t sent = 0;

        while (sent < request.size()) {
            ssize_t n = send(fd, request.data() + sent,
                             request.size() - sent, MSG_NOSIGNAL);

            if (n < 0 && errno == EINTR){
                continue;
            }

            if (n <= 0) {
                int error = errno;
                close(fd);

                return {true, false, elapsed,
                        std::string("Send failed: ") + 
                        std::strerror(error)};
            }

            sent += static_cast<size_t>(n);
        }

        char buffer[1024];
        ssize_t n;

        do {
            n = recv(fd, buffer, sizeof(buffer) - 1, 0);
        } while (n < 0 && errno == EINTR);

        if (n > 0) {
            buffer[n] = '\0';
            std::string response(buffer);

            
            const std::string expected =
                    "OK: diagnostic server reachable";
            
            bool valid =
                response.rfind(expected, 0) == 0;
                
            close(fd);

            if (valid) {
                return finishCheck(
                    {true, true, elapsed, ""},
                    checkStart
                );
            }

            return {false, true, elapsed,
                    "Unexpected application response"};
        }

        if (n == 0) {
            close(fd);
            return {false, true, elapsed,
                 "Server closed connection without a response"};
        }

        int error = errno;
        close(fd);

        if (error == EAGAIN || error == EWOULDBLOCK) {
            return {false, true, elapsed,
                 "Application response timed out"};
        }

         return {false, true,elapsed,
                std::string("Recieve error: ") + 
                std::strerror(error)};
         } 

    return finishCheck(
        {false, false, 0.0, lastError},
        checkStart
    );
}


struct TargetState {
    HealthStatus status = HealthStatus::Unknown;
    int consecutiveFailures = 0;
    int consecutiveSuccesses = 0;

    std::chrono::steady_clock::time_point stateSince = Clock::now();
    std::chrono::duration<double> totalUpTime{0};
    std::chrono::duration<double> totalDownTime{0};
    std::chrono::duration<double> totalUnknownTime{0};
};

struct TargetStats {
    unsigned long totalChecks = 0;
    unsigned long successfulChecks = 0;
    unsigned long failedChecks = 0;
    double totalLatencyMs = 0.0;
    unsigned long latencySamples = 0;
    double totalCheckDurationMs = 0.0;
};

const char* statusName(HealthStatus status) {
    switch (status) {
        case HealthStatus::Unknown: return "UNKNOWN";
        case HealthStatus::Up:      return "UP";
        case HealthStatus::Down:    return "DOWN";
    }

    return "UNKNOWN";
}

std::deque<std::string> dashboardEvents;

void addDashboardEvent(const std::string& event) {
    dashboardEvents.push_back(
        currentTimestamp() + " " + event
    );

    while (dashboardEvents.size() > 6) {
        dashboardEvents.pop_front();
    }
}

template <typename Targets>
void drawDashboard(
    const Targets& targets,
    const std::map<std::string, TargetState>& states,
    const std::map<std::string, TargetStats>& statistics,
    Clock::time_point monitoringStart,
    int interval,
    const std::string& message
) {
    erase();

    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    attron(COLOR_PAIR(1) | A_BOLD);
    mvhline(0, 0, ' ', cols);
    mvprintw(0, 2, " NETDIAG | LINUX NETWORK DIAGNOSTICS ");
    mvprintw(0, std::max(0, cols - 12), " LIVE ");
    attroff(COLOR_PAIR(1) | A_BOLD);

    double elapsed =
        std::chrono::duration<double>(
            Clock::now() - monitoringStart
        ).count();

    mvprintw(2, 2, "Monitoring: RUNNING");
    mvprintw(2, 27, "Interval: %d sec", interval);
    mvprintw(2, 52, "Elapsed: %.0f sec", elapsed);

    unsigned long total = 0;
    unsigned long successful = 0;
    double latencyTotal = 0.0;
    unsigned long latencySamples = 0;

    for (const auto& entry : statistics) {
        total += entry.second.totalChecks;
        successful += entry.second.successfulChecks;
        latencyTotal += entry.second.totalLatencyMs;
        latencySamples += entry.second.latencySamples;
    }

    double successRate = total
        ? 100.0 * successful / total
        : 0.0;

    double averageLatency = latencySamples
        ? latencyTotal / latencySamples
        : 0.0;

    mvprintw(5, 2, "Total checks: %lu", total);
    mvprintw(5, 25, "Success rate: %.2f%%", successRate);
    mvprintw(5, 53, "Avg TCP latency: %.2f ms", averageLatency);

    attron(A_BOLD);
    mvprintw(7, 2,  "TARGET");
    mvprintw(7, 26, "ENDPOINT");
    mvprintw(7, 54, "HEALTH");
    mvprintw(7, 70, "CHECKS");
    attroff(A_BOLD);

    

    int y = 8;

    for (const auto& target : targets) {
        if (y >= rows - 10) {
            break;
        }

        auto stateIt = states.find(target.name);
        auto statsIt = statistics.find(target.name);

        HealthStatus health = stateIt != states.end()
            ? stateIt->second.status
            : HealthStatus::Unknown;

        const char* healthText = statusName(health);

        int color = health == HealthStatus::Up ? 2
                  : health == HealthStatus::Down ? 3
                  : 4;

        mvprintw(y, 2, "%-20.20s", target.name.c_str());
        mvprintw(
            y,
            std::max(26, cols / 3),
            "%s:%d",
            target.host.c_str(),
            target.port
        );

        attron(COLOR_PAIR(color) | A_BOLD);
        mvprintw(y, std::max(54, cols / 2), "%s", healthText);
        attroff(COLOR_PAIR(color) | A_BOLD);

        if (statsIt != statistics.end()) {
            mvprintw(
                y,
                std::max(70, (cols * 2) / 3),
                "%lu",
                statsIt->second.totalChecks
            );
        }

        ++y;
    }

    int eventsY = std::max(9, rows - 9);

    attron(A_BOLD);
    mvprintw(eventsY, 2, "RECENT EVENTS");
    attroff(A_BOLD);

    int eventY = eventsY + 1;

    for (const auto& event : dashboardEvents) {
        if (eventY >= rows - 2) {
            break;
        }

        mvprintw(eventY++, 2, "%.*s",
                 std::max(0, cols - 4), event.c_str());
    }

    attron(A_BOLD);
    mvprintw(rows - 1, 2, "Ctrl+C: stop monitoring");
    attroff(A_BOLD);

    if (rows > 10) {
        mvprintw(rows - 2, 2, "%.*s",
                 std::max(0, cols - 4), message.c_str());
    }

    refresh();
}


void writeReport(
    const std::map<std::string, TargetStats>& statistics,
    const std::map<std::string, TargetState>& states,
    Clock::time_point monitoringStart
) {
    std::ofstream report("report.txt");

    if (!report) {
        std::cerr << "Could not open report.txt\n";
        return;
    }

    report << "NetDiag Monitoring Report\n";
    report << "Generated: " << currentTimestamp() << "\n\n";

    
    double monitoringDuration =
        std::chrono::duration<double>(
            Clock::now() - monitoringStart
        ).count();

    report << "Monitoring duration: "
           << monitoringDuration << " seconds\n\n";


    for (const auto& [name, stats] : statistics) {
        double successRate = stats.totalChecks > 0
            ? 100.0 * stats.successfulChecks / stats.totalChecks
            : 0.0;

        double averageLatency = stats.latencySamples > 0
            ? stats.totalLatencyMs / stats.latencySamples
            : 0.0;
        
        double averageCheckDuration = stats.totalChecks > 0
            ? stats.totalCheckDurationMs / stats.totalChecks
            : 0.0;

        auto it = states.find(name);

        double upSeconds = 0.0;
        double downSeconds = 0.0;
        double unknownSeconds = 0.0;

        if (it != states.end()) {
            const auto& state = it->second;

            upSeconds = state.totalUpTime.count();
            downSeconds = state.totalDownTime.count();
            unknownSeconds = state.totalUnknownTime.count();

            auto currentElapsed = 
            std::chrono::duration<double>(
                Clock::now() - state.stateSince
            ).count();

            if (state.status == HealthStatus::Up) {
                upSeconds += currentElapsed;
            } else if (state.status == HealthStatus::Down) {
                downSeconds += currentElapsed;
            } else {
                unknownSeconds += currentElapsed;
            }
        }

        double knownTime = upSeconds + downSeconds;

        double uptimePercentage = knownTime > 0
            ? 100.0 * upSeconds / knownTime
            : 0.0;

        report << "Target: " << name << '\n';
        report << "Health: "
               << (it != states.end()
                       ? statusName(it->second.status)
                       : "UNKNOWN")
               << '\n';
        report << "Total checks: " << stats.totalChecks << '\n';
        report << "Successful: " << stats.successfulChecks << '\n';
        report << "Failed: " << stats.failedChecks << '\n';
        report << "Success rate: " << std::fixed
               << std::setprecision(2) << successRate << "%\n";
        report << "Average TCP latency: " << averageLatency
               << " ms\n";
        report << "Average total check duration: "
               << averageCheckDuration << " ms\n\n";
        report << "Time UP: " << upSeconds << " seconds\n";
        report << "Time DOWN: " << downSeconds << " seconds\n";
        report << "Time UNKNOWN: " << unknownSeconds << " seconds\n";

        if (knownTime > 0) {
            report << "Uptime: " << std::fixed
                   << std::setprecision(2)
                   << uptimePercentage << "%\n";
        } else {
            report << "Uptime: N/A (no known UP/DOWN duration yet)\n";
        }

        report << "Known UP/DOWN time: "
               << std::fixed << std::setprecision(2)
               << knownTime << " seconds\n";

        report << "----------------------------------------\n\n";
    }
}

int main() {
    using json = nlohmann::json;

    struct Target {
        std::string name;
        std::string host;
        int port;
        int timeoutMs;
    };

    json config;

    try {
        std::ifstream file("config.json");

        if (!file) {
            std::cerr << "Cannot open config.json\n";
            return 1;
        }

        file >> config;
    } catch (const std::exception& e) {
        std::cerr << "Configuration error: "
                  << e.what() << '\n';
        return 1;
    }

    int interval = 0;
    std::vector<Target> targets;
    std::set<std::string> targetNames;

    try {
        if (!config.is_object()) {
            throw std::runtime_error(
                "Configuration root must be a JSON object");
        }

        interval = config.at("check_interval_seconds").get<int>();

        if (interval < 1) {
            throw std::runtime_error(
                "Check interval must be at least 1 second");
        }

        const auto& targetList = config.at("targets");

        if (!targetList.is_array()) {
            throw std::runtime_error(
                "'targets' must be a JSON array");
        }

        if (targetList.empty()) {
            throw std::runtime_error(
                "At least one target is required");
        }

        std::set<std::string> targetNames;

        for (const auto& item : targetList) {
            if (!item.is_object()) {
                throw std::runtime_error(
                    "Each target must be a JSON object");
            }

            Target target{
                item.at("name").get<std::string>(),
                item.at("host").get<std::string>(),
                item.at("port").get<int>(),
                item.at("timeout_ms").get<int>()
            };

            if (target.name.empty() || target.host.empty()) {
                throw std::runtime_error(
                    "Target name and host must not be empty");
            }

            if (target.port < 1 || target.port > 65535) {
                throw std::runtime_error(
                    "Target port must be between 1 and 65535");
            }

            if (target.timeoutMs < 1) {
                throw std::runtime_error(
                    "Target timeout must be at least 1 ms");
            }

            if (!targetNames.insert(target.name).second) {
                throw std::runtime_error(
                    "Duplicate target name: " + target.name);
            }

            targets.push_back(target);
        }
    } catch (const std::exception& e) {
        std::cerr << "Invalid configuration: "
                  << e.what() << '\n';
        return 1;
    }

    std::map<std::string, TargetState> states;
    std::map<std::string, TargetStats> statistics;
    std::signal(SIGINT, handleSignal);

    std::ofstream log("diagnostics.log", std::ios::app);

    if (!log) {
        std::cerr << "Cannot open diagnostics.log\n";
        return 1;
    }

    const int FAILURE_THRESHOLD = 3;
    const int RECOVERY_THRESHOLD = 2;
    
    auto monitoringStart = std::chrono::steady_clock::now();

    initscr();
    cbreak();
    noecho();
    curs_set(0);
    keypad(stdscr, TRUE);
    start_color();
    use_default_colors();

    init_pair(1, COLOR_CYAN, COLOR_BLUE);
    init_pair(2, COLOR_GREEN, -1);
    init_pair(3, COLOR_RED, -1);
    init_pair(4, COLOR_YELLOW, -1);

    while (!shutdownRequested.load()) {
                // Start a network check for every target concurrently.
        std::vector<std::future<Result>> checkFutures;
        checkFutures.reserve(targets.size());

        for (const auto& target : targets) {
            checkFutures.push_back(
                std::async(std::launch::async, [target]() -> Result {
                    addrinfo hints{};
                    hints.ai_family = AF_UNSPEC;
                    hints.ai_socktype = SOCK_STREAM;

                    addrinfo* addresses = nullptr;
                    std::string service = std::to_string(target.port);

                    int rc = getaddrinfo(
                        target.host.c_str(),
                        service.c_str(),
                        &hints,
                        &addresses
                    );

                    if (rc != 0) {
                        return {
                            false,
                            false,
                            0.0,
                            gai_strerror(rc)
                        };
                    }

                    Result result =
                        testConnection(addresses, target.timeoutMs);

                    freeaddrinfo(addresses);
                    return result;
                })
            );
        }
    for (std::size_t targetIndex = 0;
         targetIndex < targets.size();
         ++targetIndex) {
        
        const auto& target = targets[targetIndex];

        Result result{false, false, 0.0, ""};

        try {
            while (checkFutures[targetIndex].wait_for(
                    std::chrono::milliseconds(100))
                != std::future_status::ready) {

                drawDashboard(
                    targets, states, statistics,
                    monitoringStart, interval,
                    "Waiting for network checks..."
                );

                if (shutdownRequested.load()) {
        // Continue collecting results so background tasks finish safely.
                }
            }   
            result = checkFutures[targetIndex].get();

        } catch (const std::exception& e) {
            result.error = std::string("Async check failed: ") + e.what();
        } catch (...) {
            result.error = "Async check failed: unknown exception";
        }

        auto& state = states[target.name];
        HealthStatus oldStatus = state.status;

        auto& stats = statistics[target.name];

        ++stats.totalChecks;

        if (result.applicationSuccess) {
            ++stats.successfulChecks;
        } else {
            ++stats.failedChecks;
        }

        stats.totalCheckDurationMs += result.checkMs;

        if (result.tcpConnected) {
            stats.totalLatencyMs += result.connectMs;
            ++stats.latencySamples;
        }

        updateHealthStatus(
            state.status,
            state.consecutiveSuccesses,
            state.consecutiveFailures,
            result.applicationSuccess,
            RECOVERY_THRESHOLD,
            FAILURE_THRESHOLD
        );

        if (oldStatus != state.status) {
            auto now = Clock::now();
            auto elapsed = now - state.stateSince;

            if (oldStatus == HealthStatus::Up) {
                state.totalUpTime += elapsed;
            } else if (oldStatus == HealthStatus::Down) {
                state.totalDownTime += elapsed;
            } else if (oldStatus == HealthStatus::Unknown) {
                state.totalUnknownTime += elapsed;
            }

            state.stateSince = now;
        }

        
        addDashboardEvent(
            target.name + ": " +
            (result.applicationSuccess ? "CHECK PASSED" : "CHECK FAILED")
        );

        if (oldStatus != state.status) {
            addDashboardEvent(
                target.name + ": " +
                statusName(oldStatus) + " -> " +
                statusName(state.status)
            );
        }

        if (!result.applicationSuccess) {
            addDashboardEvent(
                target.name + ": " + result.error
            );
        }

        drawDashboard(
            targets, states, statistics,
            monitoringStart, interval,
            "Last check completed: " + target.name
        );



        log << "timestamp=\"" << currentTimestamp() << "\""
            << " target=" << target.name
            << " host=" << target.host
            << " port=" << target.port
            << " status=" << statusName(state.status)
            << " tcp=" << (result.tcpConnected ? "CONNECTED" : "FAILED")
            << " latency_ms=" << result.connectMs
            << " failures=" << state.consecutiveFailures
            << " successes=" << state.consecutiveSuccesses
            << " error=\"" << result.error << "\"\n";

        log.flush();
    }
    


    for (int i = 0; i< interval * 10; ++i){
        if (shutdownRequested.load()) {
            break;
        }
    
        std::this_thread::sleep_for(
            std::chrono::milliseconds(100)
        );

    }
}

    log.flush();
    log.close();
    endwin();

    writeReport(statistics, states, monitoringStart);

    std::cout << "Log saved to diagnostics.log\n"
              << "Report saved to report.txt\n";  

    return 0;
}

