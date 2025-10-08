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
  --traffic-dir DIR            Directory containing traffic config sets (subdirectories)
                               [default: <repo>/automated_experiment/config]
  --traffic-config PATH        Single traffic config set directory (overrides --traffic-dir)
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

if pgrep -f peerconnection_client >/dev/null 2>&1; then
    echo "Found running peerconnection_client processes — killing them..."
    sudo pkill -9 -f peerconnection_client >/dev/null 2>&1 || true
else
    echo "No peerconnection_client processes found."
fi


SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/.." && pwd)

RUN_START_TS="$(date +%Y%m%dT%H%M%S)"

TRACES_DIR="$SCRIPT_DIR/poc_traces"
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
    # Single config set directory specified
    if [[ ! -d "$TRAFFIC_CONFIG" ]]; then
        echo "Error: traffic config directory not found: $TRAFFIC_CONFIG" >&2
        exit 1
    fi
    TRAFFIC_CONFIGS=("$TRAFFIC_CONFIG")
else
    # Discover all traffic config directories in traffic directory
    if [[ ! -d "$TRAFFIC_DIR" ]]; then
        echo "Error: traffic directory not found: $TRAFFIC_DIR" >&2
        exit 1
    fi
    while IFS= read -r -d '' config_dir; do
        TRAFFIC_CONFIGS+=("$config_dir")
    done < <(find "$TRAFFIC_DIR" -mindepth 1 -maxdepth 1 -type d -print0 | sort -z)

    if [[ ${#TRAFFIC_CONFIGS[@]} -eq 0 ]]; then
        echo "Error: no traffic config directories found in $TRAFFIC_DIR" >&2
        exit 1
    fi
    echo "Found ${#TRAFFIC_CONFIGS[@]} traffic config set(s) in $TRAFFIC_DIR"
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
    local sctp_config="$2"
    local rtp_config="$3"
    local traffic_label="$4"
    local trace_file
    trace_file=$(basename "$trace_path")
    local trace_name="${trace_file%.pitree-trace}"

    local traffic_base="${traffic_label:-traffic}"
    traffic_base="${traffic_base// /_}"

    local room_id="${RUN_START_TS}_${trace_name}_${traffic_base}"

    echo -e "\n=== Running trace: $trace_name (room_id=${room_id}) ==="

    local profile_csv="$PROFILES_DIR/${trace_name}.csv"
    convert_trace "$trace_path" "$profile_csv"

    local run_stdout_dir="$STDOUT_DIR/$trace_name"
    mkdir -p "$run_stdout_dir"

    local emulator_stdout="$run_stdout_dir/network_emulator.log"
    local bandwidth_csv="$EMULATOR_LOG_DIR/${trace_name}_bandwidth.csv"
    local previous_dir="$PWD"
    cd "$SCRIPT_DIR/network_emulation"

    # Create a FIFO for control if not already present
    local fifo_path="$run_stdout_dir/emulator.fifo"
    [[ -p "$fifo_path" ]] || mkfifo "$fifo_path"

    # Start emulator with FIFO attached to stdin
    sudo ./network_emulator \
    --profile_path="$profile_csv" \
    --interface_name="$INTERFACE_NAME" \
    --bandwidth_log_path="$bandwidth_csv" \
    <"$fifo_path" >"$emulator_stdout" 2>&1 &
    local emulator_pid=$!
    cd "$previous_dir"
    echo start > "$fifo_path"

    echo "$emulator_pid" > "$run_stdout_dir/emulator.pid"

    local wait_seconds=0

    while [[ ! -f "$emulator_stdout" || -z $(grep -m1 "Type 'start' to begin traffic shaping..." "$emulator_stdout" 2>/dev/null) ]]; do
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
        --log_root="$LOG_ROOT"
        --server="$SERVER_HOST"
        --port="$SERVER_PORT"
    )

    if [[ -n "$sctp_config" ]]; then
        common_args+=("--sctp_csv=$sctp_config")
    fi
    if [[ -n "$rtp_config" ]]; then
        common_args+=("--rtp_csv=$rtp_config")
    fi

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

    sudo pulseaudio --start
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

    # Wait for emulator to complete (primary termination signal)
    echo "Waiting for network emulator to complete..."
    local exit_code=0
    wait "$emulator_pid" || exit_code=$?
    echo "Network emulator finished with exit code: $exit_code"

    # End emulation trace
    echo -e "\n=== ENDING EMULATION TRACE ==="
    echo "Bandwidth emulation completed for trace: $trace_name"

    # Clean up sender and receiver processes
    echo "Cleaning up sender and receiver processes..."

    # Send SIGTERM to sender
    if kill -0 "$sender_pid" 2>/dev/null; then
        echo "Sending SIGTERM to sender (PID: $sender_pid)"
        sudo kill -TERM "$sender_pid" 2>/dev/null || true
    fi

    # Send SIGTERM to receiver
    if kill -0 "$receiver_pid" 2>/dev/null; then
        echo "Sending SIGTERM to receiver (PID: $receiver_pid)"
        sudo kill -TERM "$receiver_pid" 2>/dev/null || true
    fi

    # Wait up to 5 seconds for graceful shutdown
    local cleanup_wait=0
    while [[ $cleanup_wait -lt 5 ]]; do
        local sender_alive=0
        local receiver_alive=0

        kill -0 "$sender_pid" 2>/dev/null && sender_alive=1
        kill -0 "$receiver_pid" 2>/dev/null && receiver_alive=1

        if [[ $sender_alive -eq 0 && $receiver_alive -eq 0 ]]; then
            echo "Sender and receiver exited gracefully"
            break
        fi

        sleep 1
        cleanup_wait=$((cleanup_wait + 1))
    done

    # Escalate to SIGKILL if still alive
    if kill -0 "$sender_pid" 2>/dev/null; then
        echo "Sender did not exit gracefully, sending SIGKILL"
        sudo kill -9 "$sender_pid" 2>/dev/null || true
    fi

    if kill -0 "$receiver_pid" 2>/dev/null; then
        echo "Receiver did not exit gracefully, sending SIGKILL"
        sudo kill -9 "$receiver_pid" 2>/dev/null || true
    fi

    # Final wait to collect process exit codes
    wait "$sender_pid" 2>/dev/null || true
    wait "$receiver_pid" 2>/dev/null || true

    if [[ -f "$emulator_stdout" ]]; then
        cp "$emulator_stdout" "$EMULATOR_LOG_DIR/${trace_name}.log"
    fi

    #if [[ $exit_code -ne 0 ]]; then
    #    echo "Trace $trace_name finished with errors." >&2
    #    return $exit_code
    #fi

    echo "Trace $trace_name completed successfully. Logs stored under $LOG_ROOT"

    sleep 3
    #return 0
}

# Create main experiment root
MAIN_EXPERIMENT_ROOT=$(mkdir -p "$OUTPUT_DIR" && cd "$OUTPUT_DIR" && pwd)/"$EXPERIMENT_ID"
mkdir -p "$MAIN_EXPERIMENT_ROOT"

TOTAL_TRACE_COUNT=0
TOTAL_TRAFFIC_CONFIGS=${#TRAFFIC_CONFIGS[@]}

for traffic_config_dir in "${TRAFFIC_CONFIGS[@]}"; do
    # Extract a unique name for this traffic config set
    traffic_name=$(basename "$traffic_config_dir")

    echo -e "\n========================================="
    echo "Starting experiments with traffic config set: $traffic_name"
    echo "Config path: $traffic_config_dir"
    echo "=========================================\n"

    # Create traffic config subdirectory under main experiment root
    TRAFFIC_ROOT="$MAIN_EXPERIMENT_ROOT/$traffic_name"
    mkdir -p "$TRAFFIC_ROOT"

    local local_sctp_config=""
    local local_rtp_config=""
    if [[ -f "$traffic_config_dir/sctp.csv" ]]; then
        local_sctp_config="$traffic_config_dir/sctp.csv"
        cp "$local_sctp_config" "$TRAFFIC_ROOT/sctp.csv"
    elif [[ -f "$traffic_config_dir/traffic_config.csv" ]]; then
        local_sctp_config="$traffic_config_dir/traffic_config.csv"
        cp "$local_sctp_config" "$TRAFFIC_ROOT/sctp.csv"
    fi

    if [[ -f "$traffic_config_dir/rtp.csv" ]]; then
        local_rtp_config="$traffic_config_dir/rtp.csv"
        cp "$local_rtp_config" "$TRAFFIC_ROOT/rtp.csv"
    fi

    if [[ -z "$local_sctp_config" ]]; then
        echo "Warning: no SCTP config found in $traffic_config_dir (expected sctp.csv)."
    fi
    if [[ -z "$local_rtp_config" ]]; then
        echo "Info: no RTP config found in $traffic_config_dir; defaults will be used."
    fi

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

        run_single_trace "$trace_file" "$local_sctp_config" "$local_rtp_config" "$traffic_name"
        TRACE_COUNT=$((TRACE_COUNT + 1))
        TOTAL_TRACE_COUNT=$((TOTAL_TRACE_COUNT + 1))
    done < <(find "$TRACES_DIR" -maxdepth 1 -type f -name '*.pitree-trace' -print0 | sort -z)

    echo -e "\nCompleted $TRACE_COUNT traces for traffic config set: $traffic_name"
    echo "Results located at: $TRAFFIC_ROOT"
done

echo -e "\n========================================="
echo "ALL EXPERIMENTS COMPLETED"
echo "Total traces run: $TOTAL_TRACE_COUNT"
echo "Total traffic config sets: $TOTAL_TRAFFIC_CONFIGS"
echo "Results directory: $MAIN_EXPERIMENT_ROOT"
echo "=========================================\n"
