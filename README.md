# NetDiag — Linux Network Diagnostics Tool

NetDiag is a C++17-based network diagnostics project for monitoring TCP connectivity and application responsiveness on Linux.

## Features

- TCP connectivity checks
- Configurable connection timeouts
- Multiple target monitoring through JSON configuration
- UP/DOWN health-state tracking
- Recovery detection after consecutive successful checks
- Live terminal dashboard using ncurses
- Diagnostic logging and report generation
- Automated tests and sanitizer checks

## Tech Stack

- C++17
- Linux socket programming
- POSIX APIs
- ncurses
- GNU Make
- JSON configuration

## Project Structure

```text
netdiag/
├── src/
│   ├── diag.cpp
│   ├── diag_dashboard.cpp
│   └── server.cpp
├── include/
│   └── health_state.h
├── tests/
│   ├── test_config.sh
│   └── test_health.cpp
├── Makefile
├── config.example.json
└── README.md
```

## Build

```bash
make
```

## Configure

```bash
cp config.example.json config.json
```

Edit `config.json` to configure the hosts, ports, and timeouts you want to monitor.

## Run

Start the demo server:

```bash
./server
```

In another terminal, run the diagnostic CLI:

```bash
./diag
```

Or launch the live dashboard:

```bash
./diag_dashboard
```

## Test

```bash
make check
make sanitize-check
```

## Learning Objectives

This project demonstrates TCP socket programming, Linux networking, C++ concurrency, health monitoring, terminal interfaces, and automated testing.

