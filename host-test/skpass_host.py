#!/usr/bin/env python3
"""SKPass HID monitor.

Reads the SKPass input reports emitted by SKPass and displays both the
decoded card number and the raw HID bytes. Requires the ``hidapi`` package
(imported as ``hid``).
"""

from __future__ import annotations

import argparse
import csv
import datetime as dt
import queue
import sys
import threading
import time
from pathlib import Path
from typing import Optional

try:
    import hid  # type: ignore
except ImportError:
    print("缺少依赖：请先执行 python -m pip install -r requirements.txt", file=sys.stderr)
    raise SystemExit(2)


VID = 0x1CCF
PID = 0x5252
REPORT_LENGTH = 9
VALID_REPORT_IDS = {1, 2}


def list_devices() -> list[dict]:
    return [d for d in hid.enumerate(VID, PID) if d.get("usage_page") == 0xFFCA]


def format_device(device: dict, index: int) -> str:
    serial = device.get("serial_number") or "(无序列号)"
    product = device.get("product_string") or "(未知产品)"
    path = device.get("path", b"")
    if isinstance(path, bytes):
        path = path.decode(errors="replace")
    return f"[{index}] {product}, serial={serial}, path={path}"


def decode_report(data: list[int]) -> tuple[int, bytes]:
    """Normalize HIDAPI data, handling both report-ID return conventions."""
    if len(data) >= REPORT_LENGTH and data[0] in VALID_REPORT_IDS:
        return data[0], bytes(data[1:9])
    if len(data) >= 8 and data[0] in VALID_REPORT_IDS:
        return data[0], bytes(data[1:8]).ljust(8, b"\x00")
    raise ValueError(f"长度或 Report ID 无效: {len(data)} bytes, {data[:3]}")


def write_row(writer: Optional[csv.writer], timestamp: str, report_id: int,
              card_id: bytes, raw: bytes) -> None:
    if writer is not None:
        writer.writerow([timestamp, report_id, card_id.hex(" ").upper(), raw.hex(" ").upper()])


def monitor(device_index: int, csv_path: Optional[Path], timeout_ms: int) -> int:
    devices = list_devices()
    if not devices:
        print(f"未找到 SKPass HID 设备 (VID=0x{VID:04X}, PID=0x{PID:04X})", file=sys.stderr)
        return 1
    if device_index < 0 or device_index >= len(devices):
        print(f"设备索引无效，应为 0..{len(devices) - 1}", file=sys.stderr)
        return 2

    device_info = devices[device_index]
    path = device_info["path"]
    output = None
    writer = None
    if csv_path:
        output = csv_path.open("a", newline="", encoding="utf-8")
        writer = csv.writer(output)
        if csv_path.stat().st_size == 0:
            writer.writerow(["timestamp", "report_id", "card_id", "raw_report"])

    dev = hid.device()
    try:
        dev.open_path(path)
        dev.set_nonblocking(False)
        print(format_device(device_info, device_index))
        print("正在监听 SKPass 报文，按 Ctrl+C 退出。")
        while True:
            data = dev.read(REPORT_LENGTH, timeout_ms)
            if not data:
                continue
            raw = bytes(data)
            try:
                report_id, card_id = decode_report(list(data))
            except ValueError as exc:
                now = dt.datetime.now().isoformat(timespec="milliseconds")
                print(f"{now} DEBUG 无法解析: {exc}; RAW={raw.hex(' ').upper()}")
                continue
            now = dt.datetime.now().isoformat(timespec="milliseconds")
            print(f"{now} CARD report_id={report_id} UID={card_id.hex(' ').upper()} "
                  f"RAW={raw.hex(' ').upper()}", flush=True)
            write_row(writer, now, report_id, card_id, raw)
            if output:
                output.flush()
    except KeyboardInterrupt:
        print("\n已停止。")
    finally:
        dev.close()
        if output:
            output.close()
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="SKPass HID 卡号调试上位机")
    parser.add_argument("--list", action="store_true", help="列出当前 SKPass 设备")
    parser.add_argument("--device", type=int, default=0, help="设备索引，默认 0")
    parser.add_argument("--csv", type=Path, help="将时间、类型、卡号和原始报文追加保存到 CSV")
    parser.add_argument("--timeout", type=int, default=1000, help="HID 读取超时毫秒数，默认 1000")
    args = parser.parse_args()

    if args.list:
        devices = list_devices()
        if not devices:
            print("未找到 SKPass HID 设备")
            return 1
        for index, device in enumerate(devices):
            print(format_device(device, index))
        return 0
    return monitor(args.device, args.csv, max(1, args.timeout))


if __name__ == "__main__":
    raise SystemExit(main())
