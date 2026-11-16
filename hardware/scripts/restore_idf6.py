"""Restore the pinned ESP-IDF 6.0.1 submodules using GitHub source archives.

The SDK source archive must already be extracted in the selected cache directory.
No device operations are performed.
"""
import argparse
import concurrent.futures
import io
import json
import re
import tarfile
import urllib.request
from pathlib import Path


def fetch(url):
    request = urllib.request.Request(url, headers={"User-Agent": "yiyi-firmware-build"})
    with urllib.request.urlopen(request, timeout=120) as response:
        return response.read()


def restore(root, path):
    dest = root / path
    marker = dest / ".yiyi-source-revision"
    if marker.exists():
        return path + " already restored"
    parent = path.rsplit("/", 1)[0]
    html = fetch(f"https://github.com/espressif/esp-idf/tree/v6.0.1/{parent}").decode()
    items = re.findall(r'\{"name":[^{}]*"contentType":"submodule"[^{}]*\}', html)
    item = next(json.loads(x) for x in items if json.loads(x).get("path") == path)
    owner, repo, _, revision = item["submoduleUrl"].strip("/").split("/")
    archive = fetch(f"https://codeload.github.com/{owner}/{repo}/tar.gz/{revision}")
    dest.mkdir(parents=True, exist_ok=True)
    with tarfile.open(fileobj=io.BytesIO(archive), mode="r:gz") as tar:
        for entry in tar.getmembers():
            parts = entry.name.split("/", 1)
            if len(parts) < 2 or not parts[1]:
                continue
            entry.name = parts[1]
            tar.extract(entry, dest, filter="data")
    marker.write_text(revision + "\n", encoding="ascii")
    return f"{path} @ {revision} ({len(archive)} bytes)"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    args = parser.parse_args()
    paths = [
        "components/heap/tlsf", "components/mbedtls/mbedtls",
        "components/lwip/lwip", "components/esp_wifi/lib", "components/esp_phy/lib",
        "components/esp_coex/lib", "components/bt/host/nimble/nimble",
        "components/unity/unity", "components/spiffs/spiffs",
        "components/bootloader/subproject/components/micro-ecc/micro-ecc",
    ]
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as executor:
        jobs = {executor.submit(restore, args.root, path): path for path in paths}
        for job in concurrent.futures.as_completed(jobs):
            try:
                print(job.result(), flush=True)
            except Exception as error:
                print(f"FAILED {jobs[job]}: {error}", flush=True)
                raise


if __name__ == "__main__":
    main()
