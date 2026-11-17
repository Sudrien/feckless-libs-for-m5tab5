/* SPDX-License-Identifier: MIT
 *
 * u-feckless-0063: the whole of feckless_box_libc in a firmware's build.
 * See CMakeLists.txt: in any build that does not call
 * feckless_abi_box_elf(), the component's sources are replaced by this
 * file, so its archive links nothing into a launcher.
 */
typedef int feckless_box_libc_not_here;
