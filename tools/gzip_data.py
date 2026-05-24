#!/usr/bin/env python3
"""
Compress web assets from data_src/ into data/ as .gz files.
Run this after editing any HTML/JS/CSS file, then do: pio run -t uploadfs
"""
import gzip, shutil, os

SRC = os.path.join(os.path.dirname(__file__), "..", "data_src")
DST = os.path.join(os.path.dirname(__file__), "..", "data")

EXTS = {".html", ".js", ".css"}

for fname in os.listdir(SRC):
    if os.path.splitext(fname)[1] not in EXTS:
        continue
    src_path = os.path.join(SRC, fname)
    dst_path = os.path.join(DST, fname + ".gz")
    with open(src_path, "rb") as f_in, gzip.open(dst_path, "wb", compresslevel=9) as f_out:
        shutil.copyfileobj(f_in, f_out)
    src_kb = os.path.getsize(src_path) / 1024
    dst_kb = os.path.getsize(dst_path) / 1024
    print(f"  {fname}: {src_kb:.0f}KB -> {dst_kb:.0f}KB gz")

print("Done. Run: pio run -t uploadfs -e esp32dev")
