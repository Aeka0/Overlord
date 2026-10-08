# VR controller prompts

VR instructions use the currently bound controls rather than controller-specific face-button assumptions. The shared HUD composer reads an immutable label snapshot from the current focused input frame. OpenVR obtains action origins and their localized names through `IVRInput`; OpenXR enumerates bound action sources and obtains their localized names. The runtime adapters refresh these presentation-only labels at most once per second and invalidate them on binding/profile changes or shutdown.

This supports Index, Touch, Vive, WMR, and other controllers through their runtime bindings, including custom bindings. No hardware-name table or separate overlay DLL is needed. Label query failures leave gameplay input active and use localized neutral action names; successfully queried actions without a binding are identified as unbound. Labels have bounded length and reject control characters and HUD markup.

Menu, firing, pickup/grip, secondary action, stance, jump, sprint, and fixed-sniper instructions share the same composer. A weapon-specific instruction uses the current weapon hand where appropriate. Sprint distinguishes the movement axis from the sprint action. Stinger instructions explain the two-hand firing grip, circular tube sight, and lock-on before firing.

Overrides retain the existing exact-key, mission, feature, and language gates. Unrecognized game strings and incomplete translations remain native. Physical gestures such as inserting a magazine retain their gesture instructions. The change describes existing controls; it does not remap gameplay actions.

## Validation

The spatial-panel test target includes pure prompt and localization tests for Index, Touch, Vive, custom bindings, unbound actions, and invalid labels. The OpenXR mock smoke test checks label publication, missing name APIs, and name-query failure while preserving focused input and haptics. Build and run both in Debug and RelWithDebInfo; CI runs these alongside the existing binding and HUD draw checks.

Headset acceptance still requires viewing the affected prompts in the game with a real runtime.
