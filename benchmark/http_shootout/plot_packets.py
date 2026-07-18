#!/usr/bin/env python3
"""Plot QUIC packet length over time, split into a server-sent and a client-sent chart.

Input is either a pcap captured with tcpdump:
    tcpdump -i any -w quic_capture.pcap 'udp port 4433'

or a tshark CSV export:
    tshark -r quic_capture.pcap -T fields -E separator=, -E header=y \
        -e frame.time_epoch -e ip.src -e ip.dst -e udp.srcport -e udp.dstport -e udp.length \
        > packets.csv

Usage:
    python plot_packets.py --pcap quic_capture.pcap --port 4433
    python plot_packets.py --csv packets.csv --port 4433 --out chart.png
"""

import argparse
import csv
import sys
from pathlib import Path

import matplotlib.pyplot as plt

SERVER_COLOR = "#2a78d6"  # categorical slot 1 (blue)
CLIENT_COLOR = "#008300"  # categorical slot 2 (green)
GRID_COLOR = "#e1e0d9"
AXIS_COLOR = "#c3c2b7"
TEXT_PRIMARY = "#0b0b0b"
TEXT_SECONDARY = "#52514e"
SURFACE = "#fcfcfb"


def load_from_pcap(path, server_port):
    from scapy.all import rdpcap, UDP

    pkts = rdpcap(str(path))
    t0 = None
    server_pts, client_pts = [], []
    for pkt in pkts:
        if UDP not in pkt:
            continue
        udp = pkt[UDP]
        if server_port not in (udp.sport, udp.dport):
            continue
        if t0 is None:
            t0 = pkt.time
        t = float(pkt.time - t0)
        length = len(udp)
        if udp.sport == server_port:
            server_pts.append((t, length))
        else:
            client_pts.append((t, length))
    return server_pts, client_pts


def load_from_csv(path, server_port):
    server_pts, client_pts = [], []
    t0 = None
    with open(path, newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            sport = int(row["udp.srcport"])
            dport = int(row["udp.dstport"])
            if server_port not in (sport, dport):
                continue
            t = float(row["frame.time_epoch"])
            if t0 is None:
                t0 = t
            t -= t0
            length = int(row["udp.length"])
            if sport == server_port:
                server_pts.append((t, length))
            else:
                client_pts.append((t, length))
    return server_pts, client_pts


def plot(server_pts, client_pts, out, title=None):
    fig, axes = plt.subplots(2, 1, figsize=(10, 7), sharex=True, facecolor=SURFACE)

    if title:
        manager = getattr(fig.canvas, "manager", None)
        if manager is not None and hasattr(manager, "set_window_title"):
            manager.set_window_title(title)
        fig.suptitle(title, color=SERVER_COLOR, fontsize=14, fontweight="bold")

    for ax, pts, color, chart_title in (
        (axes[0], server_pts, SERVER_COLOR, "Server → client"),
        (axes[1], client_pts, CLIENT_COLOR, "Client → server"),
    ):
        ax.set_facecolor(SURFACE)
        if pts:
            xs, ys = zip(*pts)
            ax.scatter(xs, ys, s=10, color=color, alpha=0.7, linewidths=0)
        ax.set_title(chart_title, loc="left", color=TEXT_PRIMARY, fontsize=11, fontweight="bold")
        ax.set_ylabel("Packet length (bytes)", color=TEXT_SECONDARY, fontsize=9)
        ax.grid(True, color=GRID_COLOR, linewidth=0.8)
        ax.spines[["top", "right"]].set_visible(False)
        ax.spines[["left", "bottom"]].set_color(AXIS_COLOR)
        ax.tick_params(colors=TEXT_SECONDARY, labelsize=8)

    axes[-1].set_xlabel("Time (s)", color=TEXT_SECONDARY, fontsize=9)
    fig.tight_layout()

    if out:
        fig.savefig(out, dpi=150, facecolor=SURFACE)
        print(f"Saved to {out}")
    else:
        plt.show()


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    src = parser.add_mutually_exclusive_group(required=True)
    src.add_argument("--pcap", type=Path, help="pcap file captured with tcpdump")
    src.add_argument("--csv", type=Path, help="tshark CSV export")
    parser.add_argument("--port", type=int, required=True, help="QUIC server UDP port")
    parser.add_argument("--out", type=Path, help="save chart to a file instead of showing it")
    parser.add_argument("--title", help="window/figure title (e.g. server name)")
    args = parser.parse_args()

    if args.pcap:
        server_pts, client_pts = load_from_pcap(args.pcap, args.port)
    else:
        server_pts, client_pts = load_from_csv(args.csv, args.port)

    if not server_pts and not client_pts:
        sys.exit(f"No packets found on port {args.port}")

    plot(server_pts, client_pts, args.out, args.title)


if __name__ == "__main__":
    main()
