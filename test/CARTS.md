# External PICO-8 carts as integration tests

pico-386 ships a small matrix of real PICO-8 cartridges that exercise the cart
loader, the PXA decompressor, and the bytecode compiler end-to-end inside the
QEMU/FreeDOS test harness. Carts are **never** committed to this repo.

## Layout

```
test/carts.manifest        TSV of permissively-licensed carts (URL + sha256)
script/fetch_carts.sh      Downloader / verifier (writes to cache/carts/)
cache/carts/               Local cart cache (gitignored)
```

`cache/` and the broader `*.p8.png` glob are in `.gitignore`, so downloaded
carts cannot accidentally be committed.

## Running the matrix

```sh
nix develop
./test.sh carts
```

The `carts` mode:

1. Builds `MAIN.EXE` once.
2. Calls `script/fetch_carts.sh` to populate `cache/carts/` from
   `test/carts.manifest`.
3. For each cart, builds a FreeDOS floppy with `MAIN.EXE <STEM>.P8`, boots it
   under QEMU, and grades the serial log against the same heuristics as
   `./test.sh integration` (serial init, cart load, Lua decompression,
   compiler outcome).
4. Optionally walks `$LEXALOFFLE_CARTS_DIR` for local-only proprietary carts.

Individual carts can still be run via the original mode:

```sh
CART_URL=... CART_NAME=MYCART ./test.sh integration
```

## Adding a permissive cart

Only add carts whose license clearly allows redistribution of the URL we link
to (MIT / Apache-2.0 / CC-BY / public domain / etc.). Good sources to vet:

- **picotool** — <https://github.com/dansanderson/picotool> (MIT). The
  `tests/testdata/` carts are bundled solely for testing tooling like ours.
- **shrinko8** — <https://github.com/thisismypassport/shrinko8> (MIT).
- **fake-08** — <https://github.com/jtothebell/fake-08> (MIT).
- **retro8** — <https://github.com/Jakz/retro8> (GPL-2.0; check before use).
- **Celeste Classic** source (Maddy Thorson / Noel Berry) — sometimes vendored
  by the above projects with permission; verify the upstream license before
  pinning.

For each new entry append a TSV row to `test/carts.manifest`:

```
<DOSNAME>\t<url>\t<sha256>\t<license>\t<source>
```

- `DOSNAME` must be 8 chars, `[A-Z0-9_]`, unique. It becomes `DOSNAME.P8` on
  the floppy.
- Pin `<url>` to a commit SHA when fetching from GitHub (`raw.githubusercontent.com/<owner>/<repo>/<sha>/...`)
  so future repo edits cannot silently change the cart.
- Use `-` for the hash on first add, then run
  `script/fetch_carts.sh --print-hashes` and paste the real sha256 back into
  the manifest.

## Lexaloffle / proprietary BBS carts

Cartridges hosted on <https://www.lexaloffle.com/bbs/> are generally
**proprietary**: the default license forbids redistribution, even though the
files are publicly fetchable. Do not add them to `test/carts.manifest`.

To run the matrix against your own local copy:

```sh
mkdir -p ~/pico8-carts
# drop *.p8.png files in there, e.g. via PICO-8's File > Export
LEXALOFFLE_CARTS_DIR=~/pico8-carts ./test.sh carts
```

The runner uppercases each filename, strips it to an 8-char DOS stem, and
feeds it through the same harness. Nothing about that path is checked in.

## Useful one-liners

```sh
script/fetch_carts.sh --list           # show manifest entries
script/fetch_carts.sh                  # download + verify
script/fetch_carts.sh --check          # verify cache against manifest
script/fetch_carts.sh --print-hashes   # emit lock-ready TSV rows
script/fetch_carts.sh --clean          # nuke cache/
```
