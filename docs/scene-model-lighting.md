# Native static-model lighting cache

`Too much model lighting used - not drawing static model` is native warning 18.
Static visibility reaches `0x14075D600`, which allocates or reuses a model-lighting
cache entry. It rejects a model when the pool is full and every eviction candidate
is already in use for the current frame. This is separate from the packed static
surface lists and the DObj/skinned surface arena.

Read-only inspection observed 4,096 allocated entries and a 4,096-entry limit.
The native manager has fixed 16,384-entry free/owner/stamp tables. Its initializer
`0x14075E230` reserves 4,096 dynamic entries per configured client and rounds the
combined texture size to a power of two, guaranteeing at least 4,096 static
entries. Native resource creation immediately uses those computed dimensions
for CPU buffers, GPU images and shader sampling constants.

`scene_model_lighting` expands the cache through the initializer's two minimum-quota compares.
With at most 8,192 reserved entries, the static minimum is 8,192; larger native
reservations retain the original minimum. A single-client configuration therefore
gets a 16,384-entry total texture and 12,288 static entries, versus the original
8,192 total / 4,096 static. Every supported native client count stays within the
16,384-entry static management tables. The original initializer still derives
all sizes, texture height, exponent and UV scales together.

The two comparison bridges modify flags only, preserve all registers and stack,
and rejoin the original conditional branches. Both sites and their input/loop
contracts are checked before patching. No allocation limit is changed during a
running frame; native allocation, eviction, reset, device recreation and warning
behavior remain intact. No lighting is disabled and no model is forced through
a failed allocation.

Native savegames archive `DynEntityClient::lightingHandle` (the two-byte field
at offset `0xA`, archived at `0x14028DAD1`). Its value includes the static partition
offset. Changing that partition therefore also requires guarding the dynamic
reuse entry at `0x14075D032`: only handles in `(static slots, total slots]` may
reach the native buffer-index calculation. Zero and stale/out-of-range handles
follow the original allocation path at `0x14075D19D`, which replaces the handle
on success and retains native exhaustion behavior. No save format is changed.

The range guard runs at every caller of the shared dynamic allocator, before
any position, bitset, or cached-lighting read. It uses only R10, which native
code overwrites on the reuse path, and does not change the stack or other
registers. All guard and sizing contracts are verified before any patch is
installed.
