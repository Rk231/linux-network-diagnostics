#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

backup="$(mktemp)"
cp config.json "$backup"

cleanup() {
    cp "$backup" config.json
    rm -f "$backup" /tmp/netdiag_test_output.txt
}
trap cleanup EXIT

# Restore the original valid configuration before each test.

restore_config() {
    cp "$backup" config.json

    # Ensure the tests have at least two targets.
    python3 - <<'PY'
import json

with open("config.json") as f:
    config = json.load(f)

targets = config.get("targets", [])

if not targets:
    raise SystemExit("Test setup failed: no targets in config.json")

if len(targets) < 2:
    second = dict(targets[0])
    second["name"] = "test-secondary-target"
    second["port"] = (
        5999 if targets[0].get("port") != 5999 else 5998
    )
    targets.append(second)

config["targets"] = targets

with open("config.json", "w") as f:
    json.dump(config, f)
PY
}


expect_failure() {
    local description="$1"

    # A timeout is not considered a successful rejection.
    local rc=0
    timeout 3s ./diag > /tmp/netdiag_test_output.txt 2>&1 || rc=$?

    if [[ "$rc" -eq 1 ]]; then
        echo "PASS: $description"
    elif [[ "$rc" -eq 124 ]]; then
        echo "FAIL: $description (monitor did not reject config)"
        cat /tmp/netdiag_test_output.txt
        exit 1
    else
        echo "FAIL: $description (exit code $rc)"
        cat /tmp/netdiag_test_output.txt
        exit 1
    fi
}

# Test 1: Duplicate target names
restore_config
python3 - <<'PY'
import json
p = "config.json"
with open(p) as f:
    c = json.load(f)
c["targets"][1]["name"] = c["targets"][0]["name"]
with open(p, "w") as f:
    json.dump(c, f)
PY
expect_failure "Duplicate target names rejected"

# Test 2: Invalid port
restore_config
python3 - <<'PY'
import json
p = "config.json"
with open(p) as f:
    c = json.load(f)
c["targets"][1]["port"] = 70000
with open(p, "w") as f:
    json.dump(c, f)
PY
expect_failure "Invalid port rejected"

# Test 3: Invalid timeout
restore_config
python3 - <<'PY'
import json
p = "config.json"
with open(p) as f:
    c = json.load(f)
c["targets"][1]["timeout_ms"] = 0
with open(p, "w") as f:
    json.dump(c, f)
PY
expect_failure "Invalid timeout rejected"

# Test 4: Malformed JSON
restore_config
printf '{ invalid json' > config.json
expect_failure "Malformed JSON rejected"

# Test 5: Missing required field
restore_config
python3 - <<'PY'
import json
p = "config.json"
with open(p) as f:
    c = json.load(f)
del c["targets"][0]["host"]
with open(p, "w") as f:
    json.dump(c, f)
PY
expect_failure "Missing required field rejected"

echo "All configuration rejection tests passed."