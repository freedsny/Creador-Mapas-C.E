# FractumSeraph's map archive

The Custom Edition maps of FractumSeraph's archive, on every native build
(not the browser's yet): browsed and downloaded from **Mods > CE MAPS >
BROWSE MAPS**, and fetched by a joiner that lacks the host's map before the
host's players are asked for it. The model is `port/linux/src/map_archive.c`,
ported from SlipstreamCE's Android-host `fractum_archive.c` (thelinkin3000).

## The list

`map_torrents.txt` from the `map-index` branch of his fork
(github.com/FractumSeraph/OpenCE), with `map_torrents.txt.sig`: Ed25519 by his
release key or its recovery key, over `fractum-halo-map-index 1\n` and the
list's exact bytes. It is taken only when that checks, kept in
`<data root>/map_archive/` and used at the next start until GitHub's has a
higher `# serial`. A line a map, tab-separated: file name, header checksum,
size, piece length, info hash, `blake2b:<hash>`, `share:<hash>`, sources. The
resource maps (checksum `00000000`) are left out.

`share:` is map sharing's own hash (`map_share_files.c`: BLAKE2b of "OpenCE map
1", the size and the 256 KB pieces' BLAKE2bs), so its first 32 bits are the
map version a host's game record carries. A joiner finds the host's map by
name and version exactly.

## Categories

The list doesn't say what a map is. `tools/map_archive_categories.py` reads it
from each map on the web seed, three or four small HTTP ranges and never the
whole file: the cache header's map type, then the scenario's AI actor palette
and encounters counts (0x420 and 0x42C). It writes
`port/assets/map_archive/map_categories.txt`, a game data file
(`GAME_DATA_FILES`) beside config.toml: file name, version, and S
(singleplayer), M (multiplayer), A (multiplayer with AI), U (user interface)
or ? (not read). A run keeps the lines it has and reads only new maps, so
re-run it before a release when the list has grown. Maps it has no line for
show under TYPE ALL only.

## BROWSE MAPS

SEARCH (part of the name), TYPE (ALL, SINGLEPLAYER, MULTIPLAYER, MULTI +
AI), SORT (name, rating, downloads, date, size: maps without the value last),
ORDER (ascending, descending; SORT sets name A to Z and the others highest
first), SHOW (all, installed, not installed),
then 20 maps a page with PREVIOUS PAGE and NEXT PAGE. A map is INSTALLED when
a file of its name (less `.map`, or less a second version's `_<8 digits>`) is
in custom_maps or one of the Map Manager's folders. A downloads a map that
isn't; up to 64 wait in a queue, and the screen can't be left while they
come.

## Downloads

`http://halomaps.fractumseraph.net/maps/<name>` (plain HTTP, as the Steam
client's CDN reads are), written to `<file>.part` while its BLAKE2b is taken,
kept only when the size and BLAKE2b are the signed list's, never over a file
already there, and named in `custom_maps/downloaded_maps.txt` as map
sharing's downloads are, so its scripts are held to the same tighter rules.

## A joiner's map

With `network.map_download` and `network.map_archive` on (Network Setup > Map
Sharing > MAP ARCHIVE FIRST), a joiner missing only the map itself (not its
resource maps) looks for the host's name and version in the list (waiting up
to 10 seconds for GitHub's when the kept copy hasn't got it). If it's there,
the map comes from the archive, ahead of any BROWSE MAPS download, named as
map sharing names it (`<name>.map`, or `<name>_<version>.map` beside another
version). The host is asked as usual all the same, and the joiner sends it
progress reports, so a sharing host keeps it while it downloads (no network
change). If the archive hasn't got the map, can't send it or stalls for a
minute, the host is asked again and its players send the map as before.

## Details and screenshots

Everything BROWSE MAPS shows of a map comes from the community server's
catalog (`GET /archive/catalog`, api-opence's `routes/routes_map_archive.js`):
a line per map with its type letter (S/M/A/U, which TYPE filters by), title,
author, date, rating, the site's category, first screenshot's URL,
description and HAC2 downloads (an eleventh field; a ten-field catalog still
reads, with no downloads). It's about 1 MB, read once after the signed list, kept in
`<data root>/map_archive/catalog.txt` and fetched again only when
`GET /archive/catalog/serial` differs from the kept one. The bundled
`map_categories.txt` is only the fallback for types while offline.

Only the screenshots come from FractumSeraph's server: the chosen row's
(after 120 ms on it), then up to 5 rows either side while idle, over one
kept-open HTTP connection, 24 kept decoded in memory.

The server's data is his site read once into its database:

    node scripts/map_archive_scrape.js     # his pages into scripts/map_archive/pages.jsonl (resumable)
    node scripts/map_archive_import.js --categories <game repo>/port/assets/map_archive/map_categories.txt

The import links each map of the list to its page by the list's own
`halomaps:<fid>` / `halonet:<zip>` fields and builds the catalog once.
