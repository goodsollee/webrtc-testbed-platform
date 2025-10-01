#!/usr/bin/env python3
"""
Automated Experiment Analysis Script
Analyzes WebRTC experiment results and generates visualization graphs.
"""

import os
import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
from pathlib import Path
from collections import defaultdict

class ExperimentAnalyzer:
    def __init__(self, results_dir):
        """
        Initialize the analyzer with results directory.

        Args:
            results_dir: Path to the results directory (e.g., results/poc101)
        """
        self.results_dir = Path(results_dir)
        self.data = defaultdict(lambda: defaultdict(dict))

    def find_receiver_logs(self, config_dir):
        """Find receiver log directory within webrtc_logs."""
        webrtc_logs = config_dir / "webrtc_logs"
        if not webrtc_logs.exists():
            return None

        # Navigate through timestamp folders
        for timestamp_dir in webrtc_logs.iterdir():
            if timestamp_dir.is_dir():
                for session_dir in timestamp_dir.iterdir():
                    if session_dir.is_dir():
                        receiver_dir = session_dir / "receiver"
                        if receiver_dir.exists():
                            return receiver_dir
        return None

    def load_data(self):
        """Load all experiment data from the results directory."""
        print(f"Loading data from {self.results_dir}...")

        # Iterate through config folders (kvcache, prompt)
        for config_folder in self.results_dir.iterdir():
            if not config_folder.is_dir() or config_folder.name == "profiles":
                continue

            config_name = config_folder.name
            print(f"  Processing config: {config_name}")

            # Load traffic config if available
            traffic_config_file = config_folder / "traffic_config.csv"
            if traffic_config_file.exists():
                self.data[config_name]['_traffic_config'] = pd.read_csv(traffic_config_file)

            # Iterate through trace folders (rest-wifi_1, xu4g_0, xu5g_0, etc.)
            for trace_folder in config_folder.iterdir():
                if not trace_folder.is_dir() or trace_folder.name in ["profiles"]:
                    continue

                trace_name = trace_folder.name

                # Load network profile if available
                profile_dir = trace_folder / "profiles"
                if profile_dir.exists():
                    profile_file = profile_dir / f"{trace_name}.csv"
                    if profile_file.exists():
                        self.data[config_name][trace_name]['profile'] = pd.read_csv(profile_file)

                # Find receiver directory
                receiver_dir = self.find_receiver_logs(trace_folder)
                if not receiver_dir:
                    print(f"    Warning: No receiver logs found for {trace_name}")
                    continue

                print(f"    Loading trace: {trace_name}")

                # Load average_stats.csv
                avg_stats_file = receiver_dir / "average_stats.csv"
                if avg_stats_file.exists():
                    self.data[config_name][trace_name]['average_stats'] = pd.read_csv(avg_stats_file)

                # Load KVCache_rx.csv
                kvcache_file = receiver_dir / "KVCache_rx.csv"
                if kvcache_file.exists():
                    self.data[config_name][trace_name]['kvcache'] = pd.read_csv(kvcache_file)

                # Load Prompt_rx.csv
                prompt_file = receiver_dir / "Prompt_rx.csv"
                if prompt_file.exists():
                    self.data[config_name][trace_name]['prompt'] = pd.read_csv(prompt_file)

        print("Data loading complete!")
        return self.data

    def calculate_average_bitrate(self, config_name, trace_name):
        """Calculate average and std of bitrate in Mbps."""
        if 'average_stats' not in self.data[config_name][trace_name]:
            return None, None

        df = self.data[config_name][trace_name]['average_stats']
        if 'bitrates' not in df.columns:
            return None, None

        # Convert bps to Mbps
        bitrates_mbps = df['bitrates'] / 1_000_000
        return bitrates_mbps.mean(), bitrates_mbps.std()

    def calculate_average_framerate(self, config_name, trace_name):
        """Calculate average and std of frame rate."""
        if 'average_stats' not in self.data[config_name][trace_name]:
            return None, None

        df = self.data[config_name][trace_name]['average_stats']
        if 'framerate' not in df.columns:
            return None, None

        capped = np.minimum(df['framerate'].astype(float), 30.0)
        return capped.mean(), capped.std()

    def calculate_kvcache_throughput(self, config_name, trace_name):
        """Calculate average KV cache throughput in Mbps."""
        if 'kvcache' not in self.data[config_name][trace_name]:
            return None, None

        df = self.data[config_name][trace_name]['kvcache']
        if 'throughput_mbps' not in df.columns:
            return None, None

        return df['throughput_mbps'].mean(), df['throughput_mbps'].std()

    def calculate_prompt_delivery_time(self, config_name, trace_name):
        """Calculate average prompt delivery time in ms."""
        if 'prompt' not in self.data[config_name][trace_name]:
            return None, None

        df = self.data[config_name][trace_name]['prompt']
        if 'delivery_delay_ms' not in df.columns:
            return None, None

        return df['delivery_delay_ms'].mean(), df['delivery_delay_ms'].std()

    def calculate_profile_bandwidth(self, config_name, trace_name):
        """Calculate average and std of bandwidth from network profile in Mbps."""
        if 'profile' not in self.data[config_name][trace_name]:
            return None, None

        df = self.data[config_name][trace_name]['profile']
        if 'bandwidth_kbps' not in df.columns:
            return None, None

        # Convert kbps to Mbps
        bandwidth_mbps = df['bandwidth_kbps'] / 1000.0
        return bandwidth_mbps.mean(), bandwidth_mbps.std()

    def get_traffic_config(self, config_name):
        """Get traffic configuration for a config."""
        if '_traffic_config' not in self.data[config_name]:
            return None
        return self.data[config_name]['_traffic_config']

    def calculate_framerate_tail_percentiles(self, config_name, trace_name):
        """Calculate tail percentiles (p01, p0.1) for framerate."""
        if 'average_stats' not in self.data[config_name][trace_name]:
            return None, None

        df = self.data[config_name][trace_name]['average_stats']
        if 'framerate' not in df.columns:
            return None, None

        capped = np.minimum(df['framerate'].astype(float), 30.0)
        # Calculate percentiles (lower percentile = worse tail performance)
        p05 = np.percentile(capped.dropna(), 5)
        p01 = np.percentile(capped.dropna(), 1)   # 1st percentile
        p0_1 = np.percentile(capped.dropna(), 0.1)  # 0.1st percentile
        return p05, p01, p0_1

    def get_capped_framerate_series(self, config_name, trace_name):
        """Return the capped (<=30) framerate series for CDF plotting. None if unavailable."""
        entry = self.data.get(config_name, {}).get(trace_name, {})
        df = entry.get('average_stats')
        if df is None or 'framerate' not in df.columns:
            return None
        return np.minimum(df['framerate'].astype(float), 30.0).dropna().values

    def _plot_ecdf(self, ax, values, label):
        """Plot ECDF for a 1-D numpy array on the provided axes."""
        if values.size == 0:
            return
        x = np.sort(values)
        y = np.arange(1, x.size + 1) / x.size
        ax.plot(x, y, label=label, linewidth=2)

    def plot_metric_comparison(self, metric_name, ylabel, title, output_file):
        """
        Generic function to plot comparison across traces with different configs as legends.

        Args:
            metric_name: Name of the metric function to call
            ylabel: Y-axis label
            title: Plot title
            output_file: Output filename
        """
        # Get all unique traces
        all_traces = set()
        for config_name in self.data.keys():
            all_traces.update(self.data[config_name].keys())
        all_traces = sorted(list(all_traces))

        # Get all configs
        configs = sorted(list(self.data.keys()))

        # Prepare data
        metric_data = {config: {'means': [], 'stds': []} for config in configs}

        for trace in all_traces:
            for config in configs:
                if trace in self.data[config]:
                    mean_val, std_val = getattr(self, f'calculate_{metric_name}')(config, trace)
                    metric_data[config]['means'].append(mean_val if mean_val is not None else 0)
                    metric_data[config]['stds'].append(std_val if std_val is not None else 0)
                else:
                    metric_data[config]['means'].append(0)
                    metric_data[config]['stds'].append(0)

        # Plot
        fig, ax = plt.subplots(figsize=(12, 6))

        x = np.arange(len(all_traces))
        width = 0.8 / len(configs)

        for idx, config in enumerate(configs):
            offset = (idx - len(configs)/2 + 0.5) * width
            bars = ax.bar(x + offset, metric_data[config]['means'], width,
                         yerr=metric_data[config]['stds'],
                         label=config, capsize=5, alpha=0.8)

        ax.set_xlabel('Trace', fontsize=12)
        ax.set_ylabel(ylabel, fontsize=12)
        ax.set_title(title, fontsize=14, fontweight='bold')
        ax.set_xticks(x)
        ax.set_xticklabels(all_traces, rotation=45, ha='right')
        ax.legend()
        ax.grid(axis='y', alpha=0.3)

        plt.tight_layout()
        plt.savefig(output_file, dpi=300, bbox_inches='tight')
        print(f"Saved: {output_file}")
        plt.close()

    def plot_framerate_cdfs(self, output_dir="plots", make_overall=True):
        """
        Plot ECDFs of frame rates (capped at 30 fps).
        - One figure per trace comparing configs.
        - Optionally, an overall figure comparing configs across all traces.
        """
        output_path = Path(output_dir)
        output_path.mkdir(exist_ok=True)

        # Determine all traces and configs
        all_traces = set()
        for config_name in self.data.keys():
            all_traces.update(self.data[config_name].keys())
        all_traces = sorted(list(all_traces))
        configs = sorted(list(self.data.keys()))

        # Per-trace CDFs
        for trace in all_traces:
            fig, ax = plt.subplots(figsize=(10, 6))
            for config in configs:
                s = self.get_capped_framerate_series(config, trace)
                if s is not None:
                    self._plot_ecdf(ax, s, label=config)
            ax.set_xlabel("Frame rate (fps) [capped at 30]", fontsize=12)
            ax.set_ylabel("ECDF", fontsize=12)
            ax.set_title(f"Frame Rate CDF — Trace: {trace}", fontsize=14, fontweight='bold')
            ax.grid(True, alpha=0.3)
            ax.set_xlim(left=0, right=30)
            ax.set_ylim(0, 1.0)
            ax.legend(title="Config")
            plt.tight_layout()
            out = output_path / f"framerate_cdf_{trace}.png"
            plt.savefig(out, dpi=300, bbox_inches='tight')
            print(f"Saved: {out}")
            plt.close()

        # Overall CDF by config (aggregate across traces)
        if make_overall:
            fig, ax = plt.subplots(figsize=(10, 6))
            for config in configs:
                # concat all traces for this config
                all_vals = []
                for trace in all_traces:
                    s = self.get_capped_framerate_series(config, trace)
                    if s is not None and s.size:
                        all_vals.append(s)
                if len(all_vals):
                    merged = np.concatenate(all_vals, axis=0)
                    self._plot_ecdf(ax, merged, label=config)
            ax.set_xlabel("Frame rate (fps) [capped at 30]", fontsize=12)
            ax.set_ylabel("ECDF", fontsize=12)
            ax.set_title("Frame Rate CDF — Overall (All Traces)", fontsize=14, fontweight='bold')
            ax.grid(True, alpha=0.3)
            ax.set_xlim(left=0, right=30)
            ax.set_ylim(0, 1.0)
            ax.legend(title="Config")
            plt.tight_layout()
            out = Path(output_dir) / "framerate_cdf_overall.png"
            plt.savefig(out, dpi=300, bbox_inches='tight')
            print(f"Saved: {out}")
            plt.close()

    def generate_all_plots(self, output_dir="plots"):
        """Generate all analysis plots."""
        output_path = Path(output_dir)
        output_path.mkdir(exist_ok=True)

        print("\nGenerating plots...")

        # 1. Bitrate comparison
        self.plot_metric_comparison(
            'average_bitrate',
            'Bitrate (Mbps)',
            'Average Bitrate Comparison Across Traces',
            output_path / 'bitrate_comparison.png'
        )

        # 2. Frame rate comparison
        self.plot_metric_comparison(
            'average_framerate',
            'Frame Rate (fps)',
            'Average Frame Rate Comparison Across Traces',
            output_path / 'framerate_comparison.png'
        )

        # 3. KV cache throughput comparison
        self.plot_metric_comparison(
            'kvcache_throughput',
            'Throughput (Mbps)',
            'KV Cache Throughput Comparison Across Traces',
            output_path / 'kvcache_throughput_comparison.png'
        )

        # 4. Prompt delivery time comparison
        self.plot_metric_comparison(
            'prompt_delivery_time',
            'Delivery Time (ms)',
            'Prompt Delivery Time Comparison Across Traces',
            output_path / 'prompt_delivery_time_comparison.png'
        )

        # 5. Frame rate CDFs (capped at 30 fps)
        self.plot_framerate_cdfs(output_dir=output_path)

        print("\nAll plots generated successfully!")

    def print_summary(self):
        """Print summary statistics."""
        print("\n" + "="*140)
        print("EXPERIMENT SUMMARY")
        print("="*140)

        for config_name in sorted(self.data.keys()):
            print(f"\n{'Config: ' + config_name:^140}")
            print("-"*140)

            # Print traffic config if available
            traffic_config = self.get_traffic_config(config_name)
            if traffic_config is not None:
                print("\nTraffic Configuration:")
                for _, row in traffic_config.iterrows():
                    print(f"  {row['Traffic name']}: {row['Protocol']} - {row['Pattern']}, "
                          f"File size: {row['File size (B/If periodic/SCTP)']} B, "
                          f"Periodicity: {row['Periodicity (ms/If periodic/SCTP)']} ms, "
                          f"SLO: {row['SLO (ms)']} ms")
                print()

            print(f"{'Trace':<15} {'Net BW (Mbps)':<16} {'Bitrate (Mbps)':<16} {'FPS (avg±std)':<16} "
                  f"{'FPS Tail':<18} {'KVCache Tput':<18} {'Prompt Lat (ms)':<20}")
            print(f"{'':15} {'':16} {'':16} {'':16} {'(p5/p1/p0.1)':<18} {'(Mbps)':<18} {'(avg±std)':<20}")
            print("-"*140)

            for trace_name in sorted(self.data[config_name].keys()):
                if trace_name.startswith('_'):  # Skip metadata entries
                    continue

                profile_bw_mean, profile_bw_std = self.calculate_profile_bandwidth(config_name, trace_name)
                bitrate_mean, bitrate_std = self.calculate_average_bitrate(config_name, trace_name)
                framerate_mean, framerate_std = self.calculate_average_framerate(config_name, trace_name)
                fps_p05, fps_p01, fps_p0_1 = self.calculate_framerate_tail_percentiles(config_name, trace_name)
                kvcache_mean, kvcache_std = self.calculate_kvcache_throughput(config_name, trace_name)
                prompt_mean, prompt_std = self.calculate_prompt_delivery_time(config_name, trace_name)

                profile_bw_str = f"{profile_bw_mean:.1f}±{profile_bw_std:.1f}" if profile_bw_mean is not None else "N/A"
                bitrate_str = f"{bitrate_mean:.2f}±{bitrate_std:.2f}" if bitrate_mean is not None else "N/A"
                framerate_str = f"{framerate_mean:.1f}±{framerate_std:.1f}" if framerate_mean is not None else "N/A"
                fps_tail_str = f"{fps_p05:.1f}/{fps_p01:.1f}/{fps_p0_1:.1f}" if fps_p0_1 is not None else "N/A"
                kvcache_str = f"{kvcache_mean:.1f}±{kvcache_std:.1f}" if kvcache_mean is not None else "N/A"
                prompt_str = f"{prompt_mean:.1f}±{prompt_std:.1f}" if prompt_mean is not None else "N/A"

                print(f"{trace_name:<15} {profile_bw_str:<16} {bitrate_str:<16} {framerate_str:<16} "
                      f"{fps_tail_str:<18} {kvcache_str:<18} {prompt_str:<20}")

        print("="*140)


def main():
    """Main function."""
    import argparse

    parser = argparse.ArgumentParser(description='Analyze automated experiment results')
    parser.add_argument('--results-dir', type=str, default='results/poc101',
                       help='Path to results directory (default: results/poc101)')
    parser.add_argument('--output-dir', type=str, default='plots',
                       help='Output directory for plots (default: plots)')
    args = parser.parse_args()

    # Create analyzer
    analyzer = ExperimentAnalyzer(args.results_dir)

    # Load data
    analyzer.load_data()

    # Print summary
    analyzer.print_summary()

    # Generate plots
    analyzer.generate_all_plots(args.output_dir)


if __name__ == "__main__":
    main()
