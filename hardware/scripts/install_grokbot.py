"""Validate/install the ES3C28P GrokBot dual-application bundle, or restore it.

Requires a complete SHA256-checked 16 MiB backup. Default operation only
validates files; --flash performs the reviewed, bounded device writes.
"""
import argparse
import hashlib
import importlib.util
import json
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LAYOUT = {
    "nvs": (1, 2, 0x9000, 0x5000),
    "otadata": (1, 0, 0xe000, 0x2000),
    "app0": (0, 0x10, 0x10000, 0x880000),
    "app1": (0, 0x11, 0x890000, 0x400000),
    "assets": (1, 0x82, 0xc90000, 0x280000),
    "spiffs": (1, 0x82, 0xf10000, 0xf0000),
}
LEGACY_LAYOUT = dict(LAYOUT, app0=(0, 0x10, 0x10000, 0x780000),
                     app1=(0, 0x11, 0x790000, 0x500000))


def normalized_layout(layout):
    result = {}
    for name, entry in layout.items():
        name = "assets" if name == "model" else name
        if name in result:
            raise ValueError("Duplicate normalized partition name")
        result[name] = entry
    return result


def partitions(data):
    entries = {}
    for offset in range(0, len(data), 32):
        entry = data[offset:offset + 32]
        if entry[:2] == b"\xeb\xeb":
            if hashlib.md5(data[:offset]).digest() != entry[16:32]:
                raise ValueError("Partition-table MD5 mismatch")
            return entries
        if len(entry) != 32 or entry[:2] != b"\xaaP":
            break
        _, kind, subtype, start, size, name, flags = struct.unpack("<HBBII16sI", entry)
        name = name.split(b"\0", 1)[0].decode("ascii")
        if name in entries or flags or not size or start + size > 0x1000000:
            raise ValueError("Invalid partition entry")
        if any(start < other[2] + other[3] and other[2] < start + size
               for other in entries.values()):
            raise ValueError("Overlapping partition entries")
        entries[name] = (kind, subtype, start, size)
    raise ValueError("Missing partition-table MD5 entry")


def check_backup(path):
    data = path.read_bytes()
    if len(data) != 0x1000000:
        raise ValueError("Backup must be exactly 16 MiB")
    expected = path.with_suffix(".sha256").read_text(encoding="ascii").strip()
    if hashlib.sha256(data).hexdigest() != expected:
        raise ValueError("Backup SHA256 mismatch")
    old = partitions(data[0x8000:0x9000])
    if normalized_layout(old) not in (LEGACY_LAYOUT, LAYOUT):
        raise ValueError("Backup must use the known 7.5/5 or 8.5/4 MiB layout")
    return data, old


def app_descriptor(path, name):
    data = path.read_bytes()
    if len(data) < 256 or data[0] != 0xe9 or data[32:36] != struct.pack("<I", 0xabcd5432):
        raise ValueError(f"Invalid {name} image header/application descriptor")
    version = data[48:80].split(b"\0", 1)[0].decode("utf-8")
    project = data[80:112].split(b"\0", 1)[0].decode("utf-8")
    if not version or not project:
        raise ValueError(f"Missing {name} application version/project")
    return project, version


def validate_bundle(firmware, desktop):
    table = firmware / "partition-table.bin"
    target = partitions(table.read_bytes())
    if normalized_layout(target) != LAYOUT:
        raise ValueError("Bundle must use the target 8.5/4 MiB partition layout")
    desktop_table = desktop / "yiyi_desk.ino.partitions.bin"
    if partitions(desktop_table.read_bytes()) != target:
        raise ValueError("Desktop and GrokBot partition tables differ")
    app = firmware / "xiaozhi.bin"
    desktop_image = desktop / "yiyi_desk.ino.bin"
    writes = [(0x8000, table), (LAYOUT["app0"][2], desktop_image),
              (LAYOUT["app1"][2], app), (LAYOUT["assets"][2], firmware / "generated_assets.bin")]
    for address, image in writes:
        limit = 0x1000 if address == 0x8000 else next(v[3] for v in LAYOUT.values() if v[2] == address)
        if not 0 < image.stat().st_size <= limit:
            raise ValueError(f"Image exceeds its assigned partition: {image}")
    project, version = app_descriptor(app, "app1")
    if (project, version) != ("xiaozhi", "2.5.3"):
        raise ValueError(f"Unexpected app1 descriptor: {project} {version}")
    desktop_project, desktop_version = app_descriptor(desktop_image, "app0")
    config = (firmware / "sdkconfig").read_text(encoding="utf-8")
    for setting in ("CONFIG_BOARD_TYPE_YIYI_ES3C28P_GROKBOT=y",
                    "CONFIG_XIAOZHI_DISABLE_FIRMWARE_UPGRADE=y",
                    "CONFIG_XIAOZHI_DISABLE_ASSET_DOWNLOAD=y",
                    "CONFIG_XIAOZHI_PRESERVE_SHARED_NVS=y"):
        if setting not in config.splitlines():
            raise ValueError(f"Required board/layout protection missing: {setting}")
    pack = firmware / "generated_assets.bin"
    source = ROOT / "xiaozhi-esp32-2.5.3-dev-grokbot/scripts/build_default_assets.py"
    spec = importlib.util.spec_from_file_location("grokbot_asset_format", source)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    assets = module.read_assets_entries(pack)
    atlas = assets["grokbot.atlas"][0]
    magic, _, width, height, fps, clips, _ = struct.unpack_from("<4sHHHBBI", atlas)
    if (magic, width, height, clips) != (b"GBZ1", 200, 200, 28):
        raise ValueError("Unexpected GrokBot atlas geometry/state count")
    index = json.loads(assets["index.json"][0])
    if index.get("srmodels") not in assets:
        raise ValueError("Missing indexed speech models")
    if index.get("text_font") is not None and index["text_font"] not in assets:
        raise ValueError("Missing indexed text font")
    return writes, {"version": version, "animation": [width, height], "states": clips,
                    "atlas_fps": fps, "desktop_project": desktop_project,
                    "desktop_descriptor_version": desktop_version}


def esptool(port, *args):
    subprocess.run([sys.executable, "-m", "esptool", "--chip", "esp32s3", "--port", port,
                    "--before", "usb_reset", *map(str, args)], check=True)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM8")
    parser.add_argument("--backup", type=Path, default=ROOT / "build/backups/before-grokbot-20261105/full-16mb.bin")
    parser.add_argument("--firmware", type=Path, default=ROOT / "build/grokbot-firmware")
    parser.add_argument("--desktop", type=Path, default=ROOT / "build/yiyi_desk")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--restore", action="store_true", help="Restore application partitions from the backup's actual layout")
    mode.add_argument("--app-only", action="store_true", help="Update app1 on the installed 8.5/4 MiB layout")
    mode.add_argument("--desktop-only", action="store_true", help="Update app0 on the installed 8.5/4 MiB layout")
    mode.add_argument("--migrate-layout", action="store_true", help="Migrate 7.5/5 to 8.5/4 MiB, preserving live assets and user data")
    parser.add_argument("--stub-read", action="store_true",
                        help="Use the esptool stub for preflight readbacks; writes and verification still use ROM")
    parser.add_argument("--flash", action="store_true")
    args = parser.parse_args(argv)
    backup, old_layout = check_backup(args.backup)
    old_normalized = normalized_layout(old_layout)
    staging = args.backup.parent / "installer-staging"
    staging.mkdir(exist_ok=True)
    if args.migrate_layout and old_normalized != LEGACY_LAYOUT:
        raise ValueError("Layout migration requires a backup of the source 7.5/5 MiB layout")
    if args.restore:
        restored = staging / "restore-images"
        restored.mkdir(exist_ok=True)
        writes = []
        regions = [("partition-table", 0x8000, 0x1000)]
        regions += [(name, *old_normalized[name][2:]) for name in ("otadata", "app0", "app1", "assets")]
        for name, address, size in regions:
            path = restored / (name + ".bin")
            path.write_bytes(backup[address:address + size])
            writes.append((address, path))
        info = {"restore": True, "restored_layout": old_layout}
    else:
        writes, info = validate_bundle(args.firmware, args.desktop)
        if args.app_only:
            writes = [(address, path) for address, path in writes if address == LAYOUT["app1"][2]]
            info["app_only"] = True
        elif args.desktop_only:
            writes = [(address, path) for address, path in writes if address == LAYOUT["app0"][2]]
            info["desktop_only"] = True
        elif args.migrate_layout:
            writes = [(address, path) for address, path in writes if address != LAYOUT["assets"][2]]
            selection = staging / "migration-desktop-otadata.bin"
            selection.write_bytes(b"\xff" * LAYOUT["otadata"][3])
            writes.append((LAYOUT["otadata"][2], selection))
            writes.sort(key=lambda item: item[0])
            info.update(migrate_layout=True, source_layout=old_layout, target_layout=LAYOUT,
                        preserved_partitions=["nvs", "assets", "spiffs", "bootloader"],
                        boot_application="app0")
    info["writes"] = [{"address": hex(address), "bytes": image.stat().st_size,
                       "sha256": hashlib.sha256(image.read_bytes()).hexdigest()}
                      for address, image in writes]
    report = args.backup.parent / ("restore-plan.json" if args.restore else
                                 "install-app-plan.json" if args.app_only else
                                 "install-desktop-plan.json" if args.desktop_only else
                                 "migrate-layout-plan.json" if args.migrate_layout else "install-plan.json")
    if report.resolve() == args.backup.resolve():
        raise ValueError("Backup path conflicts with the installer plan path")
    report.write_text(json.dumps(info, indent=2), encoding="utf-8")
    print(json.dumps(info, indent=2), flush=True)
    if not args.flash:
        return
    read_options = [] if args.stub_read else ["--no-stub"]
    live_table = staging / "partition-table-before-write.bin"
    esptool(args.port, *read_options, "read_flash", "0x8000", "0x1000", live_table)
    live = partitions(live_table.read_bytes())
    live_normalized = normalized_layout(live)
    if live_normalized not in (LEGACY_LAYOUT, LAYOUT):
        raise ValueError("Live partition table differs from the reviewed layout")
    if args.app_only or args.desktop_only:
        if live_normalized != LAYOUT:
            raise ValueError("Single-application updates require the installed 8.5/4 MiB layout")
    elif args.migrate_layout:
        if live != old_layout or live_normalized != LEGACY_LAYOUT:
            raise ValueError("Live source layout differs from the migration backup")
        for name in ("assets", "spiffs"):
            live_region = staging / (name + "-before-migration.bin")
            _, _, address, size = old_normalized[name]
            esptool(args.port, *read_options, "read_flash", hex(address), hex(size), live_region)
            if live_region.read_bytes() != backup[address:address + size]:
                raise ValueError(f"Live {name} differ from the migration backup; take a current backup")
    elif not args.restore and live_normalized != LAYOUT:
        raise ValueError("Changing the installed partition layout requires --migrate-layout")
    flat = [item for address, path in writes for item in (hex(address), str(path))]
    esptool(args.port, "--no-stub", "write_flash", "--flash_size", "16MB", *flat)
    esptool(args.port, "--no-stub", "verify_flash", "--flash_size", "16MB", *flat)
    print("Write and independent verification passed", flush=True)


if __name__ == "__main__":
    main()
