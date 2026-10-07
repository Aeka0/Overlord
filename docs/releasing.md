# Release preparation

A source publication, a client overlay and a complete playable distribution are
different artifacts. CI builds a Windows x64 client overlay; it does not publish
releases or deploy to a server.

## Source publication

- Start from a clean main checkout with recursively initialized submodules.
- Preserve the GPL, third-party notices, dependency pins and asset provenance.
- Resolve the outstanding origin/permission items in
  [source-provenance.md](source-provenance.md) before public redistribution.
- Exclude local research, old Git metadata, captures, credentials and generated
  files. Review the actual tracked file list rather than relying.gitignore.
- Review repository permissions, Actions settings and private vulnerability
  reporting when creating the public GitHub repository.
- Dependency update PRs are not configured automatically. Review dependency
  changes intentionally.

## Build and package

Build RelWithDebInfo for normal gameplay and Debug for diagnosis. See
[development.md](development.md). After building, create an overlay with:

~~~bat
python tools\package_client.py --configuration RelWithDebInfo
~~~

The archive under `output/packages/<version-name>/` includes the executable, application manifest,
input bindings, artwork, loose project scripts, the Khronos OpenXR loader and
license notices. It excludes PDBs, user settings, logs, original game files and
raw ZoneTool assets. Retain matching PDBs privately; they can contain local source paths.

The overlay requires a legally owned game and the base H2-Mod data required by
the installation. The client build does not compile data/zone_source/ and
data/zonetool/ into fastfiles. Do not advertise an overlay as a complete
first-install package until the required data has been assembled with
redistribution rights and tested on a clean game installation.

For a local candidate from an uncommitted working tree, use
`--staging-directory release`; the candidate goes into `release/<version-name>/`
(currently `release/Beta 3/`). Add `--base-data <installed-cdata-directory>` to
include the explicitly selected base H2-Mod fastfiles. The packager admits only
zone names present in `data/zone_source`, checks the official-language resource
set, and uses the current repository's scripts and sound patches. It does not
copy the original game, profiles, captures or caches. `release-info.json` records
the configuration, version, resource list and whether base data was included.
Local staging includes the selected build's PDB for private deployment and crash
analysis. The ordinary archive mode omits PDBs and still requires a clean,
committed candidate.

VR builds do not use the inherited upstream updater or SSH deployment workflow.
Legacy MOTD, featured panels and Wordle are temporarily disabled at both native
startup and Lua registration. Static project and contributor links remain active.
The old implementation is retained for later restoration, but needs a VR-owned
feed and fixes to cache validation/cancellation before its opt-in is restored.
Install updates manually from this project's Releases page. The OpenXR loader is built by the client dependency target from the pinned SDK
using tools/build_openxr_loader.py; the batch entry point delegates to that same helper.

## Tag and publish

Set the display name, four-part Windows file version and prerelease flag in
`version.json`, then tag the validated source used for published binaries.
Premake generates the EXE and runtime version from that file; the launcher uses
the same value for every language. Git metadata remains diagnostic provenance,
with an explicit `unknown` value in snapshots without history. The displayed
product version is independent of commit counts and tags.

Record the supported game build, runtime, tested headset/controllers and tested
missions. State outstanding compatibility and HMD checks. Upload only reviewed
artifacts and make the corresponding source and dependency pins available.
GitHub Actions success is not headset acceptance.
