#!/usr/bin/env python3
"""Name the PS4 profiler's samples (port/ps4_profile.cpp) and add them up by function.

Usage (in WSL):
  profile.py [--fetch] [--elf <eboot elf>] [--log <ssbu.log>] [profile_<n>.txt ...]

--fetch downloads /data/SSBU/profiles/*.txt and ssbu.log from the console into
~/ssbu-build/logs/profiles first (and then names all of them). Each profile_<n>.txt gets a
profile_<n>.named.txt beside it: per thread its busy share and its busiest functions, and the
busiest functions of all sampled threads together.

Addresses: "elf 0x.." is relative to the eboot's .text (named against the ELF of the version
that ran, from ssbu.log's first line, ~/ssbu-build/oelf/v<version>.elf); "abs 0x.." inside an NRO
library (ssbu.log's "[ro] <name>'s code: ... at 0x..") against ~/ssbu-build/build/ssbu/nro-so;
"mod <module>+0x.." stays as it is (system modules).
"""
import argparse
import collections
import glob
import os
import re
import subprocess
import sys

HOME = os.path.expanduser("~/ssbu-build")
OUT = os.path.join(HOME, "logs", "profiles")
NRO_SO = os.path.join(HOME, "build", "ssbu", "nro-so")
HOST = "ftp://" + os.environ.get("PS4_HOST", "192.168.0.90") + ":" + os.environ.get("PS4_FTP_PORT", "2121")
SYMBOLIZER = "llvm-symbolizer-21"


def fetch():
    os.makedirs(OUT, exist_ok=True)
    listing = subprocess.run(["curl", "-s", "--max-time", "30", f"{HOST}/data/SSBU/profiles/"], capture_output=True, text=True).stdout
    names = re.findall(r"(profile_\d+\.txt)\s*$", listing, re.M)
    for n in names + ["../ssbu.log"]:
        dest = os.path.join(OUT, os.path.basename(n))
        subprocess.run(["curl", "-s", "--max-time", "60", "-o", dest, f"{HOST}/data/SSBU/profiles/{n}"], check=False)
    print(f"fetched {len(names)} profiles and ssbu.log into {OUT}")
    return [os.path.join(OUT, n) for n in names]


def nro_bases(log):
    """(start, end, name) of each NRO library's code, from ssbu.log."""
    out = []
    for m in re.finditer(r"\[ro\] (\S+)'s code: (\d+) MiB at (0x[0-9a-f]+)", open(log, errors="replace").read()):
        start = int(m.group(3), 16)
        out.append((start, start + (int(m.group(2)) + 1) * (1 << 20), m.group(1)))
    return sorted(out)


def elf_for(log):
    first = open(log, errors="replace").readline()
    m = re.search(r"SSBU (\d\d\.\d\d)", first)
    if m and os.path.isfile(os.path.join(HOME, "oelf", f"v{m.group(1)}.elf")):
        return os.path.join(HOME, "oelf", f"v{m.group(1)}.elf")
    found = sorted(glob.glob(os.path.join(HOME, "oelf", "v*.elf")), key=os.path.getmtime)
    return found[-1] if found else None


def text_start(elf):
    out = subprocess.run(["llvm-nm-21", elf], capture_output=True, text=True).stdout
    m = re.search(r"^([0-9a-f]+) \S __text_start$", out, re.M)
    return int(m.group(1), 16) if m else 0


def symbolize(obj, addresses):
    """{address: function name} for addresses in one object."""
    addresses = sorted(set(addresses))
    if not addresses:
        return {}
    r = subprocess.run([SYMBOLIZER, f"--obj={obj}", "--functions=linkage", "--demangle", "--no-inlines", "--output-style=GNU"],
                       input="\n".join(hex(a) for a in addresses) + "\n", capture_output=True, text=True)
    lines = r.stdout.splitlines()
    names = {}
    for i, a in enumerate(addresses):   # (GNU style: two lines an address)
        name = lines[2 * i] if 2 * i < len(lines) else "??"
        names[a] = name if name != "??" else f"?{a:#x}"
    return names


def parse(path):
    """header lines, and [(thread line, [(count, where, waiting)])]"""
    header, threads, cur = [], [], None
    for line in open(path, errors="replace"):
        line = line.rstrip("\n")
        if line.startswith("thread "):
            cur = (line, [])
            threads.append(cur)
        elif line.startswith("  ") and cur is not None:
            m = re.match(r"\s+(\d+) (elf|abs|mod) (\S+)( wait)?((?: < (?:elf|abs|mod) \S+)*)$", line)
            if m:
                chain = tuple(re.findall(r" < (elf|abs|mod) (\S+)", m.group(5)))
                cur[1].append((int(m.group(1)), m.group(2), m.group(3), bool(m.group(4)), chain))
        else:
            header.append(line)
            cur = None
    return header, threads


def name_profile(path, elf, log):
    header, threads = parse(path)
    tstart = text_start(elf) if elf else 0
    nros = nro_bases(log) if log else []
    elf_addrs, nro_addrs = [], collections.defaultdict(list)
    for _line, samples in threads:
        for _n, kind0, where0, _w, chain in samples:
            for kind, where in ((kind0, where0),) + chain:
                if kind == "elf":
                    elf_addrs.append(tstart + int(where, 16))
                elif kind == "abs":
                    a = int(where, 16)
                    for s, e, n in nros:
                        if s <= a < e:
                            nro_addrs[n].append(a - s)
    names = {("elf", a): v for a, v in symbolize(elf, elf_addrs).items()} if elf else {}
    for n, addrs in nro_addrs.items():
        so = os.path.join(NRO_SO, n + ".so")
        if os.path.isfile(so):
            names.update({(n, a): f"{v} [{n}]" for a, v in symbolize(so, addrs).items()})

    def fname(kind, where):
        if kind == "elf":
            return names.get(("elf", tstart + int(where, 16)), "elf " + where)
        if kind == "abs":
            a = int(where, 16)
            for s, e, n in nros:
                if s <= a < e:
                    return names.get((n, a - s), f"{n}+{a - s:#x}")
            return "abs " + where
        return where   # (a system module: module+offset, the offset kept: its code is in the profile's "code" lines)

    out_path = re.sub(r"\.txt$", "", path) + ".named.txt"
    total = collections.Counter()
    with open(out_path, "w") as out:
        out.write("\n".join(header) + "\n\n")
        for line, samples in threads:
            per = collections.Counter()
            n = sum(c for c, *_ in samples)
            for c, kind, where, waiting, chain in samples:
                key = fname(kind, where) + (" (waiting)" if waiting else "") + "".join(" <- " + fname(k, w) for k, w in chain)
                per[key] += c
                if not waiting:
                    total[key] += c
            out.write(line + "\n")
            for key, c in per.most_common(25):
                out.write(f"  {100.0 * c / max(n, 1):5.1f}%  {key}\n")
            out.write("\n")
        busy = sum(total.values())
        out.write(f"All sampled threads, busy samples ({busy}) by function:\n")
        for key, c in total.most_common(80):
            out.write(f"  {100.0 * c / max(busy, 1):5.1f}%  {key}\n")
    return out_path


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--fetch", action="store_true")
    ap.add_argument("--elf")
    ap.add_argument("--log")
    ap.add_argument("profiles", nargs="*")
    a = ap.parse_args()
    paths = a.profiles
    if a.fetch:
        paths = fetch() + paths
    if not paths:
        sys.exit("no profiles (use --fetch, or name them)")
    log = a.log or os.path.join(os.path.dirname(os.path.abspath(paths[0])), "ssbu.log")
    log = log if os.path.isfile(log) else None
    elf = a.elf or (elf_for(log) if log else None)
    print(f"eboot symbols: {elf}; NRO places: {log}")
    for p in paths:
        print("->", name_profile(p, elf, log))


if __name__ == "__main__":
    main()
