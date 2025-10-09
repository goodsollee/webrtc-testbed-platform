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
TRAFFIC_DIR="$SCRIPT_DIR/test_config"
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

EMULATOR_SUPPORTS_BANDWIDTH_LOG=false
EMULATOR_HELP_OUTPUT="$("$NETWORK_EMULATOR_BIN" --helpshort 2>&1 || true)"
if grep -q -- '--bandwidth_log_path' <<<"$EMULATOR_HELP_OUTPUT"; then
    EMULATOR_SUPPORTS_BANDWIDTH_LOG=true
else
    echo "Info: network_emulator does not support --bandwidth_log_path; bandwidth CSV logging disabled."
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

    # ===== 시그널 파일 먼저 삭제 =====
    local signal_file="/tmp/emulator_ready.signal"
    rm -f "$signal_file"
    # ============================

    # FIFO 생성
    local fifo_path="$run_stdout_dir/emulator.fifo"
    [[ -p "$fifo_path" ]] || mkfifo "$fifo_path"
    sudo chmod 666 "$fifo_path"

    # FIFO를 fd 3으로 열기
    exec 3<>"$fifo_path"

    # Emulator 명령
    local emulator_cmd=(
        sudo "$NETWORK_EMULATOR_BIN"
        "--profile_path=$profile_csv"
        "--interface_name=$INTERFACE_NAME"
    )

    if [[ "$EMULATOR_SUPPORTS_BANDWIDTH_LOG" == "true" ]]; then
        emulator_cmd+=("--bandwidth_log_path=$bandwidth_csv")
    fi

    echo "Starting emulator..."
    
    # Emulator 실행
    "${emulator_cmd[@]}" <&3 >"$emulator_stdout" 2>&1 &
    local emulator_pid=$!

    sleep 1
    
    # 프로세스 확인
    if ! kill -0 "$emulator_pid" 2>/dev/null; then
        echo "ERROR: Emulator died!" >&2
        exec 3>&-
        return 1
    fi
    echo "✓ Emulator started (PID: $emulator_pid)"

    echo "$emulator_pid" > "$run_stdout_dir/emulator.pid"

    # 시그널 파일 대기 (rm 제거!)
    echo "Waiting for emulator initialization..."
    local wait_seconds=0
    while [[ ! -f "$signal_file" ]]; do
        # 디버깅: 파일 존재 확인
        if [[ $((wait_seconds % 5)) -eq 0 ]]; then
            ls -la "$signal_file" 2>&1 | sed 's/^/  DEBUG: /'
        fi
        
        if ! kill -0 "$emulator_pid" 2>/dev/null; then
            echo "ERROR: Emulator exited!" >&2
            tail -n 50 "$emulator_stdout" >&2
            exec 3>&-
            return 1
        fi
        
        sleep 1
        wait_seconds=$((wait_seconds + 1))

        if [[ $((wait_seconds % 5)) -eq 0 ]]; then
            echo "  Still waiting... (${wait_seconds}s)"
            tail -n 3 "$emulator_stdout" | sed 's/^/    /'
        fi

        if [[ $wait_seconds -gt 30 ]]; then
            echo "ERROR: Timeout!" >&2
            exec 3>&-
            return 1
        fi
    done

    echo "✓ Emulator ready! (${wait_seconds}s)"
    rm -f "$signal_file"  # ← 사용 후 삭제

    # Sender/Receiver 설정
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

    # PulseAudio 시작 및 클라이언트 실행
    echo "Starting sender and receiver..."
    sudo ip netns exec "$NS_NAME" pulseaudio --start >/dev/null 2>&1 || true

    "${sender_args[@]}" > "$sender_log" 2>&1 &
    local sender_pid=$!
    echo "  Sender PID: $sender_pid"

    sleep 3

    sudo pulseaudio --start >/dev/null 2>&1 || true
    "${receiver_args[@]}" > "$receiver_log" 2>&1 &
    local receiver_pid=$!
    echo "  Receiver PID: $receiver_pid"

    # Receiver에서 트래픽 감지 대기
    echo "Waiting for traffic in receiver.log..."
    local traffic_wait=0
    while [[ ! -f "$receiver_log" || -z $(grep -E "(Elapsed time|Frame rate|Bitrate)" "$receiver_log" 2>/dev/null) ]]; do
        if ! kill -0 "$receiver_pid" 2>/dev/null; then
            echo "ERROR: Receiver exited before traffic started!" >&2
            if [[ -f "$receiver_log" ]]; then
                echo "=== Receiver log ===" >&2
                tail -n 50 "$receiver_log" >&2
            fi
            kill "$sender_pid" 2>/dev/null || true
            exec 3>&-
            return 1
        fi
        sleep 1
        traffic_wait=$((traffic_wait + 1))

        if [[ $((traffic_wait % 10)) -eq 0 ]]; then
            echo "  Still waiting for traffic... (${traffic_wait}s)"
        fi

        if [[ $traffic_wait -gt 60 ]]; then
            echo "ERROR: Timeout waiting for traffic (60s)!" >&2
            if [[ -f "$receiver_log" ]]; then
                echo "=== Receiver log ===" >&2
                tail -n 50 "$receiver_log" >&2
            fi
            if [[ -f "$sender_log" ]]; then
                echo "=== Sender log ===" >&2
                tail -n 50 "$sender_log" >&2
            fi
            kill "$sender_pid" 2>/dev/null || true
            kill "$receiver_pid" 2>/dev/null || true
            exec 3>&-
            return 1
        fi
    done
    echo "✓ Traffic detected! (${traffic_wait}s)"

    # 트래픽이 시작된 후 emulator에 start 명령 전송
    echo -e "\n=== STARTING BANDWIDTH EMULATION ==="
    sleep 1
    echo "start" >&3  # fd 3으로 쓰기
    echo "✓ Start signal sent to emulator for trace: $trace_name"

    # Emulator 완료 대기
    echo "Waiting for network emulator to complete..."
    local exit_code=0
    wait "$emulator_pid" || exit_code=$?
    echo "✓ Network emulator finished (exit code: $exit_code)"

    # fd 닫기
    exec 3>&-

    # Sender/Receiver 정리
    echo -e "\n=== CLEANING UP ==="
    echo "Stopping sender and receiver..."

    if kill -0 "$sender_pid" 2>/dev/null; then
        echo "  Sending SIGTERM to sender (PID: $sender_pid)"
        sudo kill -TERM "$sender_pid" 2>/dev/null || true
    fi

    if kill -0 "$receiver_pid" 2>/dev/null; then
        echo "  Sending SIGTERM to receiver (PID: $receiver_pid)"
        sudo kill -TERM "$receiver_pid" 2>/dev/null || true
    fi

    # 5초간 graceful shutdown 대기
    local cleanup_wait=0
    while [[ $cleanup_wait -lt 5 ]]; do
        local sender_alive=0
        local receiver_alive=0

        kill -0 "$sender_pid" 2>/dev/null && sender_alive=1
        kill -0 "$receiver_pid" 2>/dev/null && receiver_alive=1

        if [[ $sender_alive -eq 0 && $receiver_alive -eq 0 ]]; then
            echo "✓ Sender and receiver exited gracefully"
            break
        fi

        sleep 1
        cleanup_wait=$((cleanup_wait + 1))
    done

    # 강제 종료
    if kill -0 "$sender_pid" 2>/dev/null; then
        echo "  Sender did not exit gracefully, sending SIGKILL"
        sudo kill -9 "$sender_pid" 2>/dev/null || true
    fi

    if kill -0 "$receiver_pid" 2>/dev/null; then
        echo "  Receiver did not exit gracefully, sending SIGKILL"
        sudo kill -9 "$receiver_pid" 2>/dev/null || true
    fi

    # 프로세스 종료 대기
    wait "$sender_pid" 2>/dev/null || true
    wait "$receiver_pid" 2>/dev/null || true

    # Emulator 로그 복사
    if [[ -f "$emulator_stdout" ]]; then
        cp "$emulator_stdout" "$EMULATOR_LOG_DIR/${trace_name}.log"
    fi

    echo "✓ Trace $trace_name completed successfully"
    echo "  Logs stored at: $LOG_ROOT"

    sleep 3
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

    local_sctp_config=""
    local_rtp_config=""
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
