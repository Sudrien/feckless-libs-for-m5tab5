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
