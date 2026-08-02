#!/usr/bin/env python3
"""Measure sender -> Pi demux -> Pi decode/present latency per HEVC frame.

This utility is intentionally not part of the CMake build yet. It requires the
PyAV wheel (`python -m pip install av`) and a running svrt-receiver.
"""

import argparse
from fractions import Fraction
import socket
import statistics
import time

try:
    import av
except ImportError as error:
    raise SystemExit("PyAV is required: python -m pip install av") from error


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("host", help="Raspberry Pi hostname or IP address")
    parser.add_argument("--video-port", type=int, default=9944)
    parser.add_argument("--status-port", type=int, default=9945)
    parser.add_argument("--frames", type=int, default=30)
    parser.add_argument("--warmup", type=int, default=12)
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--height", type=int, default=720)
    parser.add_argument("--fps", type=int, default=30)
    parser.add_argument("--encoder", default="libx265")
    return parser.parse_args()


def test_frame(width, height, index):
    frame = av.VideoFrame(width, height, "yuv420p")
    frame.planes[0].update(bytes([32 + index % 192]) * frame.planes[0].buffer_size)
    frame.planes[1].update(bytes([96]) * frame.planes[1].buffer_size)
    frame.planes[2].update(bytes([160]) * frame.planes[2].buffer_size)
    return frame


def open_trace(host, port, nonce, pts_us):
    control = socket.create_connection((host, port), timeout=3.0)
    control.settimeout(12.0)
    control.sendall(f"SVRT/1 TRACE {nonce} {pts_us}\n".encode("ascii"))
    return control, control.makefile("r", encoding="ascii", newline="\n")


def read_ack(line, nonce, pts_us):
    fields = line.strip().split()
    if len(fields) != 6 or fields[:2] != ["SVRT/1", "ACK"]:
        raise RuntimeError(f"invalid Pi response: {line.rstrip()}")
    if int(fields[2]) != nonce or int(fields[4]) != pts_us:
        raise RuntimeError(f"mismatched Pi response: {line.rstrip()}")
    return fields[3], int(fields[5])


def main():
    args = parse_args()
    url = f"tcp://{args.host}:{args.video_port}?tcp_nodelay=1"
    output = av.open(url, "w", format="mpegts",
                     options={"mpegts_copyts": "1", "muxdelay": "0"})
    stream = output.add_stream(args.encoder, rate=args.fps)
    stream.width = args.width
    stream.height = args.height
    stream.pix_fmt = "yuv420p"
    stream.codec_context.time_base = Fraction(1, args.fps)
    stream.codec_context.options = {
        "preset": "ultrafast",
        "tune": "zerolatency",
        "x265-params": "bframes=0:keyint=1:scenecut=0",
    }

    measured = []
    submitted_frames = 0
    total = args.warmup + args.frames
    print("packet  send->receive-ack  receive->process  send->process-ack")

    def process_packet(packet):
        if packet.pts is None:
            output.mux(packet)
            return
        packet_frame = round(Fraction(packet.pts) * packet.time_base * args.fps)
        pts_us = int(Fraction(packet.pts) * packet.time_base * 1_000_000)
        if packet_frame < args.warmup:
            output.mux(packet)
            return

        nonce = time.monotonic_ns()
        control, replies = open_trace(args.host, args.status_port, nonce, pts_us)
        try:
            sent_ns = time.monotonic_ns()
            output.mux(packet)
            first = replies.readline()
            received_ns = time.monotonic_ns()
            first_stage, pi_received_us = read_ack(first, nonce, pts_us)
            if first_stage == "TIMEOUT":
                raise RuntimeError(f"Pi timed out processing packet {packet_frame}")
            if first_stage != "RECEIVED":
                raise RuntimeError(f"unexpected Pi stage {first_stage}")

            second = replies.readline()
            processed_ns = time.monotonic_ns()
            second_stage, pi_processed_us = read_ack(second, nonce, pts_us)
            if second_stage == "TIMEOUT":
                raise RuntimeError(f"Pi timed out processing packet {packet_frame}")
            if second_stage != "PROCESSED":
                raise RuntimeError(f"unexpected Pi stage {second_stage}")
        finally:
            replies.close()
            control.close()

        to_receive = (received_ns - sent_ns) / 1_000_000
        processing = (pi_processed_us - pi_received_us) / 1000
        total_ms = (processed_ns - sent_ns) / 1_000_000
        measured.append(total_ms)
        print(f"{packet_frame - args.warmup + 1:6d}"
              f"  {to_receive:13.3f} ms"
              f"  {processing:17.3f} ms"
              f"  {total_ms:7.3f} ms")

    while submitted_frames < total:
        frame = test_frame(args.width, args.height, submitted_frames)
        frame.pts = submitted_frames
        frame.time_base = Fraction(1, args.fps)
        packets = stream.encode(frame)
        submitted_frames += 1
        for packet in packets:
            process_packet(packet)

    for packet in stream.encode():
        process_packet(packet)
    output.close()
    if measured:
        ordered = sorted(measured)
        p95 = ordered[min(len(ordered) - 1, int(len(ordered) * 0.95))]
        print(f"average={statistics.fmean(measured):.3f} ms "
              f"p95={p95:.3f} ms max={max(measured):.3f} ms")


if __name__ == "__main__":
    main()
