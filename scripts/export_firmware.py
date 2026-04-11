from pathlib import Path
import json
import re
import shutil

Import("env")

PROJECT_NAME = "Squeezie"
CHIP_FAMILY = "ESP32-S3"


def read_app_version():
    config_path = Path(env["PROJECT_DIR"]) / "include" / "config.h"
    text = config_path.read_text(encoding="utf-8")
    match = re.search(r'#define\s+APP_VERSION\s+"([^"]+)"', text)
    if not match:
        raise ValueError("APP_VERSION konnte in include/config.h nicht gefunden werden")
    return match.group(1)


def ensure_dir(path: Path):
    path.mkdir(parents=True, exist_ok=True)


def copy_file(src: Path, dst: Path):
    ensure_dir(dst.parent)
    shutil.copy2(src, dst)


def build_manifest(version: str):
    return {
        "name": PROJECT_NAME,
        "version": version,
        "new_install_prompt_erase": True,
        "builds": [
            {
                "chipFamily": CHIP_FAMILY,
                "parts": [
                    {"path": "bootloader.bin", "offset": 0},
                    {"path": "partitions.bin", "offset": 0x8000},
                    {"path": "boot_app0.bin", "offset": 0xE000},
                    {"path": "firmware.bin", "offset": 0x10000},
                ],
            }
        ],
    }


def export_firmware(source, target, env):
    project_dir = Path(env["PROJECT_DIR"])
    version = read_app_version()
    firmware_bin = Path(source[0].get_abspath())
    build_dir = firmware_bin.parent

    framework_dir = Path(env.PioPlatform().get_package_dir("framework-arduinoespressif32"))
    boot_app0 = framework_dir / "tools" / "partitions" / "boot_app0.bin"

    files = {
        "firmware.bin": firmware_bin,
        "bootloader.bin": build_dir / "bootloader.bin",
        "partitions.bin": build_dir / "partitions.bin",
        "boot_app0.bin": boot_app0,
    }

    missing = [name for name, path in files.items() if not path.exists()]
    if missing:
        print("WARN: Firmware-Export uebersprungen, fehlende Dateien: " + ", ".join(missing))
        return

    version_dir = project_dir / "Firmware" / version
    latest_dir = project_dir / "Firmware" / "latest"
    ensure_dir(version_dir)
    ensure_dir(latest_dir)

    for name, src in files.items():
        copy_file(src, version_dir / name)
        copy_file(src, latest_dir / name)

    manifest = build_manifest(version)
    for dst in (version_dir / "manifest.json", latest_dir / "manifest.json"):
        dst.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")

    print(f"Exportierte Firmware nach {version_dir}")


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", export_firmware)
