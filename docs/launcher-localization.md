# Launcher and game languages

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

## Game language selection

`launcher/game_language_catalog.hpp` lists every native official language;
the MOD's Czech and Turkish extensions are excluded. Every listed language can
be selected regardless of installed content. A lightweight, offline CASC scan
looks for local `<language>/<code>_common.ff` entries. Detected languages appear
first; undetected languages follow in a dimmer color and remain selectable.
The scan does not read payload spans or audit every map/audio file. If storage
cannot be opened, all languages remain listed as undetected. The launcher never
requires English or blocks launch based on detection results. The selected
language pack must be correctly installed to display properly.

Scanning, loading preferences and saving a selection run on a launcher-owned worker,
outside WebView2 and the game runtime. The UI polls completion without waiting
on disk. Saving validates only the official language name and configuration
format before atomically writing the preference; saving does not rescan or
validate installed content. Failed writes retain the previous selection.
The refresh button reruns the advisory scan without changing the preference.
Launching waits for pending language IO to finish.

## Validation

React launcher tests cover locale coverage, native unit conversion, incomplete
numeric drafts, inverse settings, stale-document message rejection and detected
language ordering without filtering undetected choices.
The C++ launcher tests cover Windows language-tag mapping and preference parsing.
Game-text and prompt tests format all 65 existing keys in the 14 complete
official game catalogs, while retaining native fallthrough tests for partial
MOD-only and unknown locales.
Actual game language switching and headset layout require a subsequent live
acceptance pass.
