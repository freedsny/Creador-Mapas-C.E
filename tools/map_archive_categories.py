#!/usr/bin/env python3
"""The map archive's categories (port/assets/map_archive/map_categories.txt),
which BROWSE MAPS filters by (port/linux/src/map_archive.c):

    python tools/map_archive_categories.py [--list map_torrents.txt] [--jobs 4]

FractumSeraph's map list (map_torrents.txt, docs/map_archive.md) says each
map's name, size and hashes, not what it is. This reads that from each map on
his web seed, a few small ranges of the file and never the whole of it:

- the cache header (its first 2048 bytes): the map's type, 0 a solo map, 1 a
  multiplayer map, 2 the user interface's;
- the tag data's header and the scenario's entry in the tag index;
- the scenario's first 0x438 bytes: its AI actor palette's and encounters'
  counts (struct scenario, source/scenario/scenario_definitions.h: 0x420 and
  0x42C), so a multiplayer map with encounters is MULTIPLAYER WITH AI.

It writes a line a map, tab-separated: its file name, its version (the first
8 hex digits of its share hash, as a host's game record says it) and a letter:
S solo, M multiplayer, A multiplayer with AI, U the user interface's, ? not
read (a protected map's tag index, an error). Lines already in the file are
kept, so a run after the list grew reads only the new maps (--again reads all
of them). Needs nothing outside Python's own library; the builds do not run
it, as what it writes is committed.
"""

import argparse
import concurrent.futures
import struct
import sys
import threading
import time
import urllib.parse
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUTPUT = ROOT / "port/assets/map_archive/map_categories.txt"
LIST_URL = "https://raw.githubusercontent.com/FractumSeraph/OpenCE/map-index/map_torrents.txt"
SEED = "https://halomaps.fractumseraph.net/maps/"
FORMAT = "# map archive categories v1"
USER_AGENT = "OpenCE map archive categories (tools/map_archive_categories.py)"

# a cache's tags are at this address in memory, and their offsets made from it
TAG_BASE = 0x40440000
TAG_INDEX_ENTRY = 32
SCENARIO_READ = 0x438
ACTOR_PALETTE = 0x420
ENCOUNTERS = 0x42C
CATEGORY_TYPES = {0: "S", 1: "M", 2: "U"}

_local = threading.local()


def get_range(url: str, start: int, length: int) -> bytes:
    """bytes start to start + length of url (a 206's, or the whole 200's
    part)"""
    request = urllib.request.Request(url, headers={"Range": f"bytes={start}-{start + length - 1}",
                                                   "User-Agent": USER_AGENT})
    for attempt in range(4):
        try:
            with urllib.request.urlopen(request, timeout=30) as answer:
                data = answer.read(length) if answer.status == 206 else answer.read()[start:start + length]
                return data
        except Exception:
            if attempt == 3:
                raise
            time.sleep(1 + attempt * 2)
    return b""


def category(name: str, size: int) -> str:
    """the map's letter (CATEGORY_TYPES', A, or ?)"""
    url = SEED + urllib.parse.quote(name, safe="")
    header = get_range(url, 0, 2048)
    if len(header) < 0x64 or header[:4] not in (b"daeh", b"head"):
        return "?"
    tag_offset, tag_size = struct.unpack_from("<II", header, 0x10)
    map_type = struct.unpack_from("<H", header, 0x60)[0]
    letter = CATEGORY_TYPES.get(map_type, "?")
    if letter != "M":
        return letter
    try:
        if not tag_size or tag_offset + 0x28 > size:
            return letter
        tags = get_range(url, tag_offset, 4096)
        index_address, scenario_id = struct.unpack_from("<II", tags, 0)
        scenario_index = scenario_id & 0xFFFF
        index_offset = index_address - TAG_BASE + scenario_index * TAG_INDEX_ENTRY
        if index_offset < 0 or index_offset + TAG_INDEX_ENTRY > tag_size:
            return letter
        entry = (tags[index_offset:index_offset + TAG_INDEX_ENTRY] if index_offset + TAG_INDEX_ENTRY <= len(tags)
                 else get_range(url, tag_offset + index_offset, TAG_INDEX_ENTRY))
        data_address = struct.unpack_from("<I", entry, 0x14)[0]
        data_offset = data_address - TAG_BASE
        if data_offset < 0 or data_offset + SCENARIO_READ > tag_size:
            return letter
        scenario = get_range(url, tag_offset + data_offset, SCENARIO_READ)
        palette = struct.unpack_from("<i", scenario, ACTOR_PALETTE)[0]
        encounters = struct.unpack_from("<i", scenario, ENCOUNTERS)[0]
        if 0 < palette < 4096 and 0 < encounters < 4096:
            return "A"
    except (struct.error, OSError, ValueError):
        pass
    return letter


def list_maps(text: str) -> list:
    """the list's maps, resource maps left out: (name, version, size)"""
    maps = []
    for line in text.splitlines():
        fields = line.split("\t")
        if not line or line.startswith("#") or len(fields) < 7 or fields[1] == "00000000":
            continue
        share = next((field[6:] for field in fields if field.startswith("share:")), "")
        if len(share) != 64:
            continue
        maps.append((fields[0], share[:8].upper(), int(fields[2])))
    return maps


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--list", type=Path, help="a copy of map_torrents.txt (else GitHub's)")
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--limit", type=int, default=0, help="read at most this many maps (a test)")
    parser.add_argument("--again", action="store_true", help="read every map, not only those not in the file")
    arguments = parser.parse_args()
    if arguments.list:
        text = arguments.list.read_text(encoding="utf-8", errors="replace")
    else:
        request = urllib.request.Request(LIST_URL, headers={"User-Agent": USER_AGENT})
        text = urllib.request.urlopen(request, timeout=60).read().decode("utf-8", errors="replace")
    serial = next((line.split()[2] for line in text.splitlines() if line.startswith("# serial ")), "0")
    maps = list_maps(text)
    known = {}
    if OUTPUT.exists() and not arguments.again:
        for line in OUTPUT.read_text(encoding="utf-8").splitlines():
            fields = line.split("\t")
            if len(fields) == 3 and not line.startswith("#") and fields[2] != "?":
                known[(fields[0], fields[1])] = fields[2]
    todo = [entry for entry in maps if (entry[0], entry[1]) not in known]
    if arguments.limit:
        todo = todo[:arguments.limit]
    print(f"{len(maps)} maps, {len(known)} known, {len(todo)} to read", file=sys.stderr)
    done = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, arguments.jobs)) as pool:
        futures = {pool.submit(category, name, size): (name, version) for name, version, size in todo}
        for future in concurrent.futures.as_completed(futures):
            name, version = futures[future]
            try:
                known[(name, version)] = future.result()
            except Exception as problem:  # (said, and the map left "?")
                print(f"{name}: {problem}", file=sys.stderr)
                known[(name, version)] = "?"
            done += 1
            if done % 100 == 0:
                print(f"{done} of {len(todo)}", file=sys.stderr)
    listed = {(name, version) for name, version, _ in maps}
    lines = [FORMAT, f"# serial {serial}",
             "# file name, version (the share hash's first 8 hex digits), and S solo, M multiplayer,",
             "# A multiplayer with AI, U user interface, ? not read (tools/map_archive_categories.py)"]
    lines += [f"{name}\t{version}\t{letter}" for (name, version), letter in sorted(known.items(),
              key=lambda item: (item[0][0].lower(), item[0][1])) if (name, version) in listed]
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    counts = {}
    for (name, version), letter in known.items():
        if (name, version) in listed:
            counts[letter] = counts.get(letter, 0) + 1
    print(f"wrote {OUTPUT.relative_to(ROOT)}: {counts}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
