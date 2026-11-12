"""Find and fetch a conda-forge win-64 OpenSSL build (headers + import libs)."""
import json
import sys
import urllib.request

MIRRORS = [
    "https://conda.anaconda.org/conda-forge/win-64/",
    "https://mirrors.tuna.tsinghua.edu.cn/anaconda/cloud/conda-forge/win-64/",
    "https://mirrors.bfsu.edu.cn/anaconda/cloud/conda-forge/win-64/",
]


def fetch(url, timeout=60):
    req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return r.read()


def main():
    for base in MIRRORS:
        url = base + "repodata.json"
        print("trying", url, flush=True)
        try:
            data = fetch(url)
        except Exception as exc:  # noqa: BLE001
            print("  failed:", exc, flush=True)
            continue
        print("  got", len(data), "bytes", flush=True)
        doc = json.loads(data)
        pkgs = doc.get("packages.conda", {})
        hits = []
        for name, meta in pkgs.items():
            if not name.startswith("openssl-3."):
                continue
            hits.append((meta.get("version"), name, meta.get("size")))
        hits.sort(key=lambda h: [int(x) for x in h[0].split(".")])
        for ver, name, size in hits[-6:]:
            print("  ", ver, name, size, flush=True)
        if hits:
            best = hits[-1]
            print("CHOOSE", base + best[1], flush=True)
            with open("openssl-pick.txt", "w") as fh:
                fh.write(base + best[1] + "\n")
            return 0
    print("no candidate found")
    return 1


if __name__ == "__main__":
    sys.exit(main())
