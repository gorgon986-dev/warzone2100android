#!/usr/bin/env python3
"""Emit the interesting parts of a failed Android CI build as GitHub annotations."""
import glob
import os
import re
import sys

build = sys.argv[1]
chunks = []


def tail(path, n):
    try:
        with open(path, errors="replace") as f:
            return f.read().splitlines()[-n:]
    except OSError:
        return []


def add(title, lines):
    text = "\n".join(lines).strip()
    if text:
        chunks.append((title, text[-6000:]))


# 1. Compile / link errors
blog = os.path.join(build, "build.log")
if os.path.exists(blog):
    lines = open(blog, errors="replace").read().splitlines()
    hits = [i for i, l in enumerate(lines) if re.search(r"error:|FAILED:|undefined reference|ld\.lld: error", l)]
    seen, out = set(), []
    for i in hits:
        for j in range(max(0, i - 2), min(len(lines), i + 8)):
            if j not in seen:
                seen.add(j)
                out.append(lines[j])
        if len(out) > 250:
            break
    add("Build errors", out)

# 2. vcpkg port failures (logs referenced in configure output)
clog = os.path.join(build, "configure.log")
if os.path.exists(clog):
    text = open(clog, errors="replace").read()
    add("Configure (tail)", text.splitlines()[-60:])
    for logpath in sorted(set(re.findall(r"(/\S+/buildtrees/\S+\.log)", text)))[:4]:
        add("vcpkg: " + os.path.basename(logpath), tail(logpath, 80))

# 3. Gradle
add("Gradle (tail)", tail(os.path.join(build, "gradle.log"), 80))


def esc(s):
    return s.replace("%", "%25").replace("\r", "%0D").replace("\n", "%0A")


for title, text in chunks[:9]:
    print(f"::error title={esc(title)}::{esc(text)}")
if not chunks:
    print("::error title=No logs::No build logs found")
