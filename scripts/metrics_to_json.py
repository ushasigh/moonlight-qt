#!/usr/bin/env python3
"""
Moonlight Streaming Metrics to JSON Exporter

This script parses the performance overlay metrics from Moonlight-Qt and outputs them to JSON.
It can be used in two ways:
1. Parse metrics from stdin (pipe overlay text to it)
2. Parse metrics from a log file

Usage:
    # Parse from stdin
    echo "Video stream: 1920x1080 60.00 FPS..." | python3 metrics_to_json.py
    
    # Parse from a file
    python3 metrics_to_json.py --input overlay_log.txt --output metrics.json
    
    # Continuously monitor a log file
    python3 metrics_to_json.py --input overlay_log.txt --output metrics.json --watch
"""

import re
import json
import argparse
import sys
import time
from datetime import datetime
from pathlib import Path


def parse_metrics(text: str, log_timestamp: str = None) -> dict:
    """
    Parse Moonlight overlay metrics text into a structured dictionary.
    
    Args:
        text: The overlay text containing metrics
        log_timestamp: Optional timestamp string from log (HH:MM:SS format)
        
    Returns:
        Dictionary with parsed metrics
    """
    # Use log timestamp if provided, otherwise use current time
    if log_timestamp:
        # Combine log time (HH:MM:SS) with today's date
        today = datetime.now().date()
        try:
            time_parts = log_timestamp.split(':')
            timestamp = datetime(
                today.year, today.month, today.day,
                int(time_parts[0]), int(time_parts[1]), int(time_parts[2])
            ).isoformat()
        except (ValueError, IndexError):
            timestamp = datetime.now().isoformat()
    else:
        timestamp = datetime.now().isoformat()
    
    metrics = {
        "timestamp": timestamp,
        "video_stream": {},
        "frame_rates": {},
        "host_processing_latency": {},
        "network": {},
        "timing": {}
    }
    
    # Video stream: WIDTHxHEIGHT FPS (Codec: CODEC)
    video_match = re.search(
        r'Video stream:\s*(\d+)x(\d+)\s+([\d.]+)\s*FPS\s*\(Codec:\s*([^)]+)\)',
        text
    )
    if video_match:
        metrics["video_stream"] = {
            "width": int(video_match.group(1)),
            "height": int(video_match.group(2)),
            "fps": float(video_match.group(3)),
            "codec": video_match.group(4).strip()
        }
    
    # Bitrate: X.X Mbps, Peak (Ys): Y.Y (if DISPLAY_BITRATE is defined)
    bitrate_match = re.search(
        r'Bitrate:\s*([\d.]+)\s*Mbps,\s*Peak\s*\((\d+)s\):\s*([\d.]+)',
        text
    )
    if bitrate_match:
        metrics["video_stream"]["bitrate_mbps"] = float(bitrate_match.group(1))
        metrics["video_stream"]["peak_window_seconds"] = int(bitrate_match.group(2))
        metrics["video_stream"]["peak_bitrate_mbps"] = float(bitrate_match.group(3))
    
    # Incoming frame rate from network: X.XX FPS
    incoming_fps_match = re.search(
        r'Incoming frame rate from network:\s*([\d.]+)\s*FPS',
        text
    )
    if incoming_fps_match:
        metrics["frame_rates"]["incoming_network_fps"] = float(incoming_fps_match.group(1))
    
    # Decoding frame rate: X.XX FPS
    decoding_fps_match = re.search(
        r'Decoding frame rate:\s*([\d.]+)\s*FPS',
        text
    )
    if decoding_fps_match:
        metrics["frame_rates"]["decoding_fps"] = float(decoding_fps_match.group(1))
    
    # Rendering frame rate: X.XX FPS
    rendering_fps_match = re.search(
        r'Rendering frame rate:\s*([\d.]+)\s*FPS',
        text
    )
    if rendering_fps_match:
        metrics["frame_rates"]["rendering_fps"] = float(rendering_fps_match.group(1))
    
    # Host processing latency min/max/average: X.X/X.X/X.X ms
    latency_match = re.search(
        r'Host processing latency min/max/average:\s*([\d.]+)/([\d.]+)/([\d.]+)\s*ms',
        text
    )
    if latency_match:
        metrics["host_processing_latency"] = {
            "min_ms": float(latency_match.group(1)),
            "max_ms": float(latency_match.group(2)),
            "average_ms": float(latency_match.group(3))
        }
    
    # Frames dropped by your network connection: X.XX%
    network_dropped_match = re.search(
        r'Frames dropped by your network connection:\s*([\d.]+)%',
        text
    )
    if network_dropped_match:
        metrics["network"]["frames_dropped_percent"] = float(network_dropped_match.group(1))
    
    # Frames dropped due to network jitter: X.XX%
    jitter_dropped_match = re.search(
        r'Frames dropped due to network jitter:\s*([\d.]+)%',
        text
    )
    if jitter_dropped_match:
        metrics["network"]["jitter_dropped_percent"] = float(jitter_dropped_match.group(1))
    
    # Average network latency: X ms (variance: Y ms) or N/A
    rtt_match = re.search(
        r'Average network latency:\s*(\d+)\s*ms\s*\(variance:\s*(\d+)\s*ms\)',
        text
    )
    if rtt_match:
        metrics["network"]["rtt_ms"] = int(rtt_match.group(1))
        metrics["network"]["rtt_variance_ms"] = int(rtt_match.group(2))
    else:
        na_match = re.search(r'Average network latency:\s*N/A', text)
        if na_match:
            metrics["network"]["rtt_ms"] = None
            metrics["network"]["rtt_variance_ms"] = None
    
    # Average decoding time: X.XX ms
    decode_time_match = re.search(
        r'Average decoding time:\s*([\d.]+)\s*ms',
        text
    )
    if decode_time_match:
        metrics["timing"]["average_decode_time_ms"] = float(decode_time_match.group(1))
    
    # Average frame queue delay: X.XX ms
    queue_delay_match = re.search(
        r'Average frame queue delay:\s*([\d.]+)\s*ms',
        text
    )
    if queue_delay_match:
        metrics["timing"]["average_queue_delay_ms"] = float(queue_delay_match.group(1))
    
    # Average rendering time (including monitor V-sync latency): X.XX ms
    render_time_match = re.search(
        r'Average rendering time.*?:\s*([\d.]+)\s*ms',
        text
    )
    if render_time_match:
        metrics["timing"]["average_render_time_ms"] = float(render_time_match.group(1))
    
    # Remove empty sub-dictionaries
    metrics = {k: v for k, v in metrics.items() if v}
    
    return metrics


def extract_metrics_blocks(text: str) -> list:
    """
    Extract metrics blocks from log output.
    Handles [METRICS] prefixed log lines from Moonlight.
    
    Returns:
        List of tuples: (timestamp, metrics_text)
        timestamp is HH:MM:SS string or None if not found
    """
    blocks = []
    
    # Look for [METRICS] tagged entries with timestamp prefix
    # Format: "HH:MM:SS - SDL Info (0): [METRICS] Video stream: ..."
    metrics_pattern = re.compile(
        r'(\d{2}:\d{2}:\d{2})\s*-\s*SDL\s+Info\s*\(\d+\):\s*\[METRICS\]\s*(.*?)(?=\d{2}:\d{2}:\d{2}\s*-\s*SDL\s+Info.*?\[METRICS\]|$)',
        re.DOTALL
    )
    matches = metrics_pattern.findall(text)
    if matches:
        blocks.extend([(ts, content.strip()) for ts, content in matches if content.strip()])
    
    # Fallback: Look for [METRICS] without timestamp
    if not blocks:
        simple_pattern = re.compile(r'\[METRICS\]\s*(.*?)(?=\[METRICS\]|$)', re.DOTALL)
        matches = simple_pattern.findall(text)
        if matches:
            blocks.extend([(None, m.strip()) for m in matches if m.strip()])
    
    # Fallback: raw metrics blocks (Video stream: ... pattern)
    if not blocks:
        raw_pattern = re.compile(r'(Video stream:.*?)(?=Video stream:|$)', re.DOTALL)
        matches = raw_pattern.findall(text)
        blocks.extend([(None, m.strip()) for m in matches if m.strip()])
    
    return blocks


def read_and_parse_continuous(input_path: Path, output_path: Path, interval: float = 1.0):
    """
    Continuously monitor a log file and update JSON output.
    
    Args:
        input_path: Path to the input log file
        output_path: Path to the output JSON file
        interval: Seconds between reads
    """
    all_metrics = []
    last_position = 0
    
    print(f"Watching {input_path} for new metrics...")
    print(f"Writing to {output_path}")
    print("Press Ctrl+C to stop.\n")
    
    try:
        while True:
            if input_path.exists():
                with open(input_path, 'r') as f:
                    f.seek(last_position)
                    new_content = f.read()
                    last_position = f.tell()
                    
                    if new_content.strip():
                        # Extract metrics blocks from log content
                        blocks = extract_metrics_blocks(new_content)
                        for log_timestamp, block in blocks:
                            if block:
                                metrics = parse_metrics(block, log_timestamp)
                                if metrics.get("video_stream") or metrics.get("frame_rates"):
                                    all_metrics.append(metrics)
                                    print(f"[{metrics['timestamp']}] Captured metrics entry")
                        
                        # Write updated JSON
                        with open(output_path, 'w') as out_f:
                            json.dump(all_metrics, out_f, indent=2)
            
            time.sleep(interval)
    except KeyboardInterrupt:
        print(f"\n\nStopped. Total entries captured: {len(all_metrics)}")
        print(f"Output saved to: {output_path}")


def main():
    parser = argparse.ArgumentParser(
        description="Parse Moonlight streaming metrics and output to JSON",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__
    )
    parser.add_argument(
        "--input", "-i",
        type=Path,
        help="Input file containing metrics text (reads from stdin if not provided)"
    )
    parser.add_argument(
        "--output", "-o",
        type=Path,
        default=Path("metrics.json"),
        help="Output JSON file path (default: metrics.json)"
    )
    parser.add_argument(
        "--watch", "-w",
        action="store_true",
        help="Continuously watch input file for new metrics"
    )
    parser.add_argument(
        "--pretty", "-p",
        action="store_true",
        default=True,
        help="Pretty-print JSON output (default: True)"
    )
    parser.add_argument(
        "--interval",
        type=float,
        default=1.0,
        help="Watch interval in seconds (default: 1.0)"
    )
    
    args = parser.parse_args()
    
    if args.watch:
        if not args.input:
            print("Error: --watch requires --input to be specified", file=sys.stderr)
            sys.exit(1)
        read_and_parse_continuous(args.input, args.output, args.interval)
    else:
        # One-time parse
        if args.input:
            text = args.input.read_text()
        else:
            text = sys.stdin.read()
        
        # Extract all metrics blocks with timestamps
        blocks = extract_metrics_blocks(text)
        if blocks:
            all_metrics = []
            for log_timestamp, block in blocks:
                if block:
                    metrics = parse_metrics(block, log_timestamp)
                    if metrics.get("video_stream") or metrics.get("frame_rates"):
                        all_metrics.append(metrics)
            
            indent = 2 if args.pretty else None
            output = json.dumps(all_metrics, indent=indent)
        else:
            # Fallback: parse entire text as single block
            metrics = parse_metrics(text)
            indent = 2 if args.pretty else None
            output = json.dumps(metrics, indent=indent)
        
        if args.output:
            args.output.write_text(output)
            print(f"Metrics written to {args.output}")
        else:
            print(output)


if __name__ == "__main__":
    main()
