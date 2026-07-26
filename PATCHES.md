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

To drag windows with a **hyper key**. Karabiner-Elements maps caps_lock to
`cmd+ctrl+alt+shift`; holding it and pressing mouse button1 produces those four
flags at once. Stock yabai can only match a single modifier, so a hyper key could
never trigger a drag.

This matters because yabai taps the event stream at `kCGHIDEventTap`
(`src/mouse_handler.c`), upstream of where applications can inject events. That
makes Karabiner's virtual HID device the only practical way to synthesise the
click, and Karabiner's natural output for caps_lock is the full hyper combination.

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

Not part of this repo, but this patch is useless without them:

- `~/.config/karabiner/karabiner.json` — caps_lock sends hyper on press (`to`),
  and presses `button1` when held (`to_if_held_down`)
- `~/.yabairc` — `yabai -m config mouse_modifier cmd+ctrl+alt+shift`

### Known rough edge

`src/mouse_handler.c` replays the swallowed click if no drag is detected between
mouse-down and mouse-up. So holding the trigger past the Karabiner threshold and
releasing *without moving the mouse* delivers a real click wherever the pointer
sits. Unrelated to this patch, but it is the behaviour that makes threshold
choice matter.
