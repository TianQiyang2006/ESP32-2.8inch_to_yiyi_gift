import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from scripts.build_grokbot_subset import GLOBAL_HEADER, XIAOZHI_STATES


ROOT = Path(__file__).resolve().parents[2]


def make_atlas(path: Path, names: tuple[str, ...]) -> None:
    clip_payloads = [b"\x5a" * 64 for _ in names]
    offset = GLOBAL_HEADER.size + len(names) * 32
    headers = []
    for name, payload in zip(names, clip_payloads, strict=True):
        headers.append(
            struct.pack(
                "<16sHHIII",
                name.encode("ascii"),
                3,
                2,
                offset,
                len(payload),
                400 * 400 // 2,
            )
        )
        offset += len(payload)
    path.write_bytes(
        GLOBAL_HEADER.pack(b"GBZ1", 1, 400, 400, 30, len(names), 0)
        + b"".join(headers)
        + b"".join(clip_payloads)
    )


class BuildGrokbotSubsetTest(unittest.TestCase):
    def test_output_contains_only_xiaozhi_states(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "full.atlas"
            output = root / "subset.atlas"
            make_atlas(source, XIAOZHI_STATES + ("unused-showcase",))

            subprocess.run(
                [
                    sys.executable,
                    str(ROOT / "scripts" / "build_grokbot_subset.py"),
                    str(source),
                    str(output),
                    "--max-size",
                    "0x10000",
                ],
                check=True,
            )

            data = output.read_bytes()
            magic, version, width, height, fps, count, _ = GLOBAL_HEADER.unpack_from(data)
            self.assertEqual((magic, version, width, height, fps), (b"GBZ1", 1, 400, 400, 30))
            self.assertEqual(count, len(XIAOZHI_STATES))
            names = []
            for index in range(count):
                header = data[GLOBAL_HEADER.size + index * 32 : GLOBAL_HEADER.size + (index + 1) * 32]
                names.append(header[:16].split(b"\0", 1)[0].decode("ascii"))
            self.assertEqual(tuple(names), XIAOZHI_STATES)

    def test_missing_state_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "full.atlas"
            make_atlas(source, XIAOZHI_STATES[:-1])
            result = subprocess.run(
                [
                    sys.executable,
                    str(ROOT / "scripts" / "build_grokbot_subset.py"),
                    str(source),
                    str(root / "subset.atlas"),
                ],
                capture_output=True,
                text=True,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("missing states", result.stderr)


if __name__ == "__main__":
    unittest.main()
