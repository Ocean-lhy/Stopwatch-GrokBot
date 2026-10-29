#!/usr/bin/env python3
"""Encode the 53 baked GrokBot states into a compact zlib atlas.

Every state starts from frame index 2. Frames use XOR deltas against the
previous frame; a frame index table allows exact reverse playback.
"""
from pathlib import Path
import json
import struct
import sys
import zlib

import numpy as np
from PIL import Image

WIDTH = 400
HEIGHT = 400
START_FRAME = 2
GRAY_LEVELS = np.arange(16, dtype=np.uint16) * 17
PACKED_FRAME_SIZE = WIDTH * HEIGHT // 2
GLOBAL_HEADER = struct.Struct("<4sHHHBBI")
CLIP_HEADER = struct.Struct("<16sHHIII")
FRAME_HEADER = struct.Struct("<IB3x")


def pack_frame(path: Path) -> bytes:
    image = np.asarray(Image.open(path).convert("RGBA"))
    alpha = image[:, :, 3:4].astype(np.uint16)
    rgb = image[:, :, :3].astype(np.uint16)
    gray = ((rgb * alpha + 127) // 255).max(axis=2).astype(np.uint8)
    idx = np.abs(
        gray[:, :, None].astype(np.int16) - GRAY_LEVELS[None, None, :].astype(np.int16)
    ).argmin(axis=2).astype(np.uint8)
    return ((idx[:, 0::2] << 4) | idx[:, 1::2]).tobytes()


def encode_entry(name: str, files: list[Path]) -> dict:
    records = []
    previous = None
    for i, path in enumerate(files):
        packed = pack_frame(path)
        if i == 0:
            payload, flags = packed, 0
        else:
            payload = bytes(a ^ b for a, b in zip(packed, previous))
            flags = 1
        records.append((flags, zlib.compress(payload, 9)))
        previous = packed

    index_size = len(records) * 8
    frame_offset = index_size
    index = bytearray()
    for flags, payload in records:
        index.extend(struct.pack("<II", frame_offset, len(payload) | (flags << 31)))
        frame_offset += FRAME_HEADER.size + len(payload)
    return {
        "name": name,
        "records": records,
        "index": bytes(index),
        "data_size": frame_offset,
    }


def main() -> None:
    if len(sys.argv) != 3:
        raise SystemExit("usage: encode_atlas.py FRAME_DIR OUTPUT_ATLAS")
    frames_root = Path(sys.argv[1])
    output_path = Path(sys.argv[2])
    summary = json.loads((frames_root / "summary.json").read_text())
    fps = int(summary["fps"])
    states = summary["states"]
    entries = []

    for item in states:
        files = sorted((frames_root / item["state"]).glob("*.png"))
        if len(files) != item["count"]:
            raise RuntimeError(f"{item['state']}: expected {item['count']} frames, found {len(files)}")
        if len(files) <= START_FRAME:
            raise RuntimeError(f"{item['state']}: not enough frames")
        entry = encode_entry(item["state"], files)
        entries.append(entry)
        print(f"{item['state']:16} {len(files):3} frames start={START_FRAME}")

    offset = GLOBAL_HEADER.size + len(entries) * CLIP_HEADER.size
    headers = []
    for entry in entries:
        name = entry["name"].encode("ascii")[:15]
        headers.append(CLIP_HEADER.pack(
            name, len(entry["records"]), START_FRAME, offset,
            entry["data_size"], PACKED_FRAME_SIZE,
        ))
        offset += entry["data_size"]

    output = bytearray(GLOBAL_HEADER.pack(
        b"GBZ1", 1, WIDTH, HEIGHT, fps, len(entries), 0,
    ))
    output.extend(b"".join(headers))
    for entry in entries:
        output.extend(entry["index"])
        for flags, payload in entry["records"]:
            output.extend(FRAME_HEADER.pack(len(payload), flags))
            output.extend(payload)

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(output)
    print(f"wrote {output_path} ({len(output)} bytes, {len(output)/1024/1024:.2f} MiB)")


if __name__ == "__main__":
    main()
