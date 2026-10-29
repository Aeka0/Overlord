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
