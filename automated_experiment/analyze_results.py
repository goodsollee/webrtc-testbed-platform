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

try:
    from analysis.plotting_ext.style_acm import apply_acm_style
    from analysis.plotting_ext.presets import FIGSIZE
    from analysis.plotting_ext.loader import load_figure_spec
    from analysis.plotting_ext.renderers import PlotRegistry
    from analysis.plotting_ext.spec_types import FigureDefaults, FigureEntry, FigureSpec
    HAS_EXT = True
except Exception:
    HAS_EXT = False

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

                # Load bandwidth data from emulator_logs
                emulator_logs_dir = trace_folder / "emulator_logs"
                if emulator_logs_dir.exists():
                    bandwidth_file = emulator_logs_dir / f"{trace_name}_bandwidth.csv"
                    if bandwidth_file.exists():
                        self.data[config_name][trace_name]['bandwidth'] = pd.read_csv(bandwidth_file)

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

                # Load RTP throughput (T01_throughput.csv)
                rtp_throughput_file = receiver_dir / "T01_throughput.csv"
                if rtp_throughput_file.exists():
                    self.data[config_name][trace_name]['rtp_throughput'] = pd.read_csv(rtp_throughput_file)

                # Load SCTP throughput files (KVCache and Prompt)
                sctp_throughput_files = list(receiver_dir.glob("*_dc*_throughput.csv"))
                if sctp_throughput_files:
                    sctp_data = {}
                    for sctp_file in sctp_throughput_files:
                        # Extract flow name from filename (e.g., KVCache1_dc0, Prompt_dc2)
                        flow_name = sctp_file.stem.replace('_throughput', '')
                        sctp_data[flow_name] = pd.read_csv(sctp_file)
                    self.data[config_name][trace_name]['sctp_throughput'] = sctp_data

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

    def calculate_total_sctp_throughput(self, config_name, trace_name):
        """Calculate average total SCTP throughput from T01_throughput.csv in Mbps."""
        if 'rtp_throughput' not in self.data[config_name][trace_name]:
            return None, None

        df = self.data[config_name][trace_name]['rtp_throughput']
        if 'receive_throughput_mbps' not in df.columns:
            return None, None

        return df['receive_throughput_mbps'].mean(), df['receive_throughput_mbps'].std()

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
            return None, None, None

        df = self.data[config_name][trace_name]['average_stats']
        if 'framerate' not in df.columns:
            return None, None, None

        capped = np.minimum(df['framerate'].astype(float), 30.0)
        capped_clean = capped.dropna()

        # Check if we have any data after dropping NaN values
        if len(capped_clean) == 0:
            return None, None, None

        # Calculate percentiles (lower percentile = worse tail performance)
        p05 = np.percentile(capped_clean, 5)
        p01 = np.percentile(capped_clean, 1)   # 1st percentile
        p0_1 = np.percentile(capped_clean, 0.1)  # 0.1st percentile
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

    def plot_throughput_timeline(self, output_dir="plots"):
        """
        Plot throughput timeline graphs for RTP and SCTP flows.
        Creates two separate timeline plots for each experiment (config + trace combination):
        1. Available bandwidth, RTP total, and SCTP total (aggregated)
        2. Individual SCTP flows breakdown
        Plots are saved in: plots/{result_name}/timeline/{exp_name}_*.png
        """
        output_path = Path(output_dir)
        output_path.mkdir(exist_ok=True)

        print("\nGenerating throughput timeline plots...")

        for config_name in sorted(self.data.keys()):
            # Create config-specific output directory
            config_output_dir = output_path / config_name / "timeline"
            config_output_dir.mkdir(parents=True, exist_ok=True)

            for trace_name in sorted(self.data[config_name].keys()):
                if trace_name.startswith('_'):  # Skip metadata entries
                    continue

                exp_name = f"{config_name}_{trace_name}"
                trace_data = self.data[config_name][trace_name]

                # Check if we have data
                has_bandwidth = 'bandwidth' in trace_data
                has_rtp = 'average_stats' in trace_data
                has_sctp = 'sctp_throughput' in trace_data

                if not (has_bandwidth or has_rtp or has_sctp):
                    continue

                # Use RTP video timeline as the reference time range
                if not has_rtp or 'timestamp_ms' not in trace_data['average_stats'].columns:
                    continue

                min_time_ms = trace_data['average_stats']['timestamp_ms'].min()
                max_time_ms = trace_data['average_stats']['timestamp_ms'].max()

                # Check if timestamps are valid
                if pd.isna(min_time_ms) or pd.isna(max_time_ms):
                    print(f"  Skipping {exp_name}: No valid timestamp data")
                    continue

                max_time_s = (max_time_ms - min_time_ms) / 1000.0

                # ============================================================
                # PLOT 1: Available Bandwidth + RTP Total + SCTP Total
                # ============================================================
                fig1, ax1 = plt.subplots(figsize=(14, 6))

                # Plot Available Bandwidth
                if has_bandwidth:
                    df_bw = trace_data['bandwidth']
                    if 'elapsed_ms' in df_bw.columns and 'bandwidth_kbps' in df_bw.columns:
                        time_s = (df_bw['elapsed_ms'] - min_time_ms) / 1000.0
                        bandwidth_mbps = df_bw['bandwidth_kbps'] / 1000.0
                        ax1.plot(time_s, bandwidth_mbps, linewidth=2, label='Available Bandwidth',
                                color='#808080', linestyle='--', alpha=0.8)

                # Plot RTP Total Bitrate
                if has_rtp and 'bitrates' in trace_data['average_stats'].columns:
                    df_rtp = trace_data['average_stats']
                    if 'timestamp_ms' in df_rtp.columns:
                        time_s = (df_rtp['timestamp_ms'] - min_time_ms) / 1000.0
                        bitrates_mbps = df_rtp['bitrates'] / 1_000_000
                        ax1.plot(time_s, bitrates_mbps, linewidth=2, label='RTP Total',
                                color='#1f77b4')

                # Plot SCTP Total Throughput (from T01_throughput.csv)
                if 'rtp_throughput' in trace_data:
                    df_sctp_total = trace_data['rtp_throughput']
                    if 'timestamp_ms' in df_sctp_total.columns and 'receive_throughput_mbps' in df_sctp_total.columns:
                        time_s = (df_sctp_total['timestamp_ms'] - min_time_ms) / 1000.0
                        sctp_throughput_mbps = df_sctp_total['receive_throughput_mbps']
                        ax1.plot(time_s, sctp_throughput_mbps, linewidth=2, label='SCTP Total',
                                color='#ff7f0e')

                ax1.set_xlabel('Time (s)', fontsize=12)
                ax1.set_ylabel('Throughput (Mbps)', fontsize=12)
                ax1.set_title(f'Bandwidth and Total Throughput Timeline — {exp_name}', fontsize=14, fontweight='bold')
                ax1.set_xlim(0, max_time_s)
                ax1.grid(True, alpha=0.3)
                ax1.legend(loc='best')
                plt.tight_layout()
                output_file1 = config_output_dir / f"{exp_name}_overview_timeline.png"
                plt.savefig(output_file1, dpi=300, bbox_inches='tight')
                print(f"  Saved: {output_file1}")
                plt.close()

                # ============================================================
                # PLOT 2: Individual SCTP Flows
                # ============================================================
                if has_sctp:
                    fig2, ax2 = plt.subplots(figsize=(14, 6))
                    colors = ['#2ca02c', '#d62728', '#9467bd', '#8c564b', '#e377c2', '#7f7f7f']
                    color_idx = 0

                    for flow_name, df_sctp in sorted(trace_data['sctp_throughput'].items()):
                        if 'timestamp_ms' in df_sctp.columns and 'receive_throughput_mbps' in df_sctp.columns:
                            time_s = (df_sctp['timestamp_ms'] - min_time_ms) / 1000.0
                            throughput_mbps = df_sctp['receive_throughput_mbps']
                            ax2.plot(time_s, throughput_mbps, linewidth=2,
                                   label=flow_name, color=colors[color_idx % len(colors)])
                            color_idx += 1

                    ax2.set_xlabel('Time (s)', fontsize=12)
                    ax2.set_ylabel('Throughput (Mbps)', fontsize=12)
                    ax2.set_title(f'Individual SCTP Flow Throughput Timeline — {exp_name}', fontsize=14, fontweight='bold')
                    ax2.set_xlim(0, max_time_s)
                    ax2.grid(True, alpha=0.3)
                    ax2.legend(loc='best')
                    plt.tight_layout()
                    output_file2 = config_output_dir / f"{exp_name}_sctp_flows_timeline.png"
                    plt.savefig(output_file2, dpi=300, bbox_inches='tight')
                    print(f"  Saved: {output_file2}")
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

        # 3. Total SCTP throughput comparison
        self.plot_metric_comparison(
            'total_sctp_throughput',
            'Throughput (Mbps)',
            'Total SCTP Throughput Comparison Across Traces',
            output_path / 'sctp_throughput_comparison.png'
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

        # 6. Throughput timeline plots (RTP and SCTP)
        self.plot_throughput_timeline(output_dir=output_path)

        print("\nAll plots generated successfully!")

    def generate_all_plots_acm(self, output_dir="plots", size_preset=None, spec_path=None):
        """Optional ACM-styled rendering with optional specification support."""

        cli_style = getattr(self, "_acm_cli_style", False)
        use_acm = cli_style or os.getenv("EXPR_PLOTS_STYLE", "").lower() == "acm"

        include_configs = getattr(self, "_acm_include_configs", None)
        include_traces = getattr(self, "_acm_include_traces", None)
        exclude_traces = getattr(self, "_acm_exclude_traces", None)

        if not HAS_EXT or not (
            use_acm
            or size_preset
            or spec_path
            or include_configs
            or include_traces
            or exclude_traces
        ):
            return self.generate_all_plots(output_dir)

        spec = load_figure_spec(spec_path) if spec_path else None

        valid_size = size_preset if size_preset in FIGSIZE else None
        figsize = FIGSIZE.get(valid_size, None)

        needs_filters = any([include_configs, include_traces, exclude_traces])

        if spec is None and needs_filters:
            defaults = FigureDefaults(
                size=valid_size,
                output_dir=output_dir,
            )
            spec = FigureSpec(
                defaults=defaults,
                figures=[
                    FigureEntry(
                        id="bitrate_comparison",
                        kind="groupedbar",
                        metric="average_bitrate",
                        ylabel="Bitrate (Mbps)",
                        xlabel="Trace",
                        title="Average Bitrate Comparison Across Traces",
                    ),
                    FigureEntry(
                        id="framerate_comparison",
                        kind="groupedbar",
                        metric="average_framerate",
                        ylabel="Frame Rate (fps)",
                        xlabel="Trace",
                        title="Average Frame Rate Comparison Across Traces",
                    ),
                    FigureEntry(
                        id="sctp_throughput_comparison",
                        kind="groupedbar",
                        metric="total_sctp_throughput",
                        ylabel="Throughput (Mbps)",
                        xlabel="Trace",
                        title="Total SCTP Throughput Comparison Across Traces",
                    ),
                    FigureEntry(
                        id="prompt_delivery_time_comparison",
                        kind="groupedbar",
                        metric="prompt_delivery_time",
                        ylabel="Delivery Time (ms)",
                        xlabel="Trace",
                        title="Prompt Delivery Time Comparison Across Traces",
                    ),
                    FigureEntry(
                        id="framerate_cdf_overall",
                        kind="cdf",
                        metric="framerate_cdf",
                        xlabel="Frame rate (fps) [capped at 30]",
                        ylabel="ECDF",
                        title="Frame Rate CDF — Overall (All Traces)",
                    ),
                    FigureEntry(
                        id="throughput_timeline",
                        kind="timeline",
                    ),
                ],
            )

        render_legacy_first = bool(spec_path and spec)
        if render_legacy_first:
            self.generate_all_plots(output_dir)

        with apply_acm_style():
            if spec:
                PlotRegistry.render_from_spec(
                    analyzer=self,
                    spec=spec,
                    default_output_dir=output_dir,
                    default_figsize=figsize,
                    include_configs=include_configs,
                    include_traces=include_traces,
                    exclude_traces=exclude_traces,
                )
            else:
                self.generate_all_plots(output_dir)

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
                  f"{'FPS Tail':<18} {'SCTP Tput':<18} {'Prompt Lat (ms)':<20}")
            print(f"{'':15} {'':16} {'':16} {'':16} {'(p5/p1/p0.1)':<18} {'(Mbps)':<18} {'(avg±std)':<20}")
            print("-"*140)

            for trace_name in sorted(self.data[config_name].keys()):
                if trace_name.startswith('_'):  # Skip metadata entries
                    continue

                profile_bw_mean, profile_bw_std = self.calculate_profile_bandwidth(config_name, trace_name)
                bitrate_mean, bitrate_std = self.calculate_average_bitrate(config_name, trace_name)
                framerate_mean, framerate_std = self.calculate_average_framerate(config_name, trace_name)
                fps_p05, fps_p01, fps_p0_1 = self.calculate_framerate_tail_percentiles(config_name, trace_name)
                sctp_mean, sctp_std = self.calculate_total_sctp_throughput(config_name, trace_name)
                prompt_mean, prompt_std = self.calculate_prompt_delivery_time(config_name, trace_name)

                profile_bw_str = f"{profile_bw_mean:.1f}±{profile_bw_std:.1f}" if profile_bw_mean is not None else "N/A"
                bitrate_str = f"{bitrate_mean:.2f}±{bitrate_std:.2f}" if bitrate_mean is not None else "N/A"
                framerate_str = f"{framerate_mean:.1f}±{framerate_std:.1f}" if framerate_mean is not None else "N/A"
                fps_tail_str = f"{fps_p05:.1f}/{fps_p01:.1f}/{fps_p0_1:.1f}" if fps_p0_1 is not None else "N/A"
                sctp_str = f"{sctp_mean:.1f}±{sctp_std:.1f}" if sctp_mean is not None else "N/A"
                prompt_str = f"{prompt_mean:.1f}±{prompt_std:.1f}" if prompt_mean is not None else "N/A"

                print(f"{trace_name:<15} {profile_bw_str:<16} {bitrate_str:<16} {framerate_str:<16} "
                      f"{fps_tail_str:<18} {sctp_str:<18} {prompt_str:<20}")

        print("="*140)


def main():
    """Main function."""
    import argparse

    parser = argparse.ArgumentParser(description='Analyze automated experiment results')
    parser.add_argument('--results-dir', type=str, default='results/poc101',
                       help='Path to results directory (default: results/poc101)')
    parser.add_argument('--output-dir', type=str, default='plots',
                       help='Output directory for plots (default: plots)')
    parser.add_argument('--acm-style', action='store_true', help='Enable ACM rcParams styling')
    parser.add_argument('--figsize-preset', choices=['quarter', 'half'],
                        help='Apply a predefined ACM figure size preset')
    parser.add_argument('--spec', type=str,
                        help='Optional YAML/JSON figure specification for ACM rendering')
    parser.add_argument('--include-configs', type=str,
                        help='Comma-separated list of configs to include (ACM path only)')
    parser.add_argument('--include-traces', type=str,
                        help='Comma-separated list of traces to include (ACM path only)')
    parser.add_argument('--exclude-traces', type=str,
                        help='Comma-separated list of traces to exclude (ACM path only)')
    args = parser.parse_args()

    # Create analyzer
    analyzer = ExperimentAnalyzer(args.results_dir)

    # Load data
    analyzer.load_data()

    # Print summary
    analyzer.print_summary()

    # Generate plots
    def _parse_csv(value):
        if not value:
            return None
        return [item.strip() for item in value.split(',') if item.strip()]

    analyzer._acm_cli_style = bool(args.acm_style)
    analyzer._acm_include_configs = _parse_csv(args.include_configs)
    analyzer._acm_include_traces = _parse_csv(args.include_traces)
    analyzer._acm_exclude_traces = _parse_csv(args.exclude_traces)

    acm_requested = any([
        args.acm_style,
        args.figsize_preset,
        args.spec,
        args.include_configs,
        args.include_traces,
        args.exclude_traces,
        os.getenv("EXPR_PLOTS_STYLE", "").lower() == "acm",
    ])

    if acm_requested:
        analyzer.generate_all_plots_acm(
            output_dir=args.output_dir,
            size_preset=args.figsize_preset,
            spec_path=args.spec,
        )
    else:
        analyzer.generate_all_plots(args.output_dir)


if __name__ == "__main__":
    main()
