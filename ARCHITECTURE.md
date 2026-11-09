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
- **4004** The nvs section: feckless_abi's sections/nvs.txt and the
  component feckless_abi_nvs -- feckless_nvs_handler's API but
  `nvsh_init()`, 13 names under feckless_abi_nvs_v1, the launcher
  bringing NVS up as it does the board. As the storage section:
  WHOLE_ARCHIVE, its table weak in feckless_abi_run(). Built with ESP-IDF
  v6.1 and the component manager off: Unprofitable Mahjong's ELF
  (its 0101) taking nine of them, `feckless_abi: 78 symbols, all in
  feckless_abi_v1 + feckless_abi_camera_v1 + feckless_abi_nvs_v1`, and
  its standalone .bin linking the section. Not on a Tab5.
- **4005** The camera section: feckless_abi's sections/camera.txt and
  the component feckless_abi_camera -- feckless_camera's whole API, six
  names under feckless_abi_camera_v1; `camera_init()` is the app's, as
  the board's inits are, and already a no-op the second time. The
  camera runs under the launcher's build, so the launcher carries its
  configuration; configure warns without the sensor and the ISP's
  controller. Built as 4004, in the same Mahjong build. Not on a Tab5.
- **4006** `__assert_func` and `rintf` in exports.txt, libc's and
  libm's: vendored libraries' assert() and rounding, quirc's and
  qrcodegen's in Unprofitable Mahjong's ELF, which they failed the
  check on. Names added, so a minor release: still ABI v1. Built as
  4004. Not on a Tab5.
  Tagged v0.4.0 (98d7852) before 4003-4006 were pushed, so they are in
  v0.4.1 (74a88d2), a point release.

## The 5000 series, toward v0.5.0

- **5001** CLAUDE.md opens this series. And 4003-4006 on a Tab5, with
  Unprofitable Mahjong (its 0100, 0101) at v0.4.1. Its standalone .bin,
  which links both sections and runs its embedded ELF: the board up
  from inside the ELF, `ui: resumed a saved game` (nvsh_get_blob()),
  `sc202cs: Detected Camera sensor PID=0xeb52` and `camera: sensor
  found, /dev/video0` at boot, and `camera: 1280x720 UYVY, 2560 bytes a
  line` when Scan a code opened. Then its .elf through Avoidant (its
  0035-0037): a game in progress restored after a reboot (NVS both
  ways), and a replay code read off the camera and decoded by quirc
  inside the ELF (4006's rintf and __assert_func). Documentation only.
- **5002** feckless_abi: 45 names more in the core list -- libc's
  (strspn, strdup, strftime, gmtime_r, time, gettimeofday, the ctype
  table...), libm's, libgcc's `__clzdi2`, FreeRTOS's stream buffers,
  static creation, priorities and multi-core critical sections, and
  `esp_fill_random`, `esp_rom_delay_us`, `esp_log_level_get` and heap
  reporting. What Spurious StarDict Reader, Errant Moving Map and
  Defeatist Music Player call that is neither a library of their own
  nor a section's: listed from each one's build with ESP-IDF v6.1, by
  the undefined symbols of its main and vendored components. Not
  `errno`, which is thread-local on IDF 6.1 and cannot be in the table
  (`__errno` is, already). Names added: still ABI v1. Built: Avoidant
  linking the table, every name resolved, 0x178100 bytes (0x30a0 more).
  Not built on 5.5.
- **5003** The storage section's version 2: fast seek. Both sides now
  build with CONFIG_FATFS_USE_FASTSEEK=y, so an app's FIL has a cltbl and
  it can map a large file's clusters (Errant's ffread.c, Spurious's
  sd_io_fatfs.c), and the section is feckless_abi_storage_v2. FIL is
  laid out differently, so a v1 app is refused by name before it runs;
  no app published so far takes storage. fprintf and vfprintf join the
  list. Built: Avoidant without the option, refused at configure
  (`CONFIG_FATFS_USE_FASTSEEK=y (FIL's cltbl, storage_v2)`); with it,
  0x1784a0 bytes, feckless_abi_storage_v2 in the link. Not on a Tab5.
  Tagged v0.5.0 (c6c4bc6).

## The 6000 series, toward v0.6.0

- **6001** CLAUDE.md opens this series, and says a new component starts
  at its siblings' version. Documentation only.
- **6002** feckless_abi: GPIO, UART and I2C devices in the core list,
  with reading partitions and the MAC -- 33 names, what Errant's GNSS,
  IMU and Launcher import call and every launcher links anyway. IDF 6's
  `uart_set_pin()` is a macro calling `_uart_set_pin6()`; feckless_abi.c
  supplies that on 5.5. Names added: still ABI v1. Built: Avoidant,
  0x17e160 bytes (0x5ca0 more, mostly the UART driver). Not on 5.5.
- **6003** feckless_network_handler: `wifi_psk()`, the WPA2 PSK for a
  passphrase -- PBKDF2-HMAC-SHA1, through PSA on Mbed TLS 4 and pkcs5 on
  3.6 -- from Defeatist's and Errant's wifijoin.c, so their ELFs need
  not reach Mbed TLS for it. Built in Errant (its 0043) with ESP-IDF
  v6.1, no warnings. Not on a Tab5.
- **6004** The network section: feckless_abi's sections/network.txt and
  the component feckless_abi_network -- the network handler's API
  (`wifi_init()` and `ethernet_init()` the app's), lwIP's sockets,
  esp_netif and esp_wifi_get_mac, the HTTP server with WebSockets, the
  HTTP client and the certificate bundle: 119 names under
  feckless_abi_network_v1, Errant's and Defeatist's, Mbed TLS and the
  HTTPS server aside. Both sides held to one configuration where an app
  lays out what the launcher reads: the URI length, WebSockets, the
  client's TLS options, IPv4 and IPv6. Built: Errant Moving Map as an
  ELF (its 0043), `feckless_abi: 212 symbols, all in feckless_abi_v1 +
  feckless_abi_network_v1 + feckless_abi_storage_v2`; its standalone
  .bin linking the section, 0x1edec0 bytes. Not on a Tab5.
- **6005** feckless_network_handler: `feckless_net_start()`, the network
  brought up by the board's owner, as storage is -- the saved networks,
  the USB configuration selector given `ethcfg_select()`, the owner's USB
  hook, `wifi_init()`, `ethernet_init()`, before the port comes up and
  powering nothing. `feckless_net_set_hooks()` may now be called after,
  for an app's policy: two buffers and an atomic pointer, and a NULL
  `usb_register_class` keeps the owner's. `wifi_init()` and
  `ethernet_init()` return at once the second time, so an app built
  before this still runs. Not exported: the network section gains only
  `feckless_net_started`, and stays network_v1. From Errant's RTL8152,
  which met no configuration selector because each app set its own:
  Errant's 0046 and Avoidant's 0043. Built: Errant as an ELF,
  `feckless_abi: 208 symbols` (its bring-up gone); Avoidant 0x21d180.
  Host tests pass. Not on a Tab5.
- **6006** feckless_network_handler: hooks set after a network is up
  catch up with it. *6005 on a Tab5*, Avoidant's 0043 with Errant's
  0046: `feckless_net: network up to be asked for: 0 saved networks,
  Ethernet registered` at boot, the Ethernet class installed before the
  port (`class driver eth installed`, then `VBUS on`), the RTL8152 asked
  for its CDC-ECM configuration (`0bda:8152: asking for configuration
  2`) and up on DHCP (`ecm: address 192.168.1.125 ... it is the default
  route`) before any app; Errant's .elf on it, an HTTPS request over
  the cable through the network section, answered (`range 0+127: HTTP
  404` -- the tile build it remembered is gone from the server). And no
  SNTP: the cable came up with no hooks, ntp_enabled() unanswered, and
  nothing asked again once Errant set its policy, so `no date yet (GNSS
  or SNTP)`. feckless_net_set_hooks() now starts SNTP if wanted, and
  calls online(), when a network is already up. Built: Avoidant.
  Not on a Tab5.

## The 7000 series, toward v0.7.0

- **7001** feckless_nvs_handler: `nvsh_migrate()`, a migration submitted
  as data. An app or firmware that moved its values between namespaces
  (Defeatist's 6006: three namespaces into one) wrote its own copy-and-
  erase, walking NVS with an iterator, which an ELF cannot. Now it hands
  over a table -- old namespace, key, type (blob, string, u8, u32, i32)
  -- and the new namespace: a key already there is kept, a key absent is
  skipped, an old namespace is erased whole once all its listed keys are
  copied or kept, and a failed copy leaves that namespace for the next
  call. Exported in the nvs section (a name added: nvs_v1 and ABI v1
  stand). Defeatist's nvsns.c (its 8066) is the first user and becomes a
  table. Built: `make -C components/feckless_nvs_handler/test` runs
  nvs_handler.c itself against an in-memory NVS under ASan and UBSan, 66
  checks: nothing to move writes nothing; the whole move; kept keys of
  any type; a first-listed key winning over a later namespace's; a
  failed write leaving its namespace and the next call finishing it; a
  key of another type than listed failing that namespace only; bad
  arguments refused before anything is touched. Not built against
  ESP-IDF (none in the session), not on a Tab5. Adds `heap` to the
  component's PRIV_REQUIRES for esp_heap_caps.h.
- **7002** feckless_abi: `esp_ota_set_boot_partition` in the core list.
  Defeatist's "exit to Launcher" sets the factory slot to boot and
  restarts when it runs from another slot, and only restarts when it is
  an ELF in the launcher's own (its 8067): one source for both, which
  needs the call named in the ELF though an ELF never makes it, because
  the build's check fails on any undefined symbol not in this list. A
  name added: still ABI v1, a minor release. Every firmware that builds
  the table already links `app_update` (feckless_abi requires it, 6002),
  so nothing to add to a launcher or a standalone shim. It lets an ELF
  choose the next boot slot, as it can already read the partitions and
  restart; the apps this loads are code the owner chose to run. Not
  built (no ESP-IDF in the session): exports.txt's format is one name a
  line, and the name is IDF's own `esp_ota_ops.h`. Not on a Tab5.
- **7003** feckless_abi: four names Defeatist's ELF leaves undefined,
  from a scan of what its main/ calls against exports.txt and the
  sections (no ESP-IDF in the session, so a scan of the source, not the
  linker's list): `esp_deep_sleep_start` (core: the sleep page),
  `heap_caps_check_integrity_all` and
  `heap_caps_register_failed_alloc_callback` (core: heapmap.c's
  diagnostics) and `httpd_resp_send_404` (network section, beside
  `httpd_resp_send_err`). Names added: ABI v1 and the network section's
  v1 stand. Each is a real function in its IDF header; none is a macro or
  an inline, which would not be undefined in an ELF and would be a name
  the launcher's table cannot take the address of. Left out on purpose,
  for the ELF build to settle (its undefined list is the authority, and
  a wrong name here breaks every launcher's link): `httpd_resp_sendstr`
  and `heap_caps_print_all_task_stat_overview` (could not tell a function
  from an inline), and the macros whose targets are exported already
  (`xSemaphoreTake` to `xQueueSemaphoreTake` and the rest of FreeRTOS,
  `inet_addr` to `ipaddr_addr`, `portENTER_CRITICAL`). Not built, not on
  a Tab5.

  The scan's other gaps need a decision, not a name: the audio codec
  (`esp_audio_simple_dec_*`, `esp_audio_dec_register_default`) and the
  hardware JPEG engine (`jpeg_new_decoder_engine`, `jpeg_alloc_decoder_mem`,
  `jpeg_decoder_get_info`, `jpeg_decoder_process`) are host singletons and
  want sections of their own, as camera has one; TLS (`httpd_ssl_*`,
  28 mbedtls and 13 psa names, certgen.c) wants either a section or
  linking mbedtls into the ELF, which would be a second copy beside the
  host's; `usb_serial_jtag_*` is the console setup, which belongs in the
  shim, not the ABI.
- **7004** feckless_image_handler: the pure half of image loading, moved
  from Defeatist. Image loading is generic enough for feckless -- every
  app that shows a picture asks the same questions, and an ELF on the
  ABI should not each carry its own JPEG, PNG, JPEG XL and GIF handling
  or its own claim on the one hardware JPEG engine -- so the whole of it
  is moving in stages: this patch is the part with no ESP-IDF in it;
  the decoders (the engine with its software fallback, PNG, JPEG XL),
  the fit-and-draw into gfx's framebuffer and an ABI section for them
  are the patches after it, and Defeatist's adoption of each comes last
  (its albumart.c keeps the ID3 and container parsing, the cover cache,
  the hashing and the keystone/offset taste; a cover is Defeatist's, a
  picture is feckless'). In this patch: `img_sniff()`/`img_mime()` (the
  magic bytes; was artformat.h's `artformat_sniff`/`_mime`, with
  `IMG_NONE/JPEG/PNG/JXL/GIF`), `img_jpeg_sof()` (was
  `artformat_jpeg_sof`; a truncated SOF0 still reads baseline with size
  0, as it did), `img_int_scale()` (was `art_int_scale` in albumart.c,
  with its 256-pixel bound), and `img_ico_is_ico()`/`img_ico_to_png()`
  (was icoimg.c, whole; a DIB or PNG entry out as a PNG whose deflate is
  stored blocks). Moved as they were, renamed only. `static inline` for
  the first four, `src/image_ico.c` for the icon, whose one IDF call is
  the allocator (PSRAM on the target). Version 0.6.2, as every sibling
  must be. Built: `make -C components/feckless_image_handler/test`
  runs the header and image_ico.c as they are under ASan and UBSan, 48
  checks: every format and its short, null and wrong-magic forms; the
  MIME types; the SOF walk for baseline, extended, progressive,
  arithmetic, SOS-before-SOF, no SOI, truncated and a bad segment
  length; the integer scale at its edges; an icon's 32-bit DIB
  converted and unwrapped again (CRCs, Adler-32, stored blocks, the
  pixels and their alpha), a 24-bit one, an embedded PNG copied
  unchanged, and the refusals (offsets and sizes past the end, a cursor,
  too large). Defeatist's own tests of these (arttest, which also runs
  its decoders) are unchanged until its adoption patch. Not built
  against ESP-IDF (none in the session), not on a Tab5. Nothing uses the
  component yet, so nothing links it.
- **7005** feckless_image_handler: the software decoders, moved from
  Defeatist's `components/stbjpeg` and `components/jxlimg`. stb_image
  (JPEG, which it reads progressive and at any sampling, and the first
  frame of a GIF) and J40 (JPEG XL) each decode to native RGB565,
  thinned by whole steps to the smallest size that covers the box,
  with a header read first (`img_stb_info`, `img_jxl_info`) and a peak
  estimate (`img_stb_peak_bytes`, `img_stb_gif_peak_bytes`,
  `img_jxl_peak_bytes`) so a caller can ask free memory before it
  asks for a picture. Moved as they were; renamed `stbjpeg_*` to
  `img_stb_*`, `jxlimg_*` to `img_jxl_*` and their enums likewise
  (`IMG_STB_OK`, `IMG_JXL_UNSUPPORTED`), in `feckless_image_stb.h` and
  `feckless_image_jxl.h`, `src/image_stb.c` and `src/image_jxl.c`. The
  two single-header libraries are fetched at configure time into
  `vendor/` (gitignored) by `cmake/vendored.cmake`: the same pins and
  SHA256s as Defeatist's (stb_image v2.30, J40 2270), the same
  `vendored_fetch()` with its error message pointed at this directory.
  Compile options carry over per source file: `-fwrapv` and upstream's
  warnings off for both, `-Os` and the RISC-V `int32_t` mismatches off
  for J40. Tests moved with their fixtures (small binaries, 164 KB; the
  patch is `format-patch --binary`): `stbjpegtest` (JPEG: baseline,
  progressive, SOF1, greyscale, thinning, the 5000x5000 header refused,
  truncation, and 9000 damaged files), `jxltest` (lossy, lossless,
  alpha, a container, split `jxlp` boxes; the four kinds J40 refuses,
  each kept so a newer J40 shows; the size header at 13 shapes; every
  file cut short), and `gifimgtest` (GIF87a, interlaced, transparent,
  animated; the PNG and pngle checks of Defeatist's arttest stay there
  until PNG moves). Built: `make -C components/feckless_image_handler/
  test` runs all four under ASan and UBSan -- 48, 9018, 35 and 105
  checks, no failures -- against stb_image.h and j40.h fetched at their
  pins (checked here against the SHA256s in this file). The three
  sources also compiled on the ESP_PLATFORM path (the allocator through
  heap_caps_*) against a stub header, at `-O2 -Wall -Wextra` with
  IDF's `-Wno-unused-parameter`. Not built against ESP-IDF (no IDF, and
  the registry is not reachable), not on a Tab5. Nothing links these
  yet: Defeatist keeps its own copies until its adoption patch, which
  also moves its gifstacktest (the stack the GIF decode needs, run on a
  thread of exactly that size) to these sources. Next in the
  component: PNG (pngle's streaming into a buffer), then the hardware
  JPEG engine with the fallback order, an `img_decode()` over all of
  them, and the draw and ABI section.
- **7006** feckless_image_handler: PNG, through pngle, to an RGB565
  buffer. Defeatist drew a PNG by streaming pngle's runs into the panel's
  framebuffer, fitted to the screen (its `albumart_draw_png()`), which
  knows the panel and cannot be shared; this is the same decode with a
  buffer for the destination, answering as `img_stb_decode_rgb565()` and
  `img_jxl_decode_rgb565()` do: `img_png_info()` (the IHDR, read
  directly), `img_png_peak_bytes()`, `img_png_decode_rgb565()`,
  `img_png_err_name()`, in `feckless_image_png.h` and `src/image_png.c`.
  pngle still streams, so the RGBA picture never exists (a 1400x1400
  one would be 7.8 MB); what is kept is the RGB565 result, thinned by the
  same whole-step nearest neighbour (the output pixel at (i, j) is the
  source pixel at (i*step, j*step), so a run from pngle paints the
  outputs whose source lies in it, and an Adam7 pass's coarse blocks are
  overwritten by the later exact ones). Three things differ from the old
  path, deliberately: a half-transparent pixel is blended over `bg`
  (JPEG XL's rule) where Defeatist drew it opaque; a fully transparent
  one is `bg` exactly, as it left the background; and the bound is
  `IMG_PNG_MAX_PIXELS`, 16 megapixels, in place of Defeatist's 100000 a
  side (which its scaler's 32-bit edge map needed, and which this has no
  edge map for). A truncated file gives the part that decoded and
  `IMG_PNG_OK`, as it did. Memory is the change to know: where the old
  path held nothing but the framebuffer, this holds the thinned result --
  a 1400x1400 cover into a 720 box is 3.9 MB of PSRAM, once, like the
  JPEG path. pngle and miniz are fetched at configure time into `vendor/`
  at Defeatist's pin (b1c6819), each file checked against its SHA256, by
  the same `cmake/vendored.cmake`; miniz.c compiles once as a source, as
  its one-line miniz.h shim intends. Tests: `pngimgtest` -- Defeatist's
  arttest PNG cases moved (RGB, RGBA, palette, palette with tRNS, grey,
  grey with alpha, 16-bit, Adam7), every file cut at every length, the
  blend against two backgrounds, thinning at steps 2, 1 and 5 (and the
  interlaced file at 3), the refusals (junk, junk after the signature,
  empty, NULL, a 5000x5000 and a 0x0 header) and 8000 damaged files; two
  fixtures from `test/gen_png_fixtures.py` (1500x1000, and a 2x1 for the
  blend). `stbjpegtest` gains Defeatist's arttest JPEG cases on its
  fixtures (baseline 4:2:0 and 4:4:4, progressive, greyscale, EXIF ahead
  of the SOF: the SOF walk, the quadrants, every prefix). Built: all
  five tests under ASan and UBSan -- 48, 9827, 35, 9347 and 105 checks,
  no failures (pngle's and miniz's own alignment and shift reports are
  masked as Defeatist's arttest masked them). image_png.c compiled on
  the ESP_PLATFORM path against a stub heap header, and pngle.c and
  miniz.c at `-O2 -Wall` with upstream's warnings off. Not built against
  ESP-IDF, not on a Tab5. Nothing links it yet; Defeatist's arttest
  keeps its PNG cases until its adoption patch deletes them with the
  rest.
- **7007** feckless_image_handler: `img_decode()`, one entry for every
  format, with the decoders behind an `img_ops_t` table. This is the
  order Defeatist's `albumart_draw()` followed and could only test on the
  board: a baseline JPEG goes to the hardware engine if it is ready, its
  header can be read, and the largest PSRAM block holds the MCU-padded
  result (stride is the padded width; there is no scaler); otherwise to
  TJpgDec from the smallest scale that covers the box; and only when
  TJpgDec refuses the format (`IMG_ERR_FAILED`), to stb_image. TJpgDec
  running out of memory stays `IMG_ERR_NO_MEM`, since stb needs more.
  A progressive JPEG goes to stb_image alone; any other SOF is
  `IMG_ERR_UNSUPPORTED`, with the marker in `img_pic_t`. GIF, PNG and JXL
  are refused before they start if PSRAM cannot hold their peak (JXL's
  includes its 24 KB stack), and `bg` is handed to those that blend.
  `img_pic_t.decoder` names who produced the picture, and `skipped` why a
  faster one was passed over. Every software decode is bracketed by
  `soft_begin`/`soft_end` (Defeatist drops to idle priority there); the
  hardware path is not. Deliberate change: a hardware failure to read
  the header or to allocate now falls back to software, where Defeatist
  gave up. A hardware buffer whose size or stride is not the padded one
  is freed and reported `IMG_ERR_FAILED`, not drawn sheared. The
  ESP backends (7008) fill the table; `decodetest` fills it with fakes:
  60 checks under ASan and UBSan, covering each step of the order, the
  memory refusals, NULL backends, and the pad and first-scale arithmetic.
- **7008** feckless_image_handler: the ESP backends for `img_decode()`,
  `feckless_image_esp.h` and `src/image_esp.c`: `img_esp_init()` (the
  hardware JPEG engine, made once and kept, as `albumart_init()` did) and
  `img_esp_ops()`. Moved from Defeatist's `albumart.c` with their reasons:
  the engine's DMA input and output buffers from `jpeg_alloc_decoder_mem()`
  (output padded to the MCU grid, the stride the padded width), BGR
  `rgb_order` and BT.601 stated (the byte scramble, not a colour order);
  TJpgDec through `esp_jpeg` with the 8 KB internal work buffer supplied
  (1104) and the scale ladder from `img_tjpgd_first_scale()`, not byte
  swapped; stb_image, pngle and J40 with their errors mapped onto
  `img_err_t`; GIF (`IMG_GIF_STACK`) and JXL (`IMG_JXL_STACK`) on a PSRAM
  stack with a static internal TCB, at idle priority (6147), and
  `soft_begin`/`soft_end` as the idle-priority drop (5093). New
  requirements: the component now REQUIRES `esp_driver_jpeg` and a
  manifest dependency on `espressif/esp_jpeg ^1.3.1` (a re-resolve of the
  project's lock), and a project must set `CONFIG_JD_USE_SCALE=y`. Not
  carried over: Defeatist's per-cover hash logging, which was a
  diagnosis of one reproducible file. Compile-checked against stubs with
  `-Wall -Wextra -Werror`; the engine, the stacks and the colour order
  are verified only on the board. The draw step and the ABI section
  follow.
- **7009** feckless_image_handler: fit to a box, `feckless_image_fit.h`
  and `src/image_fit.c`: the arithmetic of Defeatist's `blit_cover()`
  without the panel in it. `img_fit_size()` gives the size a picture takes
  in a box (cross-multiplied fit, both directions, never below 1x1) and
  the whole-number scale `k` when `img_int_scale()` chose one;
  `img_fit_draw()` writes it, nearest neighbour on a 16.16 step, exact
  blocks for a whole-number scale (5088), a memcpy when the step is 1.0,
  and the source's `stride` honoured (the hardware's MCU padding is never
  read). Where the result sits is the caller's: Defeatist keeps its
  `art_place()` offset, keystone and its blit, and calls
  `img_fit_draw(&pic, fb + dy * fbw + dx, fbw, cw, ch, k)`. This is why
  there is no gfx dependency and no ABI call for drawing: an app that
  writes a framebuffer it owns needs the function, not the host's panel.
  `fittest`: 25 checks under ASan and UBSan -- sizes, identity through a
  padded stride, 3x blocks, reads inside the source in both directions,
  nothing written outside cw x ch.
- **7010** The image section: feckless_abi's `sections/image.txt` and the
  component `feckless_abi_image` -- the decoder half of
  `driver/jpeg_decode.h`, five names under `feckless_abi_image_v1`
  (`jpeg_new_decoder_engine`, `jpeg_del_decoder_engine`,
  `jpeg_decoder_get_info`, `jpeg_alloc_decoder_mem`,
  `jpeg_decoder_process`), the ones 7008's `image_esp.c` calls. Only the
  hardware is the host's: the engine is a driver singleton (a second
  `jpeg_new_decoder_engine()` fails) with DMA descriptors and an
  interrupt. stb_image, pngle, J40 and the rest of
  `feckless_image_handler` are plain code and link into the app's ELF;
  TJpgDec likewise, by adding `espressif__esp_jpeg` to `ELF_COMPONENTS`.
  The task and heap names `image_esp.c` calls were in the core list
  already (`xTaskCreateStaticPinnedToCore`, `ulTaskGenericNotifyTake`,
  `xTaskGenericNotify`, `heap_caps_*`, and the rest); `feckless_abi_run()`
  registers the table through a new weak reference, as for every section.
  An app that links `img_esp_ops()` calls `feckless_abi_require_image()`.
  The encoder half is left out until something uses it. The table was
  generated and compiled on a host; the names are IDF 5.4's, and the
  build of a launcher that links the component is the check that each is
  a real linker symbol.

## The 8000 series, toward v0.8.0

- **8001** Opens the 8000s. v0.7.0 is tagged with 7001-7010 in it (the
  nvs migration table, `esp_ota_set_boot_partition` and four more names
  in the core list, `feckless_image_handler` from the pure helpers to
  `img_decode()`, its ESP backends and `img_fit_draw()`, and the image
  ABI section). CLAUDE.md names the 8000s as the series in use. Next in
  this repository is whatever Defeatist's move to an ELF finds missing:
  the audio codec (`esp_audio_simple_dec_*`) and TLS are still decisions
  (a section each, or linked into the ELF), and the board is yet to run
  the image path. No code changed.
- **8002** feckless_image_handler: `vendor/` is on the component's public
  include path, so `#include "miniz.h"` works for an app that names the
  component. Defeatist's `i18n.c` inflates its tables with tinfl (the
  ROM's; only the declarations are needed) and took `miniz.h` from its
  `pngle` component, which goes when Defeatist adopts this component
  (its 8069); a second copy of pngle's miniz beside this one would have
  defined the same symbols twice. `stb_image.h`, `j40.h` and `pngle.h`
  come with it, which nothing is bothered by. Built: nothing to build on
  a host; the include path is the one `PRIV_INCLUDE_DIRS` had, moved.
- **8003** The crypto section: `feckless_abi`'s `sections/crypto.txt` and the
  component `feckless_abi_crypto`, `feckless_abi_crypto_v1`. Defeatist's
  move to an ELF (its 8070) found three things the ABI lacks; this is the
  one that is Mbed TLS: certgen.c (a self-signed P-256 certificate:
  the key, the X.509 writer, PEM), wifijoin.c (PBKDF2-HMAC-SHA1 for a
  Wi-Fi passphrase) and approve.c (a SHA-256). The launcher has Mbed TLS
  already, for its HTTPS and HTTP; linking a second copy into the app
  would be code twice for names the launcher can give.
  The list is what those three files call, and no more: the pk layer,
  `mbedtls_x509write_crt_*` and `mbedtls_x509_crt_*` on both majors;
  `mbedtls_pk_setup`, `mbedtls_pk_info_from_type`, `mbedtls_ecp_gen_key`,
  `mbedtls_sha256` and `mbedtls_pkcs5_pbkdf2_hmac_ext` on Mbed TLS 3.6
  (IDF 5.x); `mbedtls_pk_copy_from_psa` and the `psa_*` functions for a
  key, a hash and a key derivation on 4.0 (IDF 6.x), plus
  `psa_crypto_init`, which may be called again. The static inline functions in Mbed TLS's headers
  (`mbedtls_pk_ec()`, `psa_set_key_type()` and the other `psa_set_key_*`)
  have no symbol; `mbedtls_pk_ec()` calls `mbedtls_pk_get_type()`, which
  is in the list.
  *Two majors.* IDF 5.5 ships Mbed TLS 3.6 and 6.1 ships 4.0, and the
  names an app takes differ (Defeatist's files branch on
  `MBEDTLS_VERSION_MAJOR`, as its 6031 did), as do the structures they
  take. So a section can now hold names for one value of a key only:
  `@when mbedtls 3` ... `@end` in the file, `feckless_abi_filter_section()`
  in `cmake/abitable.cmake` for the launcher's table (its own major's names
  only, read from the Mbed TLS header, IDF's version if there is none), and
  the build check unchanged, since it reads the file whole. Each major has
  a marker symbol, `feckless_abi_crypto_mbedtls3` or `..._mbedtls4`,
  defined by a launcher of that major only; `feckless_abi_require_crypto()`
  refers to it and to `feckless_abi_crypto_v1`, so an app built for 3 on a
  launcher with 4 fails in `esp_elf_relocate()` naming the marker, not
  later with a `mbedtls_pk_context` of the wrong size. The component fails
  the build on Mbed TLS 4 without `CONFIG_MBEDTLS_X509_CREATE_C` (Defeatist's
  `sdkconfig.defaults.idf6` says IDF 6 puts the X.509 writer behind it).
  `feckless_abi.c` registers the new table through a weak reference, as for
  every section.
  *Checked:* each of the 27 Mbed TLS names for 3.6 and 33 for 4.0 (each
  list also holds its marker) against the real sources (Mbed TLS 3.6.3, and 4.0.0 with TF-PSA-Crypto at its submodule's
  commit): every one is declared in a header and defined in a `.c`, none is
  `static inline`. The five names the 3.6 side takes that 4.0 keeps in
  private headers (`mbedtls_pk_setup`, `mbedtls_pk_info_from_type`,
  `mbedtls_ecp_gen_key`, `mbedtls_sha256`, `mbedtls_pkcs5_pbkdf2_hmac_ext`)
  are in 4.0's sources too but are left out of its list, because Defeatist
  uses PSA there. `test/filter_test.cmake` (host, 19 checks): the filter's
  groups and `@end`, comments with brackets and semicolons, each major's
  marker and only its own, the names for 3 only and 4 only, no duplicate;
  it fails when a name is moved to the wrong group. `feckless_abi_crypto.c`
  and the header compiled at `-O2 -Wall -Wextra -Werror` against both
  Mbed TLS versions' headers: the launcher defines `_v1` and its major's
  marker, an app refers to `_v1` and its own.
  *Not built, no ESP-IDF in the session:* that a launcher links the table
  (every name a real linker symbol in IDF's own Mbed TLS build, with IDF's
  `sdkconfig`: IDF's Mbed TLS is a fork with ESP ports for SHA and the
  rest, so a name that is in upstream's sources can still be configured
  out), which is the first thing a launcher's build says; and not run on a
  Tab5. Avoidant has to link `feckless_abi_crypto` and move to this tag
  to give the section; Defeatist then calls `feckless_abi_require_crypto()`.
- **8004** The device's HTTPS identity is the launcher's: `feckless_https.h`,
  `feckless_cert.h` and `feckless_redirect.h` in `feckless_network_handler`,
  and four names in the ABI's network section. Defeatist's remote made its
  own certificate (certgen.c, devcert.c, 5121) and handed it to
  `httpd_ssl_start()`; with apps loaded by a launcher, and sandboxing
  planned, the key belongs to the launcher or, in an app's standalone .bin,
  its shim, which link this component either way. One certificate for the
  device, so one browser exception covers every app on it and survives
  firmware updates; and an app that is never given the key cannot leak it
  (today an ELF shares the launcher's address space and could read it: the
  boundary is the point of the design, not yet a wall).
  *`feckless_cert`*: certgen.c moved with its name changed and two changes:
  `O=Feckless`, and on Mbed TLS 4 `psa_crypto_init()` before the PSA key
  (it is idempotent, and without it `psa_generate_key()` is
  `PSA_ERROR_BAD_STATE`; Defeatist relied on the TLS stack having called
  it). Built without the X.509 writer (IDF 6 without
  `CONFIG_MBEDTLS_X509_CREATE_C`) it is two stubs returning
  `FECKLESS_CERT_ERR_NO_WRITER`, so a project that only includes the header
  or builds the component without starting a server is not asked to enable
  the writer; a launcher that tries to start one is told by log line and
  `ESP_ERR_NOT_SUPPORTED`. Hostname only in the certificate, as it was
  (`DNS:<name>.local`, no IP), so its fingerprint survives DHCP.
  *`feckless_https`*: `feckless_https_start(&h, &httpd_config, port)` is
  `httpd_ssl_start()` with the launcher's identity: the app's
  `httpd_config_t` is copied (handlers' `open_fn` included, which runs on
  the server task as for `httpd_start()`), the port is the argument. The
  identity is made at the first start (`feckless-` and the chip MAC's last
  three bytes unless the launcher called `feckless_https_set_name()` first,
  which is not in the ABI), kept in NVS namespace `feckless_https` with the
  key and certificate in one commit, loaded from there afterwards.
  `feckless_https_stop()`, and `feckless_https_fingerprint()` for the
  screen that shows it. Plain NVS, not `feckless_nvs_handler`: that would
  put another component into every app's manifest. One lock, created on
  first use.
  *Redirect*: `feckless_https_redirect` is a handler for the app's own
  port-80 server, which the app keeps (it steps aside for a setup portal;
  its `open_fn` refuses what is off the network). A request that reached
  port 80 by IP address (dotted IPv4, a bracketed IPv6 literal, no Host
  header, or a Host that is not a plain name) goes to `https://<name>.local`;
  one that came by a name keeps it; the HTTPS port is added when it is not
  443; the path is kept or becomes `/` (never cut). The status is 307, not
  Defeatist's 301: a 301 is kept by a browser for good, and this one
  depends on a name that must resolve. The decision is `feckless_redirect.c`,
  pure.
  **The name has to resolve, and nothing here makes it.** Defeatist serves no
  mDNS (no `mdns`, no hostname in the tree), so `feckless-xxxxxx.local`
  resolves only where the network's own DNS knows it. A redirect to a name
  that does not resolve is worse than leaving the IP alone. The launcher
  needs an mDNS responder for the name `feckless_https_set_name()` /
  the default gives (IDF's `mdns`, a registry component, so a manifest
  change and a new `dependencies.lock`): the next patch, or this redirect
  is not to be turned on. Also: Android's browsers do not all resolve
  `.local`.
  *ABI*: `feckless_https_start`, `_stop`, `_fingerprint` and `_redirect`
  under `feckless_abi_network_v1` (adding names is compatible);
  `httpd_ssl_start()` and `httpd_ssl_stop()` are deliberately not
  exported. `feckless_abi_network.h` includes `feckless_https.h`. The
  network section's CMake warns without `CONFIG_ESP_HTTPS_SERVER_ENABLE`
  (a launcher fails to link; an app's own build is not affected, so it is
  not an error). `feckless_network_handler` gains `esp_http_server` and
  `esp_https_server` in its REQUIRES, both IDF's.
  *Checked.* On a host against the real Mbed TLS 3.6.3 and 4.0.0
  (built from source): `test/certtest.c`, 26 checks per version, and
  `test/certcheck.sh`, which has OpenSSL read what was made -- subject,
  issuer, lower-case SAN with no IP, CA:FALSE, digitalSignature,
  ecdsa-with-SHA256, P-256, v3, validity 2026-01-01 to 2049-12-31,
  self-verification, key and certificate a pair, fingerprint equal to
  OpenSSL's, a 16-byte positive serial. `test/certnowriter.c` builds the
  file with the writer undefined and links it with no Mbed TLS.
  `test/redirecttest.c`, 40 checks under ASan and UBSan: address to name,
  name kept, header injection and host-looking junk refused, the path kept
  or "/", 63-character names, a path too long, exact-fit buffers. The
  glue (`feckless_https.c`) compiled at `-O2 -Wall -Wextra -Werror` against
  stub IDF headers and both Mbed TLS versions' real ones; that found a
  `/*` inside a comment, now fixed. `make -C test` (wifistore 10088,
  netlink 46, redirect 40) passes; the cert tests need a built Mbed TLS of
  each major (`make run-cert MBED36=... MBED40=...`) and are not in CI.
  *Not checked:* any of it linked or run on a Tab5 -- no ESP-IDF in the
  session. That `esp_https_server`'s `httpd_ssl_config_t` and the
  `HTTPD_SSL_CONFIG_DEFAULT()` it fills are as the stub assumed (the
  fields are the ones Defeatist's remote.c set); that `port_secure` is
  what `httpd_ssl_start()` binds; NVS's string size limit for the key and
  the certificate; the time it takes to make the key on the P4.
  Defeatist follows: `remote.c` calls `feckless_https_start()` and its
  redirect handler, and `certgen.c`, `devcert.c` and the PBKDF2 in
  `wifijoin.c` (which `wifi_psk()` already is) go; then the crypto section
  of 8003 shrinks to the one SHA-256 `approve.c` takes.
- **8005** The name 8004's redirect sends a browser to now resolves:
  `feckless_https_start()` starts an mDNS responder for it. 8004 said the
  redirect to `<name>.local` is worse than useless without one; nothing in
  Defeatist, Avoidant or this repository answered mDNS. On the first
  successful start: `mdns_init()` (which returns OK when already up),
  `mdns_hostname_set(name)`, `mdns_instance_name_set(name)` and an
  `_https._tcp` service on the port. The interface is the one the IDF's
  `mdns` predefines for the Wi-Fi station (`WIFI_STA_DEF`, which wifi.c
  makes; `CONFIG_MDNS_PREDEF_NETIF_STA` is on by default, and so is the AP
  one, which a setup portal gets). **USB Ethernet is not covered**: its
  netifs have their own keys (ethernet.c's `attach()`), not `ETH_DEF`, and
  registering them with `mdns_register_netif()` and driving them from the IP
  events is a patch of its own, not guessed at here; over the cable the name
  resolves only where the network's DNS knows it. A failure to start is a
  log line and not fatal.
  *The name is part of the identity.* It is stored with the key and
  certificate in the same commit (`name`), and a load takes it from there, so
  the mDNS hostname, the certificate's SAN and the redirect's target cannot
  differ even if `feckless_https_set_name()` is later given another. That
  function now makes its argument a DNS label (`feckless_dns_label()`: lower
  case, anything else '-', no run or edge of them, 63 characters; pure, 14
  new checks in `redirecttest`, 54 in all under ASan and UBSan), and ignores
  one that leaves nothing. The default name moved into one function used by
  both the identity and the redirect.
  *Manifest:* `feckless_network_handler/idf_component.yml` gains
  `espressif/mdns: ">=1.8.0"`, from the registry, which a session cannot
  reach; a range and not a pin, on the reasoning the manifest already gives
  for `esp_wifi_remote` (a pin the registry cannot satisfy stops the project
  configuring). So **every project that names this component re-resolves its
  lock on its next build**, and the diff wants reading. I read the API from
  esp-protocols' `mdns-v1.14.1`: the four functions' signatures match the
  stub the glue was compiled against, `mdns_init()` returns OK when already
  initialised, and the component needs IDF >= 5.0.
  *Checked:* the glue recompiled at `-O2 -Wall -Wextra -Werror` against stub
  IDF headers and both Mbed TLS versions; the handler's host tests pass
  (wifistore 10088, netlink 46, redirect 54). *Not checked:* that it links,
  or answers a query on a Tab5 (`avahi-resolve -n feckless-xxxxxx.local`, or
  `ping`, from another machine is the test; the log line is `answering mDNS
  as ...`), that mDNS and the esp_hosted radio's multicast filtering agree
  (the C6 firmware may drop 224.0.0.251: the first thing to look at if the
  line is there and nothing resolves), the memory the responder takes.
- **8006** The network section no longer lists `httpd_resp_send_404`, which
  stopped Avoidant linking on IDF 6.1 (`undefined reference to
  'httpd_resp_send_404'` from `g_feckless_abi_network_elfsyms`). In
  `esp_http_server.h` it is a `static inline` wrapper over
  `httpd_resp_send_err(r, HTTPD_404_NOT_FOUND, NULL)`, so there is no linker
  symbol for the table to point at. I read the header of both `v5.5.5`
  (line 1366) and `release/v6.1` (line ~1457): it is inline in both, so this
  was not a 6.1 change. 7003 added the name from a scan of Defeatist's
  source and said none of its names was an inline; that was wrong for this
  one. A launcher that linked the network section with this name could not
  have linked, so none I know of exports it (I have not checked how
  Avoidant built the section before 0046), and removing it is not an ABI
  break: the section's version stands. An app that
  calls it compiles the body into itself and calls `httpd_resp_send_err`,
  which stays exported, so nothing that loads depends on the name; none of
  the apps' sources import it. No weak stand-in was added (as was done for
  `__issignalingf`): there is nothing to be compatible with.
  *Not checked:* the other names in this section and the core list. I
  scanned them against sparse copies of the IDF headers; the scan cannot
  tell an inline wrapper from a real function with an inline neighbour, and
  the sparse trees have no lwip, so the `lwip_*` socket calls and
  `ipaddr_addr` (real functions in lwip's `sockets.c` and `ip4_addr.c`, from
  memory) are unverified. If the next link names another symbol, the fix is
  the same one line. Not built with ESP-IDF, not on a Tab5.
- **8007** The crypto section shrinks to hashing, as 8004 said it would
  once the device's certificate was the launcher's. `sections/crypto.txt`
  now lists `mbedtls_sha256` (Mbed TLS 3.6) and `psa_crypto_init` and
  `psa_hash_compute` (4.0), with each major's marker symbol: two names for 3.6 and three for 4.0, where
  8003 had 28 and 34 (counted through the section filter).
  Gone: the pk layer, the X.509 writer, key generation and PBKDF2. Keys and
  certificates are `feckless_https_*`'s, in the network section, and an app
  is never given one; a Wi-Fi PSK is `wifi_psk()`. The one app that made its
  own certificate, Defeatist, stopped in its 8073, and `approve.c`'s
  SHA-256 is what is left of its Mbed TLS use.
  **It is a new section version, 2:** `feckless_abi_crypto_v1` is now
  `_v2` (`FECKLESS_ABI_CRYPTO_VERSION`, the section file's `@symbol`, the
  table's name and the component's CMake), as the rule for removing a name
  asks. Nothing needs both: I looked for `feckless_abi_require_crypto` and
  `feckless_abi_crypto` in Avoidant's and Defeatist's sources and manifests
  and neither names it, so no launcher or app built so far required v1; the
  other apps' sources I did not look in. Also gone: the component's check for
  `CONFIG_MBEDTLS_X509_CREATE_C` on Mbed TLS 4 (the section no longer
  exports the writer; `feckless_cert.c` still needs it, and Avoidant's
  `sdkconfig.defaults.idf6` sets it), and the header's and READMEs' words about
  keys and X.509.
  Checked: `cmake -P components/feckless_abi/test/filter_test.cmake` passes,
  edited for the new list (each major has its marker and its own hash name,
  and nine names that version 1 had are in neither). Not built with
  ESP-IDF; the table is generated by the same two cmake calls as before and
  I have not seen it generated. **A launcher that links the section re-links
  with a table of two or three names**, nothing else to do.
- **8008** The audio section: feckless_abi's `sections/audio.txt` and the
  component `feckless_abi_audio`, `feckless_abi_audio_v1`. Defeatist's
  first ELF link (its 8090) left esp_audio_codec's names undefined, and
  linking the codec into the ELF cannot work: its decoders are
  precompiled archives built without `-fPIC` (absolute `R_RISCV_HI20` /
  `LO12` relocations, no PC-relative or GOT ones), and a `-shared` link
  of 2.5.0's esp32p4 libraries with Espressif's esp-14.2.0 toolchain and
  `project_elf()`'s flags stops with 76 "can not be used when making a
  shared object; recompile with -fPIC". Only Espressif can rebuild them,
  so the launcher links the codec and the app takes it from here.
  The list is the decoder half of the public API, 18 names:
  `esp_audio_simple_dec_*` and `esp_audio_simple_check_audio_type` (what
  Defeatist's decoder.c and netdec.c call: open, process, get_info,
  close), the one-type `esp_audio_dec_*`, both `register_default()`s and
  `unregister_default()`s, and `esp_audio_codec_get_version`. None is an
  inline. Left out until something uses them: the encoder, and `_reg.h`'s
  registration of an app's own decoder or parser, which would leave the
  launcher's codec holding function pointers into an ELF that has gone.
  *The pin is part of the section.* The app compiles the structures it
  hands the codec (`esp_audio_simple_dec_cfg_t`, each decoder's cfg, the
  info and frame structs) from its own copy of the headers, and the
  launcher's codec reads them. So the component's manifest pins
  `espressif/esp_audio_codec: "2.5.0"` exactly, and an app and a
  launcher at the same tag build against the same one. 2.5.0 because
  2.6 needs P4 silicon revision 3.0 and the Tab5s so far are v1.3; it is
  what Defeatist's lock has held. Moving the pin to a version whose
  structures differ is `audio_v2`. Which decoders `register_default()`
  registers is the launcher's Kconfig (`CONFIG_AUDIO_DECODER_*_SUPPORT`,
  all on by default); the headers read no Kconfig.
  `feckless_abi.c` registers the table through a new weak reference, as
  for every section. An app names `feckless_abi_audio`, calls
  `feckless_abi_require_audio()`, and leaves esp_audio_codec out of its
  `ELF_COMPONENTS`.
  *Manifest:* a registry dependency, so a project that names this
  component re-resolves its lock, and the diff wants reading. The
  registry was not reachable from the session; the 2.5.0 sources came
  from esp-adf-libs at a791bb9 ("Update esp_audio_codec to v2.5.0").
  *Checked* with Espressif's riscv32-esp-elf esp-14.2.0, no ESP-IDF:
  the table generated by `feckless_abi_write_table()` and compiled with
  `feckless_abi_audio.c` at `-O2 -Wall -Wextra -Werror` against
  elf_loader's `private/elf_symbol.h`. Each of the 18 names is a defined
  global in 2.5.0's esp32p4 archives or the component's own `src/`. A
  static link of the table, the component's three `src/*.c` (all decoders
  on) and the two archives leaves no `esp_audio_*` or `esp_*_dec_*`
  reference undefined; what is left is libc, libm, libgcc,
  `esp_log_timestamp`, `esp_chip_info` and `operator new`/`delete`, which
  a launcher has. An app shaped like Defeatist's calls, linked as
  `project_elf()` does, passes `abicheck.cmake` with the real lists:
  "8 symbols, all in feckless_abi_v1 + feckless_abi_audio_v1"; without
  `feckless_abi_require_audio()` it fails naming the section; without
  `audio.txt` it fails on the same names as Defeatist's 8090 build.
  `feckless_abi.c` compiles at `-O2 -Wall -Wextra -Werror` before and
  after, against elf_loader's headers and IDF stubs. `filter_test`
  passes. *Not checked:* a launcher built with ESP-IDF that links the
  section (the component manager resolving the pin, the codec's
  `add_prebuilt_library()` archives reaching the launcher's link through
  `WHOLE_ARCHIVE`), and nothing on a Tab5. Avoidant links
  `feckless_abi_audio` to give the section. Defeatist then names it,
  calls `feckless_abi_require_audio()`, and drops esp_audio_codec from
  `ELF_COMPONENTS` and 8090's link-line loop.
- **8009** `abort` in the core list. Defeatist set
  `CONFIG_COMPILER_OPTIMIZATION_ASSERTIONS_SILENT` (its 8095, for the
  .bin's 3 MB slot), and its ELF's next clean build failed the ABI check on
  one name: `abort`. With assertions silent, IDF's `esp_err.h` makes
  `ESP_ERROR_CHECK()` call `abort()` where it called
  `_esp_error_check_failed()` (exported); the silent `assert()` still calls
  `__assert_func`, with NULLs, which is exported already. `abort` is a real
  function on both majors (`esp_libc/src/abort.c` in release/v6.1,
  `newlib/src/abort.c` in v5.5.1), so the launcher's table can point at it.
  Adding a name is compatible: the ABI's version stands.
  *Checked* with Espressif's esp-14.2.0 toolchain: the core table generated
  by `feckless_abi_write_table()` compiles at `-O2 -Wall -Wextra -Werror`
  with `extern int abort;` (the existing pragma covers the builtin's
  mismatch, as for `memcpy`) and refers to `abort`. An app ELF whose
  `app_main()` is a silent `ESP_ERROR_CHECK()`, linked as `project_elf()`
  does, passes `abicheck.cmake` with this list and fails naming `abort`
  without it. `filter_test` passes. Not built with ESP-IDF, not on a Tab5.
  A launcher re-links with one more entry; an app built with silent
  assertions needs a launcher at this tag or later.
- **8010** `feckless_keyboard`: Spurious's on-screen keyboard, lifted. The
  StarDict reader (spurious-stardict-reader-for-m5tab5, main.c at daa69a6)
  has the only on-screen keyboard among the Tab5 projects: three rows of
  ten -- q to p; a to l and an apostrophe; z to m, `-`, `.` and del --
  lower case, each key a rectangle with its label centred. Defeatist has
  none (its `portal.h`: one written for a passphrase typed once per network
  would be the largest thing on the screen), and ESP-IDF's only keyboard
  is LVGL's, which brings its own display stack beside gfx. Avoidant's
  Wi-Fi page and Defeatist's search want one; this is the first step, the
  code as it is, so the next ones (shift, digits and symbols, landscape,
  masking, redrawing only what changed) are changes to one component
  rather than to each app's copy.
  The API: a `kbd_t` saying where the band is (`x`, `y`, `w`), each row's
  height, the label scale and two colours; `kbd_draw()` puts every key in
  the framebuffer and the caller blits; `kbd_hit()` gives the character
  under a tap, `KBD_DEL`, or `KBD_NONE` outside the band; `kbd_edit()`
  applies one to a string as Spurious did (del drops the last character, a
  character is appended while one more fits). It owns no task and no touch:
  the app's UI code draws it and hands it taps, so whatever writes the
  screen stays the one thing that does (Defeatist's `ui_task`). Spurious's
  fourth row -- Up, Down, space, sounds, clear -- stays Spurious's.
  Plain code, so it links into an app's ELF (`ELF_COMPONENTS`), as
  `feckless_image_handler`'s decoders do; it calls `gfx_fill_rect`,
  `gfx_draw_text`, `gfx_text_w`, `strcmp` and `strlen`, all in the core
  list. No ABI change. It REQUIRES `feckless_graphics_handler` (for
  `gfx.h`) and nothing else; the gfx calls it makes are the same at v0.5.0,
  the tag Spurious is on, as now.
  *Checked:* `test/kbdtest` runs it against a copy of Spurious's own
  table, `key()`, `draw_keyboard()`'s three rows and `search()`'s keyboard
  branch, with the real `gfx.h` and the gfx calls recorded: the same 60
  calls in the same order with the same arguments, and the same text after
  a tap on each of the 720 x 264 pixels of the band on an empty, a short
  and a full query (570,240 taps); plus a band moved and 725 wide (its five
  leftover pixels are the tenth column's, as Spurious's were), taps outside
  it, and the edit at its limits. ASan and UBSan; two deliberate breaks
  (the last column, a label one pixel over) each fail it. The source
  compiles with Espressif's esp-14.2.0 at `-O2 -Wall -Werror=all -Wextra
  -Werror=extra -fPIC`, 768 bytes. CI runs the test. Not built with
  ESP-IDF; Spurious using it is its own patch, after a tag.
- **8011** `feckless_keyboard` becomes a keyboard: four rows, staggered as
  an ANSI one is, a number row, shift, and a symbol page. 8010 was
  Spurious's three rows of ten, lower case, as they were; with it in one
  place, this is the change Avoidant's Wi-Fi page and Defeatist's search
  were waiting for.
  *The layout,* in quarters of a key, eleven keys across (`KBD_UNITS`), so
  65 px keys on a 720 px portrait screen and 116 in landscape:
  `1`-`0` and `-` from the left edge; `q`-`p` half a key in; `a`-`l` and
  `'` a quarter further; `z`-`m`, `,` and `.` half a key further again --
  the offsets of a real keyboard. The stagger's spaces on the left hold
  `#+` (before `a`) and `shift` (before `z`), and `del` follows `.`; the
  rows' other ends are left empty. Each key is a span in quarters
  (`kkey_t` tables, one per row), and drawing and hit-testing both place it
  with one function, `key_x()`, so they cannot disagree.
  *shift* is a phone's: once for the next key (the number row's
  `!@#$%^&*()_`, capitals, `" < >`), used up by the character it types but
  not by `del` or `#+`; twice to lock, which is capitals only, as caps lock
  is, and is labelled `SHIFT`; a third time off. *`#+`* (`ab` while on)
  turns the three letter rows into the symbols shift does not give:
  ``= + [ ] { } \ | ; :``, `` ` ~ / ? @ # $ % & ``, `! * ( ) _ < >`; the
  number row stays digits and a shift waits for the letters. With the two,
  every printable ASCII character but space has a key; space, enter and
  the like are the app's, as Spurious's row under the keyboard is. A lit
  modifier is drawn in the new `on` colour. The font has no arrows or
  keycap symbols (Ark12's subset has none of U+2190-21FF or U+2300-23FF),
  so the modifiers are words a scale smaller than the characters.
  **The API changes, so an app changes with it** when it moves to this
  tag: `KBD_ROWS` 4 and `kbd_h()` with it; `kbd_t` gains `on` and the state
  (`shift`, `symbols`, zero to start); `kbd_hit()` takes the `kbd_t`
  mutable and returns `KBD_STATE` for shift or `#+` -- redraw after
  anything but `KBD_NONE`; `kbd_edit()` ignores `KBD_STATE`. Spurious, at
  v0.8.5, keeps 8010's until its next patch; its `static const kbd_t` will
  not compile against this one.
  *Checked:* `test/kbdtest`, rewritten: each row's first key where ANSI
  puts it, at 720, 725 and 1280 px wide (and the q/a/z offsets as a half,
  a quarter and a half key, to a pixel); no key past the band or over
  another; every pixel of the band in all four states taps the key drawn
  over it, or nothing in the stagger's gaps (1,013,760 taps); shift's
  states, what uses up a shift and what does not, lock as capitals only,
  the symbol page and back; a key for each of the 94 printable characters;
  taps off the band; `kbd_edit()`. ASan and UBSan; three deliberate breaks
  (a shift never used up, `a` a quarter out, taps a pixel off the drawing)
  each fail it. `make preview` (new, `render.c`) draws the four states with
  the real gfx.c and Ark12 into PPMs, and they were looked at. The source
  compiles with Espressif's esp-14.2.0 at `-O2 -Wall -Werror=all -Wextra
  -Werror=extra -fPIC`, 1,218 bytes, calling only `gfx_fill_rect`,
  `gfx_draw_text`, `gfx_text_w` and `strlen`. Not built with ESP-IDF.
- **8012** `feckless_libversions`: the library and licence list, made one
  copy, as 8010 made the keyboard one. Avoidant's 0028
  `cmake/libversions.cmake` (Lothesome's, Mahjong's and Defeatist's
  before it) becomes `cmake/libversions.cmake` here, called through
  `feckless_libversions(<target> NAME <app> LICENSE <spdx>)` from
  `project_include.cmake`. What it reads is as it was -- the lock, the
  project's vendored `components/`, Ark Pixel's commit from
  `gen_ark12.py`, the app's git commit -- with three changes: a
  component's directory is the build's own when it is one of the build's
  components (so `IDF_COMPONENT_MANAGER=0` and an ELF build of fewer
  components find their licences), else `managed_components/` beside the
  lock; Galmuri and Arabixel Basic are listed beside Ark Pixel, since
  their licences travel with the firmware too; and the table is
  `libv_t { module, checkout, licence }`, not `char *[3]`.
  `libv_draw()` draws Avoidant's settings page's three columns into a
  band (module, checkout, licence at 0, 45% and 70%), as many rows as
  fit and "and N more" when they do not, returning how many it drew so
  an app can page. No snprintf; calls only `gfx_draw_text`.
  **An app changes with it** when it moves to this tag: its own
  `cmake/libversions.cmake` goes, `k_libversions[i][0]` becomes
  `.module`, and its loop becomes `libv_draw()`; Spurious, which has no
  list yet, gets one with the call in `elf/CMakeLists.txt` and the
  component in `ELF_COMPONENTS`.
  *Checked:* `test/libvtest` -- rows, column positions and cut widths,
  colours, "and N more" (and 12,344 more), paging through to the end,
  zero-row and out-of-range bands -- against recorded gfx calls, ASan and
  UBSan; `make gen` runs the generator in script mode on a fixture lock
  and vendored component and checks every row, cmake_utilities left out,
  and the header compiling at `-Wall -Wextra -Werror`. Run in script mode
  on Avoidant's lock: 43 entries. Not built with ESP-IDF.
- **8013** `feckless_libversions` is data only. Compared against the
  other four apps' copies before the tag: every one draws the list its
  own way (columns sized to the text, two lines in portrait, paging,
  Lothesome's landscape text, Defeatist's one string a row), so
  `libv_draw()` and the component's source go, and with them the
  dependency on `feckless_graphics_handler`; the header is all that is
  left, and the generated one defines `libv_t` itself, so code including
  it needs no REQUIRES. Alongside `libversions.h`, `libversions.tsv`:
  the same rows tab-separated under a header line, for documentation
  (standards.md asks for the list there too) or a release, at
  `LIBVERSIONS_TSV`. From the copies: `EXTRA "module|checkout|licence"`
  rows, last and in order, for what only the app knows (Mahjong's
  README-pinned vendored files, Noto glyphs and MurmurHash2; Errant's
  BMI270 blob, world tile and cJSON on IDF 5); `NO_IDF`, as Defeatist
  leaves ESP-IDF out; pngle, stb_image and J40 at
  `feckless_image_handler`'s pins, for any build that has it (Defeatist's
  8069 rows, now everyone's); esp_jpeg's licence with "+ TJpgDec (ChaN)";
  CC0-1.0 recognised. A `\` or `"` in a row is escaped in the header.
  *Checked:* `make -C components/feckless_libversions/test` runs the
  generator in script mode on the fixture -- now with esp_jpeg and the
  image handler -- with three EXTRA rows (one with quotes, one bare) and
  with NO_IDF, checks every row of both files and the EXTRA order, and
  compiles a file including the header and the public one together at
  `-Wall -Wextra -Werror`. Not built with ESP-IDF.
- **8014** `feckless_libversions` reads esp_ipa's licence as Espressif
  MIT: its file opens "ESPRESSIF MIT License", which matched neither
  "Espressif Modified MIT" nor anything but the plain "MIT License" test.
  Found moving Unprofitable Mahjong to it, whose vendored esp_ipa the
  component's scan of `components/` now lists. *Checked:* the fixture
  gains a vendored component with esp_ipa's licence file and manifest;
  `make -C components/feckless_libversions/test`, both runs.
- **8015** Ark12 draws IPA. Spurious's pronunciations came out as boxes:
  Ark has no glyph at 12px, in any cut, for IPA Extensions, the stress
  and length marks or the superscripts, and Galmuri's, which it has, are
  a larger font (7 px wide on an 8-row x-height against Ark's 5 on 6).
  `RANGES` gains Latin Extended-B and Greek (β θ χ), both in Ark's
  monospaced cut; `tools/ipa12.py` draws the other 88 for Ark's 6×12
  cell -- turned and mirrored Ark letters where IPA defines them so (ə is
  e turned, ɔ is c mirrored), Ark's own glyph where IPA's is the same
  shape (ɡ ǀ ǃ ʼ), and bitmaps in Ark's metrics for the rest -- added by
  `add_ipa()` on every path, which stops if Ark already draws one.
  32,334 glyphs, 302 more; 385,513 bytes, 2,933 more. Combining marks
  (the tie bar, the nasal tilde) are not drawn: gfx has no zero-advance
  mark yet.
  *Checked:* the generator first reproduced the committed `ark12.c` and
  `ark12.h` byte for byte from Ark at ARK_COMMIT and Galmuri 2.40.3;
  regenerated, every one of the 32,032 glyphs before decodes to the same
  rows (`reencode()`, compared) and only the 302 are new. The graphics
  handler's texttest gains `test_ipa()` -- 96 IPA letters drawn
  halfwidth, ə is e turned and ɔ c mirrored pixel for pixel, ˈ above row
  4 and ˌ below row 9, həˈləʊ six cells -- and its golden count and hash
  move to the new table: 423,979 checks, no failures. The keyboard's test,
  which draws with the real font, passes. A sheet of transcriptions drawn
  from the new table was looked at. Not built with ESP-IDF.
- **8016** `feckless_mail`: the outgoing mail account, and a form to set
  it. Jinxed Writer's spec (section 6) mails notes out over SMTP and wants
  the host, port, login, app password and recipient kept "the same way the
  saved Wi-Fi networks are", and set from a settings page; set once in a
  launcher, an app it runs should find it, as the networks are found.
  `mailcfg.h`: a `mailcfg_t` (host, port -- 0 for 465 or 587 --, TLS or
  STARTTLS, login, password, From -- "" for the login --, To) as one
  versioned blob under `acct` in the `feckless_mail` namespace (or the
  caller's), through `feckless_nvs_handler`, so it links into an ELF and
  runs on the launcher's NVS (the nvs section). No encryption of its own,
  for wifistore.h's reasons. `mailcfg_check()` refuses control characters
  anywhere (a CR or LF would be a second SMTP line or header), a host
  without a dot, an empty login or password, and a From or To that is not
  one plain address, each with a sentence naming the field;
  `mailcfg_save()` stores nothing that fails it and zeroes the strings'
  tails. `mailcfg_domain()` gives `@example.com` for the writer's status
  line. Plain SMTP is not offered.
  `mailform.h`: the form, in feckless_keyboard's manner -- no task, no
  touch: a rectangle and a row height from the app, `mailform_draw()`,
  taps to `mailform_tap()` and a USB keyboard's characters, Tab, Enter and
  Escape to `mailform_key()`. A row a field, Security as two buttons, the
  password masked with Show, Cancel and Save, and with `osk` the on-screen
  keyboard and a space bar under them. Save runs the check and puts its
  sentence under the rows with the field it names focused. Sending is not
  here yet.
  *Checked:* `make -C components/feckless_mail/test` (CI runs it): the
  checks, the NVS round trip against a fake, and the form driven by keys
  and taps through the real gfx.c and feckless_keyboard at 720 x 1280 with
  the keyboard and 1280 x 720 without, every pixel of each band tapped
  (ASan, UBSan); both sources at `-O2 -Wall -Wextra -Werror`. Every call it
  makes is in the core list or the nvs section. Not built with ESP-IDF.

## The 9000 series, after v0.9.0

The 8000s ran on through the v0.8.x point releases and into v0.9.0, whose
one new thing was 8016. The 9000s start after that tag.

- **9001** `feckless_mail` sends: `mailsend()`, SMTP over TLS or STARTTLS.
  Jinxed Writer's settings page wants a test button for the account 8016
  keeps, and its spec (section 6) a background send. `mailsend.h`: one
  plain-text message, subject and body UTF-8, from `mailcfg_from()` to the
  account's To, within one `timeout_ms`; a `mailsend_result_t` with a code
  (config, memory, DNS, connect, TLS, auth, rejected, protocol, timeout),
  the last SMTP code, and a sentence that carries the server's own words
  and never the password. `mailsmtp.c` is the conversation alone over a
  transport of four calls: greeting, EHLO and its capabilities, STARTTLS
  when the account says so -- refused outright, password unsent, when the
  server does not offer it, and refused when the server sends anything
  before the handshake (RFC 3207 4.2) -- then AUTH PLAIN, or LOGIN where
  PLAIN is not offered, MAIL, RCPT, DATA with the body made CRLF and
  dot-stuffed and a non-ASCII subject as RFC 2047 encoded words cut on
  letter boundaries, a CR or LF in the subject refused before a byte is
  sent, and QUIT. `mailsend.c` is the transport: getaddrinfo, a connect
  bounded by select(), SO_RCVTIMEO/SO_SNDTIMEO set to what is left of the
  deadline before each call, and Mbed TLS over the same socket through
  set_bio (not esp-tls, which cannot upgrade a connection that has carried
  plain SMTP), verifying against `esp_crt_bundle` and the host name. On
  Mbed TLS 3.6 a CTR-DRBG is given to the config; on 4.0, which has no
  such call, `psa_crypto_init()` -- feckless_cert.c's split. Not in the
  ABI: an ELF app that calls it fails the check on the Mbed TLS names;
  one that does not never pulls the object in.
  *Checked:* `make -C components/feckless_mail/test` gains `smtptest`: a
  scripted server checking every line the client sends, each script run
  with replies whole and a byte at a time -- SMTPS with PLAIN, STARTTLS
  with LOGIN only (and an unanswered QUIT still a success), no STARTTLS
  offered with no AUTH on the wire, data before the handshake, a failed
  handshake, a refused login (multi-line 535 in the sentence, the password
  not), no usable login method, a refused recipient, a timeout, a drop, a
  greeting that is not SMTP, the subject's encoded words, RFC 4648's
  base64 vectors and the Date header (ASan, UBSan). `mailtest` still
  passes. mailsmtp.c at `-O2 -Wall -Wextra -Werror`; mailsend.c at
  `-O2 -Wall -Werror=all -Wextra -Werror=extra` against Mbed TLS v3.6.2's
  and v4.0.0's real headers with stubs for IDF's. Not built with ESP-IDF;
  not yet run against a real server.
- **9002** `mailsmtp.c`'s failure sentence without snprintf. Jinxed
  Writer's build stopped on `fail()`: `"%s: %.*s"` into the 128-byte
  `why`, a caller's sentence and up to 64 bytes of the server's line,
  which the RISC-V GCC's `-Werror=format-truncation` counts as up to 140.
  Host GCC said nothing at `-O2 -Wall -Wextra -Werror`, nor at
  `-Wformat-truncation=2`, which is how 9001 passed its check. Now
  strlcpy and strlcat, which cut where they must and have no format to
  warn about; the server's words get whatever room the caller's leave.
  *Checked:* `make -C components/feckless_mail/test` (smtptest's refused
  login still finds "Login refused: 535 5.7.8" in the sentence). Not
  built with ESP-IDF -- the failing build was the user's, and it is the
  one to rerun.
- **9003** `feckless_fetch`: one file downloaded to a volume, resumably,
  in `feckless_network_handler`. Defeatist's podcast downloader (its
  `poddl.c`, Defeatist 8020-8057, measured on the board there) less what
  was a podcast's, so that Jinxed Writer's dictionary download and any
  other can have what it learned. Its own ~220 s for 15 MB, one
  connection reopening and fsyncing the file every 64 KB, is what made
  the case: Defeatist's single connection measured three times that.
  `feckless_fetch.h`: redirects by hand (relative, scheme-relative, up to
  six; https only if asked), a part file renamed only when whole and the
  caller's VERIFY says yes, resume with Range and If-Range against the
  part file's `.id` (first reply's length and validator, a map of the
  bytes on the card, written to `.id.tmp` and renamed), up to three
  connections in 1 MB stripes for a file of 8 MB and up from a server
  with `Accept-Ranges: bytes`, one writer task leasing the card
  (`storage_io`, BACKGROUND) 16 KB at a time and gathering a segment
  64 KB at a time, a failed connection retried 1, 3 and 10 s later,
  pause, cancel, progress every 0.1 MB, play-while-downloading
  (`feckless_fetch_live`), and `feckless_fetch_why()` for a status
  line. What was Defeatist's alone -- the folder layout, the old part
  file's move, the extension, the pictures and their sidecar, the
  socket budget -- comes back to it through one callback: START,
  HEADERS (may rename), VERIFY (may refuse), DONE, CANCELLED, on the
  download's task with the card not leased. The code is poddl.c's, its
  comments carried with their numbers written "Defeatist 8043" so they
  are not read as this series'. Two changes from it: **a reply with no
  Content-Length** (chunked) read nothing -- a segment ending at -1 was
  "whole" before its first byte, and an empty file was renamed into
  place -- and now reads to the stream's end; and **a scheme-relative
  redirect** (`//host/path`) was taken for a path on the same host. Not
  ported: the thirds of Defeatist 8043 (stripes only, though a map in
  thirds still resumes) and the picture fetch. The network section
  gains the eight names, and says that the two structs are its now.
  `feckless_network_handler` requires `feckless_storage_handler` by
  name, so an app naming the one names the other. Helpers need
  `CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM`; without it, one
  connection. *Checked:* `make -C components/feckless_network_handler/test`,
  with the new `fetchtest`: the tasks on pthreads, an in-memory server
  with Range, If-Range, ETags and redirects, and a storage fake that
  fails the run if the lease is taken twice, held across a network
  read or left held. Twelve cases, every file compared byte for byte:
  one stream with its folders made and the events in order; three
  connections on 20 MB (21 requests, three at once, every byte served
  once); no Accept-Ranges; no Content-Length (which fails with
  poddl.c's line put back); four redirect cases; pause and resume on
  one and on three connections (8.3 of 12.6 MB served again, the
  first request a Range); a resume whose file changed, which starts
  over; a read broken mid-way, retried; 404, no connection, VERIFY
  refusing, starting while one runs; cancel running, cancel paused,
  discard; the name chosen at HEADERS. Under ASan and UBSan, and clean
  under ThreadSanitizer (`run-fetch-tsan`). `feckless_fetch.c` compiles
  against ESP-IDF v6.1's own `esp_http_client.h` and `esp_crt_bundle.h`
  at -O2 with `-Wall -Wextra -Werror`, and at `-Wformat-truncation=2`
  and `-Wformat-overflow=2` at -O2 and -Os, after the three copies into
  a caller's buffer became `copy_out()` (9002's lesson). Not built with
  ESP-IDF; not run on a board. Defeatist and Jinxed Writer change over
  to it in their own repositories.
- **9004** `feckless_fetch`'s statics in PSRAM, where the app allows it.
  They are 16,144 bytes of `.bss` -- the addresses, paths and header
  buffers, the writer's state, the helpers' -- and a component's
  zero-initialised statics are internal RAM unless something moves
  them, which on a Tab5 is the radio's DMA pool. In Defeatist they are
  the launcher's (the `.bin` runs the code and exports it; its player
  ELF is in PSRAM already), and Defeatist once lost its radio to 12 KB
  of `.bss` (its 5178). So every static over a few bytes carries
  `EXT_RAM_BSS_ATTR`: IDF's `.ext_ram.bss` with
  `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY`, nothing without it, so an
  app that has not set the option is as before. None is a FreeRTOS object
  (the queues, the mutex and the helpers' TCBs are on the heap), and none
  is touched with the cache off. The network handler's README asks for
  the option. *Checked:* with the option off, `.bss` is 16,144 bytes as
  before; on, 160 stay and 15,872 go to `.ext_ram.bss` (15,736 against
  ESP-IDF v6.1's own `esp_attr.h`, whose `__COUNTER__` names a section
  for each). `make -C components/feckless_network_handler/test` passes,
  `fetchtest` clean under ThreadSanitizer and passing with the statics in
  their section; the strict `-Wformat-truncation=2` build clean at -O2
  and -Os. Not built with ESP-IDF; not run on a board.
- **9005** `nvsh_list()`, `nvsh_stats()`, `nvsh_log()`: what is in the
  NVS partition. Defeatist's Podcast Index key went missing with no
  sign of what took it, and the partition -- 20 KB, shared by a
  launcher and every app the board runs -- is one nothing could list:
  an app's ELF cannot walk NVS (Defeatist's `nvsns.c` says so), and
  `nvsh_init()` erases the whole of it when it comes up full. Now
  `nvsh_list(ns, cb, arg)` walks every entry (or one namespace's) with
  IDF's iterator and hands each to a callback: namespace, key, type as a
  short name ("u8" ... "i64", "str", "blob", so IDF's enum stays out of
  the ABI), size, an integer's value, a string's or blob's FNV-1a and
  bytes -- valid during the call only, wiped and freed after it, with a
  flag for whether they are text (valid UTF-8, no overlongs, surrogates
  or C0/C1 controls but tab and newline). `nvsh_stats()` is NVS's own
  used, free, available and total entries and namespace count, which is
  what says a partition is filling. `nvsh_log(ns, contents)` writes the
  stats and a line per key to the console; with `contents` it adds what
  is text, quoted, `\n \t \" \\` escaped, cut at 160 bytes on a
  character boundary with "..." after, under a warning line that
  secrets follow -- the partition holds Wi-Fi and mail passwords, API
  secrets and the device's private key. Binary blobs, and strings with
  control characters, are never printed. The nvs section gains the
  three names, which is compatible, and says the two structs are its
  now. *Checked:* `make -C components/feckless_nvs_handler/test`, 112
  checks under ASan and UBSan, the fake NVS now with IDF's iterator,
  stats and type codes and an `ESP_LOGI` that keeps what it is given:
  every entry's type, size, value, hash and bytes as seen by the
  callback; one namespace, an empty one, stopping early, and no
  iterator left unreleased after any of them; the log without contents
  holding no secret, with contents holding them quoted and escaped, no
  binary or control-character value printed, the output valid UTF-8, a
  300-byte string of three-byte characters cut within 160 bytes on a
  boundary and a 300-byte ASCII one at exactly 160. `nvs_handler.c`
  compiles at -O2 with `-Wall -Wextra -Werror` against ESP-IDF v6.1's
  own `nvs.h`, and at `-Wformat-truncation=2 -Wformat-overflow=2` at
  -O2 and -Os. Not built with ESP-IDF; not run on a board.
- **9006** `gfx_draw_text_style()`: bold, heavy and italic text from the
  same glyphs, for Jinxed Writer's markdown. `GFX_STYLE_BOLD` draws a
  second pass a screen pixel right, `GFX_STYLE_HEAVY` two more (one and
  two pixels), `GFX_STYLE_ITALIC` shears each screen row a pixel right
  for every four above the baseline (the bottom of ark12's row 9), so
  descenders lean back. Advances, `gfx_text_w()` and columns are the
  unstyled ones: the glyph's empty column takes bold's extra pixels.
  Clipped by whole glyphs, no ellipsis. `GFX_STRIKE_Y()`/`GFX_STRIKE_H()`
  place a strikethrough over ark12's x-height (rows 6-7). Not added to
  `feckless_abi`'s exports. *Checked:* `make -C
  components/feckless_graphics_handler/test`, `test_style()` at scales 2
  to 5: no style draws what `gfx_draw_text()` does, bold and heavy add
  ink inside the advance, italic keeps the rows and leans an H right and
  a p's descender left of its cell, clipping by whole cells, the strike
  inside the x-height. Drawn to an image on the host and looked at. Not
  built with ESP-IDF; not run on a board.
- **9007** `gfx_draw_text_style()`'s bold offset is a font pixel, not a
  screen pixel: `GFX_STYLE_BOLD`'s second pass is `scale` screen pixels
  right, so a stroke is two font pixels wide at any size -- 9006's one
  screen pixel was a third of a stroke at scale 3 and barely showed.
  `GFX_STYLE_HEAVY`'s third pass is two font pixels over, one past the
  cell, so heavy letters can touch; italic's rows widen to match. Checked:
  `make -C components/feckless_graphics_handler/test`, `test_style()`
  with bold exactly `scale` wider than plain and heavy at most a font
  pixel past the advance; drawn to an image and looked at. Not built
  with ESP-IDF.
- **9008** Bold and heavy keep one-pixel gaps open, and heavy is spaced.
  Each added font pixel is drawn one screen pixel short where it would
  close a one-pixel gap, so m, w and @ stay open in bold and heavy, at
  full weight -- 9007 filled them. Heavy adds a font pixel between
  letters: its cells are `GFX_HEAVY_W(scale)`, measured by the new
  `gfx_text_w_style()`; bold's and italic's advances are unchanged.
  Arrived at by previews, the last of which is
  `components/feckless_graphics_handler/bold.png`, drawn by this code,
  and linked from the component's README. *Checked:* `make -C
  components/feckless_graphics_handler/test`, `test_style()`: heavy
  advances a font pixel more a glyph and stays inside its cells, bold and
  italic advance as plain, bold and heavy m keep both gaps open. Not
  built with ESP-IDF.
- **9009** `mailsend_with()`: a message with a file. With an attachment
  (name, MIME type, bytes) the message is multipart/mixed -- the body
  inline and 8bit as `mailsend()` sends it, so a reader shows it and a
  search finds it, then the file in base64, 76 to a line. The boundary
  is `=_feckless_` and a number base64 cannot hold, checked against the
  body. A name or type that could break the headers (a quote, CR or LF,
  a slash, a parameter in the type) is refused before a byte is sent.
  `mailsend()` is `mailsend_with()` with none, unchanged. Not in the ABI,
  as `mailsend()` is not. For Jinxed Writer's notes, sent with their
  `.md`. *Checked:* `make -C components/feckless_mail/test`, smtptest's
  new `test_attach()`, each script whole and a byte at a time: the
  multipart's headers and both parts, the body's dot stuffed, a body
  holding the first boundary taking the next, the base64 in lines of 76
  decoding to the same 200 bytes, no type as application/octet-stream,
  an empty file, and seven bad names and types refused with nothing
  sent; `warn` at -O2 -Werror. Not built with ESP-IDF; not run on a
  board.
- **9010** `mailsend_with()` takes any number of files: `att` and
  `n_att`, each file its own part after the inline body, in order. One
  bad name or type among them refuses the message with nothing sent. A
  signature change to 9009's function, which no release has had yet.
  For Jinxed Writer's notes with both their `.md` and their log.
  *Checked:* `make -C components/feckless_mail/test`, `test_attach()`:
  two files in order, the second's bytes (binary, 0x00 and 0xFF among
  them) back through its base64, three parts and one closing boundary;
  a bad name after a good one, and a count with no files, refused with
  nothing sent; 9009's cases again. Not built with ESP-IDF.
- **9011** `storage_list.h`: one folder's entries for a chooser, in
  `feckless_storage_handler`. Read in one pass with `f_readdir()`, so a
  file's size and date come with its name (Defeatist's `mediadir`
  reasoning: `stat()` per file reads a FAT folder from the top each
  time); the volume held with `storage_hold_brief()`, each FatFs call
  under the `STORAGE_IO_BACKGROUND` lease. `storage_is_hidden()`'s and
  FAT hidden or system entries left out, the app's filter deciding the
  rest and tagging what it keeps with a kind; folders first, then by
  name (case folded) or newest first, or the folder's own order. Rows of
  the app's own with `storage_list_add()`, pinned first in the order
  added (Avoidant's "Boot app2", Defeatist's cue tracks and podcast
  link); `storage_list_volumes()` for the volumes present. Entries and
  names on the heap, PSRAM first, in blocks that never move; up to `max`
  (4096 by default), with `truncated` set past it. A read error part way
  is `STORAGE_LIST_IO`, never the end of the folder. Its seven calls are
  added to the ABI's storage section, as the rest of the handler is --
  compatible, so storage_v2 still. *Checked:* `make -C
  components/feckless_storage_handler/test` (new; in CI): listtest on a
  real folder in /tmp under ASan and UBSan -- dotfiles, `._` sidecars and
  System Volume Information never offered, the filter's kinds, the three
  orders, pinned rows through each, max, a missing folder, the volumes,
  600 names of 200 bytes across several blocks; the test's copies of
  `storage_join_path()` and `storage_is_hidden()` compared with
  storage.c's. The FatFs half compiled at -O2 with `-Wall -Werror=all
  -Wextra -Werror=extra` against IDF release/v6.1's FatFs as
  `patch_fatfs.py` leaves it, with stub IDF headers. Not built with
  ESP-IDF; not run on a board.
- **9012** `feckless_chooser`: a file chooser over gfx and 9011's
  `storage_list.h`, built as `mailform` is -- the app's rectangle,
  colours and text size, `chooser_draw()`, and `chooser_key()` /
  `chooser_tap()` / `chooser_poll()` saying NONE, REDRAW, OPEN, CANCEL,
  or KEY for a key it has no use for (Jinxed's rename and delete). Rows
  of folders first, then files with size and date, the right column
  dropping the date, then the size, so a name keeps half the row; the
  path line, tail first; a scroll bar only when there is more than fits.
  Keys move, page, jump by first letter, open, and go back up onto the
  folder left; taps open a row, go up from the path line, and page from
  the bar. `root` keeps it in one folder, or lists the volumes; a folder
  that goes with its card is climbed out of. `decorate` adds the app's
  rows after each read and `colour` colours a row by the filter's kind:
  enough for Avoidant's menu, Spurious's dictionary list and Jinxed
  Writer's file mode; Defeatist keeps its browser and takes only 9011.
  Linked into an ELF; its gfx calls are the core list's, its libc calls
  already exported. *Checked:* `make -C components/feckless_chooser/test`
  (new; in CI): choosertest over the real `storage_list.c` on a folder
  in /tmp, gfx recorded, under ASan and UBSan -- each key and tap, back
  out onto the folder left, root never left, a card gone, a file deleted
  under the selection, pinned rows, and what is drawn at three widths;
  `warn` at -O2 -Werror. Two of its checks were seen to fail against
  deliberately broken code. `make preview` draws Jinxed's and Avoidant's
  pages by the real gfx.c, in the component's README. Not built with
  ESP-IDF; not run on a board.
- **9013** Radio down without a false SDMMC error. IDF 6's legacy
  `sdmmc_host_deinit_slot()`, which esp_hosted's SDIO port calls for the
  radio's slot 1, always tries to delete the shared SDMMC controller as
  well. With the card on slot 0 the delete is refused, which is right,
  but `sd_host_sdmmc.c` first logs `E SD_HOST:
  sd_host_del_sdmmc_controller(417): host controller with slot
  registered`, and the function then returns ESP_OK "for backward
  compatibility". Every `wifi_stop()` printed it; Jinxed Writer's boot
  NTP made that every power-on. `wifi_stop()` now sets the `SD_HOST`
  tag to silent around `esp_hosted_deinit()` alone and restores its
  level after. Read from IDF release/v6.1's `legacy/src/sdmmc_host.c`
  and esp-hosted-mcu v3.0.10's `eh_host_port_sdio.c`. *Checked:* not
  compiled -- wifi.c needs esp_hosted's and esp_wifi's headers; `make -C
  components/feckless_network_handler/test` passes, though it does not
  build wifi.c. Not built with ESP-IDF; not run on a board.
- **9014** The RX8130's alarm, and the charger's QC line. `rtc8130.h`
  gains `rtc8130_alarm_take()` (was AF set; AF cleared, AIE off),
  `rtc8130_alarm_arm(secs)` (minute and hour from the chip's own
  counters plus `secs`, rounded up to the minute, the day ignored; AF
  cleared, the time written, AIE set) and `rtc8130_alarm_off()`, all
  under the driver's lock, TIE, UIE and TE kept off -- the registers and
  the order Jinxed Writer's spec section 2 measured. `tab5io_init()`
  now also takes P5 of 0x44, nCHG_QC_EN, out of high-Z and drives it
  low, 50 ms before CHG_EN, as M5Tab5-UserDemo's
  `bsp_io_expander_pi4ioe_init()` / `setChargeQcEnable(true)` /
  `setChargeEnable(true)` does; it was left floating. *Checked:*
  rtc8130.c and tab5io.c compiled with `-O2 -Wall -Werror=all -Wextra
  -Werror=extra` against stub IDF headers. Not built with ESP-IDF; not
  run on a board.

### 9xxx -- IDF 5.x support removed; the ABI's `_ctype_`

The libraries build on ESP-IDF 6.1 only now (the player and the launcher
do), so the 5.x paths are gone:

- **ABI:** `exports.txt` lists `_ctype_`, not `_ctype_b`. `_ctype_b` is
  IDF 5.5's newlib name; IDF 6.1's newlib references `_ctype_`, so an
  app's ELF that used `<ctype.h>` failed the abicheck naming it. The
  crypto section drops Mbed TLS 3.6's names (`mbedtls_sha256` and the
  `feckless_abi_crypto_mbedtls3` marker); the `@when` filter stays, with
  only `mbedtls 4` in use. The 5.5-only shims in `feckless_abi.c`
  (`__issignalingf`, `_uart_set_pin6`) are removed: 6.1 supplies both.
- **Mbed TLS:** `feckless_cert.c`, `feckless_mail`'s `mailsend.c`,
  `wifi_psk()` and `feckless_abi_crypto` are PSA only.
- **IDF:** `lcd.c`, `storage.c`, `wifi.c` and `msc_host_vfs.c` take the
  6.x calls without `ESP_IDF_VERSION` tests; `usb_host_msc` always uses
  the block-device path (`diskio_usb.c` is deleted); the usb manifests
  need `idf >=6.1` and `espressif/usb` unconditionally (the linux-target
  test path stays); CI's fatfs patch check runs on `release/v6.1` only.
- Version 0.9.11 for every component.

*Checked:* `filter_test.cmake` passes. *Not checked:* any of the C,
which is not built here; a launcher build on 6.1 with the renamed export.

### 9xxx -- `__issignalingf` back in `feckless_abi.c`

The patch above removed the weak `__issignalingf` on the belief that
IDF 6.1 supplies it. It does not: the exports table still names it (an
ELF built with the 6.1 toolchain imports it), and a host link on IDF 6.1
(riscv32-esp-elf esp-15.2.0, newlib) stops with
`feckless_abi_syms.c.obj: undefined reference to '__issignalingf'`. The
shim is restored, weak, as it was. `_uart_set_pin6` stays removed: the
link did not ask for it, and 6.x has it.

*Not checked:* a link on IDF 6.1; the C was not built here.

### 9017 -- the feckless ABI is picolibc's

IDF 6 builds with picolibc; Defeatist alone selected newlib
(`CONFIG_LIBC_NEWLIB=y`, its 8134), because an app's `errno` was a
thread-local and ld refuses one in a shared object: `R_RISCV_TPREL_HI20
against errno can not be used when making a shared object`. This makes the
ABI picolibc's, so no launcher or app needs the override.

- **errno:** `feckless_abi` adds `-D__PICOLIBC_ERRNO_FUNCTION=__errno` to
  every component's compile (`idf_build_set_property(COMPILE_DEFINITIONS
  ... APPEND)`, as IDF's pthread component does for its own). `errno` is
  then `(*__errno())`, and `__errno` was already in `exports.txt`. IDF adds
  the same definition itself on `release/v6.1`, but the v6.1 tag does not
  have it (checked: the tag's `esp_libc/project_include.cmake` has no
  `__PICOLIBC_ERRNO_FUNCTION` line); an identical second `-D` is not a
  redefinition. **Not for `esp_libc`:** the tag's `esp_libc/src/picolibc/
  errno.c` has no `#undef errno` before `__thread int errno` (release/v6.1
  does), so with the definition it does not compile ("function '__errno'
  is initialized like a variable"; Avoidant's v0.2.0-pre10). `feckless_abi`
  adds `-U__PICOLIBC_ERRNO_FUNCTION` to esp_libc's target, deferred to the
  end of the project's CMakeLists because the target does not exist until
  esp_libc's own is read; `esp_libc` is then what the tag builds alone, and
  `__errno()` there returns the address of the one variable. Not built with
  ESP-IDF here: the generated compile line was checked on a stand-in
  project (`-D` before `-U`, `-U` wins).
- **Configuration check:** `feckless_abi` stops the configure on
  `CONFIG_LIBC_PICOLIBC` not set, for the launcher and the app alike: a
  `FILE`, the ctype table and `errno` are the library's, and the two sides
  must be one library.
- **ABI names:** `_ctype_` is out, `_ctype_b` is in (picolibc's table;
  `_ctype_` is a macro for an offset into it, so there is no such symbol
  for the table to name). `toupper`, `fputc` and `stdout` are in:
  picolibc inlines `putchar(c)` as `fputc(c, stdout)` into the app, so an
  app that calls `putchar`, which `exports.txt` has always listed, imports
  both. All additions but `_ctype_`; an ELF built against newlib's names
  is refused by esp_elf for `_ctype_`, by name, before any of it runs. The
  ABI version stays `feckless_abi_v1`, as it did for 0.10.0's change of
  `_ctype_b` to `_ctype_`: bumping it is for the maintainer to decide, and
  the configuration check above already stops a newlib app being built.
  `__issignalingf` stays in the table (picolibc's libc defines it; an old
  ELF may import it).
- **Comments** that described newlib are 9018's.

*Checked* with riscv32-esp-elf 15.2.0 (`esp-15.2.0_20251204`) and its
picolibc: `errno` compiles to `tprel` references without the definition and
to a call of `__errno` with it (-O0 to -Os, `-fpic`); a file using every
ctype function, `strcasecmp`, `snprintf`, `sscanf`, `strtod`, `puts`,
`putchar` and the rest of the libc section imports `_ctype_b` (below -Os;
-Os inlines ctype), `toupper`, `fputc` and `stdout` and nothing else
outside `exports.txt`; the generated table compiles; every name in the
libc and libm sections is defined by that picolibc, `__errno` and
`settimeofday` aside, which IDF's libc supplies. *Not checked:* an IDF
build, a launcher or an app link, a board.

### 9018 -- comments that said newlib

`storage_io.c` and `feckless_image_handler`'s CMakeLists.txt described
newlib's stdio and `int32_t`. picolibc's `fread()` on a bufio stream does
the same thing the slicing relies on (a request of at least the buffer's
size bypasses it; a smaller one goes through it), and `int32_t` is `long`
on this toolchain's picolibc too (checked: `__builtin_types_compatible_p`).
Comments only; no behaviour changes. The `stdio would use ... (st_blksize)`
log line is as it was: under picolibc the figure is the VFS's, not what
stdio would have chosen.

## The `umode` branch

Work toward running an app's ELF in RISC-V user mode, so that what the
feckless ABI lets an app call is something the hardware enforces and not
only the symbol table. Numbered `u-feckless-NNNN` (CLAUDE.md), applied to
the `umode` branch. Nothing here changes `feckless_abi_v1` or the way a
launcher runs an ABI v1 app today: that stays the trusted path.

### u-feckless-0001 -- opens the branch: what is known, what is not

Documentation only; no code, nothing built.

**Where it starts from.** NevynUK/ESP32-P4-Protected-Mode-Sample (MIT,
Mark Stevens; read at its 6225988) runs unprivileged code on a Tab5
(ESP32-P4 v1.0, ESP-IDF 5.5.4) under stock FreeRTOS: `mret` into U-mode,
`ecall` as the only way back, and eight windows migrating between both
cores, each hosted by an ordinary task. Its trap vector only records the
trap; the host task then services the syscall, so a syscall may block. It
takes the PMP over: `CONFIG_BOOTLOADER_REGION_PROTECTION_ENABLE=n`, so
IDF does not lock an entry that gives U-mode read/write over DRAM, and its
own unlocked entries bind U-mode but not M-mode. Code taken from it keeps
its copyright notice. Its documentation is the reference for the
per-hart state, the CLIC threshold and `mcause`/`mstatus.MPP` traps.

**What does not carry over as it is.**

- *PSRAM.* The sample grants all of it to U-mode. Launchers and shims here
  run their own code and rodata from PSRAM (`SPIRAM_XIP_FROM_PSRAM`), and
  esp_elf loads an app into the general PSRAM heap. An app needs one
  aligned PSRAM block for image, heap and stack, and only that block is
  granted; the loader allocates it.
- *Every export is a call across the boundary.* U-mode cannot call
  FreeRTOS, the heap, gfx, FatFs or sockets. Each name in `exports.txt`
  and the sections would be an `ecall` stub in the app's table, with a
  dispatcher that checks the pointers against the app's block. libm,
  libgcc and the string functions are better linked into the app than
  trapped. That is a new ABI, `feckless_abi_v2`, and rebuilt apps.
- *Callbacks and tasks.* A task entry, a timer or an HTTP handler is the
  kernel entering U-mode for the app: a host task and a window each.
- *Preemption.* The sample masks interrupts for the whole window, so a long
  computation between syscalls delays the tick on its core. Real apps
  (an FFT, a JPEG decode, a game loop) need U-mode to be preemptible,
  which means the FreeRTOS trap path understanding U-mode.
- *Narrower grants.* The sample gives U-mode execute over all IRAM text,
  leaves the apps able to touch each other, and un-routes IDF's stack
  guard interrupt on both cores. DMA masters (camera, JPEG, DSI) are not
  under the PMP; peripheral registers stay outside every grant.
- *Versions.* Verified on IDF 5.5.4 and a v1.0 chip; this repository is on
  IDF 6.1 and the Tab5s report v1.3. Both are the pre-rev3 CLIC.

**Intended order**, to be revised by what the board says: a
`feckless_umode` component with the PMP take-over and the user entry; the
app's PSRAM block; a stub and dispatcher generator from `exports.txt`;
about ten calls (`gfx_fill_rect`, touch, delay, `printf`, exit); a spike
in one app's standalone shim; only then a launcher. The kernel is one
component that launchers and shims both link, so the same ELF is run the
same way in either.

*Not checked:* anything above on a board or with ESP-IDF 6.1; the
sample was read, not run.

### u-feckless-0002 -- `feckless_umode`: user mode and the PMP grants

A new component, `components/feckless_umode`, at the siblings' version
(0.10.3). The code is the sample's (0001), moved here and reduced to what
every user of it needs; nothing dispatches a syscall yet.

- `src/umode.S`, `include/feckless_umode.h`: the sample's, with the public
  names prefixed (`feckless_umode_*`, `FUMODE_*`) so they cannot meet IDF's
  `riscv/csr.h`. `umode_pmp_grant_extram()` is gone: it patched two entries
  beside the ones IDF locks, which this component requires not to exist.
  `NOTICE` says where it came from. The sample's files are marked Apache-2.0
  and its LICENSE file is MIT; the files keep their mark and the licence text
  is `LICENSE.Apache-2.0`. Which was meant is the author's to say.
- `feckless_umode_pmp_plan()`: grants (range, R/W/X) to the sixteen pmpaddr
  values and four pmpcfg words, as TOR pairs, nothing locked; at most eight
  grants, 4-byte multiples, W only with R. No CSR is touched, so it is a host
  test. Where grants overlap the earlier one wins (lowest matching entry).
  The sample's fixed IRAM/pool/PSRAM grants are not carried over: the caller
  chooses, and the PSRAM one in particular must be the app's block and not
  all of PSRAM (0001).
- `feckless_umode_pmp_apply()`: the plan, written on every core and read
  back. Per core, with interrupts masked so the task cannot move between the
  write and the read-back; a core the first call did not reach is reached by
  `esp_ipc_call_blocking()`. A read-back that differs (a locked entry, or a
  PMP coarser than 4 bytes) is `ESP_ERR_INVALID_STATE` and a log line, not a
  grant that is not the one asked for.
- `feckless_umode_ctx_init()`: a context ready to run.
- `CMakeLists.txt` stops the configure on
  `CONFIG_BOOTLOADER_REGION_PROTECTION_ENABLE=y`.

*Checked:* `test/umode_plan_test.c` on the host under ASan and UBSan (empty,
one, two and eight grants, every rejection, outputs untouched on an error,
the context); `make asm`: `umode.S` assembles with clang for riscv32, stack
guard off and on, against stand-in headers for the guard's macros;
`make compare`: the instructions of `feckless_umode_enter`, the trap entry,
the CSR reads and `pmp_program` are the same as the sample's umode.S, both
guard settings (and a changed `slli` in the trap entry is reported, so the
comparison is not vacuous; the first version of it stopped at the local
labels and would have passed that change). `umode_pmp.c` was parsed against
stand-in headers, which found a missing `<stdbool.h>`.

*Not checked:* an ESP-IDF build of any of it, with 5.5 or 6.1 (the include
paths for `esp_private/hw_stack_guard.h` and `esp_ipc.h`, the `PRIV_REQUIRES`
list, `portSET_INTERRUPT_MASK_FROM_ISR` in an IPC callback); a board.

*Not done, and needed before a window runs:* the stack guard's interrupt
source is shared by both cores' monitors, so it has to be un-routed on every
core at start-up (sample, hard-won-lessons 3.5) or a user's stack overflow
panics the other core; ESP-IDF then no longer panics on an M-mode stack
overflow. That, the host loop that services the traps, and the first
syscalls are 0003.

### u-feckless-0003 -- the host loop, the first syscalls, the guard's interrupt

What runs a context, as far as it can be run without ESP-IDF, and the one
start-up step it needs from ESP-IDF.

- `feckless_umode_run()` (`umode_run.c`, no ESP-IDF in it): enter, then in
  this order read the stack guard's verdict (independent of the trap that
  ended the window, so first), re-enter after an interrupt, service an
  `ecall` (pc stepped past it, the result in a0), or end on anything else
  with the cause, `mtval` and pc. What it needs from the RTOS comes in
  through `feckless_umode_ops_t`, so the test runs the loop as it is against
  a scripted stand-in for `feckless_umode_enter()`.
- Syscalls (`feckless_umode_sys.h`): NOP, WRITE, DELAY_MS, YIELD, EXIT,
  GETCORE; a7 the number, a0..a6 the arguments, a0 the result. The sample's
  PUTS is not carried over (WRITE is the counted form) and the numbers are
  this component's own. From 16 up the window's `ext` function takes the call,
  and may retire the program; that is where gfx, files and the rest come in
  later. A call the host cannot answer is `FECKLESS_SYS_ERR_BADNR`; a pointer
  outside the window's range is `FECKLESS_SYS_ERR_FAULT`, which is
  recoverable (pc is still stepped), unlike a privilege violation or a stack
  excursion, which end the window.
- `feckless_umode_range_ok()` / `_strlen()`: pointers from the program are
  checked against `[lo, hi)` with no addition that can wrap. The sample did
  the same per slot; here the range is the window's, which is to be the app's
  one PSRAM block (0001), so the check and the PMP grant are the same range.
- `feckless_umode_run()` refuses a window whose own memory overlaps `[lo, hi)`:
  the saved registers and pc must not be the program's to rewrite.
- `feckless_umode_guard_unroute()` (`umode_idf.c`): the stack guard's one
  interrupt source taken out of every core's routing, as the sample does once
  at start-up, with the same price stated in its header: ESP-IDF no longer
  panics on an M-mode stack overflow on either core.
- `feckless_umode_run_default()`: the loop on FreeRTOS. The `ecall` stubs a
  program uses are header-only (`feckless_umode_syscall*`, inline asm), so
  they sit wherever the program's own code does.

*Checked:* `test/umode_run_test.c` on the host under ASan and UBSan: the range
check at and beyond its edges including wrap; strlen; every syscall, with the
results as the program would read a0 on resume; a rejected pointer reaching no
console write; the ext hook, including retiring; interrupts and core moves
counted without stepping pc; a fault ending the window unresumed; the stack
verdict taking precedence over an ordinary syscall; every refusal. Three
changes to `umode_run.c` (pc stepped by 8, range off by one, interrupts not
counted) are each caught. `make asm`: the user stubs compile to an `ecall`
with the number in a7. `umode_idf.c` was parsed against stand-in headers.

*Not checked:* an ESP-IDF build (the include for `ETS_INVALID_INUM`,
`esp_rom_route_intr_matrix()`'s signature); a board; two windows at once.

*Still to do before an ELF runs in it:* nothing yet puts a program in a
granted block, so there is no program to enter -- the app's PSRAM block, its
loader, and the table of `ecall` stubs for `esp_elf` are 0004. Interrupts
stay masked for a whole window, so long computation between calls delays the
tick (0001).

### u-feckless-0004 -- the box: an app's ELF loaded into one granted block

What 0003 lacked: a program to enter. An ELF is relocated into one block of
PSRAM, its imports resolved to `ecall` stubs in the same block, and that block
granted to U-mode and nothing else.

- `umode_box.c` (no ESP-IDF): the box. One block, the top `stack` bytes the
  program's stack, the rest a first-fit heap with 16-byte alignment. **The
  allocation table is the host's** (`feckless_box_t`, in internal RAM), not
  headers in the block: the program can write all of the block, and a header
  there would be a pointer it could aim the host's allocator with. A free or
  realloc of anything that is not exactly a live allocation's start is
  refused. 256 allocations at most.
- The stubs: `addi a7, x0, 16+i; ecall; ret` each, then an exit stub
  (`SYS_EXIT 0` and `j .`) that every constructor and the entry return to.
  Written as words by the host; `addi`'s immediate caps a table at 2031 names.
- `umode_calls.c` (no ESP-IDF): the table an embedder gives -- a name and a
  handler, or a name and data copied into the box at load (version symbols
  such as `feckless_abi_v1`: the program reads its own copy) -- and the
  window's ext hook over it. `feckless_umode_heap_calls`: malloc, calloc
  (overflow-checked), realloc, free, heap_caps_malloc (caps ignored), over the
  box.
- `umode_load.c`: `-Wl,--wrap=esp_elf_malloc,--wrap=esp_elf_free` on the
  component's link. While a box loads, every esp_elf allocation is the box's,
  so text, data and esp_elf's own buffers land there; otherwise they pass
  through to the real ones, so the trusted path is unchanged. Read in
  elf_loader 2.1.0 (esp-iot-solution 5fbb259): `esp_elf_malloc` is not weak
  (hence the wrap); `esp_elf_request()` runs preinit/init and the entry in
  M-mode, so it is not called -- the arrays are read once and each function
  run in its own window; `esp_elf_deinit()` runs fini in M-mode, so
  `fini_count` is cleared first; on the P4 `CONFIG_ELF_LOADER_BUS_ADDRESS_MIRROR`
  is off, so text runs where it was allocated and one grant covers it; and the
  P4's flush after relocation is a full D-cache write-back and I-cache
  invalidate, which covers the stubs written before it. The symbol table is
  registered with esp_elf for the load. An import that resolved to a host
  address anyway (another table still registered) would fault on use: the
  host's code is not granted.

*Checked:* `test/umode_box_test.c` on the host under ASan and UBSan, with the
box mapped below 4 GB (the calls return pointers in a `uint32_t`, as on the
target): init, alloc, free of interior/host/stack/duplicate pointers,
first-fit reuse, the table limit, realloc in place and moving, 20000 random
operations against an overlap and order check, the heap calls through the ext
hook. `make asm`: the stubs the host writes are byte for byte what the
assembler makes of `stubs_expected.S`. Four changes (a free that accepts an
interior pointer, first fit without the padding, the ecall number off by one,
`ret` through a1) are each caught. `umode_load.c` was parsed against
elf_loader 2.1.0's own headers with stand-ins for IDF's, which found a missing
include.

*Not checked:* an ESP-IDF link (that `--wrap` on an INTERFACE link option
reaches elf_loader's archive; the `elf_loader` component name with the
registry's `espressif/elf_loader`; `private/elf_types.h` being reachable); a
board; an ELF loaded.

**What this does not yet give Spurious.** A scan of its main/ for names in
`exports.txt` and the storage section finds about 75 it calls. None but the
heap is in a box table yet, and three kinds need more than a handler: libc's
pure functions (string, `snprintf`, `qsort` with its comparator) belong in
the app's ELF, not behind an ecall -- a host `qsort` would call the
comparator in M-mode; the stdio and FatFs calls pass handles (`FILE *`,
`FIL *`) whose contents the program could rewrite, so the host must hand out
numbers and keep the objects; and the board's init calls are the launcher's
and become no-ops. That is 0005 onward, with a sandbox build of the app.

### u-feckless-0005 -- `feckless_box_libc`: building an app for a box

The app's side. A boxed app's imports are `ecall`s, so pure libc goes in the
app's own ELF -- and `qsort` must: the host would run the comparator in
M-mode. `feckless_abi_box_elf()` is `feckless_abi_elf()` with that linked in.

**What the toolchain allows**, found by linking each name alone into a shared
object with `project_elf()`'s flags (riscv32-esp-elf 15.2.0,
`esp-15.2.0_20251204`, rv32imafc_zicsr_zifencei_zaamo_zalrsc/ilp32f, the
release fetched from GitHub):

- picolibc's `libc.a` is not built `-fPIC`. The string and memory functions,
  `qsort`, `bsearch`, ctype, `gmtime_r` and a few more link. `strtol`/`strtoul`/
  `atoi` do not (`R_RISCV_TPREL_HI20 against errno`: thread-local), nor
  `snprintf`/`vsnprintf`/`sscanf`/`strtod`/`strftime` (`R_RISCV_HI20` to
  their tables), nor `rand`, `abort`, `strerror`; `strdup` and
  `__assert_func` pull in picolibc's own `malloc` and locks.
- `libgcc.a`: 20 of v1's 39 helpers link with nothing undefined (listed in
  `libgcc_names.txt`); the other 19 reach `__clz_tab` absolutely
  (`__adddf3`, `__muldf3`, `__divdi3`, `__floatsidf` ...). They take and
  return values only -- no pointer, no callback -- so they stay imports and
  the host can answer them.
- `project_elf()` links with its own flags, not the project's, so `-lc`
  would be the default multilib's newlib (ilp32, against an ilp32f app), and
  `-print-file-name=libc.a` answers newlib's even with `--specs=picolibc.specs`.
  The macro finds picolibc's as the specs do:
  `$(-print-sysroot)/../picolibc/riscv32-esp-elf/lib/$(-print-multi-directory)/libc.a`.

**So:**

- `cmake/archive_subset.cmake` copies named members out of an archive --
  `picolibc_names.txt` from `libc.a`, `libgcc_names.txt` from `libgcc.a` --
  never the archive whole. `nm -A` prints `lib.a:member.o:00000000 T name`
  with no space after the member's colon; the first version expected one.
- `src/box_strto.c`: `strtol`, `strtoul`, `strtoll`, `strtoull`, `atoi`,
  `atol`, written for this (MIT), setting `errno` through `__errno()`.
  `src/box_errno.c`: the app's own `__errno()` (feckless_abi already compiles
  every component with `-D__PICOLIBC_ERRNO_FUNCTION=__errno`, 9017).
- `src/box_printf.c`: eyalroz/printf v6.4.0 (commit 92c7359, MIT, vendored
  unchanged in `vendor/printf`) under the standard names; `printf` goes a
  character at a time to the host's `putchar`. Its `%Lf` converts with
  `__trunctfdf2`, which is not a v1 name; abicheck refused the first link for
  it. A long double is passed by reference on this ABI, so a host call would
  read a pointer; it is linked from libgcc instead, where it is clean.
- `include/feckless_box.h`: `feckless_abi_require_box()`, a reference to
  `feckless_abi_box_v1`. `feckless_abi/sections/box.txt` declares that
  symbol and no names, so abicheck accepts it, and no component defines it:
  a trusted launcher refuses a boxed app by name, and a boxing one (which
  puts it in its call table as data) can tell one from the other.
- `cmake/boxcheck.cmake`, after abicheck: the ELF refers to
  `feckless_abi_box_v1`, and imports nothing in `inapp.txt`.

*Checked with the toolchain*, outside IDF: the three sources compile; the
subsets are 31 members of `libc.a` and 17 of `libgcc.a`; a test app using
`qsort` with a comparator, `strcasecmp`, `strtoul`, `snprintf` with `%f` and
`%lld`, `tolower`, `isalpha`, `errno`, `malloc` and `gfx_draw_text`, linked
with `project_elf()`'s link line, imports only `malloc`, `free`,
`gfx_draw_text`, the two version symbols and ten libgcc value helpers, and
every relocation naming a symbol names one of those; abicheck passes it
(`15 symbols, all in feckless_abi_v1 + feckless_abi_box_v1`) and boxcheck
passes it. Without `feckless_abi_require_box()`, boxcheck fails and deletes
the ELF; with `box_strto.c` left out, abicheck passes (`strtoul` is a v1 name)
and boxcheck fails naming `strtoul` and `atoi`. The first boxcheck run failed
for another reason -- `IN_LIST` needs policy CMP0057 in script mode -- and was
fixed before these. *On the host:* `strto*` against glibc, 801980
comparisons of value, end and errno, the same (an invalid base is unspecified
by C, and glibc leaves `*end` unset there, so `end` is compared for valid
bases only); four changes to it (overflow off by one, `0x` with no hex digit,
`LONG_MIN` refused, leading `0` not octal) each caught; the vendored printf
against glibc's for 16 format lines.

*Not checked:* the macro in an IDF build (`CMAKE_C_FLAGS` carrying
`-march`/`-mabi` at that point, the generated archives' dependencies,
`esp-idf/feckless_box_libc/libfeckless_box_libc.a` as the path), any of it on
a board, and an app's ELF loaded into a box. No launcher serves a boxed app's
imports yet; the libgcc value helpers, stdio and the rest are the host's table
(0006).

### u-feckless-0006 -- the host's call table, and an eighth argument

`feckless_box_calls`, a new component a launcher links: the table
`feckless_umode_load()` takes, for what Spurious's ELF calls outside storage.
Its imports were read from its main/ (its ELF build: main.c, stardict.c,
sd_io_fatfs.c, dmeta.c, with tinfl and feckless_keyboard linked in) --
`usbhost_*` and `esp_log_level_set` are its standalone build's only, and
feckless_keyboard takes only `gfx_draw_text`, `gfx_fill_rect`, `gfx_text_w`.

- **The stub changes** (feckless_umode, 0004): `gfx_draw_para` has eight
  integer arguments, the eighth in a7, which the stub overwrote with the call
  number. Each stub now starts `mv t0, a7` (t0 is caller-saved, so a function
  may clobber it), 16 bytes a stub; `feckless_umode_arg(w, 7)` reads t0, and
  from the ninth on the program's stack, range-checked.
  `feckless_umode_ret64()` sets a1 for a 64-bit result; the host loop writes
  only a0, so a handler's a1 stands.
- **libgcc**: the 15 helpers 0005 found cannot link into an app and pass only
  integer registers. A double and an int64 are a pair of registers, low word
  first, on ilp32f. Division by zero and `INT64_MIN / -1` give RISC-V's
  answers instead of C's undefined ones. The five that pass a float in fa0 are
  left out: the trap saves no FP register, and whatever the host runs before
  the handler may change fa0.
- **The log**: IDF 6.1's `esp_log(esp_log_config_t, tag, fmt, ...)` is what
  `ESP_LOGx` calls; the config is a one-word struct, passed in a0 as a
  `uint32_t` is (checked: `ESP_LOG_CONFIG_INIT(INFO)` with formatting is
  `li a0,19` with riscv32-esp-elf 15.2.0). `feckless_box_libc` now has
  `esp_log`, `esp_log_va`, `esp_log_write` and `esp_log_writev`: they format
  the line with its vsnprintf and call `feckless_box_log(kind, cfg, tag, msg)`,
  a name in the box section (`sections/box.txt`), so abicheck takes it. The
  host copies both strings (tag to 64 bytes, message cut at 512) and calls
  its own `esp_log`/`esp_log_write` with `"%s"`.
- **The board**: the app's init calls go to the real ones, which the libraries
  allow again (feckless_abi's README); the panel and the I2C bus are numbers
  in the app (`lcd_init` writes one, `gfx_init`/`touch_init` take only that
  number). `touch_get`, `gfx_wrap_line`, `time` and `gettimeofday` check
  their out-pointers before writing; the gfx calls check their strings
  (to 64 KB).

*Checked:* `test/box_calls_test.c` on the host under ASan and UBSan, through
feckless_umode's ext hook with a box mapped below 4 GB, against recording
stand-ins for gfx, lcd, touch, tab5io, the timer and the log: every call's
result and what reached the board, every refusal (host pointers, unterminated
strings, wrong tokens, a long tag), the eighth argument through t0, 64-bit
results through a1. Four changes are caught (a missing out-pointer check --
the first version of that mutation did not compile and so tested nothing --
the colour read from a6, C's division by zero, the host's panel pointer handed
out). feckless_umode's tests pass with the new stub, and the stubs are still
the assembler's bytes.

*Not checked:* `box_calls.c` against the real headers in an IDF build (its
`#include`s are the libraries' own names: `gfx.h`, `lcd.h`, `touch.h`,
`tab5io.h`); a board.

### u-feckless-0007 -- storage for a boxed app

`src/box_storage.c` in `feckless_box_calls`: what Spurious's ELF calls to
read its dictionaries -- sd_io_fatfs.c's FatFs with fast seek, stardict.c's
`fopen`/`fgets`/`fread`/`fseek`/`ftell`, main.c's `opendir`/`readdir` and
the storage handler -- added to `feckless_box_calls_v1()`, with
`feckless_abi_storage_v2` as data.

- **FatFs**: an app's `FIL` holds `obj.fs` (the volume's `FATFS`) and
  `cltbl`, both pointers FatFs follows; an app that rewrote them could aim
  FatFs at anything. So the real `FIL` is the host's (8 open at once), keyed
  by the address of the app's, which the app allocates and passes to every
  call. After each call `obj.objsize`, `fptr`, `flag` and `err` are copied
  into the app's -- what `f_size`, `f_tell`, `f_eof` and `f_error` read
  (IDF 6.1's `ff.h`, fetched to check) -- and nothing else.
- **Fast seek**: the app sets `cltbl` to its own table and calls
  `f_lseek(fp, CREATE_LINKMAP)`, reading the first word back (used, or
  needed with `FR_NOT_ENOUGH_CORE`). The host reads that count (at most 16384
  items, the table checked to fit the box), builds a table of its own, hands
  FatFs that one, and writes back only the first word. Later seeks and reads
  use the host's table while the app's `cltbl` still names the table it was
  built for; clearing or changing `cltbl` turns it off.
- `f_lseek`'s 64-bit offset arrives in a1 (low) and a2 (high): riscv32-esp-elf
  15.2.0 puts a `uint64_t` second argument there with no pair alignment
  (`mv a2,a1; li a1,5`).
- **stdio and directories**: a number for each `FILE` (8) and `DIR` (4);
  `readdir()` copies the `struct dirent` into a buffer in the box that the
  slot keeps; `storage_mount_path()` copies its string into the box. If the
  app frees one of those buffers it is allocated again.
- **The lease and the hold**: `storage_io_release()` releases only what the
  app acquired; `feckless_box_calls_end()` closes every file and directory,
  releases the app's remaining leases and its `storage_hold()`. The launcher
  calls it after `feckless_umode_unload()`.

*Checked:* `test/box_storage_test.c` on the host under ASan and UBSan,
playing Spurious's own sequences (`sd_file_open` with its 256-item table
refused and the 300 asked for, `sd_file_read` with a seek past 4 GiB, the
.ifo by `fgets`, a whole file by `fseek`/`ftell`/`fread`, the dictionary scan
with `readdir`'s `d_name` and `storage_join_path`) against a stand-in FatFs
and storage handler over real files and the host's stdio and dirent, and the
end with everything left open. Seven changes are each caught: the host's
`FATFS` pointer copied back, FatFs handed the app's table, the high word of
the offset dropped, `end` forgetting the lease, a release past the app's own
acquires, `readdir` returning the host's dirent, `fread` without its
overflow check -- that one only after the test was made to rewind and keep a
canary after a 16-byte buffer; the first version read at end of file and
passed it. 0006's test now counts three data entries.

*Not checked:* the real FatFs, storage handler and VFS in an IDF build; a
board. Not served yet: writes, `f_stat`, `f_opendir`/`f_readdir`, POSIX
`open`/`read`/`access`, `fseeko`/`ftello`, the storage list and the rest of
the storage section.

### u-feckless-0008 -- an app that gives up ends; the log level

A comparison of Spurious's sources (main/, tinfl, feckless_keyboard) against
`inapp.txt` and the table found one real gap: `ESP_ERROR_CHECK()`, three
times in its app_main, imports `_esp_error_check_failed`, which no box
served, so its load would have failed on that name. (`access`, `open`,
`read`, `write`, `log` and `fseeko` matched only words in comments;
`esp_app_get_description` is linked into the app by `feckless_abi_elf()`.)

- `_esp_error_check_failed`, `abort` and `__assert_func` log what failed --
  the code, file, line, function and expression, each string checked and
  copied, "?" for one that is not the app's -- and end the app
  (`FECKLESS_UMODE_END_KILLED`). The launcher goes on.
- `esp_log_level_get` and `esp_log_level_set` for the app's tags; `"*"` is
  refused, being every tag's default.

*Checked:* in `test/box_calls_test.c`, each ends the window, with what it
logged, and the level calls with their refusals. 73 calls. Not built with
ESP-IDF.

### u-feckless-0009 -- the new components at the branch's version

The `umode` branch was made from `ff85788`, the commit before v0.10.3's
"Bump version to 0.10.3", so its components say `version: "0.10.2"`; the
three this branch added (`feckless_umode` 0002, `feckless_box_libc` 0005,
`feckless_box_calls` 0006) were written at 0.10.3, and the "One version"
check in `test.yml` fails on two versions. They are 0.10.2 now, as the
branch is. Nothing else changes; the code at `ff85788` is v0.10.3's but for
those lines.

*Checked:* `grep -h '^version:' components/*/idf_component.yml | sort -u` is
one line, `version: "0.10.2"`. The tag workflow sets every component at once
when the branch is tagged or merged.

### u-feckless-0010 -- IDF 6.1's stack-guard macro takes its registers

The first ESP-IDF build (Avoidant's umode branch, IDF 6.1) stopped in
`umode.S`, at both hand-overs of the stack guard:

    umode.S:83: Error: illegal operands `sw ,(((0x3FF00000+0x6000)+0xb8)-(0x3FF00000+0x6000))(t0)'

IDF 6.1's `ESP_HW_STACK_GUARD_SET_BOUNDS_CUR_CORE` is `reg1 sp_min sp_max`;
IDF 5.5.4's, which the protected-mode sample was written against, took
`reg1` alone and stored a0 and a1. Called with `t0` only, `\sp_min` and
`\sp_max` were empty. Both calls now name them -- `t0 a0 a1`, where the code
already put the bounds -- so the instructions are what 5.5 assembled. The
`MONITOR_START`/`STOP` macros are unchanged between the two.

**Why `make asm` did not catch it:** its stand-in for
`esp_private/hw_stack_guard.h` (0002) made every guard macro a `nop`, so any
arguments assembled. The stand-in is now IDF 6.1's own macros, copied from
`components/esp_system/port/include/private/esp_private/hw_stack_guard.h`
(Apache-2.0, Espressif's notice kept), with the ESP32-P4's register addresses
from IDF 6.1's `assist_debug_reg.h`, `reg_base.h` and `assist_debug_ll.h`
(base 0x3FF06000, spill bits 0x300). `make compare` rewrites the sample's
5.5-style call to the 6.1 form in a copy before assembling it, and the guard
path is now compared through the real macro bodies, not nops.

Also from reading IDF 6.1's headers for the files the build had not reached:
`ETS_INVALID_INUM` is in `soc/soc.h`, which `umode_idf.c` did not include and
none of its includes bring in; it is included now. `esp_rom_route_intr_matrix()`,
`ETS_ASSIST_DEBUG_INTR_SOURCE`, `esp_ipc_call_blocking()`,
`esp_cpu_get_core_id()` and `portSET_INTERRUPT_MASK_FROM_ISR()` are where
and as the code uses them.

*Checked:* with the new stand-in, the old `umode.S` gives the build's error
(`illegal operands 'sw ,((0x3FF06000+0xb8)-0x3FF06000)(t0)'`) from the IDF
toolchain's GNU as (riscv32-esp-elf 15.2.0); the new one assembles with it,
guard on and off, and with clang (`make asm`); `make compare`: the same
instructions as the sample, guard on and off; the host tests pass.
*Not checked:* the rest of the IDF build past `umode.S`.

### u-feckless-0011 -- what the PMP read back, and its granularity

*On a Tab5* (ESP32-P4 v1.3, IDF v6.1-829-g9a97f6c54ec), Spurious's
standalone .bin of u-spurious-0001: the card mounted, esp_elf relocated the
reader into the box (`ELF: elf->entry=0x48005fe0`), and then

    E feckless_umode: PMP read back differently on cores 0x3: an entry is locked
      (CONFIG_BOOTLOADER_REGION_PROTECTION_ENABLE must be off) or the granularity
      is coarser than 4 bytes
    E standalone: the reader would not load in a box: ESP_ERR_INVALID_STATE

on both cores. `grep REGION_PROTECTION sdkconfig` there:
`# CONFIG_BOOTLOADER_REGION_PROTECTION_ENABLE is not set`. So the more likely
cause is the granularity -- the box's end, `base + size`, rounded by the
hardware -- which the sample never showed: its ranges were IRAM's and a
static pool's, aligned by the linker. This patch makes the next run say
which, instead of guessing again:

- **Before** programming, each core's sixteen entries are read; any already
  locked is logged with its cfg and address.
- **The granularity** is measured as the privileged spec gives it: with entry
  15 off (and not locked), all ones into `pmpaddr15`, read back; the bits
  below G read as zero and the granule is `2^(G+2)` bytes. Logged at INFO on
  every apply (`core N: PMP granularity B bytes`), and with the raw value on a
  mismatch.
- **On a mismatch**, each entry that differs is logged as written and as read
  (cfg and address), per core, with whether the build had
  `CONFIG_BOOTLOADER_REGION_PROTECTION_ENABLE` on.
- **The box** (`feckless_umode_load()`) is now 4 KB-aligned at both ends --
  `heap_caps_aligned_alloc(4096, ...)` and the size rounded down to 4 KB --
  where it was 64 bytes. A precaution for any granularity up to 4 KB; the
  log above will say whether it was the cause.
- Building with the IDF toolchain's gcc found one more thing, from 0002 (and
  the sample): the static assert on the CLIC mask shifted `0xFF << 24` as an
  `int`, an overflow compared with an unsigned constant (`-Wsign-compare`
  under `-Werror`). It shifts a `uint32_t` now.

*Checked:* every `feckless_umode` source parses with riscv32-esp-elf-gcc
15.2.0 at `-Wall -Wextra -Werror` against stand-in headers (IDF's log macros
there discard their arguments, which is why `granule()` is marked unused: IDF's
can, with logging compiled out); the granule arithmetic for 0xffffffff,
0xfffffffe, 0xffffffc0, 0xfffffc00 (4, 8, 256, 4096 bytes); the host tests and
`make asm`. *Not on a Tab5 yet.*

*What to send back from the next run:* the `feckless_umode: core N: PMP
granularity` lines, and if it still fails, every `feckless_umode: core N:
entry ...` line.

### u-feckless-0012 -- locked entries are the bootloader's

*0011 on a Tab5* (Spurious's standalone .bin, ESP32-P4 v1.3, IDF
v6.1-829-g9a97f6c54ec): `core 0: PMP granularity 0 bytes ... -- not probed,
entry 15 locked`, and on both cores eight entries locked before this firmware
wrote any:

    entry  0 was locked before: cfg 0x9b addr 0x09ffffff   NAPOT RW  0x20000000-0x30000000
    entry  1 was locked before: cfg 0x9b addr 0x0ffc3fff   NAPOT RW  0x3ff00000-0x3ff20000
    entry  2 was locked before: cfg 0x9d addr 0x13f03fff   NAPOT RX  0x4fc00000-0x4fc20000 (ROM)
    entry  3 was locked before: cfg 0x80 addr 0x13fc0000   OFF, the TOR base 0x4ff00000
    entry  4 was locked before: cfg 0x8f addr 0x13ff0000   TOR RWX   to 0x4ffc0000 (HP SRAM)
    entry  6 was locked before: cfg 0x9d addr 0x107fffff   NAPOT RX  0x40000000-0x44000000 (flash)
    entry 11 was locked before: cfg 0x9f addr 0x14042fff   NAPOT RWX 0x50100000-0x50108000 (LP RAM)
    entry 15 was locked before: cfg 0x9b addr 0x1401ffff   NAPOT RW  0x50000000-0x50100000

That is IDF 6.1's `esp_cpu_configure_region_protection_rev_less_than_v3()`
(`esp_hw_support/port/esp32p4/cpu_region_protect.c`, non-MEMPROT branch),
entry for entry -- so not granularity, and not the ROM. Its one caller is
`bootloader_init_mem()`, under `CONFIG_BOOTLOADER_REGION_PROTECTION_ENABLE`,
in the second-stage bootloader and in the app's `cpu_start.c`. The app's
`sdkconfig` had the option off; the bootloader's compile time,
`Nov 8 2026 16:57:03`, was the same in both runs while the app changed. So the
bootloader in the flash was built with it on, and its locks hold into the app.

The error now says so when any entry was locked before this firmware set one,
and points at `build/bootloader/config/sdkconfig.h` and at flashing the
bootloader (`idf.py flash` or `bootloader-flash`; `app-flash` keeps the old
one). With nothing locked, a mismatch is the granularity, as before.

*Checked:* `umode_pmp.c` parses with riscv32-esp-elf-gcc 15.2.0 at `-Wall
-Wextra -Werror`. Not on a Tab5. The granularity is still unmeasured: the
next run with a rebuilt bootloader logs it.

### u-feckless-0013 -- ESP-IDF's app region protection, not run

0012 blamed the bootloader. It was not: the Tab5's bootloader was built with
the option off (`build/bootloader/config/sdkconfig.h` has no
`CONFIG_BOOTLOADER_REGION_PROTECTION_ENABLE`, and the one in the flash is that
build, `Nov 8 2026 16:57:03`). The lock bits say who wrote the entries:
`cpu_region_protect.c` writes entries 3, 4, 6 and 11 with `CONDITIONAL_*`
permissions, which in a bootloader build have no L bit and in an app build
do. 0011 read them locked (0x80, 0x8f, 0x9d, 0x9f). The app wrote them.

**Why:** ESP-IDF 939043ea83 (release/v6.1, 2026-08-06), "do not gate
application region protection on bootloader config". `bootloader_mem.c` is
now `#if !BOOTLOADER_BUILD || CONFIG_BOOTLOADER_REGION_PROTECTION_ENABLE`: the
app always runs `esp_cpu_configure_region_protection()`. The Tab5's IDF,
v6.1-829-g9a97f6c54ec, has it; the v6.1 tag, which 0001-0011 were read
against, and IDF 5.5.4, which the sample was, do not. No configuration turns
it off in the app any more.

Living with its layout is no box: it gives user mode read and write over
internal SRAM (entry 4, 0x4ff00000-0x4ffc0000: the host's stacks, task
control blocks, a window's saved registers) and over the CPU's and the LP
peripherals (entries 0, 1, 15), all locked.

**So:** `src/umode_wrap.c`, and `-Wl,--wrap=esp_cpu_configure_region_protection`
on the component's link, as 0004 wraps `esp_elf_malloc`. The app's call is a
no-op that counts itself (in IRAM; the count in .data from a non-zero start,
so a later .bss clear does not hide it); the PMP stays in its reset state, as
the v6.1 tag left it. `feckless_umode_idf_region_protection_skipped()` gives
the count, and every apply logs it (`ESP-IDF's region protection skipped N
times at start-up`, one per core). Entries locked with a count of 0 is the
wrap missing from the link, and the error says so; locked with the count
right is the bootloader, as 0012 says. Lost, for an app that links this
component: what IDF's call adds -- M-mode faults on unmapped gaps and on 0x0,
and W^X for IRAM -- which the v6.1 tag did not add either with the option
off. The bootloader is a build of its own and still needs the option off:
it locks entries 0, 1, 2 and 15 whatever `CONDITIONAL_*` says.

Also, from the Tab5's sdkconfig: `CONFIG_SOC_CPU_PMP_REGION_GRANULARITY 128`.
`feckless_umode_pmp_apply()` now refuses a grant whose ends are not on
`SOC_CPU_PMP_REGION_GRANULARITY`; the box, 4 KB-aligned since 0011, is.
`feckless_umode_pmp_plan()`, which has no ESP-IDF, still checks 4 bytes.

*Checked:* in IDF at 9a97f6c54ec, P4 has one definition of
`esp_cpu_configure_region_protection()` (`port/esp32p4/cpu_region_protect.c`)
and one caller (`bootloader_mem.c`), in another component's archive. The wrap
on that shape, with riscv32-esp-elf 15.2.0: a caller in one archive, the
definition in another, `umode_wrap.o` in a third -- without the wrap
`bootloader_init_mem` jumps to `esp_cpu_configure_region_protection`, with it
to `__wrap_esp_cpu_configure_region_protection`, and IDF's is not linked.
`umode_pmp.c`, `umode_wrap.c`, `umode_load.c`, `umode_idf.c` parse at `-Wall
-Wextra -Werror`. *Not on a Tab5.*

*What to look for:* `feckless_umode: ESP-IDF's region protection skipped 2
times at start-up`, `core 0: PMP granularity 128 bytes`, no `locked before`
lines, and the reader in its box.

### u-feckless-0014 -- a box's ELF is resolved against the box alone

*0013 on a Tab5* (Spurious's standalone .bin, ESP32-P4 v1.3, IDF
v6.1-829-g9a97f6c54ec) -- **the first app in a box**:

    feckless_umode: core 0: PMP granularity 128 bytes
    feckless_umode: core 1: PMP granularity 128 bytes
    feckless_umode: ESP-IDF's region protection skipped 2 times at start-up
    feckless_umode: app in its box at 0x48003000, 24576 KB, 73 calls; 60 KB used by the load
    standalone: the reader in a box of 24576 KB
    spurious: === Spurious v0.1.0-pre1-12-gf273f56-dirty, built ... ===
    tab5_lcd: ST7121 initialised (720x1280)
    ST7123: Touch panel create success, version: 1.0.2

The wrap worked (two calls skipped, nothing locked); the granularity is 128
bytes, as `SOC_CPU_PMP_REGION_GRANULARITY` says; the reader's constructors and
app_main ran in user mode; its banner came through the box's log line, its
panel and touch through the box's board calls. Thirty seconds on, the card was
swapped, and on the remount:

    standalone: the reader ended: reason 1, instruction access fault at 0x4ff09c62
    --- 0x4ff09c62: calloc at esp_libc/src/heap.c:39

The reader had jumped to IDF's own `calloc`, in internal RAM; the PMP stopped
the fetch and the window ended with FECKLESS_UMODE_END_FAULT -- the box doing
what it is for. But the import should have been the box's stub.

**Why:** elf_loader 2.1.0's `elf_find_sym()` calls a resolver whose default,
`elf_find_sym_default()`, searches elf_loader's own tables first -- with
`CONFIG_ELF_LOADER_LIBC_SYMBOLS` (default y) `malloc`, `calloc`, `realloc`,
`free`, `puts`, `putchar`, `printf`, `__divdf3`, `__ltdf2`, `__gtdf2`,
`__floatunsidf`, `__fixunsdfsi` and the rest, with `CONFIG_ELF_LOADER_ESPIDF_SYMBOLS`
lwIP's socket calls -- as the host's functions, and the tables registered with
`esp_elf_register_symbol()` only after. 0004's registered table lost to them.
The first card evidently opened no dictionary, so `calloc` on the remount was
the first of them to run.

**So:** while a box is relocated, `elf_set_symbol_resolver()` (elf_loader's
own hook, `private/elf_symbol.h`) installs `box_resolve()`, which answers from
the box's table and nothing else; `elf_reset_symbol_resolver()` puts the
default back after. Every import resolves to a stub or a data copy in the box,
or to nothing, and then esp_elf fails the load naming it (`Can't find common
<name>`), which is the point: a name the box does not answer must not
quietly reach the host. The box's table is no longer registered with
`esp_elf_register_symbol()` at all -- with the resolver it is not needed, and
registered it was visible to anything else loaded meanwhile. Relocation looks
names up in exactly two places, both through `elf_find_sym()`.

*Checked:* `umode_load.c` parses with riscv32-esp-elf-gcc 15.2.0 at `-Wall
-Wextra -Werror` against elf_loader 2.1.0's own headers. *Not on a Tab5.*

*What to look for:* the reader through a card swap, a dictionary opened and a
word looked up; or, if the load now fails, `ELF: Can't find common <name>`,
which is a name the box's table lacks (feckless_box_calls).

### u-feckless-0015 -- reads into a box go through internal RAM

*0014 on a Tab5* -- **Spurious in its box, end to end**: banner, panel and
touch as before; the card swapped; then, all through the box's calls,

    tab5_storage: microSD mounted at /sd
    sd_io: 0:/dict/wiktionary_en-simple_all_nopic_2026-10/...dict.dz: fast seek, 4 of 256 table items
    spurious: wiktionary_en-simple_all_nopic_2026-10: 54189 words, 475 synonyms (out of order: sorted), opened in 910 ms

-- the dictionary scan (`opendir`/`readdir`, `storage_join_path`), the .ifo by
`fgets`, the .idx by `fread`, the .dict.dz through FatFs with the box's
mirrored fast-seek table (0007), and the sort in the app's own `qsort`. The
reader's imports, read from its ELF (`readelf -s`), were 59, every one in the
box's table and none an in-app name.

And one thing seen: the panel went cyan for a moment while the dictionary
loaded. That is the DSI underrun feckless_graphics_handler's lcd.c and
feckless_storage_handler's storage_io.c describe: the panel scans out of
PSRAM and cannot wait, and a large read whose DMA lands in PSRAM makes the
card a second bus master there -- "the cyan follows reads, not draws". The box
is in PSRAM, and 0007 handed the app's buffer straight to `fread()` and
`f_read()`: the whole .idx in one `fread`, the .dict.dz in 16 KB `f_read`s.

**So:** both now read through one internal, DMA-capable staging buffer in
8 KB slices (`READ_SLICE`, storage_io's `PLAYBACK_SLICE`) and copy into the box
with the CPU. A short slice or an error ends the read, as one call would have;
`fread` returns whole items; the bytes and errors are what the unsliced call
gave. If the buffer cannot be allocated, reads go direct as before. A boxed
app's reads are then better behaved than a trusted app's, whose large reads
still DMA into PSRAM.

*Checked:* `test/box_storage_test.c`: a 19000-byte `f_read` across two slice
edges lands byte for byte and is three FatFs reads; a read past the end
stops short; the stand-in FatFs records that no `f_read` was given a
destination in the box; `fread` of 20000 bytes byte for byte, and 3-byte
items over 20000 bytes give 6666 with the file at its end. Two changes are
each caught (FatFs handed the box again; `fread` counting bytes).
`box_storage.c` parses with riscv32-esp-elf-gcc 15.2.0 and picolibc's headers
at `-Wall -Wextra -Werror`. *Not on a Tab5.* What to look for: the same
dictionary opening with no cyan, and the "opened in" time about the same.

### u-feckless-0016 -- windows are interruptible

*0015 on a Tab5*: Spurious in its box, the same dictionary -- still a blue
flash, between its "opening" screen and the word list. So not the reads.

**The cause** is the window, as 0001 warned ("a long computation between
syscalls delays the tick on its core"). The sample's `umode_enter` raised the
CLIC threshold to its maximum, so no interrupt reached a window; and IDF's
DPI driver restarts the framebuffer DMA from an interrupt at the end of every
frame (IDF at 9a97f6c54ec, `esp_lcd/dsi/esp_lcd_panel_dpi.c`,
`mipi_dsi_dma_trans_done_cb` -> `dw_gdma_channel_enable_ctrl()`), on the core
that called `lcd_init()` -- the host task's, the window's. Spurious opening a
dictionary runs `qsort` over 54189 words (`out of order: sorted`) in its own
code, with no call to the host: one long window, the frame interrupt held off,
the DSI bridge starved, the panel blue until the sort is done.

**Why the sample masked:** an interrupt taken in U-mode enters the private
vector, which does not service it, and an edge-triggered source has its
pending bit cleared on hardware-vectored entry -- lost. On this target every
CLIC line is hardware-vectored (IDF's `cpu_start.c`: `esprv_int_set_vectored(i,
true)` for all 32). A level-triggered source is not lost: it stays asserted,
and the trap path puts IDF's vectors back before the `mret` that restores
MIE, so IDF takes it at once. Outside IDF's tests and other chips' ports
nothing in IDF passes `ESP_INTR_FLAG_EDGE`.

**So:**

- `feckless_umode_enter` no longer raises the threshold (it still saves it, and
  the trap restores the same value). An interrupt in U-mode is a trap like
  any other; the host loop counts it and re-enters (0003). When the trap's
  `mret` brings MIE back, IDF's handler runs -- and FreeRTOS may switch the
  host task out there, so a window is now preemptible, not cooperative.
- The trap entry re-pends an edge-triggered interrupt: for an interrupt cause,
  it reads the line's CLIC control word (`0x20801000 + 4 * id`, core-local),
  and if bit 17 (the trigger's edge bit) is set, writes 1 to its pending byte.
  `umode_pmp.c` asserts those against IDF's `clic_reg.h` (`CLIC_INT_CTRL_REG`,
  `CLIC_INT_ATTR_TRIG_S == 17`, `CLIC_INT_IP == 1`).
- `make compare` checks the CSR reads and `pmp_program` against the sample;
  `umode_enter` and the trap entry now differ from it on purpose. Its
  normalisation stripped space-indented offsets only and objdump indents
  relocation lines with tabs, so code moving was reported as different; it
  strips both now (and a changed `csrr` in `read_pmpcfg` is still caught).

*Checked:* `umode.S` assembles with the IDF toolchain's GNU as and with clang,
guard on and off, against IDF 6.1's own stack-guard macros; the static asserts
compile against the CLIC layout taken from IDF's `clic_reg.h`; the host tests
pass. *Not on a Tab5.*

*What to look for:* the dictionary opening with no blue flash; the reader's
`ended` line showing a large interrupt count (every tick on its core now lands
in a window); nothing else changed. If the board hangs or panics in an
interrupt handler while a box runs, that is an interrupt this reasoning did not
cover: send the panic's `MCAUSE` and backtrace.

### u-feckless-0017 -- windows are masked again; preemption is opt-in

0016 made things worse. Spurious standalone panicked within a second of
start-up, and again during a dictionary load:

```
Guru Meditation Error: Core  0 panic'ed (Interrupt wdt timeout on CPU0).
MEPC    : 0x4ff02b84  RA      : 0x48007f0a
--- 0x4ff02b84: feckless_umode_trap_entry at umode.S:724   (.Lkresume)
T0      : 0x00011880  T1      : 0x00010088
```

The exit path was doing what it should. T1 is the kernel's saved
mstatus, and it has MIE set. T0 is the mstatus the exit wrote, with
MPP=M and MPIE=1. The watchdog's own interrupt was taken on the first
instruction back in M-mode. So interrupts were reaching ESP-IDF at
`.Lkresume`, yet CPU0's tick had not fed the watchdog for the whole
timeout.

Our best reading is that the tick was lost, not delayed. Every line is
hardware-vectored. If an edge-configured source is claimed by the
private vector and the re-pend write does not take, the ISR never runs.
The source's status is then never cleared, so it never makes another
edge. This has not been confirmed on silicon.

Until it is, `umode_enter` again raises the CLIC threshold to its
maximum, as the sample and 0015 did. The 0016 path, with the threshold
left alone and the edge re-pend, now sits behind
`CONFIG_FECKLESS_UMODE_PREEMPTIBLE`, a new Kconfig option that defaults
to off. The blue flash while a dictionary loads comes back with this:
the panel is starved for as long as the box computes without a syscall.

### u-feckless-0018 -- SD_HOST's per-init INFO line is quiet

On IDF 6.1 the SD host driver logs `I SD_HOST: src_freq_hz: 160000000`
every time the host is initialised. The storage poll initialises it once
a second until a card mounts. One Spurious boot printed 58 of those lines
before the card answered. `sd_mount_at()` now lowers the `SD_HOST` tag to
WARN, unless something has already set it lower. The driver's warnings
and errors still show. Network's temporary `ESP_LOG_NONE` during
`esp_hosted_deinit()` saves and restores the level, so the two don't
conflict.

### u-feckless-0019 -- the mount poll is silent

Under Avoidant (log of 2026-11-09), pulling the card made every 1 Hz
mount attempt print five lines: `sdmmc_init_ocr: send_op_cond (1)
returned 0x107`, two `sdmmc_card_init` / `sdcard_init failed`, and two
HINTs telling the user to reboot. That is the poll's normal state with no
card, not a fault. `sd_mount_at()` with `verbose == false`, the poll's
call, now sets `sdmmc_common`, `sdmmc_sd`, `sdmmc_cmd`, `vfs_fat_sdmmc`,
`vfs_fat` and `SD_HOST` to NONE around `esp_vfs_fat_sdmmc_mount()` and
restores each saved level afterwards. The boot mount (`verbose == true`)
still shows everything. The storage handler's own messages are
unaffected, including the drop-to-a-slower-clock warning.

### u-feckless-0020 -- maths and heap_caps in a box

The next boxed app is Lothesome, the audio analyzer. It calls `powf`,
`cosf`, `atan2f`, `sqrtf` and `fmaxf`, and esp-dsp's FFT set-up uses the
trig. picolibc has these in its `libc.a`, but most were not compiled
position-independent. Linked with `-shared`, `atan2f`, `expf`, `exp2f`,
`fmodf`, `logf`, `log10f`, `log2f`, `powf`, the sin/cos range reduction
(`__rem_pio2f`) and `scalbnf` all fail with
`relocation R_RISCV_HI20 against a local symbol`. Building them from
source would mean bringing picolibc's maths build along too.

So `feckless_box_libc` gets its own, `src/box_math.c`, compiled with the
app:
- expf, exp2f, exp10f: range reduction by ln 2 split in two parts, then
  a degree-7 polynomial.
- logf: fdlibm's atanh series on [0.707, 1.414). log2f and log10f are
  logf scaled.
- sinf, cosf, tanf: k·π/2 subtracted using π/2 in nine 8-bit pieces.
  For k < 2^16 each product is exact, so a float near a multiple of π/2
  keeps its precision. Above |x| = 65536 the argument is first taken
  mod a float 2π, so the phase is only approximate.
- atanf: reduced to [0, tan π/12], then a series. atan2f, asinf and
  acosf are built on it.
- powf: exp2 of y·log2 x, with y·e split so that it doesn't round.
  Bases 10 and 2 go to exp10f and exp2f.
- fmodf: exact.
- The rounding functions, fmax/fmin and copysign: bit operations.
- sqrtf: `fsqrt.s`.

Compiled with `-fPIC` for the P4, `box_math.o` has no undefined
symbols, and with `box_heap.o` it links into a shared object at both -O2
and -Os.

`test/math_test.c` compares each function with the host's libm over
400 000 random arguments. Worst errors seen: expf 1.2 ulp, exp10f 1.4,
logf 1.3, log10f 2.2, sinf/cosf 3.1 up to 60 000 and 1.9 at the floats
nearest k·π/2, tanf 3.0, atan2f 2.6, tanhf 2.5, powf 6.5 for
x ≤ 100, |y| ≤ 8, and 10^y 1.3. sqrtf and the rounding functions are
exact. The test also checks the special values from C99 Annex F. powf's
error grows with |y·log2 x|, because float has no wider type for the
logarithm.

`src/box_heap.c` adds `heap_caps_malloc`, `_calloc`, `_realloc` and
`_free` as the box's own `malloc` family. The caps are ignored: a box
has one heap, and a host call that does DMA copies through its own
staging buffer. `heap_caps_aligned_alloc` is left out, because the
box's `free` would not know the offset. All of these names are in
`inapp.txt`, so a boxed ELF that imports any of them fails the build.

### u-feckless-0021 -- audio, USB and the font in a box

This is step 3 of the plan for running Lothesome in a box. Step 5, the
maths, and step 4's heap went in with 0020.

`feckless_box_calls` gets a new file, `src/box_audio.c`, with:
- `audio_out_init`, `_set_volume`, `_headphones`, `_capture_begin`,
  `_capture_read`, `_capture_end` and `_monitor_write`;
- `usbhost_init` and `_start`;
- twelve `uac_*` calls, covering streaming out and the USB microphone.

`box_calls.c` adds `tab5io_exp1`/`exp2`, `touch_present` and
`touch_set_rotation`. That brings the table to 104 entries.

The handles work like the panel and the bus. The app gets a number for
each expander, and `audio_out_init()` and `usbhost_init()` accept only
the right one. Each buffer is checked over its whole length before a
driver touches it, with the multiplication checked for overflow:
- a capture read: `max_frames * 2` int32, the size `audio_out.h`
  requires whichever source is running;
- a microphone read: `max_frames` times the channels the open reported
  (0 if no microphone is open);
- a monitor write: `frames * 2` int16;
- `uac_write`: `len` bytes.

The drivers copy with the CPU into and out of their own DMA buffers, so
box memory is never a DMA target. A read or write waits at most 2 s;
the app's task is blocked in the host call meanwhile. `uac_product()`
and `uac_mic_product()` copy their string into a buffer in the box, one
buffer each, so one call does not overwrite the other's result. Each is
capped at 127 characters. `uac_mic_open_bits` reads its in/out
arguments once and writes them once, through locals.
`feckless_box_calls_end()` closes a microphone the app left open, and
stops a stream or capture it started.

The font: `ark12.h`'s `ark12_glyph()` is inline and reads the tables in
place, so a boxed app that draws its own text needs them in the box. The
six tables are now data entries in the table. `ark12_lit` is declared
without a size, so `ark12.h` gets `ARK12_LIT_BYTES` (266488), and
`tools/gen_ark12.py` now writes that define too.

At about 380 KB the tables were too big to copy into every box, which
is what a data entry used to cost. So `feckless_umode`'s resolver now
copies a data entry the first time the ELF imports it, allocating from
the box as the relocation's own allocations do, and no longer at load.
Spurious imports none of them, so its box doesn't grow.

`heap_caps_malloc` stays in `feckless_umode`'s heap calls for apps
built before 0020. An app built now defines it itself, and
`inapp.txt` stops it from importing it.

Host tests: `box_calls_test` checks every new call against recording
stand-ins (`test/fake_audio.c`; fake `audio_out.h`, `uac.h`,
`usbhost.h` and `ark12.h`). That covers the token checks, buffers
running off the end of the box, a multiplication that overflows, host
addresses, the timeout cap, the two product buffers, and teardown
running exactly once. Two mutations were each caught: dropping the
overflow guard, and dropping the stop of the capture at the end.
`feckless_umode`'s tests pass. The lazy copy in `umode_load.c` runs
only under ESP-IDF, so it is not host-tested.
