# Transparent particle shadows in stereo

## Report and direct evidence

The right eye lost smoke/dust shadows cast onto ground and objects. This was
distinct from the previously corrected per-eye fog ray reconstruction. The
reported scene contained a broad sandstorm area; the headset was subsequently
turned off while SteamVR and the game continued rendering valid stereo pairs.

A bounded same-pair GPU readback (pair 65515) captured the A8 transparent-shadow
atlas actually sampled by a terrain pixel shader at PS t3. Its size was
2048 x 6144, three stacked partitions. Left-eye bytes ranged from 38 to 255,
mean 198.78172; right-eye bytes were uniformly 255. 6,797,978 bytes differed.
Aligned scene captures also showed the corresponding ground brighter on the
right. No per-eye color correction or normalization was used to create the
comparison.

Draw counts, shader identity and bound resource identity alone did not reveal
the defect: both eyes issued the shadow quad draws to the same atlas. Temporary
observers were removed after each capture. H2's shader load-definition pointers
had been reused; shader inspection used validated DXBC private data on the
observed D3D shader objects instead of trusting those stale pointers.

## Native chain

- Target 28 is the A8 transparent-shadow output in this capture.
- `0x140736A90` prepares the shadow view and draw-list executor;
  `0x140736C40` executes each partition through `0x1407A3A90`.
- Both eyes had equal draw-list headers and surface references, including lists
  30/31, with native light views distinct from either eye camera.
- `0x1407B9780` consumes the code-surface mesh selected from
  `backend + 0x5409C0 + mesh * 0x58`. At `0x1407B9AB6` it subtracts the current
  index origin from the retained surface's CPU index address and divides by two.
- At `0x1407872F0`, the left/natural shadow draws had `(triangles=2, start=0,
  base_vertex=0)`. Right-eye starts instead reached values such as 1,503,100,928
  and 2,354,446,336. The right shader therefore had no valid quad to rasterize,
  leaving the white atlas clear value.
- The shadow backend's data identity matched its owner record's frontend data.
  Its primary source was the native light view (e.g. `0x14F4611E0`), rather than
  the arena eye record or private right-eye clone. These addresses are capture
  evidence, not runtime identifiers.

## Ownership correction

`note_backend_view_copy` previously returned immediately for any primary source
other than the eye record. That was correct for eye-camera/model state updates,
but also skipped dynamic-index restoration for owned shadow/light geometry.

The two authorities are now separate:

- Camera/model origin and projection handling still requires the exact eye
  source. Native shadow/light matrices remain untouched.
- Dynamic-index capture/replay covers the exact native geometry-executor
  boundary when its backend data is the current owner's frontend arena,
  including shadow/light subviews. Foreign arenas and fullscreen/query-only
  boundaries do not gain this authority.

The existing ordered left/right snapshots, checked restoration and natural
post-left cleanup are reused. The bounded boundary budget is 128 to include
the additional native subviews. The fix does not copy an eye image, share a
screen-space result, disable shadows, change shader parameters or recreate
particle simulation for the second eye.

## Verification and limits

- D3D11 GPU census/WARP regression checks classify camera vs owned-shadow
  boundaries independently, reproduce the invalid index delta, restore the
  left-compatible range, reject foreign backend data, and restore natural
  post-left origins. Existing multi-boundary and GPU resource tests pass.
- Stereo probe regression passes, including existing projection/history logic.
- Client Debug v142 build passes.

Readbacks and native index samples above are from the failing build. The new
build still requires in-game visual acceptance: revisit the sandstorm, compare
cast dust shadows in both eyes, and check nearby smoke/transparent effects while
moving the head. Automated passes do not constitute headset acceptance.
