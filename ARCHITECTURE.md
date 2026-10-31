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
