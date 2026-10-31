#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Patch a copy of ESP-IDF's fatfs component for exFAT, in place.

    python3 patch_fatfs.py <copy of $IDF_PATH/components/fatfs>

CMakeLists.txt runs this on the copy it makes in the build directory;
nothing in the IDF tree is touched. It is defeatist-music-player-for-
m5tab5's tools/enable_exfat.sh (as at its 8058), the same three edits,
moved here so every Tab5 project gets them from one place:

  1. ffconf.h: FF_FS_EXFAT 1, FF_LBA64 1, FF_USE_TRIM 0.
  2. ffconf.h: every FF_* defined as a bare CONFIG_* bool guarded, so an
     unset one is 0 and not an undeclared name.
  3. ff.c: load_xdir() wrapped to call ff_tab5_bad_entry() (6035).

Each edit checks what it expects to find and fails the build, rather
than half-patch, when a new IDF's FatFs differs.
"""
import os
import re
import sys

# ffconf.h's values, and why (enable_exfat.sh):
#   FF_FS_EXFAT  exFAT itself.
#   FF_LBA64     exFAT volumes routinely exceed 32-bit sector addressing.
#   FF_USE_TRIM  the SD and MSC disk IO layers do not implement TRIM,
#                and FatFs calls it during exFAT operations; left on, it
#                surfaces as ESP_ERR_INVALID_RESPONSE from the trim path.
FF_VALUES = (("FF_FS_EXFAT", "1"), ("FF_LBA64", "1"), ("FF_USE_TRIM", "0"))


def set_ff(conf):
    text = open(conf).read()
    for name, want in FF_VALUES:
        pat = re.compile(r"^#define[ \t]+%s[ \t].*$" % name, re.MULTILINE)
        if len(pat.findall(text)) != 1:
            raise SystemExit("error: expected one %s line in %s; IDF layout may have changed" % (name, conf))
        text = pat.sub("#define %s\t%s" % (name, want), text)
    open(conf, "w").write(text)


# ffconf.h maps most FF_* options straight onto Kconfig symbols, e.g.
#
#     #define FF_USE_LABEL    CONFIG_FATFS_USE_LABEL
#
# Kconfig emits nothing at all for a bool that is off, so FF_USE_LABEL
# expands to a bare undeclared identifier. Stock IDF never notices: the
# only use of it in ff.c compiles only with exFAT. Rewrite those lines
# so an unset symbol means 0. Behaviour is unchanged.
def guard_bools(path):

    text = open(path).read()

    pattern = re.compile(
        r'^#define[ \t]+(FF_[A-Z0-9_]+)[ \t]+(CONFIG_[A-Z0-9_]+)[ \t]*$',
        re.MULTILINE)

    fixed = []

    def guard(m):
        ff, cfg = m.group(1), m.group(2)
        fixed.append(ff)
        return ('#ifdef %s\n#define %s\t%s\n#else\n#define %s\t0\n#endif'
                % (cfg, ff, cfg, ff))

    text = pattern.sub(guard, text)
    open(path, 'w').write(text)

    if fixed:
        print('guarded against unset Kconfig bools: ' + ', '.join(fixed))


# 6035: say where FatFs found a damaged exFAT entry. load_xdir() checks a
# file's directory entry set -- order, sizes, checksum -- and returns
# FR_INT_ERR, which the VFS reports as EIO, the same errno as a failed
# read: a card with one torn entry and a dying card both said "I/O
# error". The definition is renamed and a wrapper of the old name calls
# it, so every caller is covered without touching one, and on failure
# calls ff_tab5_bad_entry() (weak here; feckless_storage_handler's
# storage.c logs it).
def wrap_load_xdir(path):
    text = open(path).read()
    head = 'static FRESULT load_xdir ('
    if text.count(head) != 1:
        raise SystemExit('error: expected one "%s" in %s; FatFs changed' % (head, path))
    start = text.index(head)
    end = text.index('\n}\n', start) + 3
    body = text[start:end].replace(head, 'static FRESULT load_xdir_checked (', 1)
    # The checksum test is the last one, made once every name entry is in
    # dirbuf. Flag it there, so the wrapper knows a name it prints is this
    # entry's and not left over from the one before.
    sumline = re.compile(r'if \(xdir_sum\(dirb\) != ld_(?:16|word)\(dirb \+ XDIR_SetSum\)\) return FR_INT_ERR;')
    if len(sumline.findall(body)) != 1:
        raise SystemExit('error: expected one checksum test in load_xdir() in %s; FatFs changed' % path)
    body = sumline.sub('if (xdir_sum(dirb) != (WORD)(dirb[XDIR_SetSum] | dirb[XDIR_SetSum + 1] << 8)) '
                       '{ tab5_sumfail[dp->obj.fs->pdrv] = 1; return FR_INT_ERR; }', body)
    text = (text[:start]
            + '/* tools/enable_exfat.sh (6035): set by load_xdir_checked(), read by load_xdir(). */\n'
            + 'static BYTE tab5_sumfail[FF_VOLUMES];\n\n'
            + body
            + """
/* tools/enable_exfat.sh (6035): report where a damaged entry set is,
 * and its name when that is known to be its own: only when the checksum
 * test failed, which load_xdir_checked() flags. It is the last test,
 * made after every name entry is in dirbuf. An earlier failure leaves
 * the previous entry's name there (a host test printed exactly that),
 * so it gets none. */
void __attribute__((weak)) ff_tab5_bad_entry (BYTE pdrv, LBA_t sect, UINT ofs,
                                              const char* name, int in_root)
{
	(void)pdrv; (void)sect; (void)ofs; (void)name; (void)in_root;
}

static FRESULT load_xdir (FF_DIR* dp)
{
	const LBA_t sect = dp->sect;
	const UINT ofs = (UINT)(dp->dir - dp->obj.fs->win);
	tab5_sumfail[dp->obj.fs->pdrv] = 0;
	FRESULT res = load_xdir_checked(dp);
	if (res == FR_INT_ERR) {
		const BYTE *d = dp->obj.fs->dirbuf;
		char name[3 * 64 + 1];
		UINT n = 0;
		if (tab5_sumfail[dp->obj.fs->pdrv]) {
			UINT len = d[XDIR_NumName], k;
			if (len > 64) len = 64;
			for (k = 0; k < len; k++) {		/* UTF-16 (BMP) to UTF-8 */
				const UINT e = 2 * SZDIRE + (k / 15) * SZDIRE + 2 + (k % 15) * 2;
				const WCHAR c = (WCHAR)(d[e] | d[e + 1] << 8);
				if (c < 0x80) {
					name[n++] = (c >= 0x20) ? (char)c : '?';
				} else if (c < 0x800) {
					name[n++] = (char)(0xC0 | (c >> 6));
					name[n++] = (char)(0x80 | (c & 0x3F));
				} else {
					name[n++] = (char)(0xE0 | (c >> 12));
					name[n++] = (char)(0x80 | ((c >> 6) & 0x3F));
					name[n++] = (char)(0x80 | (c & 0x3F));
				}
			}
		}
		name[n] = 0;
		ff_tab5_bad_entry(dp->obj.fs->pdrv, sect, ofs, n ? name : NULL,
		                  dp->obj.sclust == 0 || dp->obj.sclust == (DWORD)dp->obj.fs->dirbase);
	}
	return res;
}
""" + text[end:])
    open(path, 'w').write(text)
    print('wrapped load_xdir() in ' + path)


def main():
    copy = sys.argv[1]
    conf = os.path.join(copy, "src", "ffconf.h")
    ffc = os.path.join(copy, "src", "ff.c")
    for f in (conf, ffc):
        if not os.path.isfile(f):
            raise SystemExit("error: expected %s; IDF layout may have changed" % f)
    set_ff(conf)
    guard_bools(conf)
    wrap_load_xdir(ffc)
    with open(conf) as f:
        got = dict(re.findall(r"^#define[ \t]+(FF_FS_EXFAT|FF_LBA64|FF_USE_TRIM)[ \t]+(\S+)", f.read(), re.MULTILINE))
    if got != dict(FF_VALUES):
        raise SystemExit("error: ffconf.h has %s after patching" % got)


if __name__ == "__main__":
    main()
