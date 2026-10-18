#!/usr/bin/env python3
"""Downloads MSYS2 UCRT64 packages and everything they depend on (tools/package/fetch_windows_deps.sh).

usage: msys2_fetch.py OUTDIR PACKAGE...

Reads the repository database (ucrt64.db, a zstd-compressed tar of <name>-<version>/desc files), follows %DEPENDS% from the given packages
(version constraints dropped, virtual names matched through %PROVIDES%), downloads each package file into OUTDIR and checks it against the
%SHA256SUM% the database lists. A file already in OUTDIR with the right checksum is kept. Prints the resolved set with versions, so a build
log shows exactly what went into it. Needs the zstd program to read the database.
"""
import hashlib
import io
import os
import re
import subprocess
import sys
import tarfile
import time
import urllib.request

MIRRORS = [m for m in os.environ.get("ONAIR_MSYS2_MIRROR", "").split() if m] or [
    "https://repo.msys2.org/mingw/ucrt64", "https://mirror.msys2.org/mingw/ucrt64"]   # ONAIR_MSYS2_MIRROR: other mirrors (space-separated)
DB = "ucrt64.db"


def fetch(name):
    last = None
    for attempt in range(4):
        for base in MIRRORS:
            try:
                req = urllib.request.Request(f"{base}/{name}", headers={"User-Agent": "OnAir-build"})
                with urllib.request.urlopen(req, timeout=120) as r:
                    return r.read()
            except Exception as e:  # try the next mirror, then again after a pause
                last = e
        time.sleep(5 * (attempt + 1))
    raise SystemExit(f"error: could not download {name}: {last}")


def parse_desc(text):
    fields, key = {}, None
    for line in text.splitlines():
        if line.startswith("%") and line.endswith("%"):
            key = line.strip("%")
            fields[key] = []
        elif line and key:
            fields[key].append(line)
    return fields


def bare(dep):
    return re.split(r"[<>=]", dep, maxsplit=1)[0].strip()


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    out, roots = sys.argv[1], sys.argv[2:]
    os.makedirs(out, exist_ok=True)

    raw = fetch(DB)
    tar_bytes = subprocess.run(["zstd", "-dc"], input=raw, stdout=subprocess.PIPE, check=True).stdout
    pkgs, provides = {}, {}
    with tarfile.open(fileobj=io.BytesIO(tar_bytes)) as t:
        for m in t.getmembers():
            if not m.name.endswith("/desc"):
                continue
            d = parse_desc(t.extractfile(m).read().decode("utf-8"))
            name = d["NAME"][0]
            pkgs[name] = d
            for p in d.get("PROVIDES", []):
                provides.setdefault(bare(p), name)

    def find(dep):
        n = bare(dep)
        if n in pkgs:
            return n
        if n in provides:
            return provides[n]
        return None

    want, todo = [], list(roots)
    while todo:
        dep = todo.pop(0)
        n = find(dep)
        if n is None:
            pre = "mingw-w64-ucrt-x86_64-"
            core = bare(dep).replace(pre, "")
            near = sorted(k for k in pkgs if core in k.replace(pre, "") or (len(k) - len(pre) >= 3 and k.replace(pre, "") in core))[:8]
            raise SystemExit(f"error: {dep} is not in the MSYS2 UCRT64 repository ({len(pkgs)} packages read)"
                             + (f"; similar names: {', '.join(near)}" if near else ""))
        if n in want:
            continue
        want.append(n)
        todo.extend(pkgs[n].get("DEPENDS", []))

    total = 0
    for n in sorted(want):
        d = pkgs[n]
        fn, sha = d["FILENAME"][0], d["SHA256SUM"][0]
        path = os.path.join(out, fn)
        if os.path.exists(path) and hashlib.sha256(open(path, "rb").read()).hexdigest() == sha:
            state = "ok"
        else:
            data = fetch(fn)
            got = hashlib.sha256(data).hexdigest()
            if got != sha:
                raise SystemExit(f"error: {fn}: SHA-256 {got}, the database says {sha}")
            with open(path + ".part", "wb") as f:
                f.write(data)
            os.replace(path + ".part", path)
            state = "fetched"
        total += os.path.getsize(path)
        print(f"{state:9} {n} {d['VERSION'][0]}")
    print(f"{len(want)} packages, {total / 1e6:.0f} MB")


if __name__ == "__main__":
    main()
