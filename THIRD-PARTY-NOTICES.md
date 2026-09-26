# Third-Party Notices

StarfieldHeadTracking bundles, statically links, or credits the third-party components
listed below. Each remains the property of its authors and is used under its own
licence. Where a licence requires the copyright notice, the conditions and the
disclaimer to accompany a binary distribution, the full text is reproduced here
verbatim, and this file ships at the root of every release ZIP we publish.

No Starfield code, no extracted game assets and no game data files are
contained in this repository, and none are redistributed in any release ZIP.
The only game-derived material this repository is set up to carry is the README
demo clip, which is recorded gameplay footage rather than anything taken out of
the game's data files. The terms it is published under are set out in full under
"Starfield footage and screenshots" below, and they apply from the moment that
file is added.

| Component | Version | Licence | How it ships |
|-----------|---------|---------|--------------|
| Ultimate ASI Loader | v9.7.2 | MIT | Bundled verbatim in the installer ZIP |
| injector | `f7fd18f` (inside Ultimate ASI Loader v9.7.2) | Zlib | Compiled into the vendored loader DLL |
| miniz | 3.0.0 (inside Ultimate ASI Loader v9.7.2) | MIT | Compiled into the vendored loader DLL |
| MinHook | v1.3.4 (`c3fcafd`), modified | BSD-2-Clause | Compiled into `StarfieldHeadTracking.asi` |
| inih | r55, modified | BSD-3-Clause | Compiled into `StarfieldHeadTracking.asi` |
| cameraunlock-core | b4df73a5d8076968fcbf7e4088dd49db11a2684e | MIT | Compiled into `StarfieldHeadTracking.asi` |
| OpenTrack | n/a | ISC | Not bundled; UDP protocol interoperability only |
| CommonLibSF | n/a | GPL-3.0-or-later | Neither bundled nor linked; consulted on two struct layouts, see below |
| Xbox GDK / Creation Engine 2 / Scaleform GFx | n/a | n/a | Neither bundled nor linked; see "Engine layout" below |

---

## Ultimate ASI Loader

Vendored at `vendor/ultimate-asi-loader/`, shipped in the installer ZIP and used as the
install-time source. Taken from the upstream release asset untouched; the
upstream licence file ships beside it at `vendor/ultimate-asi-loader/LICENSE`.

- **Upstream:** https://github.com/ThirteenAG/Ultimate-ASI-Loader
- **Version:** `v9.7.2`
- **Commit:** `ab722befd52581a34449b603926cfab476e66b05`
- **SHA-256:** `22fda9c71eaae02460f311bf3441638340ab591586d78f1de213c4819dcb883c`
- **License:** MIT
- **Usage:** loads `StarfieldHeadTracking.asi` into the game process. The file
  is named `dinput8.dll` upstream and is installed beside the game executable
  as `winmm.dll`, which is the import `Starfield.exe` actually has.
- **Bundled:** yes. Shipped verbatim in the release ZIP and installed from
  there; the installer never reaches the network for it.

```
MIT License

Copyright (c) 2023 ThirteenAG

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
```

That `dinput8.dll` is a static binary and is not one component. The
`Ultimate-ASI-Loader-x64` target in `premake5.lua` at v9.7.2 compiles
`external/injector/minhook/src/**.c`,
`external/injector/utility/FunctionHookMinHook.cpp` and `external/miniz/miniz.c`
alongside the loader's own sources, so redistributing it redistributes MinHook,
injector and miniz as well, and each has its own section in this file.
MemoryModule, d3d8to9 and the minidx9 DirectX headers belong to the 32-bit
target only and are absent from this binary. The MinHook section covers the copy
inside the loader as well as any linked into the mod itself; the licence text is
the same.

---

## injector

Compiled into the vendored `dinput8.dll`. The loader's `FunctionHookMinHook`
wrapper, which the `Ultimate-ASI-Loader-x64` target compiles from
`external/injector/utility/FunctionHookMinHook.cpp`, and the MinHook submodule
that repository carries. Nothing in this repository calls or links it; it ships
only inside that binary.

- **Upstream:** https://github.com/ThirteenAG/injector
- **Version:** commit `f7fd18f7fcb4691f470b7a047697e591a39a94fc`, the submodule
  Ultimate ASI Loader v9.7.2 pins at `external/injector/`
- **License:** Zlib
- **Usage:** none of ours. It backs the loader's own function-hook wrapper.
- **Bundled:** yes, inside the vendored `dinput8.dll` in the release ZIP.

The binary is unaltered upstream, so the "altered source versions" condition
below does not arise. It is reproduced whole regardless.

```
Copyright (C) 2012-2014 LINK/2012 <dma_2012@hotmail.com>

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

   1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.

   2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.

   3. This notice may not be removed or altered from any source
   distribution.
```

---

## miniz

Compiled into the vendored `dinput8.dll`. Zip reading for the loader's
`LoadVirtualFilesFromZip` path, which the `Ultimate-ASI-Loader-x64` target
compiles from `external/miniz/miniz.c`. Nothing in this repository calls or
links it; it ships only inside that binary.

- **Upstream:** https://github.com/richgel999/miniz
- **Version:** 3.0.0, as vendored at `external/miniz/` in Ultimate ASI Loader v9.7.2
- **License:** MIT
- **Usage:** none of ours. It backs the loader's own zip reading path.
- **Bundled:** yes, inside the vendored `dinput8.dll` in the release ZIP.

```
Copyright 2013-2014 RAD Game Tools and Valve Software
Copyright 2010-2014 Rich Geldreich and Tenacious Software LLC

All Rights Reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```

---

## MinHook

Source committed at `extern/minhook/` and compiled into `StarfieldHeadTracking.asi`. The
committed tree is the authoritative record of exactly what is built.

- **Upstream:** https://github.com/TsudaKageyu/minhook
- **Version:** `v1.3.4`
- **Commit:** `c3fcafdc10146beb5919319d0683e44e3c30d537`
- **License:** BSD-2-Clause
- **Usage:** installs the camera, aim, weapon, input and HUD hooks the mod
  runs on.
- **Bundled:** yes, compiled into `StarfieldHeadTracking.asi`.

MinHook carries two copyright holders: Tsuda Kageyu for MinHook itself, and
Vyacheslav Patkov for the Hacker Disassembler Engine that `src/hde/` is built
from. Both notices appear below exactly as upstream ships them.

This copy is modified: `MH_Initialize` uses `GetProcessHeap()` rather than
standing up a private heap with `HeapCreate`, and `MH_Uninitialize` skips the
matching `HeapDestroy`. BSD-2-Clause permits the change; it is recorded here so
the attribution is not mistaken for a claim of an unmodified copy.

```
MinHook - The Minimalistic API Hooking Library for x64/x86
Copyright (C) 2009-2017 Tsuda Kageyu.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

 1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
 2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER
OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

================================================================================
Portions of this software are Copyright (c) 2008-2009, Vyacheslav Patkov.
================================================================================
Hacker Disassembler Engine 32 C
Copyright (c) 2008-2009, Vyacheslav Patkov.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

 1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
 2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

-------------------------------------------------------------------------------
Hacker Disassembler Engine 64 C
Copyright (c) 2008-2009, Vyacheslav Patkov.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

 1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
 2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

---

## inih

Source at `extern/ini.c` and `extern/ini.h`, compiled into
`StarfieldHeadTracking.asi`. The committed files are the authoritative record of
exactly what is built; the upstream licence also ships beside them at
`extern/LICENSE.inih`.

- **Upstream:** https://github.com/benhoyt/inih
- **Version:** `r55`
- **License:** BSD-3-Clause
- **Usage:** parses `HeadTracking.ini`.
- **Bundled:** yes, compiled into `StarfieldHeadTracking.asi`.

This copy is modified: the `INI_API` visibility macros and the `INI_API`
qualifiers on the four public `ini_parse*` declarations are removed, since the
parser is compiled straight into the `.asi` and never exported from a shared
library. A handful of upstream comments are also worded differently. The
BSD-3-Clause licence permits the change; it is recorded here so the attribution
is not mistaken for a claim of an unmodified copy. Ben Hoyt's copyright notice,
conditions and disclaimer are reproduced verbatim below and are retained at the
top of both source files.

```
inih -- simple .INI file parser

SPDX-License-Identifier: BSD-3-Clause

Copyright (c) 2009-2024, Ben Hoyt

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of Ben Hoyt nor the names of its contributors
      may be used to endorse or promote products derived from this software
      without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY BEN HOYT ''AS IS'' AND ANY
EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL BEN HOYT BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

---

## cameraunlock-core

Git submodule at `cameraunlock-core/`, compiled into `StarfieldHeadTracking.asi`.
This is our own shared code, released under its own MIT licence separate from
this mod's `LICENSE`, so its notice has to travel with the binary in its own
right. It ships as `licenses/cameraunlock-core-LICENSE.txt` in both release
ZIPs and is reproduced here as well.

- **Upstream:** https://github.com/itsloopyo/cameraunlock-core
- **Version:** pinned commit `b4df73a5d8076968fcbf7e4088dd49db11a2684e`
- **License:** MIT
- **Usage:** supplies the shared tracker receiver, pose interpolation,
  smoothing and camera maths.
- **Bundled:** yes, compiled into `StarfieldHeadTracking.asi`; its licence text
  also ships as `licenses/cameraunlock-core-LICENSE.txt`.

```
MIT License

Copyright (c) 2026 itsloopyo

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
```

---

## OpenTrack

- **Upstream:** https://github.com/opentrack/opentrack
- **Version:** n/a, protocol only
- **License:** ISC
- **Usage:** the mod reads the OpenTrack UDP pose datagram layout so OpenTrack
  and compatible trackers can drive it.
- **Bundled:** no. Nothing of OpenTrack's is shipped or linked.

Not bundled and not linked. This mod implements the OpenTrack UDP pose datagram
layout so that OpenTrack (https://github.com/opentrack/opentrack, ISC licence)
and compatible trackers can drive it. No OpenTrack code, headers or binaries
are copied, linked or redistributed, so its licence triggers no notice
obligation here. It is credited because the wire format is its work.

---

## Engine layout

Nothing of Bethesda's is bundled, linked or compiled into
`StarfieldHeadTracking.asi`, and no part of the game is redistributed. The mod
does hold a short list of numbers describing the shape of the running game's own
code and data: the addresses of the function the renderer projects through, of
the weapon render pass, the player's ADS state, and the HUD's own update; which slot
of the camera's function table is its per-frame update; where a scene-graph node
keeps its transforms and where the camera keeps its frustum; and the size and
member offsets of a handful of small structures the mod has to read. Most of the
scene-graph offsets are not written down at all - they are matched against the
live data every launch and held only in memory.

The camera and render-pass layouts and function addresses were measured in a
legitimately purchased copy of the game. They are recorded as numeric constants
inside this project's own namespaces.

The class names the mod looks up - `PlayerCamera`, `NiCamera`, the HUD's own
menu classes, and whatever the running camera state calls itself - are read out
of the retail executable's own runtime type information at load time. They are
the game's names for its own types, quoted so the mod can find them. No game
implementation is reproduced here.

Every one of those names is read from a retail build. The diagnostic HUD probe
in `src/game/hud_probe.cpp` that logs some of them is compiled only when this
project is built with `STARFIELDHT_DEV_HOTKEYS=ON`, which is a build option of
this mod and is off by default; it is not a build of the game, and no
non-public build of the game was used for anything here.

One of the structures measured this way belongs to Scaleform GFx, the user
interface middleware the game's menus are built on. The value layout in
`src/ui/stock_reticle.cpp` and the menu's movie pointer offset were checked
against CommonLibSF's layout declarations and the running game. The HUD
display-object path comes from the locally installed HUD movie. No CommonLibSF
implementation, Scaleform source, or HUD asset is copied into this repository or
bundled with the mod. CommonLibSF has its own section below, because its licence
is one a reader has to be able to check rather than take on trust.

---

## CommonLibSF

Consulted, never copied and never linked. It is listed here because it is
licensed GPL-3.0-or-later, and a reader auditing this boundary is entitled to
see that stated rather than have to go and find it.

- **Upstream:** https://github.com/Starfield-Reverse-Engineering/CommonLibSF
- **Version:** n/a, consulted rather than consumed
- **License:** GPL-3.0-or-later, with that project's modding and linking
  exceptions
- **Usage:** its published declarations of the Scaleform GFx value layout and of
  the menu's movie pointer offset were read as a second opinion on two numbers -
  a structure size with two member offsets, and one pointer offset - that were
  measured in the running game. Both were confirmed against the running game,
  and the running game is what `src/ui/stock_reticle.cpp` encodes.
- **Bundled:** no. No CommonLibSF header, source file or binary is present in
  this repository, compiled into `StarfieldHeadTracking.asi`, or shipped in any
  release ZIP. Nothing here links against it.

What was taken is a handful of integers describing the shape of Bethesda's
compiled structures. Those integers are measurements of a third party's binary,
not CommonLibSF's own expression, and the same values fall out of reading the
game directly, which is how they were arrived at and then re-checked here. On
that basis this project carries no obligation under GPL-3.0, and none is
claimed to have been discharged: there is no derived work to license, because
nothing of theirs was taken into the build.

The credit is owed regardless of whether the licence bites. That project's work
saved time, and saying so is the point of this file.

---

## Starfield footage and screenshots

- **File:** `assets/readme-clip.gif`, the clip the README embeds.
- **Rights holder:** Bethesda Game Studios (developer) and Bethesda Softworks
  (publisher), together with the rights holders of any third-party marks
  visible in frame.
- **Usage:** recorded from the game running with this mod, captured on a
  legitimately purchased copy, shown so a reader can see what the mod does
  before installing it. Nothing is taken out of the game's own data files.
- **Bundled:** never. The packaging scripts ship no part of `assets/`, so the
  clip reaches neither release ZIP nor anything the launcher deploys.
- **Licence:** none is granted or implied by this repository. This material is
  not covered by the MIT licence in `LICENSE`, and nothing here permits reuse
  of it. Rights holders who would rather it were not published: open an issue
  or reach us on Discord and it comes down.

---

## Starfield

Starfield and all related names, logos, characters and marks are
trademarks of Bethesda Softworks LLC, ZeniMax Media Inc., and their respective
owners. Scaleform and GFx are trademarks of Autodesk, Inc. They are used here
only to identify the game this mod applies to and the middleware behind the
structure it reads, which is nominative use and not a claim of any right
in them. This project is an unofficial, fan-made modification. It is not
affiliated with, endorsed by, or sponsored by the game's developers, its
publishers, its engine vendor, or any other rights holder. It redistributes no
game code, no game assets and no proprietary DLLs, and it requires a
legitimately purchased copy of the game. The engine offsets and function
addresses the source refers to are measurements of the running game, taken on a
legitimately purchased copy and recorded as numbers and nothing else. No game
source of any kind is stored in this repository.
