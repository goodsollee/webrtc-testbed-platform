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
  --traffic-config PATH        SCTP traffic CSV to use for both peers
                               [default: <repo>/automated_experiment/config/traffic_config.csv]
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

TRACES_DIR="$SCRIPT_DIR/traces"
TRAFFIC_CONFIG="$SCRIPT_DIR/config/traffic_config.csv"
OUTPUT_DIR="$SCRIPT_DIR/results"
LATENCY_MS=30
INTERFACE_NAME=""
NS_NAME="ns1"
NS_INTERFACE="veth_ns"
SERVER_HOST="goodsol.overlinkapp.org"
SERVER_PORT=8888
SENDER_HEADLESS="true"
RECEIVER_HEADLESS="false"
EXPERIMENT_ID=""
Y4M_PATH=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --experiment-id) EXPERIMENT_ID="$2"; shift 2 ;;
        --traces-dir) TRACES_DIR="$2"; shift 2 ;;
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

if [[ ! -f "$TRAFFIC_CONFIG" ]]; then
    echo "Error: traffic config not found: $TRAFFIC_CONFIG" >&2
    exit 1
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

EXPERIMENT_ROOT=$(mkdir -p "$OUTPUT_DIR" && cd "$OUTPUT_DIR" && pwd)/"$EXPERIMENT_ID"
PROFILES_DIR="$EXPERIMENT_ROOT/profiles"
LOG_ROOT="$EXPERIMENT_ROOT/webrtc_logs"
EMULATOR_LOG_DIR="$EXPERIMENT_ROOT/emulator_logs"
STDOUT_DIR="$EXPERIMENT_ROOT/stdout"
mkdir -p "$PROFILES_DIR" "$LOG_ROOT" "$EMULATOR_LOG_DIR" "$STDOUT_DIR"

convert_trace() {
    local trace_file="$1"
    local output_file="$2"
    python3 "$SCRIPT_DIR/convert_trace.py" "$trace_file" "$output_file" --latency-ms "$LATENCY_MS"
}

run_single_trace() {
    local trace_path="$1"
    local trace_file
    trace_file=$(basename "$trace_path")
    local trace_name="${trace_file%.pitree-trace}"

    echo "\n=== Running trace: $trace_name ==="

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

    local emulator_log_file="$SCRIPT_DIR/network_emulation/network_emulator.log"
    local wait_seconds=0
    while [[ ! -f "$emulator_log_file" || -z $(grep -m1 "Network emulator running" "$emulator_log_file" 2>/dev/null) ]]; do
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
        --experiment_mode=emulation
        --room_id="$trace_name"
        --log_date="$EXPERIMENT_ID"
        --log_root="$LOG_ROOT"
        --sctp_csv="$TRAFFIC_CONFIG"
        --server="$SERVER_HOST"
        --port="$SERVER_PORT"
    )

    local sender_args=(
        sudo ip netns exec "$NS_NAME" "$REPO_ROOT/out/Default/peerconnection_client"
        "${common_args[@]}"
        --network_interface="$NS_INTERFACE"
        --is_sender=true
        --headless="$SENDER_HEADLESS"
    )
    if [[ -n "$Y4M_PATH" ]]; then
        sender_args+=("--y4m_path=$Y4M_PATH")
    fi

    local receiver_args=(
        sudo "$REPO_ROOT/out/Default/peerconnection_client"
        "${common_args[@]}"
        --network_interface="$INTERFACE_NAME"
        --is_sender=false
        --headless="$RECEIVER_HEADLESS"
    )

    "${sender_args[@]}" > "$sender_log" 2>&1 &
    local sender_pid=$!

    "${receiver_args[@]}" > "$receiver_log" 2>&1 &
    local receiver_pid=$!

    local exit_code=0
    wait "$receiver_pid" || exit_code=$?
    wait "$sender_pid" || exit_code=$?

    if kill -0 "$emulator_pid" 2>/dev/null; then
        sudo kill -INT "$emulator_pid"
        wait "$emulator_pid" || true
    fi

    if [[ -f "$emulator_log_file" ]]; then
        cp "$emulator_log_file" "$EMULATOR_LOG_DIR/${trace_name}.log"
        : > "$emulator_log_file"
    fi

    if [[ $exit_code -ne 0 ]]; then
        echo "Trace $trace_name finished with errors." >&2
        return $exit_code
    fi

    echo "Trace $trace_name completed successfully. Logs stored under $LOG_ROOT/$EXPERIMENT_ID/$trace_name"
    return 0
}

TRACE_COUNT=0
while IFS= read -r -d '' trace_file; do
    run_single_trace "$trace_file"
    TRACE_COUNT=$((TRACE_COUNT + 1))
done < <(find "$TRACES_DIR" -maxdepth 1 -type f -name '*.pitree-trace' -print0 | sort -z)

echo "\nCompleted $TRACE_COUNT traces. Aggregated logs located at $EXPERIMENT_ROOT"
