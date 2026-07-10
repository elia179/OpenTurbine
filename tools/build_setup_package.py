#!/usr/bin/env python3
r"""Build OpenTurbine_Recommended.zip for the Windows setup tool.

Run after both firmware environments and their LittleFS images have been built:

    pio run -e esp32dev
    pio run -e esp32dev -t buildfs
    pio run -e esp32s3dev
    pio run -e esp32s3dev -t buildfs
    python tools/build_setup_package.py --esptool C:\path\to\esptool.exe

The output ZIP is intentionally deterministic enough for release checks and is
validated before it is written.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import sys
import tempfile
import zipfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BUILD_ROOTS = [
    ROOT / ".pio3",
    ROOT / ".pio3" / "build",
    ROOT / ".pio" / "build",
]
WEB_ASSETS = [
    "app.js.gz",
    "calibration.html.gz",
    "config.html.gz",
    "hardware.html.gz",
    "index.html.gz",
    "log.html.gz",
    "sequence.html.gz",
    "style.css.gz",
    "tools.html.gz",
    "theme.js.gz",
]
TARGETS = {
    "esp32dev": {
        "chip": "ESP32",
        "bootloader_address": "0x1000",
        "partition_csv": "partitions.csv",
    },
    "esp32s3dev": {
        "chip": "ESP32-S3",
        "bootloader_address": "0x0000",
        "partition_csv": "partitions_16mb.csv",
    },
}
COMMON_FLASH = [
    ("0x8000", "partitions.bin"),
    ("0xe000", "boot_app0.bin"),
    ("0x10000", "firmware.bin"),
]


def read_version() -> str:
    version_h = ROOT / "src" / "system" / "version.h"
    text = version_h.read_text(encoding="utf-8", errors="replace")
    match = re.search(r'#define\s+OT_VERSION\s+"([^"]+)"', text)
    if not match:
        raise RuntimeError(f"Could not find OT_VERSION in {version_h}")
    return match.group(1)


def find_boot_app0() -> Path | None:
    candidates = [
        ROOT / ".pio3" / "packages" / "framework-arduinoespressif32" / "tools" / "partitions" / "boot_app0.bin",
        ROOT / ".pio" / "packages" / "framework-arduinoespressif32" / "tools" / "partitions" / "boot_app0.bin",
        Path.home() / ".platformio" / "packages" / "framework-arduinoespressif32" / "tools" / "partitions" / "boot_app0.bin",
    ]
    for candidate in candidates:
        if candidate.exists():
            return candidate
    return None


def find_env_build_dir(env: str) -> Path:
    for build_root in BUILD_ROOTS:
        candidate = build_root / env
        if candidate.exists():
            return candidate
    return BUILD_ROOTS[0] / env


def find_esptool(provided: str | None) -> Path | None:
    if provided:
        candidate = Path(provided).expanduser()
        if candidate.exists() and candidate.name.lower() == "esptool.exe":
            return candidate
        return None
    for base in (ROOT / ".pio3", ROOT / ".pio"):
        for candidate in base.glob("packages/**/esptool.exe"):
            if candidate.is_file():
                return candidate
    return None


def partition_offset(csv_name: str, partition_name: str) -> str:
    path = ROOT / csv_name
    for raw_line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = raw_line.split("#", 1)[0].strip()
        if not line:
            continue
        parts = [p.strip() for p in line.split(",")]
        if len(parts) >= 4 and parts[0] == partition_name:
            return parts[3]
    raise RuntimeError(f"Could not find partition {partition_name!r} in {path}")


def copy_required(src: Path, dst: Path, missing: list[str]) -> None:
    if src.exists():
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)
    else:
        missing.append(str(src.relative_to(ROOT) if src.is_relative_to(ROOT) else src))


def maybe_copy_driver(src: str | None, dst: Path) -> None:
    if not src:
        return
    path = Path(src).expanduser()
    if not path.exists():
        raise RuntimeError(f"Driver installer not found: {path}")
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(path, dst)


def stage_package(stage: Path, esptool: Path, cp210x: str | None, ch340: str | None) -> dict:
    missing: list[str] = []
    (stage / "tools").mkdir(parents=True, exist_ok=True)
    copy_required(esptool, stage / "tools" / "esptool.exe", missing)
    maybe_copy_driver(cp210x, stage / "drivers" / "cp210x" / "CP210xVCPInstaller_x64.exe")
    maybe_copy_driver(ch340, stage / "drivers" / "ch340" / "CH341SER.EXE")

    boot_app0 = find_boot_app0()
    if boot_app0 is None:
        missing.append(".pio3/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin")

    manifest = {
        "project": "OpenTurbine",
        "version": read_version(),
        "recommended": True,
        "targets": {},
    }

    for env, meta in TARGETS.items():
        env_stage = stage / env
        env_build = find_env_build_dir(env)
        copy_required(env_build / "bootloader.bin", env_stage / "bootloader.bin", missing)
        copy_required(env_build / "partitions.bin", env_stage / "partitions.bin", missing)
        copy_required(env_build / "firmware.bin", env_stage / "firmware.bin", missing)
        copy_required(env_build / "littlefs.bin", env_stage / "littlefs.bin", missing)
        if boot_app0 is not None:
            copy_required(boot_app0, env_stage / "boot_app0.bin", missing)

        web_stage = env_stage / "web_assets"
        for name in WEB_ASSETS:
            copy_required(ROOT / "data" / name, web_stage / name, missing)

        littlefs_address = partition_offset(meta["partition_csv"], "littlefs")
        usb_flash = [{"address": meta["bootloader_address"], "file": "bootloader.bin"}]
        usb_flash.extend({"address": address, "file": filename} for address, filename in COMMON_FLASH)
        usb_flash.append({"address": littlefs_address, "file": "littlefs.bin"})
        manifest["targets"][env] = {
            "chip": meta["chip"],
            "firmware_ota": "firmware.bin",
            "web_assets": "web_assets",
            "usb_flash": usb_flash,
        }

    if missing:
        raise RuntimeError(
            "Cannot build setup package; missing required files:\n  - "
            + "\n  - ".join(missing)
            + "\n\nBuild both PlatformIO environments and pass --esptool C:\\path\\to\\esptool.exe."
        )

    (stage / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return manifest


def write_zip(stage: Path, output: Path) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        for path in sorted(stage.rglob("*")):
            if path.is_file():
                zf.write(path, path.relative_to(stage).as_posix())


def write_sha256(output: Path) -> Path:
    digest = hashlib.sha256(output.read_bytes()).hexdigest()
    sha_path = output.with_suffix(output.suffix + ".sha256")
    sha_path.write_text(f"{digest}  {output.name}\n", encoding="ascii")
    return sha_path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--esptool", help="Path to the Windows esptool.exe to bundle.")
    parser.add_argument("--cp210x-driver", help="Optional Silicon Labs CP210x driver installer.")
    parser.add_argument("--ch340-driver", help="Optional WCH CH340 driver installer.")
    parser.add_argument(
        "--output",
        default=str(ROOT / "dist" / "setup_tool" / "OpenTurbine_Recommended.zip"),
        help="Output ZIP path.",
    )
    args = parser.parse_args()

    esptool = find_esptool(args.esptool)
    if esptool is None:
        print("error: esptool.exe was not found; pass --esptool C:\\path\\to\\esptool.exe", file=sys.stderr)
        return 2

    output = Path(args.output).resolve()
    with tempfile.TemporaryDirectory(prefix="openturbine_setup_") as tmp:
        stage = Path(tmp)
        manifest = stage_package(stage, esptool, args.cp210x_driver, args.ch340_driver)
        write_zip(stage, output)
    sha_path = write_sha256(output)

    print(f"wrote {output}")
    print(f"wrote {sha_path}")
    print(f"version {manifest['version']} with targets: {', '.join(manifest['targets'])}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
