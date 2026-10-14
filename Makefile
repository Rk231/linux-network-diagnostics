
CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -Wpedantic -g -pthread
CPPFLAGS = -Iinclude
SANFLAGS = -fsanitize=address,undefined -fno-omit-frame-pointer

.PHONY: all clean test check sanitize sanitize-check

all: server diag diag_dashboard

server: src/server.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) src/server.cpp -o server

diag: src/diag.cpp include/health_state.h
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) src/diag.cpp -o diag

diag_dashboard: src/diag_dashboard.cpp include/health_state.h
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) src/diag_dashboard.cpp -o diag_dashboard -lncurses

test_health: tests/test_health.cpp include/health_state.h
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/test_health.cpp -o test_health

test: test_health
	./test_health

check: test
	bash tests/test_config.sh

sanitize:
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(SANFLAGS) src/server.cpp -o server_san
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(SANFLAGS) src/diag.cpp -o diag_san

test_health_san: tests/test_health.cpp include/health_state.h
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(SANFLAGS) tests/test_health.cpp -o test_health_san

sanitize-check: test_health_san
	./test_health_san

clean:
	rm -f server diag diag_dashboard test_health server_san diag_san test_health_san diagnostics.log report.txt
