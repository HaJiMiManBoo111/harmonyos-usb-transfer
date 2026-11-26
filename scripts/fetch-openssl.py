"""Download and extract a conda-forge .conda package into tools/openssl."""
import os
import sys
import tarfile
import urllib.request
import zipfile

try:
    from compression import zstd
except ImportError:  # older python
    zstd = None

URL = "https://conda.anaconda.org/conda-forge/win-64/openssl-3.6.5-hf411b9b_0.conda"
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
OUT = os.path.join(ROOT, "tools", "openssl")
TMP = os.path.join(ROOT, "tools", "openssl.pkg.conda")


def main():
    os.makedirs(os.path.join(ROOT, "tools"), exist_ok=True)
    if not os.path.exists(TMP):
        print("downloading", URL, flush=True)
        req = urllib.request.Request(URL, headers={"User-Agent": "Mozilla/5.0"})
        with urllib.request.urlopen(req, timeout=300) as r, open(TMP, "wb") as fh:
            total = 0
            while True:
                chunk = r.read(1 << 20)
                if not chunk:
                    break
                fh.write(chunk)
                total += len(chunk)
        print("downloaded", total, "bytes", flush=True)
    else:
        print("cached", os.path.getsize(TMP), "bytes", flush=True)

    os.makedirs(OUT, exist_ok=True)
    with zipfile.ZipFile(TMP) as z:
        names = z.namelist()
        print("entries", names, flush=True)
        payload = [n for n in names if n.startswith("pkg-") and n.endswith(".tar.zst")]
        if not payload:
            payload = [n for n in names if n.endswith(".tar.zst")]
        member = payload[0]
        print("payload", member, flush=True)
        blob = z.read(member)

    if zstd is None:
        print("ERROR: no zstd module", flush=True)
        return 1

    plain = zstd.decompress(blob)
    print("decompressed", len(plain), flush=True)
    tar_path = os.path.join(ROOT, "tools", "openssl.pkg.tar")
    with open(tar_path, "wb") as fh:
        fh.write(plain)
    with tarfile.open(tar_path) as t:
        members = [m for m in t.getmembers() if not m.name.startswith("info/")]
        t.extractall(OUT, members=members)
    print("extracted to", OUT, flush=True)

    inc = os.path.join(OUT, "Library", "include", "openssl", "ssl.h")
    print("ssl.h:", os.path.exists(inc), flush=True)
    libdir = os.path.join(OUT, "Library", "lib")
    if os.path.isdir(libdir):
        for n in sorted(os.listdir(libdir)):
            if n.lower().endswith((".lib", ".dll")):
                print("  ", n, flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
