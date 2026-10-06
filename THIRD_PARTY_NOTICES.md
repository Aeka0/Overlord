# Third-party notices

## Parent project and dependency notices

Overlord includes code derived from H2-Mod and retains its GNU GPLv3 license
and existing source notices. See `docs/source-provenance.md` for source and asset
provenance. Dependency versions are pinned by the Git tree;
their own licenses remain applicable. Do not replace their notices with the
root project license.

The submodules retain notices at the following paths:

| Dependency | Notice in the checkout |
| --- | --- |
| GSL | `deps/GSL/LICENSE` |
| WinToast | `deps/WinToast/LICENSE.txt` |
| asmjit | `deps/asmjit/LICENSE.md` |
| CascLib (MIT) | `deps/casclib/LICENSE` |
| curl | `deps/curl/COPYING` |
| discord-rpc | `deps/discord-rpc/LICENSE` |
| gsc-tool / cxxopts | `deps/gsc-tool/LICENSE`, `deps/gsc-tool/deps/cxxopts/LICENSE` |
| ImGui | `deps/imgui/LICENSE.txt` |
| nlohmann/json | `deps/json/LICENSE.MIT` |
| LibTomCrypt / LibTomMath | `deps/libtomcrypt/LICENSE`, `deps/libtommath/LICENSE` |
| Lua | Copyright/license block in `deps/lua/lua.h`; reproduced in `licenses/Lua.txt` |
| MinHook | `deps/minhook/LICENSE.txt` |
| RapidJSON | `deps/rapidjson/license.txt` |
| sol2 | `deps/sol2/LICENSE.txt` |
| stb | `deps/stb/LICENSE` |
| udis86 | `deps/udis86/LICENSE` |
| zlib | `deps/zlib/LICENSE` |

Premake 5.0.0-beta2 is bundled as a build tool. Its upstream license is in
`licenses/Premake.txt`, from the matching
[premake-core tag](https://github.com/premake/premake-core/blob/v5.0.0-beta2/LICENSE.txt).

The font metadata declares SIL OFL 1.1. The official IBM Plex notice and full
license are in `licenses/IBM-Plex-OFL.txt`, obtained from the
[IBM Plex project](https://github.com/IBM/plex/blob/master/LICENSE.txt); the bundled Arabic font also carries
Copyright 2019 IBM Corp. The inherited mixed fonts need their component
copyright/source inventory recovered. See `docs/source-provenance.md` for this,
the shader-tool permission question and inherited artwork review.

The package helper copies the applicable dependency license files from the
pinned checkouts. Preserve them when distributing a client overlay. The root
GPL does not relicense the original game's content or third-party fonts.

CascLib is linked statically for offline inspection of installed official game
language packs. Its upstream checkout is unmodified; the launcher never enables
online storage or downloads. See [CascLib](https://github.com/ladislav-zezula/CascLib).

## Valve OpenVR SDK

Overlord includes the public OpenVR client shim and headers from OpenVR SDK 2.15.6.
The shim is linked statically, while the application resolves the user's active
SteamVR library from its registered OpenXR runtime manifest; no Valve runtime binary is redistributed. The retained
license is available at `deps/openvr/LICENSE`.

The shim includes JsonCpp by Baptiste Lepilleur under public-domain/MIT terms;
its full embedded notice is retained in `licenses/OpenVR-JsonCpp.txt`.

Copyright (c) 2015, Valve Corporation. All rights reserved. Redistribution and
use in source and binary forms, with or without modification, are permitted under
the conditions in that license. The software is provided "AS IS", without
warranty, and Valve and its contributors disclaim liability as described in the
full license text.

## Khronos OpenXR loader

The client build and overlay include application-local `openxr_loader.dll`
built from the unmodified pinned OpenXR-SDK `release-1.1.62` at commit
`57af7fc61f9f2d492580cb28aab6d0ea59d8d417`. Its source is available from
[OpenXR-SDK](https://github.com/KhronosGroup/OpenXR-SDK).

Loader sources and relevant common/generated sources are licensed under
`Apache-2.0 OR MIT`; the loader build system is Apache-2.0. The loader includes
JsonCpp under its public-domain/MIT terms. The package retains the SDK's license,
COPYING overview, Apache-2.0, MIT, CC-BY-4.0 and JsonCpp notices under `licenses/OpenXR*`.
Preserve those files when redistributing the loader. Per-file notices in the
pinned checkout remain authoritative.

The loader dispatches the standard API. Users install and activate their vendor's
OpenXR runtime separately; this project distributes no vendor runtime. OpenXR
is the default backend, with OpenVR available through explicit selection. Hardware
validation is scoped to the combinations documented in the runtime guide.

## REFramework reference implementation

The D3D11/OpenXR architecture was researched with reference to REFramework.
No REFramework binary is distributed by this project.

Copyright (c) 2019 praydog

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
