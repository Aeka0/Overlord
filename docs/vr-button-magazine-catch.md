# Attached magazine grasp and button-release catch

Button-release weapons accept a fresh offhand Trigger pinch on their attached
magazine. The hand follows the seated grasp, but the magazine stays locked at its
authored receiver pose. Pulling cannot remove it. Releasing the pinch, moving more
than the existing 35 cm part-retention distance away, or a tracking discontinuity
ends the grasp before processing a simultaneous release-button press.

After headset feedback, all registered exposed button magazines use
their authored body bounding box and index-pad contact, with the same 50 mm
outside margin as direct-pull magazines. M4, ACR, Vector, Mini-Uzi, PP2000, G18,
M93R and TMP replace the initial small seated-wrist sphere with receiver-mesh
measurements. M16 and SCAR already had body boxes. Native pose/mesh identities
remain per weapon; the shared detector performs no runtime mesh scanning.

When the rear release button actually ejects a grasped magazine, the existing
ammunition transaction transfers its exact contents into offhand custody. Empty
magazines remain real held items. No reserve refund or cosmetic dropped duplicate
is created. A failed native compare keeps the original feed and grasp, consumes
the press, and requires another release-button edge to retry. The existing
loaded/absent-magazine bolt-release priority is preserved.

The M9, M1911, USP (including silenced) and Desert Eagle (including gold) have
enclosed short magazines. Catching requires the offhand to hold both its support
Grip and Trigger when the rear hand presses release. Without the extra Trigger
hold, the magazine drops normally and support remains. A successful catch
exchanges support for an ordinary Trigger-maintained magazine grasp: Grip can
be released independently, and releasing Trigger drops/refunds the held contents
under the existing policy. Trigger also maintains the hand after reinsertion.
Extended-magazine G18/M93R and the other exposed button magazines use the
ordinary Trigger pinch at the magazine body.

The hand-interaction coordinator reserves an explicit replacement of the exact
same weapon instance's support session. Other held items, firing grips and
unsettled sessions cannot be replaced. The carry adapter removes support only
after the ammunition transfer succeeds, and final observations publish the real
result. Secondary-button history uses the same continuity and replay guards as
the other centrally tracked buttons.

Every caught magazine must leave the magazine well's capture volume plus a
15 mm withdrawal margin before a later approach can insert it. This was reduced
from 40 mm after headset feedback. The separate 40 mm staged-contact retention
margin is unchanged, so insertion contact does not become more jitter-sensitive. Remaining
still, jittering at the capture boundary, changing orientation, or replaying the
same frame cannot immediately reinsert it. There is no timer that silently
reenables insertion while the hand remains inside. Teleports landing within the
clearance volume keep the withdrawal requirement; ordinary continuous return
still checks the existing insertion angle and maximum step.

Attached rifle magazines use the body-wrap recipe for acquisition and for the
continued grasp after insertion. A free magazine retains its chosen grasp until
it enters the receiver. Contact geometry and presentation use the same resolver;
the gun-constrained pose never feeds back into raw tracking. P90's dedicated
forward/reverse recipes and knife co-grasps keep their own shape contracts.

M4, M16, AUG, FAL, TAR and ACR reuse their reviewed wrap recipes. SCAR, M14/M21
and M82 add receiver-held wrap recipes fitted from the shared anatomical hand
template against their own magazine and receiver geometry. SCAR now defaults to
wrap for free magazines too; M14/M21 and M82 keep their original free default.
SCAR and AK use the same controller-relative magazine frame as the six comfort
profiles, independently of how their grasp style is selected. This removes the
weapon-support wrist offset from hand-held magazine pitch, roll and yaw. The other single-style rifle magazines
already use side wraps. The new fits retain native hand scale and coupled
four-finger bending, and use separate thumb placement for receiver clearance.
Both hands pass static finger/receiver triangle-intersection checks on the
reference glove. Short exposed bodies can leave lower fingers below the base;
these static checks do not establish headset comfort or fit on every glove.

This implements the interaction prerequisite for ammunition recovery and discard
penalties. No new penalty setting or recoverable world-magazine ledger is enabled.
Existing refunds on deliberate release and forced cleanup remain in effect.

Regression covers both hands, all registered exposed button profiles, partial and
empty magazines, failed native writes, duplicate frames, breakaway on the release
frame, the clearance boundary, reinsertion and repeated catches, short-pistol
Trigger ownership, bolt-release priority, and central support-session replacement.
Headset acceptance is still required for reach, pose fit and the feel of the
clearance boundary.

The M16 charging-handle box now matches the M4 head's dimensions, positioned at
the M16's actual rear head and fitted upper contact. Its long forward stem is no
longer part of acquisition. The 11 cm contact assistance and measured 8.1 cm
mechanical travel remain separate. Both hands retain the authored head contact.
