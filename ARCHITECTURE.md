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
  Tagged v0.2.0 (2edea9f). The apps moved to it: Duplicitous 0030,
  Lothesome 0047, Mahjong's, Aimless 0040, Defeatist 8060 (Aimless 0039
  and Defeatist 8059 first moved their firmware into m5tab5/). Those
  builds, with ESP-IDF release/v6.1 and the component manager off, are
  the first P4 builds of feckless_network_handler (Aimless 0x1bef10
  bytes, Defeatist 0x2ec3d0) and feckless_camera (Mahjong 0x13ffa0, with
  esp_video 2.5.0) from this repository. None yet on a Tab5.

## The 3000 series, toward v0.3.0

- **3001** CLAUDE.md opens this series, and says what 1.0.0 waits on now
  that every library is here. feckless_camera's README links its
  siblings in this repository, not their archived ones.
- **3002** fatfs: ESP-IDF's fatfs with exFAT, as a component of its own
  name, so it stands in for IDF's (a component added later overrides
  one of the same name; managed components come after IDF's). At
  configure time its CMakeLists.txt copies the installed IDF's
  components/fatfs into the build directory, runs tools/patch_fatfs.py
  over the copy, and includes IDF's CMakeLists.txt from it with
  COMPONENT_DIR pointing there; its Kconfig sources IDF's, and its
  project_include.cmake includes IDF's. patch_fatfs.py is Defeatist's
  tools/enable_exfat.sh's three edits, moved into Python unchanged: on
  release/v6.1's FatFs its output is byte for byte the script's.
  CONFIG_FECKLESS_FATFS_EXFAT=n (default y) builds IDF's unpatched, in
  place of TAB5_NO_EXFAT. CI patches v5.5.5's and release/v6.1's FatFs
  from a sparse clone. Built in Duplicitous with ESP-IDF release/v6.1,
  the component manager off, its cmake/exfat.cmake and components/fatfs
  taken out: `fatfs (feckless): exFAT, from .../components/fatfs`, ff.c
  compiled from build/feckless_fatfs, libfatfs.a with create_xdir,
  load_xdir_checked and a weak ff_tab5_bad_entry that the storage
  handler's replaces; duplicitous.bin 0x135040 bytes, 16 more than with
  its own copy. With the option off: IDF's ff.c, no exFAT, 0x132770.
  The same with ESP-IDF v5.5.5: `fatfs (feckless): exFAT, from
  .../idf55/components/fatfs`, that IDF's FatFs patched and built,
  0x138880 bytes -- one component, two IDFs' FatFs. Not on a Tab5.
- **3003** usb_host_msc, imported unchanged from Defeatist's
  m5tab5/components/usb_host_msc (c4be9ad): Espressif's 1.3.0 with
  Defeatist's 5035 and 6031 (transfers cut to what the Tab5's DMA memory
  can give), each marked "Vendored change". Identical to Duplicitous's
  copy. Apache-2.0, with its LICENCE.
- **3004** usb_host_msc on the one version: its manifest at 0.2.0 with
  this repository's url, so the tag sets it with the rest, saying in a
  comment it was modified from Espressif's 1.3.0; esp-usb's url and
  repository fields go with that. feckless_storage_handler warns at
  configure time when the usb_host_msc it gets is not this one. The
  READMEs say to name it; CLAUDE.md says eight components. No code
  changes.
- **3005** usb_host_uac, imported unchanged from Defeatist's
  m5tab5/components/usb_host_uac (c4be9ad): Espressif's 1.5.0 with
  Defeatist's 5026 (stop once), 5032 and 6062 (its rings in PSRAM,
  freed as made), each marked "Vendored change" in uac_host.c.
  Apache-2.0, with its LICENSE.
- **3006** usb_host_uac on the one version: its manifest at 0.2.0 with
  this repository's url, a comment saying it was modified from 1.5.0.
  feckless_drivers' manifest no longer pins espressif/usb_host_uac 1.5.0
  -- both at once would be two components for one name -- so every
  project on the drivers names usb_host_uac from here; the drivers warn
  at configure time when the one they get is not this one. The READMEs
  and CLAUDE.md say so. No code changes. Built: Duplicitous with
  ESP-IDF release/v6.1 and the component manager off, its own
  components/usb_host_msc, components/fatfs and cmake/exfat.cmake taken
  out, with fatfs, usb_host_msc and usb_host_uac from here: no warning,
  duplicitous.bin 0x135330 bytes. With its own usb_host_msc put back,
  configure warned `feckless_storage_handler: usb_host_msc is
  .../components/usb_host_msc, not feckless-libs'`. Not on a Tab5.
  esp_ipa, Mahjong's fixed copy, is not brought in: esp_video requires
  espressif/esp_ipa 2.4.0 from the registry, and a git dependency of
  the same name is not known to override that -- a project's own
  components/esp_ipa does. Mahjong keeps its copy until a registry
  esp_ipa has the less-than-v3 libraries.
- **3007** feckless_abi: sections. Some exports are not every
  launcher's to give -- storage, for one: Lothesome's standalone .bin is
  a launcher for its own ELF and links none. A section is a list of its
  own, sections/<name>.txt, with an "@symbol" of its own, given by a
  component feckless_abi_<name> that a launcher links only if it has
  what it exports. feckless_abi_run() registers each section's table,
  found through a weak reference, beside the core one. abicheck.cmake
  reads every section: an app's import may be in exports.txt or a
  section's list, and an app taking a section's names must refer to its
  symbol, so a launcher without the section refuses the app by that
  name. The table writer moved into cmake/abitable.cmake, shared with
  the sections; the core table it writes has the same entries as before
  (one declaration more). No names added to v1: still ABI v1, and an
  app built before this loads on a launcher built after, and the other
  way round. Built: Lothesome with ESP-IDF release/v6.1 and the
  component manager off, `feckless_abi: 85 symbols, all in
  feckless_abi_v1`, as before.
- **3008** The storage section: feckless_abi's sections/storage.txt and
  the component feckless_abi_storage, which a launcher links to give it
  -- feckless_storage_handler's API but its two inits, ff.h's calls but
  f_mount, f_mkfs and f_fdisk, and libc's and POSIX file I/O, 92 names
  under feckless_abi_storage_v1. It requires feckless_abi, the storage
  handler and fatfs; WHOLE_ARCHIVE, since only feckless_abi_run()'s
  weak reference names its table. Both sides are held to one FatFs
  configuration -- this repository's fatfs with exFAT, long names,
  UTF-8, 4096-byte sectors, a per-file cache in a dynamic buffer, no
  fast seek, no locks -- since an app's FIL, FF_DIR and FILINFO are laid
  out by its own build. Built with ESP-IDF release/v6.1 and the
  component manager off: Duplicitous linking it, every name resolved,
  0x137280 bytes (0x1f50 more); a test app taking fopen, fgets, f_open,
  f_close and storage_present, `feckless_abi: 9 symbols, all in
  feckless_abi_v1 + feckless_abi_storage_v1`; the same without
  feckless_abi_require_storage(), refused and the ELF deleted (`takes
  names from the storage section but does not refer to
  feckless_abi_storage_v1`); with f_mkfs, refused as outside the ABI;
  with CONFIG_FECKLESS_FATFS_EXFAT=n and CONFIG_FATFS_FS_LOCK=4,
  refused at configure, naming both. Not on a Tab5.
  Tagged v0.3.0 (9806e21).

## The 4000 series, toward v0.4.0

- **4001** CLAUDE.md opens this series; the README's example names
  v0.3.0, not v0.1.0.
- **4002** The launcher is Avoidant (Duplicitous until its 0032) and the
  map Errant Moving Map (Aimless until its 0042): the storage section's
  README and two comments in the network handler say so. Earlier
  entries keep the names they had. Comments and documentation only.
- **4003** feckless_nvs_handler: the NVS partition a value at a time.
  `nvsh_init()` brings it up once, erasing a partition that is full or
  of a newer format; `nvsh_get_*()`/`nvsh_set_*()` for blobs, strings,
  u8, u32 and i32, and erasing a key or a namespace, each opening,
  committing and closing for itself, so no `nvs_handle_t` crosses the
  API -- the shape an ABI section can export. From Unprofitable
  Mahjong's save.c, which used NVS directly. Built with ESP-IDF v6.1 and
  the component manager off, in Mahjong (its 0100): no warnings. Not on
  a Tab5.
