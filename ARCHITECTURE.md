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

## The 0000 series, toward v1.0.0

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
