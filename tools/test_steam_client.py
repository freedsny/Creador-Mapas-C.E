"""The Steam Workshop screen's Steam client (port/linux/src/steam_*.c, every build's), built natively with -Wall
-Wextra -Werror, and its offline parts checked: its SHA-1, HMAC, AES-256, CRC-32 and base64 against known answers,
the connection's envelope refusing a changed byte, the session file (desktop's: its owner's alone; Android's, built
with HALO_ANDROID against a stand-in for the Keystore: the refresh token sealed, never written as it is, an earlier
build's plain file moved into the app's private storage, forgetting it), the depots' LZMA ("VZa"), Zstandard ("VSZa")
and zip chunks, a content server's chunked answers, manifests' bounds, a staged folder removed or moved past links,
and the paths a Workshop item's files may take in its package. Nothing here reaches Steam."""
import hashlib
import hmac
import lzma
import os
from pathlib import Path
import shutil
import stat
import struct
import subprocess
import zipfile
import zlib

import pytest

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "port/linux/src"
ZLIB = ROOT / "port/third_party/zlib"
STEAM_SOURCES = ["steam_cm.c", "steam_crypto.c", "steam_depot.c", "steam_net.c", "steam_unpack.c"]
TOKEN = "eyJhbGciOiJFZERTQSJ9.eyJzdWIiOiI3NjU2MTE5OTAwMDAwMDAwMSJ9.c2lnbmF0dXJl"


def build(work, defines):
    clang = shutil.which("clang")
    if not clang:
        pytest.skip("clang required")
    work.mkdir(parents=True, exist_ok=True)
    objects = []
    for source in ["posix_files.c", "posix_mcc_share.c", "posix_server.c"]:
        objects.append(work / (source + ".o"))
        subprocess.run([clang, "-std=gnu99", "-w", "-c", str(SRC / source), "-o", str(objects[-1])], check=True)
    for source in ["adler32.c", "crc32.c", "inffast.c", "inflate.c", "inftrees.c", "zutil.c"]:
        objects.append(work / (source + ".o"))
        subprocess.run([clang, "-std=gnu99", "-w", "-DZ_PREFIX", "-c", str(ZLIB / source), "-o", str(objects[-1])],
                       check=True)
    for source in [*(SRC / name for name in STEAM_SOURCES), ROOT / "tools/tests/steam_client_driver.c"]:
        objects.append(work / (source.name + ".o"))
        # (-Wall -Wextra -Werror, but for the unpacker's third-party decoders and the
        # driver, which includes steam_downloads.c and its vendored QR and picture decoders)
        warnings = ["-w"] if source.name in ("steam_unpack.c", "steam_client_driver.c") else \
            ["-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter"]
        # (platform.h's two calls alone, not the SDK's headers it reads: tools/tests/steam_client_platform.h)
        result = subprocess.run([clang, "-std=gnu11", "-D_GNU_SOURCE", *defines, *warnings,
                                 "-include", str(ROOT / "tools/tests/steam_client_platform.h"),
                                 "-I", str(SRC), "-I", str(ZLIB), "-c", str(source), "-o", str(objects[-1])],
                                capture_output=True, text=True)
        assert result.returncode == 0, result.stderr
    exe = work / "steam_client"
    subprocess.run([clang, *map(str, objects), "-lpthread", "-o", str(exe)], check=True)
    return exe


@pytest.fixture(scope="module")
def client(tmp_path_factory):
    return build(tmp_path_factory.mktemp("steam-client"), [])


@pytest.fixture(scope="module")
def android_client(tmp_path_factory):
    return build(tmp_path_factory.mktemp("steam-client-android"), ["-DHALO_ANDROID=1"])


def run(client, *arguments, secret=None):
    environment = dict(os.environ)
    environment.pop("STEAM_SEAL_TEST_SECRET", None)
    if secret is not None:
        environment["STEAM_SEAL_TEST_SECRET"] = secret
    result = subprocess.run([str(client), *map(str, arguments)], capture_output=True, env=environment)
    assert result.returncode == 0, result.stderr.decode(errors="replace")
    return result.stdout


def test_crypto_known_answers(client):
    lines = run(client, "crypto").decode().split("\n")
    assert lines[0] == hashlib.sha1(b"abc").hexdigest()
    assert lines[1] == hmac.new(b"key", b"The quick brown fox jumps over the lazy dog", hashlib.sha1).hexdigest()
    # (FIPS 197's appendix C.3: AES-256)
    assert lines[2] == "8ea2b7ca516745bfeafc49904b496089"
    assert lines[3] == "00112233445566778899aabbccddeeff"
    assert lines[4] == "hello world"
    assert lines[5] == f"{zlib.crc32(b'123456789'):08x}"


def test_envelope_refuses_a_changed_byte(client):
    assert run(client, "envelope").decode().split("\n")[:2] == ["round trip 1", "tampered 0"]


def session_file(root):
    return root / "steam" / "session.txt"


def private_file(root):
    return root / "private" / "steam" / "session.txt"


def test_desktop_session_is_its_owners_alone(client, tmp_path):
    run(client, "save", tmp_path, "player", TOKEN)
    text = session_file(tmp_path).read_text()
    assert "account=player\n" in text and f"token={TOKEN}\n" in text
    assert stat.S_IMODE(session_file(tmp_path).stat().st_mode) == 0o600
    assert not (tmp_path / "steam" / "session.txt.part").exists()
    assert run(client, "load", tmp_path).decode() == f"account player\ntoken {TOKEN}\n"


def test_desktop_session_forgotten(client, tmp_path):
    run(client, "save", tmp_path, "player", TOKEN)
    # (a save left unfinished goes with it)
    (tmp_path / "steam" / "session.txt.part").write_text(f"token={TOKEN}\n")
    run(client, "forget", tmp_path)
    assert TOKEN not in session_file(tmp_path).read_text()
    assert not (tmp_path / "steam" / "session.txt.part").exists()
    assert run(client, "load", tmp_path).decode() == "account player\ntoken \n"


def test_android_token_sealed_in_private_storage(android_client, tmp_path):
    run(android_client, "save", tmp_path, "player", TOKEN, secret="device one")
    text = private_file(tmp_path).read_text()
    assert "account=player\n" in text and "sealed=keystore:" in text
    assert TOKEN not in text and "token=" not in text
    assert not session_file(tmp_path).exists()
    assert run(android_client, "load", tmp_path, secret="device one").decode() == f"account player\ntoken {TOKEN}\n"


def test_android_another_key_opens_nothing(android_client, tmp_path):
    run(android_client, "save", tmp_path, "player", TOKEN, secret="device one")
    assert run(android_client, "load", tmp_path, secret="device two").decode() == "account player\ntoken \n"


def test_android_without_a_seal_never_written(android_client, tmp_path):
    # (the Keystore refusing: the token is not written, the sign-in the run's alone)
    run(android_client, "save", tmp_path, "player", TOKEN)
    text = private_file(tmp_path).read_text()
    assert TOKEN not in text and "token=" not in text and "sealed=" not in text


def test_android_plain_token_line_not_taken(android_client, tmp_path):
    run(android_client, "save", tmp_path, "player", TOKEN, secret="device one")
    path = private_file(tmp_path)
    path.write_text(path.read_text().split("sealed=")[0] + f"token={TOKEN}\n")
    assert run(android_client, "load", tmp_path, secret="device one").decode() == "account player\ntoken \n"


def test_android_earlier_builds_file_moved_and_sealed(android_client, tmp_path):
    # (an earlier build kept the token as it is in the data folder: read once,
    # sealed into the app's private storage, and the old file removed)
    legacy = session_file(tmp_path)
    legacy.parent.mkdir(parents=True)
    legacy.write_text(f"account=player\ntoken={TOKEN}\nseed={'ab' * 16}\n")
    assert run(android_client, "load", tmp_path, secret="device one").decode() == f"account player\ntoken {TOKEN}\n"
    assert not legacy.exists()
    text = private_file(tmp_path).read_text()
    assert TOKEN not in text and "sealed=keystore:" in text and f"seed={'ab' * 16}" in text
    assert run(android_client, "load", tmp_path, secret="device one").decode() == f"account player\ntoken {TOKEN}\n"


def test_android_forgotten_with_its_key(android_client, tmp_path):
    run(android_client, "save", tmp_path, "player", TOKEN, secret="device one")
    out = run(android_client, "forget", tmp_path, secret="device one").decode()
    assert "key forgotten 1" in out
    assert "sealed=" not in private_file(tmp_path).read_text()


def unpack(client, tmp_path, chunk, size):
    (tmp_path / "chunk").write_bytes(chunk)
    out = run(client, "unpack", tmp_path / "chunk", size)
    head, _, body = out.partition(b"\n")
    return head == b"unpacked 1", body


PLAIN = bytes(range(256)) * 64 + b"the end of a chunk"


def test_lzma_chunk(client, tmp_path):
    properties = {"id": lzma.FILTER_LZMA1, "dict_size": 1 << 16, "lc": 3, "lp": 0, "pb": 2}
    raw = lzma.compress(PLAIN, format=lzma.FORMAT_RAW, filters=[properties])
    props = bytes([(2 * 5 + 0) * 9 + 3]) + struct.pack("<I", 1 << 16)
    chunk = b"VZa" + b"\0" * 4 + props + raw + struct.pack("<II", zlib.crc32(PLAIN), len(PLAIN)) + b"zv"
    assert unpack(client, tmp_path, chunk, len(PLAIN)) == (True, PLAIN)
    assert unpack(client, tmp_path, chunk, len(PLAIN) + 1)[0] is False


def test_zstd_chunk(client, tmp_path):
    zstd = shutil.which("zstd")
    if not zstd:
        pytest.skip("zstd required")
    frame = subprocess.run([zstd, "-q", "-c"], input=PLAIN, capture_output=True, check=True).stdout
    chunk = b"VSZa" + b"\0" * 4 + frame + b"\0" * 15
    assert unpack(client, tmp_path, chunk, len(PLAIN)) == (True, PLAIN)


def test_zip_chunk(client, tmp_path):
    zipped = tmp_path / "chunk.zip"
    with zipfile.ZipFile(zipped, "w", zipfile.ZIP_DEFLATED) as archive:
        archive.writestr("z", PLAIN)
    assert unpack(client, tmp_path, zipped.read_bytes(), len(PLAIN)) == (True, PLAIN)


def test_unknown_chunk_refused(client, tmp_path):
    assert unpack(client, tmp_path, b"XXXX" + PLAIN, len(PLAIN))[0] is False


@pytest.mark.parametrize("name,expected", [
    ("maps/Level.map", "level.map"),
    ("halo1/maps/bitmaps.map", "bitmaps.map"),
    ("ModInfo.json", "modinfo.json"),
    ("Images/Preview.PNG", "images/preview.png"),
    ("maps/sub/level.map", None),
    ("other/level.map", None),
    ("../escape.json", None),
    ("/rooted.json", None),
    ("a//b.json", None),
    ("c:/drive.json", None),
    ("readme.txt", None),
])
def test_item_paths(client, name, expected):
    assert run(client, "path", name).decode().strip() == (expected or "(refused)")


def http(client, tmp_path, answer, maximum=1 << 20):
    path = tmp_path / "answer.bin"
    path.write_bytes(answer)
    result = subprocess.run([str(client), "http", str(path), str(maximum)], capture_output=True)
    assert result.returncode == 0, result
    return result.stdout.decode(errors="replace")


CHUNKED = b"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"


def test_chunked_body(client, tmp_path):
    assert http(client, tmp_path, CHUNKED + b"5\r\nhello\r\n6;x=1\r\n world\r\n0\r\n\r\n") == "ok 11 hello world\n"


@pytest.mark.parametrize("length", [b"FFFFFFFFFFFFFFF0", b"-10", b"200000", b"zz", b""])
def test_chunk_length_refused(client, tmp_path, length):
    # (after 16 bytes, a length of 2^64 - 16 wrapped the bounds checks to zero
    # and asked for an enormous copy; past maximum, negative, not hex or
    # missing: each refused, nothing copied)
    answer = CHUNKED + b"10\r\n" + b"y" * 16 + b"\r\n" + length + b"\r\n" + b"x" * 64 + b"\r\n0\r\n\r\n"
    assert http(client, tmp_path, answer).startswith("fail ")


def test_remove_tree_leaves_what_a_link_leads_to(client, tmp_path):
    # (a link inside a staged folder: neither followed nor removed, so what it
    # leads to is left alone; the rest goes)
    outside = tmp_path / "outside"
    outside.mkdir()
    (outside / "keep.txt").write_text("keep")
    staging = tmp_path / "mcc_staging" / "steam_1"
    (staging / "maps").mkdir(parents=True)
    (staging / "maps" / "a.map").write_bytes(b"map")
    (staging / "link").symlink_to(outside, target_is_directory=True)
    run(client, "remove_tree", staging)
    assert (outside / "keep.txt").read_text() == "keep"
    assert not (staging / "maps").exists()


def test_move_folder_refuses_a_link(client, tmp_path):
    # (the fallbacks after a refused publication, Android's file by file, take
    # no path the publication would refuse)
    outside = tmp_path / "outside"
    outside.mkdir()
    (outside / "keep.txt").write_text("keep")
    staging = tmp_path / "mcc_staging" / "steam_1"
    staging.mkdir(parents=True)
    (staging / "a.map").write_bytes(b"map")
    (tmp_path / "mcc_maps").mkdir()
    (tmp_path / "mcc_maps" / "link").symlink_to(outside, target_is_directory=True)
    assert run(client, "move_folder", staging, tmp_path / "mcc_maps" / "link" / "1").decode() == "moved 0\n"
    assert sorted(path.name for path in outside.iterdir()) == ["keep.txt"]
    assert run(client, "move_folder", staging, tmp_path / "mcc_maps" / "1").decode() == "moved 1\n"
    assert (tmp_path / "mcc_maps" / "1" / "a.map").read_bytes() == b"map"


def varint(value):
    out = b""
    while value > 127:
        out += bytes([(value & 127) | 128])
        value >>= 7
    return out + bytes([value])


def pb_bytes(field, data):
    return varint(field << 3 | 2) + varint(len(data)) + data


def pb_varint(field, value):
    return varint(field << 3) + varint(value)


def chunk_record(offset, size, packed=None, sha=b"\x11" * 20):
    return pb_bytes(6, pb_bytes(1, sha) + pb_varint(3, offset) + pb_varint(4, size) + pb_varint(5, packed or size))


def manifest_bytes(*mappings, metadata=pb_varint(4, 0)):
    payload = b"".join(pb_bytes(1, mapping) for mapping in mappings)
    return (struct.pack("<II", 0x71F617D0, len(payload)) + payload +
            struct.pack("<II", 0x1F4812BE, len(metadata)) + metadata + struct.pack("<II", 0x32C415AB, 0))


def mapping(name, size, *chunks):
    return pb_bytes(1, name) + pb_varint(2, size) + b"".join(chunks)


def parse(client, tmp_path, data):
    (tmp_path / "manifest").write_bytes(data)
    return run(client, "manifest", tmp_path / "manifest").decode()


def test_manifest_read(client, tmp_path):
    good = manifest_bytes(mapping(b"maps\\a.map", 3000, chunk_record(0, 1000), chunk_record(1000, 2000)),
                          mapping(b"b.json", 10, chunk_record(0, 10)))
    assert parse(client, tmp_path, good) == "ok 2 3\n"


@pytest.mark.parametrize("bad", [
    # a chunk past the file's end, and two that overlap
    mapping(b"a.map", 1500, chunk_record(1000, 1000)),
    mapping(b"a.map", 3000, chunk_record(0, 2000), chunk_record(1000, 1000)),
    # an offset that wraps, a chunk bigger than any download, a chunk with no hash
    mapping(b"a.map", 3000, chunk_record((1 << 64) - 10, 20)),
    mapping(b"a.map", 8 << 20, chunk_record(0, 8 << 20)),
    mapping(b"a.map", 10, pb_bytes(6, pb_varint(3, 0) + pb_varint(4, 10) + pb_varint(5, 10))),
    # a chunk record cut short inside (its nested message malformed)
    mapping(b"a.map", 10, pb_bytes(6, pb_bytes(1, b"\x11" * 20)[:-5])),
    # a file larger than the bounds
    mapping(b"a.map", 32 << 30),
])
def test_manifest_refused(client, tmp_path, bad):
    # (one bad file and the whole list is refused, not just that file left out)
    good = mapping(b"b.json", 10, chunk_record(0, 10))
    assert parse(client, tmp_path, manifest_bytes(good, bad)).startswith("fail ")


def test_manifest_malformed_metadata_refused(client, tmp_path):
    good = mapping(b"b.json", 10, chunk_record(0, 10))
    assert parse(client, tmp_path, manifest_bytes(good, metadata=varint(4 << 3 | 2) + varint(50))).startswith("fail ")
