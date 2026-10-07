# Spare-magazine latch contact

AK47, M14/M21, Dragunov, FAL, M200, MP5K, UMP45,
FAMAS, AUG, TAR-21 and F2000 use the complete spare-magazine body as the
striking region. All 27 registered profiles inherit their family's geometry.
Other magazines do not gain impact release merely because they support
physical extraction.

Each weapon's `magazine_collision.hpp` stores one conservative oriented box,
fitted to the complete body in magazine-local space. All body vertices are
enclosed, including permanent components such as Tavor's follower plate;
cartridge geometry is excluded. The box uses principal body axes and 0.05 mm
authoring padding. Its middle, side faces, lips and base can all make contact.
The previous 2 cm top/bottom slabs and their per-weapon headers are removed.

Curves, internal hollows and small surface details are deliberately simplified
inside the body enclosure. This is bounded collision geometry, not per-triangle
mesh collision. The collider follows the actual magazine transform and raw
tracked hand motion; render snapping cannot generate a strike. Authoring and
mesh scans are offline only, with no per-frame vertex traversal or allocation.

`swept_box_contact` queries the closest point on the complete box against the
existing latch sphere. Conservative advancement covers translation and shortest
quaternion rotation between samples; it does not approximate the faces using
corners or a finite probe grid. Work is bounded at 128 steps per box, with
0.05 mm numerical contact tolerance. A large tracking discontinuity resets the
approach rather than sweeping through the intervening receiver.

The winning material point must have sufficient forward speed and travel from
the separated approach. Changing the closest corner cannot generate apparent
velocity. Entering contact consumes the approach even when the motion or native
transaction is rejected. The complete body must leave the rearm radius before
another approach is accepted. AK/M14/Dragunov retain their 4 cm latch radius and
7.5 cm rearm distance; FAL retains its own existing latch tolerance.

Only a successfully committed impact removal starts a 300 ms insertion delay
on that weapon instance. The spare stays held and its ammunition is preserved.
Withdrawal and tracking checks continue during the delay, but the magazine
cannot seat before the deadline. At 300 ms an aligned spare at the mouth can
seat once the ordinary post-impact withdrawal requirement has been satisfied.
Failed strikes and ordinary manual/button removal do not start this delay.
Gesture resets and hand changes cannot shorten it; a different weapon instance
or a backwards clock clears obsolete timing state.

The common box-motion primitive admits one or two active boxes in a fixed
two-box budget. Only active regions participate in validity, sweep bounds,
contact and rearming; topology changes reset continuity. Current magazine
profiles use one body box. Existing compound-box queries remain available to
other consumers without treating inactive storage as geometry.

Tests cover centre and all six body sides across every enabled profile, rotated
guns and multiple world scales, along with pure rotation, fast crossings, slow
overlap, reverse approaches, teleports, invalid region counts, shape changes,
contact consumption, native write failure and the 299/300 ms insertion boundary.
Offline source checks verify every body vertex is inside its compiled collider.
Headset acceptance remains a separate check of reach and physical feel.

## Release motion

Successful paddle/latch strikes immediately impart directional velocity and tumble
to the old magazine. AK47, M14/M21, Dragunov, FAL, M200, MP5K, UMP45, FAMAS and
Tavor's forward paddle opt in, including their registered variants. AUG, FN2000
and Tavor's rear release button immediately fall under gravity when struck with
a spare, without impact velocity or spin. Ordinary controller-button releases
retain their authored rail-and-gravity drop.

The accepted sweep supplies the velocity of its winning material point, including
lateral and rotational motion. The shared response transfers 80% of that vector,
capped at 3 m/s in total, rather than per axis. The lever from the magazine body enclosure's centre
to the struck hardware determines the tumble axis, capped at 6 rad/s. This is a
bounded release response, not a general rigid-body simulation. Tuning is shared
in `magazine_release_motion.hpp`; release hardware opts in independently.

Every accepted strike bypasses the authored exit rail and starts free flight at
the attached magazine's exact pose. No exit offset, delay or extra rail velocity
is added. Rotation is about the body enclosure's centre, and gravity acts from
the release time on the same analytic trajectory used by simulation, catches and
final rendering. A later moving gun cannot pull the item along. The same release
constructor and immutable snapshot also supply cosmetic overflow drops. Failed native
transactions, consumed approaches, ordinary hand release and later button presses
cannot reuse a previous strike. Headset acceptance of strength and tumble remains
separate from automated motion and transaction checks.

## Tavor's two releases

Tavor has separate rear-button and forward-paddle contacts. The rear button is
at receiver-local (-25.0921, 0, 1.5021) cm and accepts forward/upward strikes;
the forward paddle is at (-11.5590, 0, 2.0883) cm and accepts rearward strikes.
The old single contact combined the forward paddle's position with the rear
button's direction. Both points are measured on the existing receiver mesh;
the 2.5 cm contact and 6 cm rearm radii remain unchanged.

An optional `second_latch` reuses the same complete magazine collider with an
independent position/direction. The two detectors keep separate approach
histories and may publish only one removal per sample; either successful hit
consumes both histories, including when the native write fails. Other weapons
retain their single-contact path. A reverse approach to one latch can validly
strike the other: isolated direction tests and combined-contact tests are
therefore separate. All Tavor skins inherit these contacts.
