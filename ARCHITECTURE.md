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
