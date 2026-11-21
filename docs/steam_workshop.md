# Steam Workshop

Settings > **STEAM WORKSHOP** signs in with Steam and downloads MCC content
into `mcc_maps`, on every build but the browser's (Steam refuses other sites'
pages). The Steam client is the game's own, `port/linux/src/steam_*.c`, and
the screen's model is `steam_downloads.c` (menu_functions.c's "port steam"
functions draw it). OpenCE runs a copy of it in its Android app's host; the
fixes it made there are taken back here (see "From OpenCE").

## What it does

- **Sign in** with a QR code that the Steam mobile app scans (first on
  desktop and consoles), or with the account name and password, then the
  Steam app's approval or a Steam Guard code (first on Android, since a
  phone can't scan its own screen). The password is RSA-encrypted under
  Steam's key for the account and is never stored.

  The account must own Halo: The Master Chief Collection and Halo: Combat
  Evolved Anniversary, because Steam only gives the depots' keys to owners.
  An account that doesn't is told so, and BROWSE WORKSHOP stays shut.
- **MANAGE ACCOUNT:** the account, and **LOG OUT**, which revokes the sign-in
  with Steam (`Authentication.RevokeToken`) and then forgets it on the device.
  If Steam can't be reached, the sign-in is forgotten on the device only, and
  the screen says that Steam's own settings can deauthorize it.
- **MCC FILE STATUS:**
  - downloads the three shared resource maps from Halo: CE Anniversary's
    depot (976733) into `mcc_maps/_shared/singleplayer` and
    `mcc_maps/_shared/multiplayer`;
  - lists the downloads under way and queued, then the Workshop packages in
    `mcc_maps`, which it updates or removes. Each one's details end with its
    publisher.
- **BROWSE WORKSHOP:** the Halo 1 Workshop, 20 items at a time, by search,
  sort and type, with each item's preview picture and its publisher after
  its description.
- **SERVER LISTS:** the community server's curated lists of Workshop maps.
- Installing an item builds it in `mcc_staging/steam_<id>`, then moves it into
  `mcc_maps/<id>/` whole, replacing an older copy. `steam/downloads.txt`
  records which version each item came from, which is how an update is told.
  Downloads wait in a queue of 32.

## Publishers

An item's publisher is the Steam name of the account that published it, who
may not be the original maker of what it holds ("Publisher" on the screens). Nothing
on the card names it, so the game asks Steam the first time an item is
shown in a run (`port/linux/src/workshop_authors.c`), on every platform but
the web, whose page Steam refuses: the item's creator from
the public Web API (`ISteamRemoteStorage/GetPublishedFileDetails`, which needs
no key or sign-in), then the name on that account's profile
(`steamcommunity.com/profiles/<id>/?xml=1`). The lobby asks the same way for a
Workshop map, and the MCC map lists under its description. Nothing is written to disk, and an item Steam could not
answer for is asked about again after 10 minutes at the soonest.

## Joining an MCC game

A joiner signed in with Steam installs a host's MCC package from Steam when
the package is a Workshop item (`port/linux/game/mcc_share.c`'s
`mcc_share_install_begin`, `steam_downloads_install_start`), with no prompt.
The item goes to the front of the download queue, so MCC FILE STATUS shows it.
MCC's shared files come from Steam too when they're missing; no host sends
them (`docs/mcc_maps.md`). The host's offer is kept meanwhile, so the host
keeps counting the joiner as downloading. The package is checked against the
host's fingerprint once it's in; if it doesn't match, or the install fails,
the joiner is asked to download it from the host as before.

Nothing new goes between the machines, so any host takes it. A joiner who is
not signed in gets the host's prompt, whose DOWNLOAD WITH STEAM opens this
screen to sign in.

On Android the fingerprints the lobby and the host's offer need are worked
out in the app's host with the CPU's SHA-256 and CRC32 instructions
(`port/android/host/host_mcc_hash.c`) and kept on the card
(`mcc_fingerprints.txt`).

## How it reaches Steam

- **Connection:** a TCP session with one of Steam's connection servers, found
  through `ISteamDirectory` over HTTPS.
  - The channel key is sent encrypted under Steam's public universe RSA key,
    then every message is AES-encrypted with HMAC-derived IVs, as SteamKit does.
  - A server lookup that was tampered with can't read or change the session.
- **Downloads:** a depot's manifest comes over HTTPS alone, since the chunks
  are checked against its hashes. Chunks come from the content servers over
  plain HTTP on kept-alive connections, as Steam's own client fetches them.
  - Each chunk is encrypted with the depot key and checked against its SHA-1
    after unpacking (LZMA, Zstandard or zip: `steam_unpack.c`).
  - A server that refuses a download (401, 403) is passed over after 500 ms
    and left out for five minutes.
  - A manifest with a malformed record, a chunk outside its file or
    overlapping another, an oversized chunk or file, or too many files is
    refused whole, rather than installing part of a package.
  - The download's speed shows beside its progress.

## The sign-in on the device

A refresh token is as good as the account to whoever holds it until it's
revoked. The session file (`steam/session.txt`) is written whole to a `.part`
file, then renamed over the old one.

- **Android:** in the app's private storage, not the data folder, with the
  token sealed by an AES-GCM key in the Android Keystore that never leaves it
  (`port/android/host/host_steam_seal.c`, `HaloActivity.steamSeal`). A copied
  file opens on no other phone, nor on this one after an uninstall or a log
  out (the key is deleted). If the Keystore can't seal the token, it isn't
  written, and the sign-in lasts until the game closes. An earlier build's
  file in the data folder is moved and sealed on the first start.
- **Windows, Linux and macOS (native):** in the data folder's `steam` folder,
  readable by its owner alone (mode 0600, or an owner-only ACL on Windows).
- **Switch, PS5 and the guest builds:** in the data folder's `steam` folder.
- The password, the Steam Guard code, the token, the channel's keys and the
  message buffers that held any of them are wiped before they're freed.

## Removing and moving files

A staged folder's removal and its move into `mcc_maps` touch only paths that
`posix_mcc_share_path_safe` takes: no link from `mcc_maps`, `mcc_staging` or
`steamapps` down. A link in the tree is neither followed nor removed. Where
the no-replace rename is refused (Android's shared storage), the files move
one at a time, under the same check.

## From OpenCE

OpenCE took this client into its Android app's host (its PRs #669 and #670).
These of its changes are here:

- the chunked-answer length bound, manifests over HTTPS and their bounds;
- refused content servers passed over;
- the token never stored unsealed on Android, the Keystore seal, revoking at
  log out, and the wipes;
- no link followed when removing or moving;
- the download speed;
- the joiner's install from the Workshop.

Not taken: running the client in the host, and its screen changes (no QR
sign-in, the Mbed TLS crypto, previews decoded by Android).

## Tests

`tools/test_steam_client.py` builds the client natively with `-Wall -Wextra
-Werror` and checks everything that works offline:

- the crypto against known answers;
- the session file, desktop's and Android's (a stand-in for the Keystore);
- the chunk packings, chunked answers and manifests' bounds;
- removing and moving past links;
- the paths an item's files may take.

The sign-in, the downloads, the revoke and the joiner's install need Steam and
an account that owns MCC, so they're only tested on devices.
