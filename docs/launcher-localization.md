# Launcher and installed game languages

The launcher provides English, Simplified Chinese, Traditional Chinese, Russian,
French, German, Spanish, Japanese and Korean. The locale resources cover settings,
first-use setup, errors, accessibility labels and all 75 help articles plus their
seven shared reload recipes. Help search indexes every available translation.
The four Asian locales use the same typography rules previously used by
Simplified Chinese, including the embedded Latin fonts and system glyph fallback.

On first use, when `players2/h2-mod/launcher.json` does not exist, the launcher
matches Windows' first preferred **UI language**, not the keyboard layout or
regional format. Regional variants share the corresponding language. Chinese
script subtags take precedence over region; Traditional Chinese covers Hant,
Taiwan, Hong Kong and Macau. Unsupported languages fall back to English. The
initial match is saved, and later launches honor the saved preference. If that
write fails, the matched language remains visible with a localized save error.

Game language is independent. The language page writes the existing
`language` field in the game's per-user `config.json` for the next launch.
Other fields are preserved, malformed or oversized configuration is not
overwritten, and atomic replacement uses Unicode Windows paths.

## Offline availability contract

`launcher/game_language_catalog.hpp` admits only the native official language
names with official language-pack roots; the MOD's Czech and Turkish extensions
are excluded. `launcher/game_language.cpp` opens the installation's CASC/TVFS
storage using the pinned, unmodified CascLib submodule. Neither online storage
nor permission to download missing metadata is enabled.

For each official language, every file listed under its language/code prefix
must be local. Each file's spans must resolve in local archives, and the first
and last byte of every span must be readable. Startup, common and audio content
must all be present. A Battle.net tag, empty directory, shared startup resource
or MOD localization file alone does not establish availability. This is an
installation check, not a full payload-integrity scan.

Enumeration runs on a launcher-owned worker, outside WebView2 and outside the game
runtime. The UI polls completion without waiting on disk and offers a refresh
button. Saving reopens the storage and repeats admission before writing. A
missing saved language is reported without selecting or writing a substitute.
Failed writes retain the previous selection. Launching waits for an outstanding
scan or save, and a confirmed missing saved language requires selecting an
installed pack first. Scanning never writes game settings.

## Validation

React launcher tests cover locale coverage, native unit conversion, incomplete
numeric drafts, inverse settings and stale-document message rejection.
The C++ launcher tests cover Windows language-tag mapping and preference parsing.
Game-text and prompt tests format all 65 existing keys in the 14 complete
official game catalogs, while retaining native fallthrough tests for partial
MOD-only and unknown locales.
Real installation probing is read-only; actual game language switching and
headset layout require a subsequent live acceptance pass.
