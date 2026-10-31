# CLAUDE.md

How to work on this repository. Read it before writing a patch.

Ten ESP-IDF components for the M5Stack Tab5, in `components/`, released
together under one tag. `README.md` says what each is.

## Patches

Claude creates `git am`-able patches authored as
`Claude <noreply@anthropic.com>`, and presents each one as soon as it
exists -- a session can end with patches stuck behind it.

Claude must not commit to, push to, branch on, or open a pull request
against the repository. The URL a session is given is for pulling only.
Local commits made to run `git format-patch` stay local.

**Patches are cumulative.** Each applies on top of what is already here,
including patches handed over and not yet pushed. Never hand back a
rewritten copy of a file, and never reissue a corrected version of a
patch that has been handed over: send a follow-up, with the next number,
that changes what needs changing. A patch already handed over is
history.

**A patch has one number, and it is written down once.** The number
runs on from patch to patch across sessions. It is supplied by
`--start-number`, never put in the subject:

    git format-patch --start-number 9 -1 -o out/

gives `0009-<subject>.patch`. Write the subject as the subject.

**Before choosing a number, look at the last one used:** the newest
entry in the series at the end of `ARCHITECTURE.md`, which every patch
adds itself to. Take the next one.

**Where the numbering is now: the 7000 series, toward v0.7.0.** A
series is named for the release it leads to -- the 0000s toward v0.1.0,
the 2000s toward v0.2.0, the 3000s toward v0.3.0, the 4000s toward
v0.4.0 (and 4003-4006, which came after its tag, in v0.4.1), the 5000s
toward v0.5.0, the 6000s toward v0.6.0 (and 6005-6006, in v0.6.1 and
v0.6.2), the 7000s toward v0.7.0 -- so a patch number
says which release it went into. A point release does not start a
series. Since v0.2.0 every feckless library is here; 1.0.0 waits on the
ABI settling, with file I/O in it. **The first patch after a tag updates this
paragraph** and opens the next series' heading in `ARCHITECTURE.md`.

**What a patch's `ARCHITECTURE.md` entry says:** its number and
subject, one or two lines of what it does, how it was built or tested,
and -- once a board has run it -- what the log showed.

## The components

- **One tag for all of them.** The `version:` in every
  `components/*/idf_component.yml` is the same, and the tag workflow
  sets them together. `feckless_abi` checks its siblings' versions
  against its own at configure time. **A new component starts at the
  `version:` its siblings have when it is written**, and is checked
  again before handing over: 4003-4005's three were written at 0.3.0,
  landed after v0.4.0's bump, and failed the "One version" check until
  v0.4.1 set them.
- **Siblings by name only.** A component names another in its
  `CMakeLists.txt` (`REQUIRES feckless_drivers`), never as a git
  dependency in its manifest: a component cannot name the tag it is
  about to be. The app's manifest lists every component it uses.
- **Registry dependencies are pinned** in the manifest of the component
  that uses them -- except an Espressif component this repository
  carries a fixed copy of (usb_host_msc, usb_host_uac): that one is
  required by name, named by the app, and the requiring component warns
  at configure time when it is not ours.
- **The Espressif registry is not reachable from a session.** A session
  can build by fetching the registry components' sources from GitHub
  (esp-bsp, esp-iot-solution, esp-usb) into a scratch directory and
  building with `IDF_COMPONENT_MANAGER=0`; a patch says whether it was
  built that way, or not at all.
- **Host tests:** `make -C components/<name>/test` for
  feckless_drivers, feckless_graphics_handler,
  feckless_network_handler, feckless_nvs_handler and
  feckless_image_handler.
- **`fatfs` is IDF's, patched at configure time** (3002). Its edits are
  `components/fatfs/tools/patch_fatfs.py`, checked in CI against the
  IDFs the Tab5 projects build with; a new IDF whose FatFs it refuses
  is a patch to that script, never a vendored copy of FatFs.

## The ABI

`components/feckless_abi/exports.txt` is what an app's ELF may take from
its launcher, and its README says what is compatible. Adding a name is a
minor release. Removing one, or changing a signature or a struct an
exported function takes, in any component the ABI exports, is a new ABI
version: `FECKLESS_ABI_VERSION` and `FECKLESS_ABI_SYMBOL` together, in a
major release. The repository's tag and the ABI version are separate
numbers.

**Sections** (3007) are export lists a launcher gives only if it links
their component: `components/feckless_abi/sections/<name>.txt`, each with
its `@symbol`, and `components/feckless_abi_<name>`. A section's version
is its own, by the same rules. The storage section (3008) also pins the
FatFs configuration both sides build with; a change to that list is a
new storage version.
