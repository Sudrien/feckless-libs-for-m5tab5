# ARCHITECTURE.md

What the feckless libraries are, and why they got that way. The rules
for writing a patch are in `CLAUDE.md`; this file is the record.
Entries are in the order they were written. Where a later one
contradicts an earlier one, the later one is what happened.

## Before this repository

Four repositories, each with its own tag: feckless-drivers-for-m5tab5,
feckless-graphics-handler-for-m5tab5, feckless-storage-handler-for-m5tab5
and feckless-abi-for-m5tab5. Most of the drivers and the graphics
handler came out of defeatist-music-player-for-m5tab5; the ABI pinned
the other two at exact tags. Every change crossed repositories: a fix in
a library, its tag, the ABI's tag naming it, then a lock bump in each
app. feckless-graphics-handler v0.2.8 was tagged without the change it
was for, and cost a round on a Tab5 (Duplicitous's 0024). Their
histories stay in the archived repositories.

## The 0000 series, toward v0.1.0

- **0001** The repository: README, LICENSE, CLAUDE.md, this file.
- **0002** feckless_drivers, imported unchanged from
  feckless-drivers-for-m5tab5 v0.5.10 (dad9e7a) into
  `components/feckless_drivers`.
- **0003** feckless_graphics_handler, imported unchanged from
  feckless-graphics-handler-for-m5tab5 v0.2.9 (2c869be).
- **0004** feckless_storage_handler, imported unchanged from
  feckless-storage-handler-for-m5tab5 77df253: v0.1.0, which every app
  pins, and the rename to `-for-m5tab5` after it, which changed only
  names and urls.
- **0005** feckless_abi, imported unchanged from feckless-abi-for-m5tab5
  v0.2.5 (94a9206).
- **0006** One release: every manifest at 1.0.0 with this repository's
  url, siblings by name only. feckless_abi's manifest no longer pins the
  drivers and the graphics handler by their old git urls; the app names
  each component at the same tag, and feckless_abi's CMakeLists.txt
  fails the build when a sibling's version is not its own. The
  usb_host_uac pin moves from feckless_abi to feckless_drivers, which
  requires it. READMEs' install snippets name the new url and `path:`.
  No code changes: exports.txt's names are as they were, so it is ABI v1
  still, and an app built against feckless-abi v0.2.5 loads on a
  launcher built from this.
- **0007** Workflows: the tag workflow, from the storage handler's,
  setting all four manifests' versions at once; and the host tests on
  every push, with a check that the four versions are one. Run here:
  micpcmtest 1470 checks, the graphics handler's 922, no failures.
- **0008** Built: 0006 under Duplicitous. Duplicitous's m5tab5/ at
  its main, with ESP-IDF release/v6.1 (9a97f6c, the build Lothesome's
  log shows), `IDF_COMPONENT_MANAGER=0` and these four components plus
  the registry ones' sources from GitHub as EXTRA_COMPONENT_DIRS: built,
  no errors. Its own manifest was not changed for it, which the manager
  being off makes moot; moving it here is Duplicitous's patch. With
  feckless_drivers' manifest set to 1.0.1, configure stopped at
  `feckless_abi: feckless_drivers is 1.0.1, feckless_abi is 1.0.0`.
  Not run on a Tab5.
  Tagged v0.1.0; the tag workflow set every manifest to 0.1.0 (e6ddd3d).
  0006's 1.0.0 was a guess at the version, not the version.

## The 2000 series, toward v0.2.0

- **2001** The docs at v0.1.0: the install snippets, the history notes
  and exports.txt's comment said v1.0.0. CLAUDE.md's numbering opens
  this series, numbered as the apps' are.
- **2002** feckless_network_handler, imported unchanged from
  feckless-network-handler-for-m5tab5 v0.2.2 (e4ef6e5), which aimless
  and defeatist pin, into `components/feckless_network_handler`. It
  requires none of the other four.
- **2003** feckless_network_handler on the one version: its manifest at
  0.1.0 with this repository's url, so the next tag sets it with the
  rest; its README's snippet at `path:` and v0.2.0, the first tag that
  has it; the README's table and CLAUDE.md name five components; its
  host tests in test.yml. No code changes. Run here: the network
  handler's host tests, 46 checks, and the drivers' and graphics
  handler's as before, no failures. Not built for the P4: its code is
  v0.2.2's, which aimless builds.
- **2004** feckless_camera, imported unchanged from
  feckless-camera-for-m5tab5 v0.1.0 (44ad3dc), which Unprofitable
  Mahjong pins, into `components/feckless_camera`. It takes the I2C bus
  as an argument (`camera_init(tab5io_bus())`) and requires no other
  component; esp_video and what it brings stay out of any build that
  does not name it.
- **2005** feckless_camera on the one version: its manifest's url is
  this repository's (its version was already 0.1.0); its README's
  snippet at `path:` and v0.2.0; the README's table and CLAUDE.md name
  six components, and the README says what it needs at run time -- the
  bus, lcd_init() first, and on P4 silicon before v3 an esp_ipa the app
  supplies. No code changes; no host tests to run. Not built here:
  esp_video 2.5.0 and what it brings were not fetched. Its code is
  v0.1.0's, as Mahjong builds it.
