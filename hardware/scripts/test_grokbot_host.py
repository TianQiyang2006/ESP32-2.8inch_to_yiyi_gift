"""Run upstream host checks with this workspace's Windows native toolchain."""
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "xiaozhi-esp32-2.5.3-dev-grokbot"


def main():
    temporary = ROOT / "build/grokbot-host-temp"
    temporary.mkdir(parents=True, exist_ok=True)
    os.environ["TEMP"] = os.environ["TMP"] = str(temporary)
    tempfile.tempdir = str(temporary)
    os.environ["PYTHONUTF8"] = "1"
    os.environ["PYTHONIOENCODING"] = "utf-8"
    zig = ROOT / "tools/host-python/ziglang/zig.exe"
    if zig.exists():
        os.environ["CXX"] = f"{zig.as_posix()} c++ -target x86_64-windows-gnu"
        os.environ["CC"] = f"{zig.as_posix()} cc -target x86_64-windows-gnu"
        os.environ["ZIG_GLOBAL_CACHE_DIR"] = str(ROOT / "build/zig-cache/global")
        os.environ["ZIG_LOCAL_CACHE_DIR"] = str(ROOT / "build/zig-cache/local")
        cjson = Path.home() / ".cache/yiyi-xiaozhi/grokbot-yiyi-build/managed_components/espressif__cjson/cJSON"
        if (cjson / "cJSON.c").exists():
            obj = temporary / "cjson.o"
            subprocess.run([str(zig), "cc", "-target", "x86_64-windows-gnu", "-std=c99",
                            "-c", str(cjson / "cJSON.c"), "-o", str(obj)], check=True)
            os.environ["ASSETS_CJSON_CFLAGS"] = "-I" + cjson.as_posix()
            os.environ["ASSETS_CJSON_LIBS"] = obj.as_posix()
    if os.name == "nt":
        # WindowsApps/bash.exe is a WSL launcher, not an installed host Bash.
        os.environ["PATH"] = os.pathsep.join(
            p for p in os.environ["PATH"].split(os.pathsep) if "WindowsApps" not in p)
        original_cleanup = tempfile.TemporaryDirectory.cleanup

        def cleanup(directory):
            # Some upstream tests register cwd restoration with addCleanup, which
            # runs after the temporary-directory context exits. Windows cannot
            # remove the process's current directory, so restore it first.
            if Path.cwd().is_relative_to(Path(directory.name)):
                os.chdir(SOURCE)
            original_cleanup(directory)

        tempfile.TemporaryDirectory.cleanup = cleanup
    os.chdir(SOURCE)
    sys.path.insert(0, str(SOURCE))
    suite = unittest.defaultTestLoader.discover(str(SOURCE / "scripts/tests"))
    if os.name == "nt":
        # The CI shell tests invoke Bash unconditionally. A Windows WSL launcher
        # cannot execute their local Git-fixture paths as a POSIX host shell.
        import test_ci_selection
        test_ci_selection.WorkflowDiffTests.__unittest_skip__ = True
        test_ci_selection.WorkflowDiffTests.__unittest_skip_why__ = "requires a POSIX host Bash/Git shell"
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main())
