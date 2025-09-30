#!/usr/bin/env python3
"""Generate RTP and SCTP metrics plots from automated experiment logs."""

from __future__ import annotations

import argparse
import csv
import math
from collections import defaultdict
from pathlib import Path
from typing import Dict, Iterable, List, Tuple

import matplotlib.pyplot as plt
import numpy as np

SCRIPT_DIR = Path(__file__).resolve().parent

FlowMetrics = Dict[str, Dict[str, List[float]]]


def load_traffic_profiles(config_path: Path) -> List[Tuple[str, float]]:
    profiles: List[Tuple[str, float]] = []
    with config_path.open("r", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            name = row.get("Traffic name", f"Flow{len(profiles)+1}")
            slo_raw = row.get("SLO (ms)")
            slo_ms = float(slo_raw) if slo_raw else math.nan
            profiles.append((name, slo_ms))
    return profiles


def gather_frame_metrics(frame_csv: Path) -> Tuple[List[float], List[float]]:
    latencies: List[float] = []
    bitrates: List[float] = []
    with frame_csv.open("r", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            try:
                departure = float(row["first_packet_departure"])
            except (KeyError, ValueError):
                departure = -1.0
            if departure < 0:
                try:
                    departure = float(row["estimated_first_packet_departure"])
                except (KeyError, ValueError):
                    departure = 0.0

            render_time = 0.0
            for key in ("render", "last_packet_arrival"):
                try:
                    render_time = float(row[key])
                    if render_time > 0:
                        break
                except (KeyError, ValueError):
                    continue

            latency = max(0.0, render_time - departure)
            latencies.append(latency)

            try:
                encoded_size = float(row["encoded_size"])
                inter_delay = float(row["inter_frame_delay_ms"])
            except (KeyError, ValueError):
                continue
            if inter_delay > 0:
                bitrate_kbps = (encoded_size * 8.0) / inter_delay
                bitrates.append(bitrate_kbps)
    return latencies, bitrates


def read_flow_csv(csv_path: Path) -> List[Tuple[float, float, float]]:
    samples: List[Tuple[float, float, float]] = []
    with csv_path.open("r", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            try:
                timestamp = float(row.get("timestamp_ms", row.get("timestamp", "0")))
                chunk_bytes = float(row.get("chunk_bytes", "0"))
                total_bytes = float(row.get("total_bytes", "0"))
            except ValueError:
                continue
            samples.append((timestamp, chunk_bytes, total_bytes))
    return samples


def accumulate_sctp_metrics(sender_csv: Path, receiver_csv: Path) -> Tuple[List[float], List[float]]:
    tx_samples = read_flow_csv(sender_csv)
    rx_samples = read_flow_csv(receiver_csv)
    if not tx_samples or not rx_samples:
        return [], []

    count = min(len(tx_samples), len(rx_samples))
    latencies = [max(0.0, rx_samples[i][0] - tx_samples[i][0]) for i in range(count)]

    throughputs: List[float] = []
    for i in range(1, len(rx_samples)):
        prev = rx_samples[i - 1]
        curr = rx_samples[i]
        delta_bytes = curr[2] - prev[2]
        delta_time = curr[0] - prev[0]
        if delta_time <= 0:
            continue
        throughputs.append((delta_bytes * 8.0) / delta_time)

    return latencies, throughputs


def plot_cdf(ax: plt.Axes, data: Iterable[float], title: str, xlabel: str) -> None:
    values = np.array([d for d in data if not math.isnan(d) and math.isfinite(d)])
    if values.size == 0:
        ax.set_title(f"{title}\n(no data)")
        ax.set_xlabel(xlabel)
        ax.set_ylabel("CDF")
        ax.grid(True, linestyle="--", alpha=0.4)
        return
    sorted_vals = np.sort(values)
    cdf = np.linspace(0, 1, len(sorted_vals), endpoint=False)
    cdf = np.append(cdf, 1.0)
    sorted_vals = np.append(sorted_vals, sorted_vals[-1])
    ax.plot(sorted_vals, cdf)
    ax.set_title(title)
    ax.set_xlabel(xlabel)
    ax.set_ylabel("CDF")
    ax.grid(True, linestyle="--", alpha=0.4)


def plot_sctp_bars(axs: Iterable[plt.Axes], flows: List[str], means: List[List[float]], stds: List[List[float]], slo_ratios: List[float]) -> None:
    axes = list(axs)
    labels = flows

    axes[0].bar(labels, means[0], yerr=stds[0], capsize=4)
    axes[0].set_ylabel("Throughput (kbps)")
    axes[0].set_title("SCTP throughput")
    axes[0].grid(True, axis="y", linestyle="--", alpha=0.3)

    axes[1].bar(labels, means[1], yerr=stds[1], capsize=4, color="#ff7f0e")
    axes[1].set_ylabel("Latency (ms)")
    axes[1].set_title("SCTP latency")
    axes[1].grid(True, axis="y", linestyle="--", alpha=0.3)

    axes[2].bar(labels, slo_ratios, color="#2ca02c")
    axes[2].set_ylim(0, 1)
    axes[2].set_ylabel("SLO satisfaction")
    axes[2].set_title("SLO ratio")
    axes[2].grid(True, axis="y", linestyle="--", alpha=0.3)

    for ax in axes:
        ax.tick_params(axis="x", rotation=20)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input-dir", type=Path, required=True, help="Root directory containing per-trace logs")
    parser.add_argument(
        "--traffic-config",
        type=Path,
        default=SCRIPT_DIR / "config/traffic_config.csv",
        help="Traffic configuration CSV",
    )
    parser.add_argument("--output-dir", type=Path, required=True, help="Directory to write plots into")
    args = parser.parse_args()

    input_dir = args.input_dir
    if not input_dir.exists():
        raise SystemExit(f"Input directory not found: {input_dir}")

    traffic_config = args.traffic_config
    if not traffic_config.is_file():
        raise SystemExit(f"Traffic config not found: {traffic_config}")

    output_dir = args.output_dir
    output_dir.mkdir(parents=True, exist_ok=True)

    profiles = load_traffic_profiles(traffic_config)

    all_frame_latencies: List[float] = []
    all_frame_bitrates: List[float] = []
    sctp_metrics: FlowMetrics = defaultdict(lambda: {"latencies": [], "throughputs": []})

    for trace_dir in sorted(input_dir.glob("*/receiver")):
        frame_csv = trace_dir / "frame_metrics.csv"
        if frame_csv.is_file():
            latencies, bitrates = gather_frame_metrics(frame_csv)
            all_frame_latencies.extend(latencies)
            all_frame_bitrates.extend(bitrates)

        sender_dir = trace_dir.parent / "sender"
        if not sender_dir.exists():
            continue
        for idx, (flow_name, _slo_ms) in enumerate(profiles, start=1):
            rx_csv = trace_dir / f"File{idx}_rx.csv"
            tx_csv = sender_dir / f"File{idx}_tx.csv"
            if not rx_csv.is_file() or not tx_csv.is_file():
                continue
            latencies, throughputs = accumulate_sctp_metrics(tx_csv, rx_csv)
            metrics = sctp_metrics[flow_name]
            metrics["latencies"].extend(latencies)
            metrics["throughputs"].extend(throughputs)

    # Plot RTP metrics
    rtp_fig, rtp_axes = plt.subplots(1, 2, figsize=(12, 4))
    plot_cdf(rtp_axes[0], all_frame_latencies, "RTP frame latency", "Latency (ms)")
    plot_cdf(rtp_axes[1], all_frame_bitrates, "RTP frame bitrate", "Bitrate (kbps)")
    rtp_fig.tight_layout()
    rtp_path = output_dir / "rtp_metrics_cdf.png"
    rtp_fig.savefig(rtp_path, dpi=150)

    # Plot SCTP metrics
    flow_labels: List[str] = []
    throughput_mean: List[float] = []
    throughput_std: List[float] = []
    latency_mean: List[float] = []
    latency_std: List[float] = []
    slo_ratios: List[float] = []

    for flow_name, slo_ms in profiles:
        metrics = sctp_metrics.get(flow_name)
        if not metrics:
            continue
        tp = np.array(metrics["throughputs"], dtype=float)
        lt = np.array(metrics["latencies"], dtype=float)
        flow_labels.append(flow_name)
        throughput_mean.append(float(np.nanmean(tp)) if tp.size else 0.0)
        throughput_std.append(float(np.nanstd(tp)) if tp.size else 0.0)
        latency_mean.append(float(np.nanmean(lt)) if lt.size else 0.0)
        latency_std.append(float(np.nanstd(lt)) if lt.size else 0.0)
        if lt.size and not math.isnan(slo_ms):
            slo_ratios.append(float(np.mean(lt <= slo_ms)))
        else:
            slo_ratios.append(0.0)

    if flow_labels:
        sctp_fig, sctp_axes = plt.subplots(1, 3, figsize=(16, 4))
        plot_sctp_bars(sctp_axes, flow_labels, [throughput_mean, latency_mean], [throughput_std, latency_std], slo_ratios)
        for ax in sctp_axes:
            ax.tick_params(axis="x", rotation=20)
        sctp_fig.tight_layout()
        sctp_path = output_dir / "sctp_metrics.png"
        sctp_fig.savefig(sctp_path, dpi=150)
    else:
        print("No SCTP flow metrics found.")

    print(f"Saved RTP metrics to {rtp_path}")
    if flow_labels:
        print(f"Saved SCTP metrics to {sctp_path}")


if __name__ == "__main__":
    main()
