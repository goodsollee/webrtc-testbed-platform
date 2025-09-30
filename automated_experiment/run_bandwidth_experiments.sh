#!/usr/bin/env bash
set -euo pipefail

################################################################################
# Argument parsing
################################################################################
usage() {
    cat <<USAGE
Usage: $0 --experiment-id ID [options]

Required arguments:
  --experiment-id ID           Unique identifier used to group log output.

Optional arguments:
  --traces-dir DIR             Directory containing *.pitree-trace files
                               [default: <repo>/automated_experiment/traces]
  --traffic-dir DIR            Directory containing traffic config CSV files
                               [default: <repo>/automated_experiment/config]
  --traffic-config PATH        Single SCTP traffic CSV (overrides --traffic-dir)
  --output-dir DIR             Root directory where experiment artifacts are stored
                               [default: <repo>/automated_experiment/results]
  --latency-ms N               Fixed latency (ms) applied when converting traces [default: 30]
  --interface NAME             Physical interface to shape (auto-detected if omitted)
  --namespace NAME             Network namespace created by the emulator [default: ns1]
  --ns-interface NAME          Interface name inside the namespace [default: veth_ns]
  --server HOST                Signalling server host [default: goodsol.overlinkapp.org]
  --port PORT                  Signalling server port [default: 8888]
  --sender-headless BOOL       Run sender headless? [default: true]
  --receiver-headless BOOL     Run receiver headless? [default: false]
  --y4m PATH                   Optional Y4M file for the sender
  -h, --help                   Show this message
USAGE
}

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/.." && pwd)

RUN_START_TS="$(date +%Y%m%dT%H%M%S)"

TRACES_DIR="$SCRIPT_DIR/traces"
TRAFFIC_DIR="$SCRIPT_DIR/config"
TRAFFIC_CONFIG=""
OUTPUT_DIR="$SCRIPT_DIR/results"
LATENCY_MS=30
INTERFACE_NAME=""
NS_NAME="ns1"
NS_INTERFACE="veth_ns"
SERVER_HOST="goodsol.overlinkapp.org"
SERVER_PORT=8888
SENDER_HEADLESS="true"
RECEIVER_HEADLESS="true"
EXPERIMENT_ID=""
Y4M_PATH="/home/home/goodsol/workspace/QCON/webrtc/dataset/1080_test.y4m"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --experiment-id) EXPERIMENT_ID="$2"; shift 2 ;;
        --traces-dir) TRACES_DIR="$2"; shift 2 ;;
        --traffic-dir) TRAFFIC_DIR="$2"; shift 2 ;;
        --traffic-config) TRAFFIC_CONFIG="$2"; shift 2 ;;
        --output-dir) OUTPUT_DIR="$2"; shift 2 ;;
        --latency-ms) LATENCY_MS="$2"; shift 2 ;;
        --interface) INTERFACE_NAME="$2"; shift 2 ;;
        --namespace) NS_NAME="$2"; shift 2 ;;
        --ns-interface) NS_INTERFACE="$2"; shift 2 ;;
        --server) SERVER_HOST="$2"; shift 2 ;;
        --port) SERVER_PORT="$2"; shift 2 ;;
        --sender-headless) SENDER_HEADLESS="$2"; shift 2 ;;
        --receiver-headless) RECEIVER_HEADLESS="$2"; shift 2 ;;
        --y4m) Y4M_PATH="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown argument: $1"; usage; exit 1 ;;
    esac
done

if [[ -z "$EXPERIMENT_ID" ]]; then
    echo "Error: --experiment-id is required" >&2
    usage
    exit 1
fi

if [[ ! -d "$TRACES_DIR" ]]; then
    echo "Error: traces directory not found: $TRACES_DIR" >&2
    exit 1
fi

# Build list of traffic configs
TRAFFIC_CONFIGS=()
if [[ -n "$TRAFFIC_CONFIG" ]]; then
    # Single config specified
    if [[ ! -f "$TRAFFIC_CONFIG" ]]; then
        echo "Error: traffic config not found: $TRAFFIC_CONFIG" >&2
        exit 1
    fi
    TRAFFIC_CONFIGS=("$TRAFFIC_CONFIG")
else
    # Discover all CSV files in traffic directory
    if [[ ! -d "$TRAFFIC_DIR" ]]; then
        echo "Error: traffic directory not found: $TRAFFIC_DIR" >&2
        exit 1
    fi
    while IFS= read -r -d '' config_file; do
        TRAFFIC_CONFIGS+=("$config_file")
    done < <(find "$TRAFFIC_DIR" -maxdepth 1 -type f -name '*.csv' -print0 | sort -z)

    if [[ ${#TRAFFIC_CONFIGS[@]} -eq 0 ]]; then
        echo "Error: no CSV files found in $TRAFFIC_DIR" >&2
        exit 1
    fi
    echo "Found ${#TRAFFIC_CONFIGS[@]} traffic config(s) in $TRAFFIC_DIR"
fi

if [[ -n "$Y4M_PATH" && ! -f "$Y4M_PATH" ]]; then
    echo "Error: Y4M file not found: $Y4M_PATH" >&2
    exit 1
fi

if [[ -z "$INTERFACE_NAME" ]]; then
    INTERFACE_NAME=$(ip route get 8.8.8.8 2>/dev/null | awk '{for(i=1;i<=NF;i++) if($i=="dev") {print $(i+1); exit}}')
    if [[ -z "$INTERFACE_NAME" ]]; then
        echo "Error: failed to auto-detect interface. Pass --interface explicitly." >&2
        exit 1
    fi
fi


NETWORK_EMULATOR_BIN="$SCRIPT_DIR/network_emulation/network_emulator"
if [[ ! -x "$NETWORK_EMULATOR_BIN" ]]; then
    echo "Building network emulator..."
    (cd "$SCRIPT_DIR/network_emulation" && make)
fi

if [[ ! -x "$REPO_ROOT/out/Default/peerconnection_client" ]]; then
    echo "Error: peerconnection_client binary not found at $REPO_ROOT/out/Default/peerconnection_client" >&2
    exit 1
fi

convert_trace() {
    local trace_file="$1"
    local output_file="$2"
    python3 "$SCRIPT_DIR/convert_trace.py" "$trace_file" "$output_file" --latency-ms "$LATENCY_MS"
}

run_single_trace() {
    local trace_path="$1"
    local traffic_config="$2"
    local trace_file
    trace_file=$(basename "$trace_path")
    local trace_name="${trace_file%.pitree-trace}"
    local room_id="${RUN_START_TS}_${trace_name}"

    echo -e "\n=== Running trace: $trace_name (room_id=${room_id}) ==="

    local profile_csv="$PROFILES_DIR/${trace_name}.csv"
    convert_trace "$trace_path" "$profile_csv"

    local run_stdout_dir="$STDOUT_DIR/$trace_name"
    mkdir -p "$run_stdout_dir"

    local emulator_stdout="$run_stdout_dir/network_emulator.log"
    local previous_dir="$PWD"
    cd "$SCRIPT_DIR/network_emulation"
    sudo ./network_emulator --profile_path="$profile_csv" --interface_name="$INTERFACE_NAME" > "$emulator_stdout" 2>&1 &
    local emulator_pid=$!
    cd "$previous_dir"
    echo "$emulator_pid" > "$run_stdout_dir/emulator.pid"

    local wait_seconds=0

    while [[ ! -f "$emulator_stdout" || -z $(grep -m1 "Press any key to start traffic shaping..." "$emulator_stdout" 2>/dev/null) ]]; do
        if ! kill -0 "$emulator_pid" 2>/dev/null; then
            echo "Emulator exited before becoming ready. Check $emulator_stdout" >&2
            return 1
        fi
        sleep 1
        wait_seconds=$((wait_seconds + 1))
        if [[ $wait_seconds -gt 30 ]]; then
            echo "Timed out waiting for emulator to become ready." >&2
            return 1
        fi
    done

    local sender_log="$run_stdout_dir/sender.log"
    local receiver_log="$run_stdout_dir/receiver.log"

    local common_args=(
        --room_id="$room_id"
        --log_date="$EXPERIMENT_ID"
        --log_root="$LOG_ROOT"
        --sctp_csv="$traffic_config"
        --server="$SERVER_HOST"
        --port="$SERVER_PORT"
    )

    local sender_args=(
        sudo ip netns exec "$NS_NAME" "$REPO_ROOT/out/Default/peerconnection_client"
        "${common_args[@]}"
        --is_sender=true
        --headless="$SENDER_HEADLESS"
    )
    if [[ -n "$Y4M_PATH" ]]; then
        sender_args+=("--y4m_path=$Y4M_PATH")
    fi

    local receiver_args=(
        sudo "$REPO_ROOT/out/Default/peerconnection_client"
        "${common_args[@]}"
        --is_sender=false
        --headless="$RECEIVER_HEADLESS"
    )

    sudo ip netns exec "$NS_NAME" pulseaudio --start

    "${sender_args[@]}" > "$sender_log" 2>&1 &
    local sender_pid=$!

    sleep 3

    "${receiver_args[@]}" > "$receiver_log" 2>&1 &
    local receiver_pid=$!

    # Wait for traffic to appear in receiver.log
    echo "Waiting for traffic in receiver.log..."
    local traffic_wait=0
    while [[ ! -f "$receiver_log" || -z $(grep -E "(Elapsed time|Frame rate|Bitrate)" "$receiver_log" 2>/dev/null) ]]; do
        if ! kill -0 "$receiver_pid" 2>/dev/null; then
            echo "Receiver exited before traffic started. Check $receiver_log" >&2
            kill "$sender_pid" 2>/dev/null || true
            return 1
        fi
        sleep 1
        traffic_wait=$((traffic_wait + 1))
        if [[ $traffic_wait -gt 60 ]]; then
            echo "Timed out waiting for traffic in receiver.log" >&2
            kill "$sender_pid" 2>/dev/null || true
            kill "$receiver_pid" 2>/dev/null || true
            return 1
        fi
    done
    echo "Traffic detected in receiver.log after ${traffic_wait}s"

    # Start emulation trace
    echo -e "\n=== STARTING EMULATION TRACE ==="
    sleep 1
    echo "Starting bandwidth emulation for trace: $trace_name"
    echo "start" | sudo tee /proc/$(cat "$run_stdout_dir/emulator.pid")/fd/0 > /dev/null 2>&1 || echo "Note: Could not send start signal to emulator stdin"

    local exit_code=0
    wait "$receiver_pid" || exit_code=$?
    wait "$sender_pid" || exit_code=$?

    # End emulation trace
    echo -e "\n=== ENDING EMULATION TRACE ==="
    sleep 1
    echo "Bandwidth emulation completed for trace: $trace_name"

    if kill -0 "$emulator_pid" 2>/dev/null; then
        sudo kill -INT "$emulator_pid"
        wait "$emulator_pid" || true
    fi

    if [[ -f "$emulator_stdout" ]]; then
        cp "$emulator_stdout" "$EMULATOR_LOG_DIR/${trace_name}.log"
    fi

    if [[ $exit_code -ne 0 ]]; then
        echo "Trace $trace_name finished with errors." >&2
        return $exit_code
    fi

    echo "Trace $trace_name completed successfully. Logs stored under $LOG_ROOT/$EXPERIMENT_ID/$trace_name"
    return 0
}

# Create main experiment root
MAIN_EXPERIMENT_ROOT=$(mkdir -p "$OUTPUT_DIR" && cd "$OUTPUT_DIR" && pwd)/"$EXPERIMENT_ID"
mkdir -p "$MAIN_EXPERIMENT_ROOT"

TOTAL_TRACE_COUNT=0
TOTAL_TRAFFIC_CONFIGS=${#TRAFFIC_CONFIGS[@]}

for traffic_config in "${TRAFFIC_CONFIGS[@]}"; do
    # Extract a unique name for this traffic config
    traffic_name=$(basename "$traffic_config" .csv)

    echo -e "\n========================================="
    echo "Starting experiments with traffic config: $traffic_name"
    echo "Config path: $traffic_config"
    echo "=========================================\n"

    # Create traffic config subdirectory under main experiment root
    TRAFFIC_ROOT="$MAIN_EXPERIMENT_ROOT/$traffic_name"
    mkdir -p "$TRAFFIC_ROOT"

    # Save traffic config copy to traffic root
    cp "$traffic_config" "$TRAFFIC_ROOT/traffic_config.csv"

    TRACE_COUNT=0
    while IFS= read -r -d '' trace_file; do
        trace_file_name=$(basename "$trace_file")
        trace_name="${trace_file_name%.pitree-trace}"

        # Create directories for this specific trace under traffic config
        EXPERIMENT_ROOT="$TRAFFIC_ROOT/$trace_name"
        PROFILES_DIR="$EXPERIMENT_ROOT/profiles"
        LOG_ROOT="$EXPERIMENT_ROOT/webrtc_logs"
        EMULATOR_LOG_DIR="$EXPERIMENT_ROOT/emulator_logs"
        STDOUT_DIR="$EXPERIMENT_ROOT/stdout"
        mkdir -p "$PROFILES_DIR" "$LOG_ROOT" "$EMULATOR_LOG_DIR" "$STDOUT_DIR"

        run_single_trace "$trace_file" "$traffic_config"
        TRACE_COUNT=$((TRACE_COUNT + 1))
        TOTAL_TRACE_COUNT=$((TOTAL_TRACE_COUNT + 1))
    done < <(find "$TRACES_DIR" -maxdepth 1 -type f -name '*.pitree-trace' -print0 | sort -z)

    echo -e "\nCompleted $TRACE_COUNT traces for traffic config: $traffic_name"
    echo "Results located at: $TRAFFIC_ROOT"
done

echo -e "\n========================================="
echo "ALL EXPERIMENTS COMPLETED"
echo "Total traces run: $TOTAL_TRACE_COUNT"
echo "Total traffic configs: $TOTAL_TRAFFIC_CONFIGS"
echo "Results directory: $MAIN_EXPERIMENT_ROOT"
echo "=========================================\n"
