"""Exercise bounded GrokBot migration/update/restore plans without hardware."""
import contextlib
import hashlib
import importlib.util
import io
import itertools
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest import mock

import install_grokbot as installer


def partition_table(layout):
    entries = b"".join(struct.pack("<HBBII16sI", 0x50aa, kind, subtype, start, size,
                                   name.encode("ascii"), 0)
                       for name, (kind, subtype, start, size) in layout.items())
    checksum = b"\xeb\xeb" + b"\xff" * 14 + hashlib.md5(entries).digest()
    return (entries + checksum).ljust(0x1000, b"\xff")


def app_image(project, version):
    data = bytearray(512)
    data[0] = 0xe9
    struct.pack_into("<I", data, 32, 0xabcd5432)
    data[48:48 + len(version)] = version.encode()
    data[80:80 + len(project)] = project.encode()
    return bytes(data)


class GrokBotInstallerTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.firmware = self.root / "firmware"
        self.desktop = self.root / "desktop"
        self.firmware.mkdir()
        self.desktop.mkdir()
        table = partition_table(installer.LAYOUT)
        (self.firmware / "partition-table.bin").write_bytes(table)
        (self.desktop / "yiyi_desk.ino.partitions.bin").write_bytes(table)
        (self.firmware / "xiaozhi.bin").write_bytes(app_image("xiaozhi", "2.5.3"))
        (self.desktop / "yiyi_desk.ino.bin").write_bytes(app_image("arduino-lib-builder", "esp-idf"))
        (self.firmware / "sdkconfig").write_text("\n".join([
            "CONFIG_BOARD_TYPE_YIYI_ES3C28P_GROKBOT=y",
            "CONFIG_XIAOZHI_DISABLE_FIRMWARE_UPGRADE=y",
            "CONFIG_XIAOZHI_DISABLE_ASSET_DOWNLOAD=y",
            "CONFIG_XIAOZHI_PRESERVE_SHARED_NVS=y",
        ]), encoding="utf-8")
        spec = importlib.util.spec_from_file_location(
            "installer_test_assets", installer.ROOT /
            "xiaozhi-esp32-2.5.3-dev-grokbot/scripts/build_default_assets.py")
        assets = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(assets)
        self.pack = assets.pack_assets_entries({
            "grokbot.atlas": (struct.pack("<4sHHHBBI", b"GBZ1", 1, 200, 200, 20, 28, 0), 0, 0),
            "index.json": (json.dumps({"srmodels": "srmodels.bin"}).encode(), 0, 0),
            "srmodels.bin": (b"test speech models", 0, 0),
        })
        (self.firmware / "generated_assets.bin").write_bytes(self.pack)
        self.backup = self.root / "full-16mb.bin"
        self.save_backup(installer.LEGACY_LAYOUT)

    def save_backup(self, layout):
        data = bytearray(b"\xff" * 0x1000000)
        data[0x8000:0x9000] = partition_table(layout)
        for number, name in enumerate(("app0", "app1"), 1):
            address, size = layout[name][2:]
            data[address:address + size] = bytes([number]) * size
        # Include old font/resource bytes absent from the rebuilt resource pack.
        address, size = installer.LAYOUT["assets"][2:]
        self.resource_region = (self.pack + b"preserved old font").ljust(size, b"\xff")
        data[address:address + size] = self.resource_region
        data[0xe000:0x10000] = b"\x33" * 0x2000
        self.backup_bytes = bytes(data)
        self.backup.write_bytes(self.backup_bytes)
        self.backup.with_suffix(".sha256").write_text(hashlib.sha256(data).hexdigest(), encoding="ascii")

    def cli(self, *options, live=None, resources=None, filesystem=None, flash=True):
        calls = []
        self.tool_calls = calls

        def fake_esptool(port, *arguments):
            calls.append(arguments)
            if "read_flash" in arguments:
                index = arguments.index("read_flash")
                address, size = int(arguments[index + 1], 0), int(arguments[index + 2], 0)
                target = Path(arguments[index + 3])
                if address == 0x8000:
                    target.write_bytes(partition_table(live or installer.LEGACY_LAYOUT))
                elif address == installer.LAYOUT["assets"][2]:
                    self.assertEqual((address, size), installer.LAYOUT["assets"][2:])
                    target.write_bytes(self.resource_region if resources is None else resources)
                else:
                    self.assertEqual((address, size), installer.LAYOUT["spiffs"][2:])
                    target.write_bytes(self.backup_bytes[address:address + size] if filesystem is None else filesystem)

        argv = ["--backup", str(self.backup), "--firmware", str(self.firmware),
                "--desktop", str(self.desktop), *options]
        if flash:
            argv.append("--flash")
        with mock.patch.object(installer, "esptool", side_effect=fake_esptool), contextlib.redirect_stdout(io.StringIO()):
            installer.main(argv)
        return calls

    def test_target_bundle_is_bounded_at_new_addresses(self):
        writes, info = installer.validate_bundle(self.firmware, self.desktop)
        self.assertEqual([address for address, _ in writes], [0x8000, 0x10000, 0x890000, 0xc90000])
        self.assertEqual(info["states"], 28)
        self.assertEqual(info["desktop_project"], "arduino-lib-builder")

    def test_desktop_table_mismatch_is_rejected(self):
        (self.desktop / "yiyi_desk.ino.partitions.bin").write_bytes(partition_table(installer.LEGACY_LAYOUT))
        with self.assertRaisesRegex(ValueError, "tables differ"):
            installer.validate_bundle(self.firmware, self.desktop)

    def test_desktop_image_header_is_validated(self):
        (self.desktop / "yiyi_desk.ino.bin").write_bytes(b"not firmware")
        with self.assertRaisesRegex(ValueError, "Invalid app0"):
            installer.validate_bundle(self.firmware, self.desktop)

    def test_app1_image_over_four_mib_is_rejected(self):
        path = self.firmware / "xiaozhi.bin"
        path.write_bytes(path.read_bytes().ljust(0x400001, b"\0"))
        with self.assertRaisesRegex(ValueError, "exceeds"):
            installer.validate_bundle(self.firmware, self.desktop)

    def test_desktop_image_over_eight_and_half_mib_is_rejected(self):
        path = self.desktop / "yiyi_desk.ino.bin"
        path.write_bytes(path.read_bytes().ljust(0x880001, b"\0"))
        with self.assertRaisesRegex(ValueError, "exceeds"):
            installer.validate_bundle(self.firmware, self.desktop)

    def test_backup_accepts_legacy_model_name_and_target_layout(self):
        for layout in (dict(installer.LEGACY_LAYOUT), dict(installer.LAYOUT)):
            layout["model"] = layout.pop("assets")
            with self.subTest(app1=layout["app1"]):
                self.save_backup(layout)
                _, actual = installer.check_backup(self.backup)
                self.assertEqual(actual, layout)

    def test_backup_checksum_failure_is_rejected(self):
        self.backup.with_suffix(".sha256").write_text("0" * 64, encoding="ascii")
        with self.assertRaisesRegex(ValueError, "SHA256"):
            installer.check_backup(self.backup)

    def test_restore_slices_backup_layout_in_both_directions(self):
        for layout in (installer.LEGACY_LAYOUT, installer.LAYOUT):
            with self.subTest(app1=layout["app1"]):
                self.save_backup(layout)
                calls = self.cli("--restore", live=installer.LAYOUT)
                write = next(call for call in calls if "write_flash" in call)
                index = write.index("16MB") + 1
                writes = [(int(write[i], 0), Path(write[i + 1])) for i in range(index, len(write), 2)]
                self.assertEqual([address for address, _ in writes],
                                 [0x8000, 0xe000, layout["app0"][2], layout["app1"][2], 0xc90000])
                for address, path in writes:
                    self.assertEqual(path.read_bytes(), self.backup_bytes[address:address + path.stat().st_size])
                self.assertEqual((self.root / "installer-staging/restore-images/app1.bin").stat().st_size, layout["app1"][3])
                self.assertTrue(any("verify_flash" in call for call in calls))

    def test_migration_preserves_resources_and_starts_desktop(self):
        calls = self.cli("--migrate-layout")
        write = next(call for call in calls if "write_flash" in call)
        index = write.index("16MB") + 1
        addresses = [int(value, 0) for value in write[index::2]]
        self.assertEqual(addresses, [0x8000, 0xe000, 0x10000, 0x890000])
        self.assertEqual((self.root / "installer-staging/migration-desktop-otadata.bin").read_bytes(), b"\xff" * 0x2000)
        self.assertEqual(sum("read_flash" in call for call in calls), 3)
        self.assertTrue(all("--no-stub" in call for call in calls if "read_flash" in call))
        self.assertEqual(sum("verify_flash" in call for call in calls), 1)
        plan = json.loads((self.root / "migrate-layout-plan.json").read_text())
        self.assertIn("assets", plan["preserved_partitions"])

    def test_stub_read_only_changes_preflight_reads(self):
        calls = self.cli("--migrate-layout", "--stub-read")
        reads = [call for call in calls if "read_flash" in call]
        writes = [call for call in calls if "write_flash" in call or "verify_flash" in call]
        self.assertEqual(len(reads), 3)
        self.assertTrue(all("--no-stub" not in call for call in reads))
        self.assertEqual(len(writes), 2)
        self.assertTrue(all("--no-stub" in call for call in writes))

    def test_stale_backup_resources_block_migration_before_write(self):
        changed = b"X" + self.resource_region[1:]
        with self.assertRaisesRegex(ValueError, "Live assets differ"):
            self.cli("--migrate-layout", resources=changed)
        self.assertFalse(any("write_flash" in call for call in self.tool_calls))
        self.assertFalse((self.root / "install-plan.json").exists())

    def test_stale_backup_filesystem_blocks_migration_before_write(self):
        with self.assertRaisesRegex(ValueError, "Live spiffs differ"):
            self.cli("--migrate-layout", filesystem=b"changed filesystem")
        self.assertFalse(any("write_flash" in call for call in self.tool_calls))

    def test_migration_requires_legacy_backup_and_matching_live_layout(self):
        with self.assertRaisesRegex(ValueError, "source layout differs"):
            self.cli("--migrate-layout", live=installer.LAYOUT)
        self.save_backup(installer.LAYOUT)
        with self.assertRaisesRegex(ValueError, "source 7.5/5"):
            self.cli("--migrate-layout", flash=False)

    def test_full_install_on_legacy_requires_migration_mode(self):
        with self.assertRaisesRegex(ValueError, "requires --migrate-layout"):
            self.cli()

    def test_single_application_updates_require_target_and_write_one_image(self):
        for flag, address in (("--app-only", 0x890000), ("--desktop-only", 0x10000)):
            with self.subTest(flag=flag):
                with self.assertRaisesRegex(ValueError, "installed 8.5/4"):
                    self.cli(flag)
                calls = self.cli(flag, live=installer.LAYOUT)
                write = next(call for call in calls if "write_flash" in call)
                index = write.index("16MB") + 1
                self.assertEqual([int(value, 0) for value in write[index::2]], [address])
                self.assertTrue(any("verify_flash" in call for call in calls))

    def test_modes_are_mutually_exclusive(self):
        flags = ["--restore", "--app-only", "--desktop-only", "--migrate-layout"]
        for pair in itertools.combinations(flags, 2):
            with self.subTest(pair=pair), contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as raised:
                    installer.main(list(pair))
                self.assertEqual(raised.exception.code, 2)

    def test_dry_run_performs_no_device_calls(self):
        calls = self.cli("--migrate-layout", flash=False)
        self.assertEqual(calls, [])


if __name__ == "__main__":
    unittest.main(verbosity=2)
