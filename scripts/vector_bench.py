#!/usr/bin/env python3
"""
Vector Search Performance Benchmark
Connects to AstraDB via RESP protocol to measure vector command QPS.
Usage: python3 scripts/vector_bench.py [--host 127.0.0.1] [--port 6379] [--dim 768] [--count 10000]
"""

import socket
import struct
import time
import random
import argparse

def resp_encode(*args):
    """Build RESP protocol command."""
    parts = [f"*{len(args)}\r\n".encode()]
    for a in args:
        if isinstance(a, bytes):
            parts.append(f"${len(a)}\r\n".encode() + a + b"\r\n")
        else:
            s = a.encode() if isinstance(a, str) else str(a).encode()
            parts.append(f"${len(s)}\r\n".encode() + s + b"\r\n")
    return b"".join(parts)

def resp_decode_line(sock):
    """Read one RESP reply."""
    line = b""
    while not line.endswith(b"\r\n"):
        line += sock.recv(1)
    return line[:-2]

class AstraDBClient:
    def __init__(self, host="127.0.0.1", port=6379):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.connect((host, port))
        self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)

    def cmd(self, *args):
        req = resp_encode(*args)
        self.sock.sendall(req)
        line = resp_decode_line(self.sock)
        prefix = line[0:1]
        if prefix == b"+":
            return line[1:].decode()
        elif prefix == b"-":
            raise RuntimeError(line[1:].decode())
        elif prefix == b":":
            return int(line[1:])
        elif prefix == b"$":
            length = int(line[1:])
            if length == -1:
                return None
            data = b""
            while len(data) < length + 2:
                data += self.sock.recv(length + 2 - len(data))
            return data[:length]
        elif prefix == b"*":
            count = int(line[1:])
            items = []
            for _ in range(count):
                items.append(self._read_value())
            return items
        return None

    def _read_value(self):
        line = resp_decode_line(self.sock)
        prefix = line[0:1]
        if prefix == b"+":
            return line[1:].decode()
        elif prefix == b"-":
            return line[1:].decode()
        elif prefix == b":":
            return int(line[1:])
        elif prefix == b"$":
            length = int(line[1:])
            if length == -1:
                return None
            data = b""
            while len(data) < length + 2:
                data += self.sock.recv(length + 2 - len(data))
            return data[:length]
        elif prefix == b"*":
            count = int(line[1:])
            items = []
            for _ in range(count):
                items.append(self._read_value())
            return items
        return None

    def close(self):
        self.sock.close()


def random_vector(dim):
    return b"".join(struct.pack("f", random.gauss(0, 1)) for _ in range(dim))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=6379)
    parser.add_argument("--dim", type=int, default=768)
    parser.add_argument("--count", type=int, default=10000)
    parser.add_argument("--search-k", type=int, default=10)
    parser.add_argument("--search-qps-duration", type=int, default=10, help="seconds to run search QPS test")
    parser.add_argument("--concurrency", type=int, default=1)
    args = parser.parse_args()

    print(f"Connecting to {args.host}:{args.port}...")
    c = AstraDBClient(args.host, args.port)

    # 1. Create index
    print(f"\n=== Creating index (dim={args.dim}, cosine) ===")
    t0 = time.time()
    c.cmd("VCREATE", "bench", str(args.dim), "cosine", "M", "16", "EF", "200")
    elapsed = time.time() - t0
    print(f"VCREATE: {elapsed*1000:.2f} ms")

    # 2. Bulk insert vectors
    print(f"\n=== Bulk inserting {args.count} vectors ({args.dim}d) ===")
    t0 = time.time()
    vecs = [random_vector(args.dim) for _ in range(args.count)]
    gen_time = time.time() - t0
    print(f"  Vector generation: {gen_time*1000:.1f} ms")

    t0 = time.time()
    for i, vec in enumerate(vecs):
        c.cmd("VSET", "bench", f"d:{i}", vec)
        if (i + 1) % max(1, args.count//10) == 0:
            elapsed = time.time() - t0
            rate = (i + 1) / elapsed
            eta = (args.count - i - 1) / rate if rate > 0 else 0
            print(f"  [{i+1}/{args.count}] {rate:.0f} vec/s, ETA {eta:.1f}s")
    insert_time = time.time() - t0
    print(f"  Total insert: {insert_time:.2f}s ({args.count/insert_time:.0f} vec/s)")

    # 3. Verify index
    info = c.cmd("VINFO", "bench")
    print(f"\n  VINFO: {info}")

    # 4. Search QPS benchmark (single connection)
    print(f"\n=== Search QPS (k={args.search_k}, duration={args.search_qps_duration}s) ===")
    queries = [random_vector(args.dim) for _ in range(100)]
    t0 = time.time()
    searches = 0
    while time.time() - t0 < args.search_qps_duration:
        q = queries[searches % len(queries)]
        results = c.cmd("VSEARCH", "bench", q, str(args.search_k))
        searches += 1
    qps_time = time.time() - t0
    qps = searches / qps_time
    print(f"  {searches} searches in {qps_time:.2f}s = {qps:.0f} QPS")
    print(f"  Avg latency: {1000/qps:.2f} ms")

    # 5. Mixed R/W workload
    print(f"\n=== Mixed R/W workload (80% read, 20% write, {args.search_qps_duration}s) ===")
    write_vec = random_vector(args.dim)
    t0 = time.time()
    ops = 0
    writes = 0
    while time.time() - t0 < args.search_qps_duration:
        if random.random() < 0.8:
            q = queries[ops % len(queries)]
            c.cmd("VSEARCH", "bench", q, str(args.search_k))
        else:
            writes += 1
            c.cmd("VSET", "bench", f"w:{writes}", write_vec)
        ops += 1
    mixed_time = time.time() - t0
    print(f"  {ops} ops in {mixed_time:.2f}s = {ops/mixed_time:.0f} ops/s")
    print(f"  {writes} writes, {ops-writes} reads")

    c.close()
    print("\nDone.")


if __name__ == "__main__":
    main()
