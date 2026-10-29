# Defensive-check review

2026-10-29. This review follows the corresponding H1 audit, but changes H2's own
contracts and implementations. H2 has no matching D3D11/DXGI implementation-DLL
allowlist in its graphics admission path; its loader import interception is not
an interface whitelist. Adapter LUID, device feature level, native ABI, GPU
ownership and resource-retirement checks remain necessary.

## OpenXR image waits

An `XR_TIMEOUT_EXPIRED` image wait now defers the open frame instead of retiring
the session. The acquired index and native source-pair lease remain owned;
subsequent Presents wait on that image without another acquire. An eye already
rendered/released for this prediction is not rendered again. Menu swapchains use
the same acquired/waited lifetime; resizing first settles the old image.

The timeout is reported in compositor diagnostics, not failure history. A
successful frame clears the pending-wait diagnostic. Actual API failures still
use the existing error/cleanup path, and an unready image is never released or
destroyed. This follows the
[OpenXR wait contract](https://registry.khronos.org/OpenXR/specs/1.0/man/html/xrWaitSwapchainImage.html).

Validation: `vr-runtime-mock-smoke` passed in Debug and RelWithDebInfo. Cases
cover repeated waits on either eye, automatic continuation in the same session,
menu wait recovery, and retained ownership after actual API/cleanup failures.
This is mock/WARP evidence, not real compositor or headset acceptance.

## Texture dimensions

Native UI capture, OpenXR eyes/menu/movie transfer, SteamVR movie capture and
spatial composition now share the D3D11 16384-side limit in `texture_extent.hpp`.
The 8M/16M/32M pixel heuristics and inconsistent 8192-side checks no longer
reject valid source/target dimensions. HUD/scope geometry uses the same bound.
The existing bounded pools, descriptor/device checks and actual allocation
results remain authoritative. No automatic downscaling was added.

Validation: Debug and RelWithDebInfo builds and runs of `vr-runtime-mock-smoke`
and `vr-spatial-panel-tests` passed. The former transfers a 4096×2160 menu and
checks its diagnostic thumbnail; the latter draws to an actual 8193×16 WARP
target. Full native HUD capture and headset acceptance remain live-test work.

## Optical projection ranges

`eye_projection::valid()` now supplies one finite, ordered-frustum rule for
view publication, eye/culling construction and projection readback. Removed
the zero-crossing requirement and empirical tangent-width/half-FOV limits.
Native float projection coefficients must still be finite, with positive
perspective scales and near distance. Projection-center sums use double
intermediates to avoid overflowing finite float bounds during construction.

Validation: `vr-engine-stereo-probe-smoke` passed in Debug and RelWithDebInfo,
including positive-offset, wide and narrow optics, NDC-edge/readback checks,
culling construction and rejection of a zero-width frustum. This does not add
canted-eye rendering: H2's current native adapter still requires the supported
eye-orientation relationship. Removing that separate guard would silently
flatten unsupported optics; it was retained.

## Repeated backend validation

Backend view-copy and world-owner entry no longer rescan the finalized matrices
in a claim already admitted by the binding ring. The owner path also immediately
passes those unchanged views to the scene-clone boundary, which retains its
input validation. Publication no longer repeats pair identity/routing checks
already performed by `validate_finalized`.

Native finalizer, public publication/clone entry points, record identity, CPU
completion, GPU lifetime and restore checks remain. This removes redundant
work without introducing a second validation cache or weakening the producer
boundary. No FPS improvement is claimed without measurement.

Validation: Debug and RelWithDebInfo runs of `vr-engine-stereo-probe-smoke` and
`vr-runtime-mock-smoke` passed after the final changes, including invalid
publication rejection, scene cloning, backend copy and error/retirement cases.

The complete `client` target also built successfully in Debug and
RelWithDebInfo with v142 after all four corrections. The local build used the
VS2019 generator supported by the installed CMake and the launcher's existing
Node override; no tracked build-system or dependency changes were required.
Neither client was deployed or run in the game/headset during this review.
