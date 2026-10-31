#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""The feckless section of exports.txt, from a build.

    python tools/feckless_exports.py <build dir> <managed_components dir>

Prints every global the two libraries define that their public headers
(include/, and fonts/*/ for the graphics handler) name, sorted: the
libraries' whole public API at the tags the build used. Run it after
moving a tag, and paste the result over the section in exports.txt.
"""
import glob
import os
import re
import subprocess
import sys

LIBS = ("feckless_drivers", "feckless_graphics_handler")


def main():
    build, managed = sys.argv[1], sys.argv[2]
    nm = os.environ.get("NM", "riscv32-esp-elf-nm")
    headers = ""
    for lib in LIBS:
        for pat in ("include/*.h", "fonts/*/*.h"):
            for h in glob.glob(os.path.join(managed, lib, pat)):
                headers += open(h, encoding="utf-8").read()
    words = set(re.findall(r"[A-Za-z_][A-Za-z0-9_]*", headers))
    syms = set()
    for lib in LIBS:
        a = os.path.join(build, "esp-idf", lib, f"lib{lib}.a")
        out = subprocess.run([nm, "-g", "--defined-only", a],
                             capture_output=True, text=True, check=True).stdout
        for line in out.splitlines():
            f = line.split()
            if len(f) == 3 and f[1] in "TDRB" and f[2] in words:
                syms.add(f[2])
    print("\n".join(sorted(syms)))


if __name__ == "__main__":
    main()
