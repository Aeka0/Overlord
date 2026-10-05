# In-game text locales

Each file contains only one language's existing MOD-owned in-game text. Files
use language tags consistent with the launcher (`en`, `zh-CN`) but the two
catalogs remain independent. Game text follows the game's `loc_language`.

All 14 written locales used by the 17 official game language packs cover every
existing semantic key. `es-419` shares the neutral Spanish controller catalog.
The MOD-only Czech and Turkish catalogs retain partial coverage and the
exact-locale native fallthrough contract. Launcher UI supports its separate
nine-language catalog.

| File | Language |
| --- | --- |
| `en.hpp` | English |
| `zh-CN.hpp` | Simplified Chinese |
| `zh-TW.hpp` | Traditional Chinese |
| `fr.hpp` | French |
| `de.hpp` | German |
| `it.hpp` | Italian |
| `es.hpp` | Spanish |
| `es-419.hpp` | Latin American Spanish |
| `ru.hpp` | Russian |
| `pl.hpp` | Polish |
| `pt.hpp` | Portuguese |
| `ja.hpp` | Japanese |
| `ar.hpp` | Arabic |
| `cs.hpp` | Czech |
| `ko.hpp` | Korean |
| `tr.hpp` | Turkish |

Entries explicitly name their `key`; their order does not need to follow the
enum. Edit an existing sentence here. For a new semantic key, update
`../catalog_types.hpp`, define its argument schema and supply reviewed
translations. Chinese-only keys are valid during Chinese authoring; do not add
placeholder English. Adding a native game language also requires the central language
mapping and catalog registration. No HUD callsite needs a per-language branch.

Omit untranslated entries. Do not copy English text into another language to
fill gaps: that would incorrectly mark the translation as present and prevent
the action-prompt layer from retaining the game's original instruction.

Save these files as UTF-8 and write readable native text in `u8` literals, for
example `u8"从装备位取出信号弹，移除盖子点燃"`. The local `.editorconfig` declares
UTF-8, and Premake enables MSVC `/utf-8` for every configuration, including PCHs
and tests. The Win32 A/W API character-set selection is a separate setting.
Preserve named parameters such as `{button}` and `{item}`; the sentence can
change their order. Duplicate or invalid keys and incompatible translation
parameters fail compilation.

Use `<em>...</em>` around literal interaction controls and locations that should
appear yellow. The shared formatter produces emphasis runs; language files do
not embed renderer color codes. `{button}` receives emphasis from its typed
argument. Tags cannot nest. Chinese action instructions do not use sentence
periods; this rule does not rewrite original game text on a translation miss.

`../../game_text_catalog.hpp` assembles and validates these immutable catalogs.
They remain embedded in the executable, with no runtime file loading or new
deployment resources. Scope, native-source fallback and operation names are
handled by the shared prompt layer described in
[the i18n architecture](../../../../../docs/game-text-i18n.md).
