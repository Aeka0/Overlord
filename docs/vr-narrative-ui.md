# Native narrative UI in VR

The VR path in all builds extracts native dialogue subtitles, pulse/typewriter
announcements and solid fullscreen black/white fades through the existing native HUD
capture hooks. Composition does not rebuild text or advance animation timers.
Native text layout, Chinese wrapping, fonts, glow and draw order remain owned by
the game. Approved action-instruction adaptations pass through the shared
[HUD prompt registry](game-text-i18n.md) before native text rendering; the
narrative renderer contains no prompt replacement rules or translations.

Native script HUD text also enters through exact allocator ranges, so ordinary
`hintprint` instructions no longer require subtitle/typewriter flags. Estate
DSM labels and numeric progress share a separate lower 1.2 m plane; general
story text and dialogue remain at 2 m. Opening pulse/typewriter announcements
use a separate 1.6 m plane, and fullscreen fades cover the scene once. See
[scripted scenes and common hints](vr-estate-scripted-scenes.md).

Each native canvas spans 60 degrees horizontally and preserves its source aspect
ratio, keeping apparent text size when changing depth. Native lower-left location,
time and mission announcements stay lower left at 1.6 m; subtitles retain their
native lower-center layout at 2 m. Both eyes project those same canvases using their own current
scene matrices. This is independent of the weapon HUD toggle.

Fullscreen fade RGBA is extended outside the story canvas to every eye pixel.
Second Sun (`dcemp`) creates a native `white` overlay, including a 0.2-second
whiteout before ISS and 4-second fades back into each scene. Its original
color, alpha and draw order are retained; multiple solid fades compose in order.
Native fades also blend preceding announcement RGB toward the fade color while preserving glyph
coverage; announcement text drawn after a fade remains visible. The scene fade
is composed once, followed by the separate announcement ink, so splitting depth
does not apply the fade twice or expose titles through a later opaque fade.
The native command handler still runs once. The two captured planes are
published/read together and shared by both eyes. Composition copies the already composed eye target, including
weapon HUD and diagnostics, before applying native encoded-space alpha and
returning to linear output. A fade therefore cannot reveal an earlier HUD layer.

## Source and lifetime contracts

The [trainer results HUD](vr-trainer.md) keeps all native script text, pulsing
values and result separator lines on the same 2 m canvas. Its ordinary values
already use the native text producer. Exact script allocation ownership also
admits the result-line material as ink; it cannot become a fullscreen fade.
The separate difficulty-selection LUI is covered by the
[common VR menu candidate](vr-native-menus.md); HMD acceptance is pending.

Scripted tutorial backgrounds share the story text canvas. The native image
boundary supplies the rectangle before the blur material's additional UI
placement transform; this avoids scaling an already positioned rectangle again.
Both native tutorial borders and text retain their original font/material draws.
The background's bounds and animated alpha travel with the same immutable capture
used by both eyes. Composition applies a bounded tent blur to each eye's own
already composed scene, then draws the sharp native ink above it.

Tutorial borders use native reverse subtraction with a white tint and animated
alpha. They cannot be flattened into a transparent source-over target: doing
so previously rejected the whole capture, including correctly localized text.
Owned `h1_hud_tutorial_border`, `h1_hud_fng_results_border` and
`h2_hud_ssdd_results_line` commands enter a separate positive
subtractand/coverage capture using the shared native blend converter. The same
completed stream publishes this backing as an immutable companion to its text.
Composition applies blur, then `max(background * (1-alpha) - subtractand, 0)`,
then ordinary text, all on the same per-eye canvas. Result separators use the
same witnessed reverse-subtractive route as their outer border. The results
background `h2_hud_ssdd_results_blur` shares the native image-layout capture
and per-eye blur metadata path. A failed border capture does not clear valid
text. Native script ownership outranks the positional ammo-widget selector,
preventing either hand from capturing overlapping result labels or values.
Companion leases are released only from pool-only retired frames, allowing both
bounded texture pools to recycle without modifying published images.

Image elements are emitted outside the text-element callback. The verified image
call site therefore records its own allocation and narrative-image provenance;
it must not require an active text callback. Both blur and border images retain
their original producer coordinates and are matched to the same command stream.

Only `h1_hud_tutorial_blur` commands inside a validated script-element allocation
are replaced in the flagged pass. The same fingerprints, arena reset and 250 ms
expiry used by narrative text guard that replacement. Unknown/ordinary LUI blur,
weapon-panel blur and ordinary menu effects keep their existing paths. The original
HUD scripts and animation timers are unchanged. Native tutorial borders are ink,
not fullscreen fades. At most eight backdrop rectangles are composed per capture.

- Subtitle classification uses backend text flag `0x100`; typewriter/pulse text
  uses the complete `0xc0` mask. These are native rendering categories and include
  other native typewriter announcements. Frontend subtitle style `0x400` is not
  a valid backend selector.
- The complete pulse/typewriter mask selects the nearer announcement channel;
  subtitle ownership (`0x100`) takes precedence if both markers are present.
  Explicit DSM progress ownership keeps its existing 1.2 m plane. Fade-only
  frames do not allocate an announcement target.
- Plain pickup hints and ammo numbers are excluded. No localized string matching,
  OCR or assumed screen-position classification is used.
- Authored [scripted action instructions](vr-scripted-sequences.md) additionally
  qualify through their exact allocator-owned command range, with expiry and
  byte fingerprint validation shared with world prompts. Their ordinary text
  flags alone never qualify other HUD text. They use the same narrative target;
  they are excluded from world-marker atlas selection.
- Only native solid `black`, or black-tinted `white`, quads that cover the whole
  active viewport qualify as fades. Legacy HUD StretchPic (opcode 10), rotated
  XYWH (12), XY (16), and LUI XYUV (17) share the quad decoder used by directional
  UI. Each format retains its native vertices and color offset; StretchPic's
  trailing padding is not a rotation angle. Diagonal rotation, partial scissoring, letterbox bars,
  vignettes and other textured overlays cannot become fullscreen black.
- Three leased textures prevent overwriting a publication still used by either
  eye. A complete empty UI stream clears the publication. Invalid streams also
  clear it, and captures older than 250 ms expire. Weapon HUD retention does not
  apply to transient narrative content.
- Capture uses the existing dispatch, quad, text and draw observer. It does not
  add another D3D hook or replay the native UI dispatcher. Commands are bounded
  to 2048 entries / 1 MiB; selected primitives to 128. Text keeps the native
  16-bit command length. Capture targets are bounded to 8192 per dimension and
  16 million pixels. No GPU readback or cross-thread GPU waits are introduced.

This path composes onto native stereo scene pairs. Frontend movie/loading screens
that produce no stereo scene are outside this scene-composition seam; this change
does not submit a desktop image or repeat an old scene for those screens.
Death quotes and their optional hints use a separate verified native producer
range with the same narrative capture lifetime. Death blur is a separate native
post-processing stage applied to each eye before HUD composition; see
[player death](vr-player-death.md).

## Verification

`vr-spatial-panel-tests` covers native text markers, long command lengths, invalid
coordinates, full-viewport versus letterbox/diamond/overscan coverage, expiry,
current-target composition, encoded-space fade alpha, full-black eye corners and
D3D state restoration on WARP. The engine stereo and no-loader smoke tests cover
the unchanged stereo submission boundary.

A read-only capture during an `ending` checkpoint retry confirmed
that the missing opening fade was a legacy StretchPic `black` overlay spanning
the native 1638 x 1307 viewport, with alpha descending from 255 through 206, 204
and 59. The earlier selector accepted only LUI XYUV. A sanitized command fixture
now covers legacy selection, padding, alpha, and letterbox rejection; WARP checks
the observed fade steps at the center and corners of both eye projections.

`vr_narrative_status` reports composed pairs, failures and the latest capture's
size, age and fade alpha. `vr_hud_capture_status` adds narrative capture, text,
fade and rejection counters. Neither command enables continuous logging or
readback. Native dialogue, mission-opening timing, visual placement and fade
comfort still require HMD acceptance; automated checks do not certify those.
