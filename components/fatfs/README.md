# fatfs (feckless)

ESP-IDF's fatfs component with exFAT, for the Tab5 projects. It has
fatfs's name, so it stands in for IDF's: a component added later
overrides one of the same name, and the component manager adds this one
after IDF's.

ESP-IDF compiles exFAT out of FatFs (`ffconf.h`, `FF_FS_EXFAT 0`) and
has no option for it. At configure time this component copies the
installed IDF's `components/fatfs` into the build directory, runs
`tools/patch_fatfs.py` over the copy, and builds it with IDF's own
CMakeLists.txt. Nothing in the IDF tree is touched, and whichever IDF
builds the project is the FatFs patched: there is no copy of one IDF's
code to go stale under another. The copy is made again when the IDF, its
FatFs or `patch_fatfs.py` changes.

`patch_fatfs.py` makes the edits Defeatist's `tools/enable_exfat.sh` did,
and the result is byte for byte the same:

- `ffconf.h`: `FF_FS_EXFAT 1`; `FF_LBA64 1`, since exFAT volumes pass
  32-bit sector addresses; `FF_USE_TRIM 0`, since the SD and MSC disk IO
  layers do not implement TRIM and FatFs calls it during exFAT
  operations.
- `ffconf.h`: every `FF_*` defined as a bare `CONFIG_*` bool guarded, so
  one that is off is 0 rather than an undeclared name. exFAT is what
  exposes them.
- `ff.c`: `load_xdir()` wrapped, so a damaged exFAT entry set calls
  `ff_tab5_bad_entry(pdrv, sector, offset, name, in_root)` -- weak here;
  `feckless_storage_handler` logs it -- rather than being only "I/O
  error".

Each edit checks what it expects and fails the configure, rather than
half-patch, if a new IDF's FatFs is different.

## Using it

```yaml
# main/idf_component.yml
dependencies:
  fatfs:
    git: https://github.com/Sudrien/feckless-libs-for-m5tab5.git
    path: components/fatfs
    version: "v0.3.0"
```

The project's own `components/fatfs`, if it has one -- the copy
`cmake/exfat.cmake` used to make -- would win over this, so delete it,
with `cmake/exfat.cmake`, `cmake/idfcopy.cmake` and
`tools/enable_exfat.sh`.

exFAT needs long file names: `CONFIG_FATFS_LFN_HEAP=y` (or `_STACK`).

`CONFIG_FECKLESS_FATFS_EXFAT=n` (on by default) builds IDF's FatFs as it
is, FAT32 only. It replaces the `TAB5_NO_EXFAT` environment variable.
Like any `sdkconfig.defaults` change, it needs `rm sdkconfig` to take.

IDF's fatfs options (`CONFIG_FATFS_*`) and its `project_include.cmake`
functions, such as `fatfs_create_partition_image()`, come through from
IDF's own files.

## Licence

This component's files are MIT. What it builds is IDF's fatfs
component (Apache-2.0) wrapping ChaN's FatFs (the FatFs licence, a
one-clause BSD-style notice to keep), patched as above. exFAT was a
Microsoft patent family; Microsoft published the specification in 2019
and committed it to the Open Invention Network's patent non-aggression
pool. If that matters to you, set `CONFIG_FECKLESS_FATFS_EXFAT=n`.
