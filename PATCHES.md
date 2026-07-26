# Local patches

Changes carried on top of [AhsanFazal/yabai](https://github.com/AhsanFazal/yabai)
(itself a fork of [asmvik/yabai](https://github.com/asmvik/yabai) carrying macOS 27
"Golden Gate" scripting-addition support).

Each patch is a single commit so that `git pull --rebase` from upstream replays it
cleanly. If a rebase ever conflicts, the notes below should be enough to redo the
change from scratch.

---

## 1. `mouse_modifier` accepts combined modifiers

**Commit:** "allow `mouse_modifier` to accept combined modifiers"

**Files:** `src/mouse_handler.h`, `src/message.c`, `doc/yabai.asciidoc`

### What it does

Lets `mouse_modifier` be a `+`-separated combination instead of exactly one modifier:

```bash
yabai -m config mouse_modifier cmd+ctrl+alt+shift
```

### Why

To move and resize windows by **holding keys and moving the mouse, with no click**.

Karabiner-Elements presses a mouse button while a key combination is held, and yabai
treats that as an ordinary drag. But the combination naturally carries more than one
modifier — here `ctrl+alt` — and stock yabai can only match exactly one, so the
click was never claimed and fell through to the application as a real click-drag.

Karabiner specifically (rather than a userspace tool) because yabai taps the event
stream at `kCGHIDEventTap` (`src/mouse_handler.c`), upstream of where applications
can inject events. A click posted by, say, Hammerspoon is simply never seen: no
drag, no error. Only a virtual HID device reaches that far upstream.

> **Tried and abandoned:** driving this from a hyper key (caps_lock →
> `cmd+ctrl+alt+shift`). Karabiner's `to_if_held_down` disturbs the modifiers held
> by that same manipulator's `to`, so `hyper`+`s` arrived as a bare `s`. Putting the
> drag on a key that is *not* itself a modifier remap avoids this entirely. The
> patch is still required, because `ctrl+alt` is two modifiers.

### How

The flags were **already** a bitmask (`MOUSE_MOD_ALT = 0x02` … `MOUSE_MOD_FN = 0x20`)
and `mouse_handler()` compares them with exact equality:

```c
if (mod == mouse_state->modifier) { ... }
```

so a combined value works there with no change. Only the config parser was
restrictive — it assigned a single flag per branch rather than OR-ing.

Two helpers were added to `src/mouse_handler.h`:

- `mouse_mod_from_string(text, length)` — parses `"cmd+ctrl"` into a bitmask,
  returning `0` on an unknown component (safe as a failure value, since
  `MOUSE_MOD_NONE` is `0x01`, not `0`)
- `mouse_mod_to_string(modifier, buffer, size)` — renders a bitmask back to
  `"cmd+ctrl+alt+shift"`

`src/message.c` then uses both in the `mouse_modifier` config branch.

### The subtlety worth remembering

The existing `mouse_mod_str[]` table is a **sparse array indexed by flag value**:

```c
static char *mouse_mod_str[] = {
    [MOUSE_MOD_NONE] = "none", [MOUSE_MOD_ALT] = "alt", ...
};
```

It cannot render a combination — `mouse_mod_str[0x1E]` is an uninitialised slot,
so reading back a combined modifier would print garbage or crash. That is why
`mouse_mod_to_string()` exists and why the read-back path had to change too.
`mouse_mod_str[]` is left in place for any other consumer.

### Semantics preserved

Matching is still **exact**, not a subset test. With `cmd+ctrl+alt+shift`
configured, `cmd+click` does nothing and `hyper+shift+click` does nothing — only
the exact combination drags. Nothing about single-modifier configurations changes.

### Related local configuration

Not part of this repo, but this patch is useless without it.

`~/.yabairc`:

```bash
yabai -m config mouse_modifier     ctrl+alt
yabai -m config mouse_replay_click off        # see patch 2
```

`~/.config/karabiner/karabiner.json` — three rules, all pressing a mouse button
while keys are held. yabai picks the action from the **button**, not the modifier
(`mouse_action1` = button1 = move, `mouse_action2` = button2 = resize):

| Keys | Emits | Result |
| :--- | :--- | :--- |
| left-ctrl + left-option | `button1` + `ctrl+alt` | move |
| left-ctrl + left-option + left-command | `button2` + `ctrl+alt` | resize |
| right-command | `button1` + `ctrl+alt` | move |

Two details that are easy to get wrong:

- The **three-key rule must be ordered before the two-key rule**, since Karabiner
  takes the first match — otherwise ctrl+option wins and you get a move.
- Command is **consumed but not asserted** on the resize event. Adding it would
  make the flags `ctrl+alt+cmd`, which fails the exact-equality match and lets a
  real right-click through.

`simultaneous` rules always defer the first key of the set, so
`basic.simultaneous_threshold_milliseconds` is lowered to 60 (default 1000).
Raising it makes chords easier to hit and plain Option/Command shortcuts laggier.

### Known rough edge

`src/mouse_handler.c` replays the swallowed click if no drag is detected between
mouse-down and mouse-up. So holding the trigger past the Karabiner threshold and
releasing *without moving the mouse* delivers a real click wherever the pointer
sits. Addressed by patch 2 below.

---

## 2. `mouse_replay_click` — suppress the replayed click

**Commit:** "add `mouse_replay_click` to suppress the replayed click"

**Files:** `src/mouse_handler.h`, `src/mouse_handler.c`, `src/message.c`, `doc/yabai.asciidoc`

### What it does

```bash
yabai -m config mouse_replay_click off
```

Stops yabai from replaying a consumed `mouse_modifier`+click into the application
when no drag followed.

### Why

Upstream, `mouse_handler()` swallows the mouse-down when the modifier matches, then
on mouse-up:

```c
if (mouse_state->consume_mouse_click) {
    if (!mouse_state->drag_detected) {
        CGEventTapPostEvent(proxy, mouse_state->consumed_event);   // replay
        CGEventTapPostEvent(proxy, event);
    }
```

That is correct for a normal modifier: `alt`+click should still click. But when the
modifier is a **hyper key that is also your keyboard shortcut prefix**, it is wrong.
Karabiner presses button1 once the key has been held past a threshold, so any hyper
chord (hold caps_lock, then press `s`) ends with a mouse-up and no drag — and the
click is replayed onto whatever is under the pointer.

Without this patch the only mitigation is a *long* hold threshold, which forces you
to chord quickly and still delays the drag. With it, the threshold can be ~0: arming
button1 during a chord becomes harmless, so you can hold hyper for as long as you
like before pressing the next key, and dragging starts immediately.

Nothing is lost, because hyper+click is not a gesture any application implements.

### How

A `no_replay_click` flag on `struct mouse_state`, guarding the replay.

Note it is stored **inverted**. `g_mouse_state` is a global with no explicit
initialiser, so it is zero-initialised; `no_replay_click = false` therefore means
"replay", which is exactly the upstream behaviour. A positively-named flag would
have defaulted to off and silently changed behaviour for anyone who never sets it.
The config layer flips it back so the user-facing setting reads naturally
(`mouse_replay_click on|off`, default `on`).

### Related local configuration

- `~/.yabairc` — `yabai -m config mouse_replay_click off`
- `~/.config/karabiner/karabiner.json` — with the replay gone, a trigger no longer
  needs a hold threshold at all. right-command uses a plain `to`, so button1 goes
  down on key-down and dragging starts instantly; pressing it without moving the
  mouse now does nothing rather than clicking. That is only safe because of this
  patch.
