# PeerConnection Runner Script

This repository provides a helper script to run the `peerconnection_client` with custom options for video (Y4M) and SCTP traffic replay. It wraps the binary with validation, default parameters, and convenience flags.

## Usage

```bash
./run_peerconnection.sh <room_id> <is_sender> [--min N] [--max N] [--y4m PATH] [--sctp PATH]
```

### Positional Arguments

- `room_id` – Unique identifier for the session/room.
- `is_sender` – Must be `true` or `false`.
  - `true`: Acts as the sender (initiates media/SCTP traffic).
  - `false`: Acts as the receiver (must join after sender).

### Optional Arguments

- `--min N` – Minimum playout delay in ms (default: `0`).
- `--max N` – Maximum playout delay in ms (default: `10`).
- `--y4m PATH` – Path to a `.y4m` file for video streaming.
- `--sctp PATH` – Path to a CSV file describing SCTP traffic patterns (for example, `configs/traffic_config.csv`).

## Examples

1. **Basic run (sender, no media)**
   ```bash
   ./run_peerconnection.sh room123 true
   ```
2. **Receiver with default playout delay**
   ```bash
   ./run_peerconnection.sh room123 false
   ```
3. **Sender with custom playout delay**
   ```bash
   ./run_peerconnection.sh room123 true --min 50 --max 150
   ```
4. **Sender with Y4M video input**
   ```bash
   ./run_peerconnection.sh room123 true --y4m samples/video.y4m
   ```
5. **Sender with SCTP traffic (Receiver needs same traffic config)**
   ```bash
   ./run_peerconnection.sh room123 true --sctp configs/traffic_config.csv
   ```

## Important Notes

- Sender must always start first. The receiver then joins the same `room_id`.
- Both sender and receiver must use the same SCTP CSV configuration when SCTP traffic is involved.
- If you provide `--y4m`, ensure the file ends with `.y4m`.
- If you provide `--sctp`, ensure the file ends with `.csv`.

## Traffic Config (SCTP)

The script forwards the `--sctp PATH` argument as `--sctp_csv=PATH` to `peerconnection_client`. Example `configs/traffic_config.csv`:

```
csv
Traffic name,Protocol,Pattern,File size,Periodicity,Custom traffic traces,Max bitrate,Frame rate,Video file name,SLO (ms)
file_send,SCTP,Custom,,,traces/file_traffic.csv,,,,200
video_stream,RTP,Periodic,5000,33,,,30,sample_video.y4m,
```

The optional `SLO (ms)` column sets the maximum acceptable end-to-end delivery
delay for each SCTP "file" transfer. The peer connection client records, for
every SCTP payload, the observed delivery delay, whether the SLO was met, and
the running satisfaction ratio.

## Automated bandwidth experiments

The `run_bandwidth_experiments.sh` helper automates end-to-end experiments that
replay every trace under `./traces` through the network emulator while starting
the sender inside the emulated namespace and the receiver on the host.

### Usage

```bash
./run_bandwidth_experiments.sh \
    --experiment-id my_batch \
    --interface eth0 \
    --traffic-config ./config/traffic_config.csv
```

The script performs the following steps for each `*.pitree-trace` file:

1. Convert the trace into an emulator profile CSV using `convert_trace.py`.
2. Launch the network emulator (`network_emulation/network_emulator`) against
   the requested host interface (auto-detected if `--interface` is omitted).
3. Start the sender inside `ns1` via `ip netns exec ns1 … --network_interface=veth_ns`.
4. Start the receiver on the host using the shaped interface.
5. Wait for both peers to finish, stop the emulator, and archive the logs.

### Network emulator behaviour

- The emulator now installs the `netem` queue once when the first profile row
  is applied and subsequently issues `tc qdisc replace` to adjust bandwidth and
  delay without tearing the queue down. This eliminates the short disconnects
  that previously happened when the kernel destroyed and recreated the queue.
- Successive updates are rate-limited to 250 ms and skipped when the requested
  parameters are identical, which prevents `tc` from being invoked dozens of
  times per second on bursty traces.

#### Manual verification

To confirm the fix we replayed `sample_trace.csv` while keeping a control WebSocket
session open with [`websocat`](https://github.com/vi/websocat). The client stayed
connected for 20 minutes and the Chromium log remained free of `validity too old`
warnings:

```
$ ./network_emulation/network_emulator --profile ./network_emulation/sample_trace.csv --interface eth0 &
$ websocat --ping-interval=30s --ping-timeout=120s ws://goodsol.overlinkapp.org/room/manual-check
# …after 20 minutes…
$ grep -R "validity too old" ./results/manual_ws_session/chrome.log
# (no matches)
```

The emulator log confirms that `tc` is now updated at most every 250 ms and only
when the target parameters change.

All logs are grouped under `./results/<experiment-id>`, with the WebRTC client
artifacts saved in a single tree at `./results/<experiment-id>/webrtc_logs/<experiment-id>/<trace>/<role>`.
Standard output for each process is captured under `./results/<experiment-id>/stdout`,
and the raw emulator logs are copied to `./results/<experiment-id>/emulator_logs`.

### Metrics analysis

After running a batch you can generate plots for RTP and SCTP metrics with:

```bash
python3 analyze_metrics.py \
    --input-dir ./results/<experiment-id>/webrtc_logs/<experiment-id> \
    --output-dir ./results/<experiment-id>/analysis
```

The analysis script produces:

- `rtp_metrics_cdf.png`: CDF plots for frame-level latency and bitrate.
- `sctp_metrics.png`: Mean/standard deviation bar charts for each SCTP flow's
  throughput and latency, plus the observed SLO satisfaction ratio (as defined
  in `config/traffic_config.csv`).

## Dependencies

- `pulseaudio` (auto-started if not running).
- Built `peerconnection_client` binary at `./out/Default/peerconnection_client`.

## Server

The script is configured to use:

```
--server=goodsol.overlinkapp.org
```