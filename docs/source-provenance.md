# Source and asset provenance

## Code

H2-MOD VR is a modified version of H2-Mod with VR rendering, input, and gameplay
adaptations maintained in this repository. The parent project and earlier
contributors are acknowledged in the root README. Its GPLv3 license and
existing source notices are retained.
A new Git root does not change ownership or third-party license terms.

The .gitmodules file and Git tree pin dependency sources. Dependencies retain
their own histories and notices. Bundled OpenVR source is recorded in
THIRD_PARTY_NOTICES.md. The bundled Premake executable identifies itself as
5.0.0-beta2; its upstream license is retained under licenses/.

## Fonts

These five font files contain SIL Open Font License 1.1 declarations in their
SFNT name metadata. They remain under OFL, not the project's GPL.

| File under data/zonetool/ | Embedded family | Evidence / remaining work |
| --- | --- | --- |
| ara_h2_mod_font_default_bold/defaultBold.ttf | IBM Plex Sans Arabic SemiBold | Embedded copyright: 2019 IBM Corp.; retain the Plex notice and OFL. |
| fonts/bank.ttf | H*-Mod Mix Gothic | Recover component font copyrights/source inventory before redistribution. |
| fonts/default.otf | H*-Mod Mix Med | Recover component font copyrights/source inventory before redistribution. |
| fonts/mix.ttf | H*-Mod Mix Med | Recover component font copyrights/source inventory before redistribution. |
| fonts/defaultBold.otf | H*-Mod Mix Open | Recover component font copyrights/source inventory before redistribution. |

The official Plex notice and full OFL text are in licenses/IBM-Plex-OFL.txt.
Preserve embedded notices. A license identifier alone does not recover missing
copyright attribution for a merged font.

## Other inherited assets and tools

- data/zonetool/ contains source textures, font files and asset descriptions
  inherited from H2-Mod. Confirm the origins and redistribution rights of image
  resources before publishing them or compiled fastfiles. The code license is
  not blanket permission for original game artwork.
- assets/steamvr/ contains project artwork and relative application manifests.
  The 600 x 900 cover-capsule.png was supplied as CoverLarge.png and imported
  without image edits for SteamVR's vertical capsule.
  Retain the source/permission evidence for artwork used in a public release.
- data/cdata/ also contains six inherited voice-over FLAC files. Confirm their
  redistribution rights before making the source snapshot public; the client
  overlay packager deliberately excludes these audio files.
- The pinned deps/shader-tool checkout has no standalone license file. Confirm
  its applicable permission before redistributing its code or linked
  implementation; a submodule URL does not supply missing permission.
- Proprietary game executables, game archives, local asset-export directories,
  captures, minidumps and generated fastfiles are excluded from this snapshot.

These are explicit review items, not a claim that every inherited asset has
already received complete legal clearance.
