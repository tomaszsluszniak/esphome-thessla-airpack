#!/usr/bin/env bash
# Builds tests/host_e2e.yaml for the ESPHome "host" platform with plain g++ and runs it
# against tools/airpack_simulator.py. Usage: tests/run_host_test.sh [seconds]
#
# Needs an ESPHome checkout importable by python3 (ESPHOME_PYTHONPATH) and ArduinoJson headers
# (ARDUINOJSON_INCLUDE). With a normal `pip install esphome`, `esphome run tests/host_e2e.yaml`
# does the same build through platformio.
set -euo pipefail
cd "$(dirname "$0")"
DURATION="${1:-40}"
BUILD=.esphome/build/airpack-host-test
LOGDIR="${LOGDIR:-$PWD/.logs}"
mkdir -p "$LOGDIR"

PYTHONPATH="${ESPHOME_PYTHONPATH:-}" python3 -m esphome compile --only-generate host_e2e.yaml >"$LOGDIR/generate.log" 2>&1 \
  || { tail -20 "$LOGDIR/generate.log"; exit 1; }

FLAGS="-std=gnu++20 -O1 -DUSE_HOST -DESPHOME_LOG_LEVEL=ESPHOME_LOG_LEVEL_${LOG_LEVEL:-DEBUG} -fno-exceptions \
  -Wno-sign-compare -Wno-unused-but-set-variable -Wno-unused-function -Wno-unused-variable"
rm -rf "$BUILD/obj" && mkdir -p "$BUILD/obj"
for f in $(find "$BUILD/src" -name '*.cpp'); do
  o="$BUILD/obj/$(echo "${f#"$BUILD"/src/}" | tr '/' '_').o"
  g++ $FLAGS -I "$BUILD/src" ${ARDUINOJSON_INCLUDE:+-I "$ARDUINOJSON_INCLUDE"} -c "$f" -o "$o" &
done
wait
g++ "$BUILD"/obj/*.o -o "$BUILD/airpack-host" -lpthread

SIM_VERBOSE="${SIM_VERBOSE:-}" python3 ../tools/airpack_simulator.py /tmp/ttyAIRPACK >"$LOGDIR/sim.log" 2>&1 &
SIM=$!
trap 'kill $SIM 2>/dev/null || true' EXIT
sleep 1
timeout "$DURATION" script -qfc "$BUILD/airpack-host" /dev/null 2>&1 \
  | sed -u 's/\x1b\[[0-9;]*m//g' >"$LOGDIR/firmware.log" || true
echo "firmware log: $LOGDIR/firmware.log"
echo "simulator log: $LOGDIR/sim.log"
