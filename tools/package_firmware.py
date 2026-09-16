#!/usr/bin/env python3
"""Package the two built ESP32-S3 OLED environments as flash-at-0x0 images.

Requires esptool==4.9.0 in this Python environment and both PlatformIO builds.
Run from a clean Git checkout. No device is accessed.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
VARIANTS = {
    "esp32-s3": "radio_power_esp32s3_oled_web.bin",
    "esp32-s3-no-web": "radio_power_esp32s3_oled_no_web.bin",
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True)
    parser.add_argument("--output-dir", type=Path, default=ROOT / "dist")
    args = parser.parse_args()
    if subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT).strip():
        parser.error("Commit source changes before packaging a release")
    source_sha = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    core = Path(os.environ.get("PLATFORMIO_CORE_DIR", str(Path.home() / ".platformio")))
    boot_app = core / "packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"
    out = args.output_dir.resolve()
    out.mkdir(parents=True, exist_ok=True)
    manifest = {"version": args.version, "source_commit": source_sha,
                "chip": "esp32s3", "board": "01Space ESP32-S3-0.42OLED",
                "flash_size_bytes": 4194304, "flash_address": "0x0", "images": []}
    assets = []
    for env, name in VARIANTS.items():
        build = ROOT / ".pio/build" / env
        parts = [(0x0, build / "bootloader.bin"), (0x8000, build / "partitions.bin"),
                 (0xE000, boot_app), (0x10000, build / "firmware.bin")]
        for _, part in parts:
            if not part.is_file():
                parser.error(f"Missing {part}; run pio run -e {env}")
        target = out / name
        command = [sys.executable, "-m", "esptool", "--chip", "esp32s3",
                   "merge_bin", "-o", str(target)]
        for address, part in parts:
            command.extend([hex(address), str(part)])
        subprocess.run(command, check=True)
        image = target.read_bytes()
        if len(image) > 4194304:
            raise ValueError("Merged firmware exceeds 4 MB flash")
        for address, part in parts:
            data = part.read_bytes()
            if image[address:address + len(data)] != data:
                raise ValueError(f"Merged bytes differ from {part}")
        manifest["images"].append({"file": name, "environment": env, "oled": True,
                                  "web": env == "esp32-s3", "size": len(image),
                                  "sha256": hashlib.sha256(image).hexdigest(),
                                  "parts": [{"offset": hex(addr), "file": p.name,
                                             "sha256": hashlib.sha256(p.read_bytes()).hexdigest()}
                                            for addr, p in parts]})
        assets.append(target)
    info = out / "manifest.json"
    info.write_text(json.dumps(manifest, indent=2) + "\n")
    assets.append(info)
    readme = out / "README.md"
    shutil.copyfile(ROOT / "README.md", readme)
    assets.append(readme)
    (out / "SHA256SUMS").write_text("".join(
        f"{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}\n" for p in assets))
    print(f"Release {args.version}: verified merged images in {out}")


if __name__ == "__main__":
    main()
