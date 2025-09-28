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

## Dependencies

- `pulseaudio` (auto-started if not running).
- Built `peerconnection_client` binary at `./out/Default/peerconnection_client`.

## Server

The script is configured to use:

```
--server=goodsol.overlinkapp.org
```