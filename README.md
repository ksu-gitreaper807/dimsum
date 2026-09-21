# Software Dim — a compositor-wide software dimmer for KWin 6.7 (Wayland)

Multiplies the **already-rendered desktop image** by a configurable factor so you
can go below the hardware backlight minimum.

```text
outputRGB = renderedRGB * dimAmount        # default dimAmount = 0.20
```

Target: Dell Precision-class laptop, internal `eDP-1` at
`3840x2400 @ ~59.994 Hz`, `intel_backlight` already pinned at `1/400`,
Intel + Mesa, OpenGL 4.6, KDE Plasma 6.7.5 / KWin 6.7.5, Wayland, CachyOS.

---

## 1. What this is

A **native C++ KWin effect plugin** (`kwin4_effect_software_dim`) that inserts
one post-processing pass at the end of the compositor's per-output paint and
runs a single fragment multiply over the result.

It is not a window. It is not an overlay surface. It does not touch
`/sys/class/backlight`, `brightnessctl`, EDID, kernel parameters or any
application.

* Toggle: `Meta+Alt+D`
* Brighter / darker: `Meta+Alt+Up` / `Meta+Alt+Down` (step `0.05`, floor `0.05`)
* Slider popup: `Meta+Alt+S` — draggable, shows current level, auto-hides, `Esc` to close

## 2. Why hardware brightness is not enough

`intel_backlight` reports `max_brightness = 400` and is already at `1`. The
backlight controller has no step below that, so no amount of writing to sysfs
gets you darker. The only remaining lever is to reduce the light the panel is
asked to emit per pixel, i.e. reduce the RGB values the compositor hands to the
display. That is a compositing problem, and the compositor is the right place to
solve it: it is the one component that sees the final image and nothing else.

## 3. The JavaScript API cannot do this

Stated plainly, because it decides the whole design:

> **The KWin scripted-effect API cannot perform a compositor-wide
> post-processing pass.**

What the scripted API does offer, and why each one is wrong here:

| Scripted capability | What it actually is | Why it fails the requirement |
|---|---|---|
| `KWinComponents.SceneEffect` + a dark `Rectangle` | A **declarative/QML scene** the effect contributes, painted as part of the scene graph | It is a scene, not a filter. Depending on how it is attached it can stand in for the scene rather than sit on top of it — which is exactly how the earlier prototype blacked the screen out. It also cannot sample "what has already been drawn". |
| `effect.addFragmentShader()` + `effect.set({window, type: Effect.Shader, shader})` | A **per-window** shader | Conceptually wrong target. It is applied while each window is drawn, so anything that is not an `EffectWindow` — the wallpaper above all — is never touched. It also does not compose: the last shader wins, it does not chain. |
| `effects.windowShown.connect(...)` | — | Does not exist in KWin 6. The signals are `windowAdded` / `windowClosed`, which is where the earlier `Cannot call method 'connect' of undefined` came from. |

So: **C++ is required.** That is not a preference, it is the only supported way
to get a shader onto the final framebuffer.

## 4. Why SceneEffect was rejected

A `SceneEffect` *becomes* content. It participates in the scene graph, so it can
occlude, replace or be replaced by other scene content, and it has no handle on
the composited result. The previous prototype's full black screen is the failure
mode of that architecture, not a bug in it.

## 5. Why per-window shaders are insufficient

Requirements this fails outright:

* the wallpaper is not an `EffectWindow`, so it stays at full brightness;
* each window is shaded independently, so overlapping translucent windows are
  dimmed twice (or not at all, depending on blend order);
* other effects that run after yours are not dimmed.

## 6. How this implementation works

```text
Applications
      |
KWin window rendering
      |
KWin compositor scene  (wallpaper + windows + panels + notifications + other effects)
      |
  +---+-------------------------------------------+
  | SoftwareDimEffect::paintScreen()              |
  |                                               |
  |  pass 1: effects->paintScreen(offscreenTarget)|   -> GPU texture, size = output in device px
  |  pass 2: draw that texture through dim.frag   |   -> color.rgb *= dimAmount
  +---+-------------------------------------------+
      |
final framebuffer
      |
display (backlight untouched, still at its minimum)
```

The mechanics, and where each one comes from:

1. `SoftwareDimEffect` is a plain `KWin::Effect` at effect-chain position `99`,
   i.e. late — so the capture already contains every other effect's output.
2. `paintScreen(const RenderTarget &, const RenderViewport &, int, const
   Region &, LogicalOutput *)` is called once per output per frame. This is
   the documented hook for exactly this job; KWin's own Zoom/Magnifier effect
   uses the same shape.
3. Pass 1 builds a private `RenderTarget`/`RenderViewport` pair around our own
   `GLFramebuffer`, pushes it, and calls `effects->paintScreen(...)`. That call
   does **not** recurse into us: KWin's `EffectsHandler` tracks the current
   position in the effect chain and continues with the *next* effect, ending in
   the scene. Zamundaaa describes this contract directly — "if an effect wants
   to override the properties now, it just creates its own `RenderTarget` and
   `RenderViewport` and passes that to rendering methods".
4. Pass 2 pops back to the real framebuffer and draws the captured texture as
   one full-screen quad through `software_dim.frag`, which does the
   multiply. (KWin 6.7.5 loads that file verbatim on every context; the
   `_core` twin in the `.qrc` is never resolved by this KWin and is kept only
   for forward compatibility — see the comment at its top.)
5. `dimAmount` is a **uniform**, so changing the level costs a `setUniform` and
   a repaint. The shader is never recompiled, and the effect is never
   re-created.
6. `blocksDirectScanout()` returns `true` while active. Without it KWin can hand
   a fullscreen window's buffer straight to the display controller and skip the
   compositor, and a fullscreen app would stay bright.

What is deliberately **not** in here: no config GUI, no daemon, no D-Bus
service, no screen capture, no CPU readback, no image processing, no kcfg code
generation. Config is read straight out of `kwinrc`.

## 7. Colour space, gamma and HDR

The multiply happens on whatever KWin writes into the output framebuffer. For an
SDR output that is **gamma-encoded (sRGB transfer function) space**, not linear
light. Consequences:

* `dimAmount = 0.20` gives you 20% of the *encoded* value, which is roughly
  3.3% of linear luminance. That is perceptually much darker than "20%
  brightness", which for a sub-backlight-minimum dimmer is usually what you
  want — but do not read it as a physical luminance ratio.
* Dimming in encoded space preserves hue and relative saturation, and avoids
  banding that a linear-space multiply would introduce after re-encoding.

If you want a true linear-light dim later, the change is confined to one file:
convert to linear in `software_dim.frag`, multiply, convert back. KWin's
`RenderTarget` already carries a `ColorDescription`, and `GLShader` has a
`ShaderTrait::TransformColorspace` plus `setColorspaceUniforms()` for
exactly this. HDR/PQ/HLG outputs are **not** handled: the dim would be applied
to PQ-encoded values, which is wrong in a way that is visible as a colour shift.
Treat HDR outputs as unsupported for now.

## 8. Build

Dependencies (Arch / CachyOS):

```fish
sudo pacman -S --needed base-devel cmake extra-cmake-modules qt6-tools kwin \
                        kconfig kcoreaddons kglobalaccel kwindowsystem \
                        libepoxy qt6-base \
                        vulkan-headers wayland wayland-protocols \
                        plasma-wayland-protocols libdrm
```

`vulkan-headers`, `wayland-protocols`, `plasma-wayland-protocols`, `libdrm` and
`wayland` are **make** dependencies of `kwin` on Arch — they are not pulled as
runtime deps, but `KWinConfig.cmake` does `find_dependency(Vulkan)`,
`find_dependency(Wayland)`, etc., so you need them to *build* against KWin.
If you see `Could NOT find WrapVulkanHeaders (missing: Vulkan_INCLUDE_DIR)`,
install `vulkan-headers`.

Arch does **not** use a `kf6-` prefix — that is Fedora's convention, and pacman
answers it with `error: target not found`. On Arch `kf6` is a *group*, and the
individual framework packages are `kconfig`, `kcoreaddons`, `kglobalaccel`,
`kwindowsystem`.

Arch also has no `-dev` package split, so `kwin` itself provides the effect
development files — the `KWin` CMake config (`KWinConfig.cmake`, exporting the
`KWin::kwin` target) and the headers under `/usr/include/kwin/{effect,core,
opengl,...}`; `libepoxy` provides `epoxy/gl.h`. (Cross-checked against the
AUR PKGBUILD of an existing Plasma 6 out-of-tree effect, whose `makedepends`
are exactly `git cmake extra-cmake-modules qt6-tools kwin`.) The three `k*`
frameworks are already on your system as `kwin` dependencies, so `--needed`
will normally skip them — they are listed for completeness.

If `verify-api.sh` cannot find the headers even though `kwin` is installed, it
prints the diagnostics to locate them; re-run it with
`KWIN_INCLUDE_ROOT=/that/prefix` once you know where they are.

Other distributions:

```bash
# Debian / Ubuntu / Neon
sudo apt install build-essential cmake extra-cmake-modules pkg-config \
    kwin-dev qt6-base-dev qt6-base-dev-tools libepoxy-dev libwayland-dev \
    libvulkan-dev libdrm-dev \
    libkf6config-dev libkf6coreaddons-dev libkf6globalaccel-dev \
    libkf6windowsystem-dev

# Fedora
sudo dnf install gcc-c++ cmake extra-cmake-modules pkgconf-pkg-config \
    kwin-devel qt6-qtbase-devel libepoxy-devel wayland-devel \
    vulkan-headers libdrm-devel \
    kf6-kconfig-devel kf6-kcoreaddons-devel kf6-kglobalaccel-devel \
    kf6-kwindowsystem-devel
```

Then, from the repository root (where this README lives):

```fish
./scripts/verify-api.fish    # <-- run this first, see next section
./scripts/build.fish
```

Fish-native versions are provided alongside the original Bash scripts; use the
`.fish` files when running from Fish, or the `.sh` files when running from Bash.

Or by hand (also from the repository root):

```fish
cmake -B build -DCMAKE_BUILD_TYPE=Release -DQT_MAJOR_VERSION=6 -DKF_MAJOR_VERSION=6 -DBUILD_WITH_QT6=ON
cmake --build build
```

If you previously tried `cd dimsum` and got `cd: The directory 'dimsum' does not exist`,
you are already inside the repository — just run `./scripts/...` directly.

## 9. `scripts/verify-api.{sh,fish}` — read this before you build

KWin's effect API is a moving target, and I wrote this tree against the
**upstream 6.7 sources**, not against your installed headers — I had no KWin to
compile against here. So the tree ships a checker that answers the only question
that matters on your machine:

```fish
./scripts/verify-api.fish
```

It locates the installed `kwin/effect/effect.h` anchor, then greps for every
single symbol the effect uses and prints a table of `ok` / `MISSING` with file
and line, e.g.

```text
  paintScreen hook                   ok         effect/effect.h:709
  GLFramebuffer::pushFramebuffer     ok         opengl/glframebuffer.h:91
  GLTexture::allocate                MISSING    opengl/gltexture.h  (expected: allocate *\()
```

It also prints the **actual installed `paintScreen` signature** and tells you if
your KWin returns `[[nodiscard]] bool` (KWin 6.7.90 / Plasma 6.8) instead of
`void` (6.7.x), which is the one change this tree would need for a newer Plasma.

Point it at a non-standard prefix with `KWIN_INCLUDE_ROOT=/path ./scripts/verify-api.fish`.

**Run it. If it reports `MISSING`, do not guess — the table names the file and
the pattern, and §15 maps each one to the line to change.**

## 10. Install

```fish
./scripts/install.sh
```

That builds if needed, installs the plugin, and offers to remove the old
scripted prototype if `kpackagetool6 --type=KWin/Effect --list` still shows a
`kwin4_effect_software_dim` KPackage (a KPackage and a native plugin must not
share an effect id).

### Why this needs sudo

KWin discovers native effect plugins by scanning **Qt's system plugin
directory** — on Arch, `/usr/lib/qt6/plugins/kwin/effects/plugins`. (The
built-in effects are compiled statically into `kwin_wayland`, so do not be
surprised if that directory is empty on a fresh system — it is still the right
place.) `~/.local/lib/qt6/plugins` is not on Qt's plugin search path, so a
user-local `.so` is not found unless you export `QT_PLUGIN_PATH` in the session
environment *before* KWin starts. That works, but it is fragile and not
something KDE supports for effect plugins, so the honest default is a system
install.

If you want it anyway:

```fish
cmake -B build -DCMAKE_INSTALL_PREFIX=$HOME/.local \
               -DKWIN_EFFECTS_INSTALL_DIR=lib/qt6/plugins/kwin/effects/plugins
cmake --build build && cmake --install build
# then, before the session starts (e.g. in ~/.config/plasma-workspace/env/):
set -gx QT_PLUGIN_PATH $HOME/.local/lib/qt6/plugins $QT_PLUGIN_PATH
```

If `install.sh` cannot auto-detect the directory, pass it explicitly:

```fish
cmake -B build -DKWIN_EFFECTS_INSTALL_DIR=lib/qt6/plugins/kwin/effects/plugins
```

### Enable

System Settings → Window Management → Desktop Effects → *Software Dim*, or:

```fish
kwriteconfig6 --file kwinrc --group Plugins --key kwin4_effect_software_dimEnabled true
qdbus6 org.kde.KWin /KWin reconfigure
```

## 11. Usage and shortcuts

The effect registers three global shortcuts itself (via `KGlobalAccel`, the same
way KWin's own Invert effect does), so they appear in System Settings → Keyboard
→ Shortcuts under KWin and are rebindable there.

| Shortcut | Action id | What it does |
|---|---|---|
| `Meta+Alt+D` | `Software Dim` | Toggle dimmer on/off (`setAutoRepeat(false)` — one press is one toggle) |
| `Meta+Alt+Up` | `Software Dim: Increase Brightness` | `dimAmount += 0.05`, clamped to `1.00` |
| `Meta+Alt+Down` | `Software Dim: Decrease Brightness` | `dimAmount -= 0.05`, clamped to `0.05` |
| `Meta+Alt+S` | `Software Dim: Show Slider` | Popup slider — draggable, real-time, auto-hides after 3.5s, `Esc` to close |

Rebinding in System Settings sticks; the code only sets defaults.

The effect starts **disabled**. Loading it does not dim your screen until you
press `Meta+Alt+D` — a dimmer that turns itself on at login is a bad surprise.

### Slider popup (`dimsum-slider`)

A separate Qt Widgets app (`/usr/bin/dimsum-slider`) that can be launched:

* via the `Meta+Alt+S` global shortcut (registered by the effect itself, launches the binary with `QProcess::startDetached`)
* or manually: `dimsum-slider` from any shell (works from fish too)

Features:

* Frameless, always-on-top, centered, dark translucent with rounded corners
* `QSlider` from `DimMin` (default 5%) to 100%, label shows `%`
* Checkbox to enable/disable dimmer
* Drag anywhere on the popup to move it
* Real-time dimming: on drag it writes `DimAmount`/`Enabled` to `kwinrc` and calls `org.kde.KWin /KWin reconfigure` via D-Bus
* Auto-hides after 3.5s of no interaction, or on `Esc` / click outside (quits app)

The binary is built alongside the effect:

```fish
cmake -B build -DQT_MAJOR_VERSION=6 -DKF_MAJOR_VERSION=6
cmake --build build
./build/dimsum-slider   # test without installing
sudo cmake --install build  # installs to /usr/bin/dimsum-slider
```

## 12. Configuration

Everything lives in `~/.config/kwinrc`:

```ini
[Effect-kwin4_effect_software_dim]
DimAmount=0.2
DimStep=0.05
DimMin=0.05
Enabled=false
```

| Key | Default | Meaning |
|---|---|---|
| `DimAmount` | `0.20` | The multiply factor. `1.00` = no change, `0.05` = darkest. |
| `DimStep` | `0.05` | How far `Meta+Alt+Up/Down` moves it. |
| `DimMin` | `0.05` | Lower clamp. Hard floor is `0.01` (`0.00` would be a black screen). |
| `Enabled` | `false` | Persisted toggle state, written on every toggle. |

Edit by hand, then `qdbus6 org.kde.KWin /KWin reconfigure` — `reconfigure()`
re-reads the group and repaints.

## 13. Performance

Per frame, while active:

* one full-screen texture, allocated **once** per output/size and reused (it is
  only reallocated when the output size changes, and freed when the effect is
  switched off);
* one extra full-screen render pass into it;
* one full-screen textured quad with a single multiply in the fragment shader;
* zero CPU readback, zero `glReadPixels`, zero screenshots, zero per-frame
  allocations.

The honest cost is that this is **not** free: it is two full-screen passes
instead of one, at `3840x2400`, and it forces compositing on (direct scanout is
blocked while active, by design — otherwise fullscreen apps bypass the dim).
On Intel integrated graphics expect a measurable but small increase in GPU
utilisation. If you want the absolute cheapest possible dimmer, see §16 for the
blended-overlay variant.

Nothing here runs when the effect is off: `isActive()` returns false, so KWin
never calls `paintScreen()` and no texture exists.

## 14. Known limitations

* **Cursor.** On a hardware cursor (the normal Intel/Wayland case) the cursor is
  a separate DRM plane and is **not** dimmed — which is what you want. With a
  software cursor it is part of the scene and will be dimmed.
* **Colour space / HDR.** See §7. SDR only.
* **One pass, all outputs.** Every connected output is dimmed with the same
  factor. Per-output control is not implemented (§17).
* **API drift.** KWin's effect API changes between Plasma releases. This tree
  targets 6.7.x; `verify-api.sh` tells you whether it still matches.
* **Other compositors / X11.** None. This is KWin-only and does not build for
  or depend on X11.
* **Compositing suspended.** If you toggle compositing off (`Alt+Shift+F12`) or
  a fullscreen app suspends it, the dimmer stops applying. That is inherent to
  any compositor-side effect.

## 15. Troubleshooting

**`verify-api.sh` reports `MISSING` for a symbol.** The table gives you the file
and the pattern. The likely candidates and what to do:

| Symbol | If missing, your KWin renamed/changed | Where to fix |
|---|---|---|
| `paintScreen hook` | returns `[[nodiscard]] bool` from 6.7.90 | `src/softwaredim.h` and `src/softwaredim.cpp`: change the return type and `return true;` at the end of the function |
| `paintScreen Region/Output types` | region/output parameter types again | `src/softwaredim.h` and `src/softwaredim.cpp`: match the installed signature (6.7.x is `const Region &` + `LogicalOutput *`) |
| `GLTexture::allocate` | `GLTexture::allocateInternalFormat(GLint, QSize)` | one line in `ensureOffscreen()` |
| `GLFramebuffer(GLTexture *)` | constructor replaced by a factory again | `ensureOffscreen()` |
| `pushShader(GLShader *)` | overload takes `std::shared_ptr<GLShader>` | make `m_shader` a `shared_ptr` in the header |
| `KWIN_EFFECT_FACTORY macro` | — | `src/main.cpp` falls back to `K_PLUGIN_FACTORY_WITH_JSON`, which **builds but will not load**: KWin 6.7.5 stamps its effect plugins with the IID `org.kde.kwin.EffectPluginFactory6.7.5` (6.7.90 uses `…Factory6.7.90`), and a plain KF6 factory carries none. Build against the installed `kwin` headers so the macro branch is taken |

**CMake configure fails with `qt_generate_foreign_qml_types() is only available in Qt 6`.**

This is ECM's `QtVersionOption` defaulting to Qt5 when `QT_MAJOR_VERSION` is not
set before `KDECMakeSettings`. The fix is already in this tree's
`CMakeLists.txt` (it sets `QT_MAJOR_VERSION=6` and `KF_MAJOR_VERSION=6` before
including `KDECMakeSettings`) and in `scripts/build.{sh,fish}` (they pass
`-DQT_MAJOR_VERSION=6 -DKF_MAJOR_VERSION=6 -DBUILD_WITH_QT6=ON`). If you still
hit it:

```fish
rm -rf build
cmake -B build -DCMAKE_BUILD_TYPE=Release -DQT_MAJOR_VERSION=6 -DKF_MAJOR_VERSION=6 -DBUILD_WITH_QT6=ON
cmake --build build
```

Ensure `extra-cmake-modules` is >= 6.0 (`pacman -Q extra-cmake-modules` /
`apt show extra-cmake-modules`). See also <https://github.com/KDAB/GammaRay/issues/742>.

**CMake configure fails with `Could not find KWin's effect development files (KWin::kwin target)` even though kwin is installed.**

`KWinConfig.cmake` itself calls `find_dependency()` for `Vulkan`, `Wayland`,
`Libdrm`, `Qt6Quick`, `KF6WindowSystem`, etc. On Arch those are *make* deps of
`kwin`, not runtime deps, so `kwin` can be installed without them, but
`find_package(KWin)` will fail. The line just above the error tells you which
one is missing, e.g.:

```text
-- Could NOT find WrapVulkanHeaders (missing: Vulkan_INCLUDE_DIR)
```

Fix:

```fish
sudo pacman -S vulkan-headers wayland wayland-protocols plasma-wayland-protocols libdrm kwindowsystem
# Debian/Ubuntu:
sudo apt install libvulkan-dev libwayland-dev libdrm-dev libkf6windowsystem-dev
# Fedora:
sudo dnf install vulkan-headers wayland-devel libdrm-devel kf6-kwindowsystem-devel
rm -rf build
./scripts/build.fish
```

This tree now also has a fallback that manually creates `KWin::kwin` from
`/usr/include/kwin` + `/usr/lib/libkwin.so` if `KWinConfig.cmake` fails, but
installing the missing headers is still the correct fix.

**Shader fails to compile** (`kwin_effect_software_dim: Failed to compile the
dim shader` in the journal, plus a numbered source dump from KWin under the
`kwin_opengl` category). On 6.7.5 the `.frag` files must declare their own
`sampler` / `texcoord0` / `fragColor` — KWin prepends only `#version`,
precision qualifiers and `TRAIT_*` defines (`GLShader::preprocess` in
`src/opengl/glshader.cpp`), exactly like its own effect shaders do. If a future
KWin starts injecting them, the failure mode flips to a GLSL redefinition
error, and the fix is to delete that block from both `.frag` files:

```glsl
uniform sampler2D sampler;
in vec2 texcoord0;
out vec4 fragColor;
```

Then rebuild. The effect never blanks the screen over this: if the shader does
not compile, `m_valid` stays false, `isActive()` returns false and every hook
becomes a pass-through.

**`loadEffect` returns `false`.**

```fish
qdbus6 org.kde.KWin /Effects listOfEffects | grep software_dim   # is it discovered at all?
qdbus6 org.kde.KWin /KWin supportInformation | head -40          # is compositing OpenGL?
journalctl -b -u plasma-kwin_wayland --since "5 minutes ago" --no-pager | grep -iE 'software_dim|shader|effect'
```

`supported()` returns false unless `compositingType() == OpenGLCompositing`.

**Screen went black.** It should not be able to — but the escape hatch is one
command, from a VT (`Ctrl+Alt+F3`) if you have to:

```bash
qdbus6 org.kde.KWin /Effects unloadEffect kwin4_effect_software_dim
```

If even that is unreachable, disable it in config and restart the session:

```bash
kwriteconfig6 --file kwinrc --group Plugins --key kwin4_effect_software_dimEnabled false
```

**Effect id collision.** If a scripted `kwin4_effect_software_dim` KPackage is
still installed:

```fish
kpackagetool6 --type=KWin/Effect --remove kwin4_effect_software_dim
```

**Journal spam.** There should be none. The logging category is
`kwin_effect_software_dim`, at `QtWarningMsg` by default, and every log call is
event-driven (load, toggle, brightness change, reconfigure, shader failure).
Nothing in the paint path logs. To see the info lines:

```fish
QT_LOGGING_RULES="kwin_effect_software_dim.debug=true" kwin_wayland --replace   # or via kdebugsettings
journalctl -b -u plasma-kwin_wayland --since "5 minutes ago" | grep -i software_dim
```

## 16. A cheaper variant, if you want one

Pass 1 + pass 2 can be replaced by a single **alpha-blended full-screen black
quad** drawn in `postPaintScreen()`:

```text
out = scene * (1 - a) + black * a      with a = 1 - dimAmount
```

which is arithmetically identical to `scene * dimAmount`. It needs no offscreen
texture and no second full-screen pass, so it is strictly cheaper. It is not
what is implemented here, because the offscreen pass is the canonical
post-processing hook and it is where a future linear-light or HDR-aware dim has
to live. Say the word and it is a small change.

## 17. Multi-monitor

`paintScreen()` is called per output, so every connected output is dimmed
automatically — internal, external, any number. Nothing is hard-coded to
`eDP-1`.

Per-output control is not implemented, but nothing stands in its way:
`paintScreen()` already receives a `LogicalOutput *`, and
`LogicalOutput::name()` is in the installed `kwin/core/output.h` set. Adding
it means keeping a `QSet<QString>` of dimmed output names and consulting it in
`paintScreen()`. Global dimming is the documented behaviour for now.

## 18. Testing

```fish
./scripts/test.sh            # six stages, interactive
./scripts/test.sh --quick    # stages 1-3 only, no keypresses
```

The stages, which mirror the acceptance list:

1. the `.so` is in an effect plugin directory, and no scripted prototype
   shares the id;
2. `qdbus6 org.kde.KWin /Effects loadEffect kwin4_effect_software_dim` → `true`;
3. `qdbus6 org.kde.KWin /Effects isEffectLoaded kwin4_effect_software_dim` → `true`;
4. `Meta+Alt+D` toggles, `Meta+Alt+Up/Down` step;
5. wallpaper, panels, windows, notifications and fullscreen apps all dim; cursor
   and input unaffected; no flicker or corruption;
6. toggling off restores normal output; `unloadEffect` → `true` and
   `isEffectLoaded` → `false`.

## 19. Uninstall

```fish
./scripts/uninstall.sh
```

Unloads the effect, sets `kwin4_effect_software_dimEnabled=false`, removes the
plugin and the metadata copy, and removes any leftover scripted prototype. Idempotent.

## 20. Source tree

```text
.
├── CMakeLists.txt
├── LICENSE
├── README.md
├── scripts/
│   ├── build.sh / build.fish              # verify-api, then configure + build
│   ├── install.sh / install.fish          # build + install + clean up the old prototype
│   ├── test.sh                             # the six acceptance stages
│   ├── uninstall.sh
│   └── verify-api.sh / verify-api.fish     # check installed KWin headers and symbols
└── src/
    ├── main.cpp          # plugin entry point (KWIN_EFFECT_FACTORY_*)
    ├── metadata.json     # compiled into the .so (Id removed, see troubleshooting)
    ├── softwaredim.h
    ├── softwaredim.cpp
    ├── softwaredim.qrc
    ├── shaders/
    │   ├── software_dim.frag        # the shader, on every context
    │   └── software_dim_core.frag   # identical twin, kept for forward compat
    └── slider/
        ├── main.cpp          # dimsum-slider executable entry
        ├── sliderpopup.h     # draggable popup with QSlider
        └── sliderpopup.cpp   # writes kwinrc + D-Bus reconfigure
```

## 21. Verification status — what was checked, and what was not

Being explicit, because it changes how much you should trust a first build:

**Checked against the KWin `v6.7.5` sources** (the exact tag this tree targets,
fetched from `KDE/kwin` during development — file, line and spelling):

* Installed layout: headers under `kwin/{effect,core,opengl,...}`,
  `KWinConfig.cmake` exporting `KWin::kwin` — `src/CMakeLists.txt`,
  `KWinConfig.cmake.in`.
* `paintScreen(const RenderTarget &, const RenderViewport &, int, const Region
  &, LogicalOutput *)` returning `void`,
  `RenderTarget(GLFramebuffer *, std::shared_ptr<ColorDescription>)`,
  four-argument `RenderViewport(RectF, double, RenderTarget, QPoint)`,
  `GLFramebuffer::pushFramebuffer/popFramebuffer`,
  `effects->paintScreen(...)`, `GLShader::Mat4Uniform::ModelViewProjectionMatrix`,
  `viewport.projectionMatrix()`, `viewport.deviceSize()`,
  `GLTexture::render(QSizeF)` — `src/effect/effect.h`, `src/core/`,
  `src/opengl/`, and KWin's Zoom effect, whose offscreen pass this effect
  mirrors (`src/plugins/zoom/zoom.cpp`).
* `GLTexture::allocate(GLenum, QSize, int)` — and that `GLFramebuffer` has no
  `create()` factory on 6.7.5, only the `GLFramebuffer(GLTexture *)`
  constructor. `GLShader` has no `isValid()` — `generateShaderFromFile()`
  returns `nullptr` on failure.
* `KWIN_EFFECT_FACTORY_SUPPORTED_ENABLED` and the version-stamped
  `EffectPluginFactory_iid` (`org.kde.kwin.EffectPluginFactory6.7.5`) —
  `src/effect/effect.h`, enforced by `src/effect/effectloader.cpp`, which is
  why the macro must come from your installed headers rather than being
  hand-rolled, and why an effect built against one Plasma will not load on
  another.
* Effect plugins load from `<qt plugin dir>/kwin/effects/plugins`, not from
  `~/.local` — `src/effect/effectloader.cpp`. The `verify-api` scripts confirm
  the directory by asking Qt (`qtpaths6`/`qmake6`) rather than guessing it.
* The `.frag` files must declare `sampler`/`texcoord0`/`fragColor` themselves:
  `GLShader::preprocess` (`src/opengl/glshader.cpp`) prepends only `#version`,
  precision qualifiers and `TRAIT_*` defines, and 6.7.5's
  `generateShaderFromFile` loads the exact path it is given (the `_core`
  suffix in its header comment is not implemented). Same declarations as
  `src/plugins/invert/shaders/invert.frag`.
* `Effect` signals are `windowAdded` / `windowClosed` — `windowShown` /
  `windowDeleted` do not exist in KWin 6 (this is the error your prototype hit).
* Global shortcut registration pattern, including `setAutoRepeat(false)` —
  `src/plugins/invert/invert.cpp`.
* `Q_LOGGING_CATEGORY(…, "kwin_effect_<id>", QtWarningMsg)` and the
  `#include "moc_<name>.cpp"` convention — both upstream effects.
* KWin 6.7 declares `paintScreen` returning `void`; KWin 6.7.90 (Plasma 6.8)
  changed it to `[[nodiscard]] bool` — a third-party effect that builds against
  both.
* `scripts/verify-api.sh` was run against the 6.7.5 headers and reports all
  symbols present.

**Not checked**, because there was no KWin, Qt6, KF6 or CMake in the porting
sandbox, and no running compositor anywhere near it:

* this tree has **not been compiled**;
* it has **not been loaded by a running compositor**.

`scripts/verify-api.sh` exists to close the remaining gap — installed headers
vs. this source — on your machine in one command, before you invest in a build.

## 22. License

MIT — see [LICENSE](LICENSE).
