# Static-model transparent surface lists

`Too many visible static model surfaces` with `R_SMODEL_SURFTYPE_RIGID` and
`CAMERA_REGION_LIT_TRANS` comes from the static list writer at `0x140714940`.
It is native warning 20. The writer compares its next 4-byte instance or
12-byte key/instance record against the region's end pointer, then rejects the
model and reports its material. This does not describe the DObj/skinned surface
arena in [scene surface storage](scene-surface-storage.md), nor the 2D HUD stream.

The native budget table at `0x140990B78` has 14 camera-region rows. Each row
contains a uint16 output-byte quota and a separate uint16 temporary-sort count,
repeated for four static-surface types. Lit transparent originally has only
1,024 bytes per type despite accepting a 128-entry sorting batch. A batch of
128 unique keys requires 1,536 bytes, even before other batches contribute.

`scene_static_surface_storage` increases that region's output quota to 4,096
bytes. Temporary sorting counts, 16-instance batches, draw keys, model IDs,
visibility, FOV and warning behavior remain native. The cumulative backing bank
is expanded by four as well, from `0xD2000` to `0x348000` bytes per frontend.
Every changed reservation is at most four times its original size; neighboring
native storage is never borrowed.

This list format publishes raw pointers and byte counts to the native view.
It has no scaled uint16 `surfId` conversion. All 15 renderer arena dependencies
are relocated together: 11 references to `frontend+0xD55700`, two references
to `frontend+0xD57700`, and two inverse displacements in special shadow setup.
Preserve those +0x2000 addends. The inverse LEAs at `0x1407116CD` and
`0x140712CEA` cancel the arena displacement to recover `RBP+0x5880` and
`RBP+0x5830`. Their signed immediates must become `stack_addend-new_displacement`.
Leaving the original inverse operands shifts both stack sorting pointers and
output end pointers by the relocation delta, causing the S.S.D.D./ISS crash
at `0x1407148B1`. They are eight-byte instructions with the immediate at +4;
the forward instructions are seven bytes with the immediate at +3.
The backend consumes the already-published list pointers; no stream copy or
per-frame lookup is introduced. Both banks retain the original frontend spacing,
atomic reservation cursor and reset lifecycle.