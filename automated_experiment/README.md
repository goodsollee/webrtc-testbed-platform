# PeerConnection Runner Script

This repository provides a helper script to run the `peerconnection_client` with custom options for video (Y4M) and SCTP traffic replay. It wraps the binary with validation, default parameters, and convenience flags.

---

## Usage

```bash
./run_peerconnection.sh <room_id> <is_sender> [--min N] [--max N] [--y4m PATH] [--sctp PATH]
