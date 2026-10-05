# SCAR shotgun variant and support correction

Read-only sampling of the installed build identified the held gun
as `scar_h_reflex_shotgun` (68), linked to `scar_h_reflex_shotgun_attach` (69).
The module had no runtime record and reported unsupported/duplicate/aliased
binding. Its geometry was already present. The classifier accepted only the
bare `scar_h_shotgun` name, so the reflex variant never acquired module authority.

The interaction contract requires the sampled order: ordinary support, firing grip, then pump.
Firing attempts were 3.1–5.3 cm from the current contact, with inward scores about
0.93–0.98: inside the existing 7.5 cm / 0.766 acquisition gates. Their rejection
was not evidence for moving the firing point or widening firing authority.
Pump attempts were approximately 4.1–8.2 cm from the authored pump contact.

## Changes

The three reviewed shotgun host families (AK47, SCAR and FAL) now accept bounded
optic/skin variant names with a complete `shotgun` component and their exact
`<host>_attach` child. Native linked definition, type/class, four-round capacity,
pump flag, segmented loading, eight pellets, one-round insertion and independent
ammo-key/ownership checks remain mandatory. No other host family is added.

SCAR shotgun ordinary support acquisition expands from 10 to 13 cm and retention
from 22 to 26 cm. The existing broad support angles (75 degrees acquisition,
100 degrees retention) accept palm-up or palm-inward for shotgun attachments.
Live ordinary attempts included inward scores 0.77–0.98 with negative upward
scores; a palm-up-only rule rejected these otherwise nearby approaches.

The same policy is applied to carry acquisition, carry retention and final hand
presentation. Clear firing intent still excludes ordinary support within the
secondary firing contact. The firing gate itself and M203/GP25 behavior remain
unchanged. Pump acquisition/retention inherits the host's support distances;
native pump travel and ammunition state rules are unchanged. Neither contact
anchor was moved.

## Loaded SCAR catalog in the sampled level

| Main weapon | Native token | Linked secondary |
| --- | --- | --- |
| `scar_h_acog` | 33 | None |
| `scar_h_reflex` | 34 | None |
| `scar_h_shotgun` | 35 | `scar_h_shotgun_attach` (36) |
| `scar_h_grenadier` | 37 | `scar_h_m203` (38) |
| `scar_h_thermal` | 42 | None |
| `scar_h_reflex_shotgun` | 68 | `scar_h_reflex_shotgun_attach` (69) |

Only token 68 was in the player's inventory at capture. These are loaded native
definitions, not proof that each has a spawned pickup. Tokens are session-local
evidence and are not compiled into the classifier. No silenced SCAR definition
was found in this snapshot.

## Verification

The follow-up catalog audit checked 13 observed host/child combinations across
the prior native captures and the current SCAR capture. It also identified three
M4 optic combinations named `m4m203_acog/eotech/reflex`; these did not match the
former `m4_` host prefix. Classification now includes the `m4m203` family and
pairs M4/M16/SCAR/ACR/AK only with their own native secondary-name families.
Native alternative links and ammunition/assembly checks remain authoritative;
shared reserve pools do not merge secondary identities. Regression also retains
the existing ACR launcher example and rejects cross-family and malformed names.