# Source and asset provenance

## Code

Overlord includes code derived from H2-Mod, with independently maintained VR
rendering, input, and gameplay adaptations. The parent project's GPLv3 license
and existing source notices are retained. See THIRD_PARTY_NOTICES.md for the
applicable project and dependency notices.
A new Git root does not change ownership or third-party license terms.

The .gitmodules file and Git tree pin dependency sources. Dependencies retain
their own histories and notices. Bundled OpenVR source and the application-local OpenXR loader are recorded in
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

The launcher WOFF files in `src/launcher-ui/public/fonts` retain the original
embedded font bytes and metadata. Moving them into the React asset pipeline
does not change the provenance requirements above.

## Other inherited assets and tools

- assets/icon/Icon.ico is the supplied Overlord application icon. The executable
  embeds this ICO directly; src/client/resources/icon.png is its largest embedded
  PNG frame, extracted without image edits. Retain the icon's permission evidence
  alongside the other project artwork before public redistribution.
- data/zonetool/ contains source textures, font files and asset descriptions
  inherited from H2-Mod. Confirm the origins and redistribution rights of image
  resources before publishing them or compiled fastfiles. The code license is
  not blanket permission for original game artwork.
- assets/steamvr/ contains project artwork and relative application manifests.
  The 600 x 900 cover-capsule.png was supplied as CoverLarge.png and imported
  without image edits for SteamVR's vertical capsule.
  Retain the source/permission evidence for artwork used in a public release.
- data/cdata/ also contains six inherited voice-over FLAC files. Confirm their
  redistribution rights before publishing the source or a client overlay; the
  package helper currently collects `.flac` files from this directory.
- The pinned deps/shader-tool checkout has no standalone license file. Confirm
  its applicable permission before redistributing its code or linked
  implementation; a submodule URL does not supply missing permission.
- Proprietary game executables, game archives, local asset-export directories,
  captures, minidumps and generated fastfiles are excluded from this snapshot.

These are explicit review items, not a claim that every inherited asset has
already received complete legal clearance.
