#!/usr/bin/env python3
"""Fail release builds that outgrow OTA or filesystem safety margins."""

from __future__ import annotations

import argparse
import csv
from pathlib import Path


def number(value: str) -> int:
    return int(value.strip(), 0)


def partitions(path: Path) -> dict[str, tuple[int, int]]:
    result: dict[str, tuple[int, int]] = {}
    with path.open(encoding="utf-8-sig", errors="strict", newline="") as source:
        for row in csv.reader(line for line in source if not line.lstrip().startswith("#")):
            if not row or not row[0].strip():
                continue
            if len(row) < 5:
                raise ValueError(f"invalid partition row in {path}: {row}")
            result[row[0].strip()] = (number(row[3]), number(row[4]))
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--partitions", required=True, type=Path)
    parser.add_argument("--firmware", required=True, type=Path)
    parser.add_argument("--filesystem", type=Path)
    parser.add_argument("--app-reserve", type=number, default=0x10000)
    parser.add_argument("--minimum-filesystem", type=number, default=0xC0000)
    args = parser.parse_args()

    table = partitions(args.partitions)
    required = {"app0", "app1", "littlefs"}
    missing = required - table.keys()
    if missing:
        raise SystemExit(f"missing partitions: {', '.join(sorted(missing))}")

    app0 = table["app0"]
    app1 = table["app1"]
    littlefs = table["littlefs"]
    if app0[1] != app1[1]:
        raise SystemExit("OTA app slots must have equal size")
    if app0[0] % 0x10000 or app1[0] % 0x10000:
        raise SystemExit("OTA app offsets must be 64 KiB aligned")
    if app0[0] + app0[1] > app1[0] or app1[0] + app1[1] > littlefs[0]:
        raise SystemExit("app or filesystem partitions overlap")
    if littlefs[1] < args.minimum_filesystem:
        raise SystemExit(
            f"LittleFS is {littlefs[1]} bytes; minimum is {args.minimum_filesystem}"
        )

    firmware_size = args.firmware.stat().st_size
    headroom = app0[1] - firmware_size
    if headroom < args.app_reserve:
        raise SystemExit(
            f"firmware {firmware_size} leaves only {headroom} bytes in the OTA slot; "
            f"required reserve is {args.app_reserve}"
        )
    if args.filesystem:
        filesystem_size = args.filesystem.stat().st_size
        if filesystem_size > littlefs[1]:
            raise SystemExit(
                f"filesystem image {filesystem_size} exceeds partition {littlefs[1]}"
            )

    print(
        f"OK: firmware={firmware_size}, OTA slot={app0[1]}, headroom={headroom}, "
        f"LittleFS={littlefs[1]}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
