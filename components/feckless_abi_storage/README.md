# feckless_abi_storage

The feckless ABI's storage section: `feckless_storage_handler`, FatFs
with exFAT, and libc's and POSIX file I/O, for an app's ELF, from a
launcher that links this component. The list is feckless_abi's
`sections/storage.txt`; feckless_abi's README says what a section is.

## A launcher

Name `feckless_abi_storage` in the manifest and in main's `REQUIRES`,
with `feckless_storage_handler`, `fatfs` and `usb_host_msc` -- all from
this repository, at one tag. Bring storage up as before (`storage_io_init()`,
`storage_init()`); `feckless_abi_run()` registers the section's table.
About 8 KB in Avoidant (Duplicitous as was): the table, and the file calls it keeps in the
link.

## An app

```yaml
  feckless_abi_storage:
    git: https://github.com/Sudrien/feckless-libs-for-m5tab5.git
    path: components/feckless_abi_storage
    version: "v0.3.0"
```

with `feckless_abi`, `feckless_drivers`, `feckless_graphics_handler`,
`feckless_storage_handler`, `fatfs` and `usb_host_msc` beside it, and
`feckless_abi_storage` in main's `REQUIRES`. Then:

```c
#include "feckless_abi.h"
#include "feckless_abi_storage.h"

void app_main(void)
{
    feckless_abi_require();
    feckless_abi_require_storage();
    ...
```

The launcher brings storage up; the app finds `/sd` and `/usb` mounted
(`storage_present()`, `storage_mount_path()`), and neither calls
`storage_init()` nor mounts or formats a volume: `storage_init()`,
`storage_io_init()`, `f_mount()`, `f_mkfs()` and `f_fdisk()` are not
exported. `fseek()` and `ftell()` take `long`, 32 bits here; past 4 GB
on exFAT, `f_lseek()` or `storage_io_read_at()`.

## The FatFs both sides share

An app's `FIL`, `FF_DIR` and `FILINFO` are its own, laid out by its
build's FatFs configuration, and the launcher's FatFs reads and writes
them; `TCHAR` is in the functions' signatures. So both builds are held
to one configuration, and this component's CMakeLists.txt fails either
that differs:

| Option | Why |
|---|---|
| `fatfs` this repository's, `CONFIG_FECKLESS_FATFS_EXFAT=y` | `FF_FS_EXFAT`, `FF_LBA64` |
| `CONFIG_FATFS_LFN_HEAP=y` or `_STACK=y` | `FILINFO`'s long name |
| `CONFIG_FATFS_API_ENCODING_UTF_8=y` | `TCHAR` is `char` |
| `CONFIG_WL_SECTOR_SIZE=4096` | `FF_MAX_SS` |
| `CONFIG_FATFS_PER_FILE_CACHE=y` | `FF_FS_TINY 0`: `FIL`'s buffer |
| `CONFIG_FATFS_USE_DYN_BUFFERS=y` | `FIL`'s buffer a pointer |
| `CONFIG_FATFS_USE_FASTSEEK=n` | `FIL`'s `cltbl` |
| `CONFIG_FATFS_FS_LOCK=0` | `FIL`'s and `FF_DIR`'s `lockid` |

Changing one is a new section version, `storage_v2`.

## Licence

MIT. See `LICENSE` at the repository's top.
