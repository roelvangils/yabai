#ifndef MOUSE_H
#define MOUSE_H

#define MOUSE_EVENT_MASK_FFM (1 << kCGEventMouseMoved) | \
                             (1 << kCGEventLeftMouseDown) | \
                             (1 << kCGEventLeftMouseUp) | \
                             (1 << kCGEventLeftMouseDragged) | \
                             (1 << kCGEventRightMouseDown) | \
                             (1 << kCGEventRightMouseUp) | \
                             (1 << kCGEventRightMouseDragged) | \
                             (1 << /* kCGSEventDockControl */ 30)

#define MOUSE_EVENT_MASK     (1 << kCGEventLeftMouseDown) | \
                             (1 << kCGEventLeftMouseUp) | \
                             (1 << kCGEventLeftMouseDragged) | \
                             (1 << kCGEventRightMouseDown) | \
                             (1 << kCGEventRightMouseUp) | \
                             (1 << kCGEventRightMouseDragged) | \
                             (1 << /* kCGSEventDockControl */ 30)

#define MOUSE_HANDLER(name) CGEventRef name(CGEventTapProxy proxy, CGEventType type, CGEventRef event, void *context)

enum mouse_drop_action
{
    MOUSE_DROP_ACTION_NONE,
    MOUSE_DROP_ACTION_STACK,
    MOUSE_DROP_ACTION_SWAP,
    MOUSE_DROP_ACTION_WARP_TOP,
    MOUSE_DROP_ACTION_WARP_RIGHT,
    MOUSE_DROP_ACTION_WARP_BOTTOM,
    MOUSE_DROP_ACTION_WARP_LEFT
};

enum mouse_mod
{
    MOUSE_MOD_NONE  = 0x01,
    MOUSE_MOD_ALT   = 0x02,
    MOUSE_MOD_SHIFT = 0x04,
    MOUSE_MOD_CMD   = 0x08,
    MOUSE_MOD_CTRL  = 0x10,
    MOUSE_MOD_FN    = 0x20
};

enum mouse_mode
{
    MOUSE_MODE_NONE,
    MOUSE_MODE_MOVE,
    MOUSE_MODE_RESIZE,
    MOUSE_MODE_SWAP,
    MOUSE_MODE_STACK
};

struct mouse_window_info
{
    float dx, dy, dw, dh;
    bool changed_x, changed_y;
    bool changed_w, changed_h;
    bool changed_position;
    bool changed_size;
};

struct mouse_state
{
    CFMachPortRef handle;
    CFRunLoopSourceRef runloop_source;
    bool consume_mouse_click;
    bool drag_detected;
    CGEventRef consumed_event;
    enum mouse_mode action1;
    enum mouse_mode action2;
    volatile uint8_t modifier;
    // LOCAL PATCH: when true, a modifier+click that never became a drag is
    // NOT replayed into the application. Stored inverted so that the
    // zero-initialised default (false) keeps upstream behaviour.
    // See `mouse_replay_click` in message.c and PATCHES.md.
    volatile bool no_replay_click;
    enum mouse_mode drop_action;
    enum mouse_mode current_action;
    CGPoint down_location;
    uint64_t last_moved_time;
    struct window *window;
    CGRect window_frame;
    uint32_t ffm_window_id;
    uint8_t direction;
    struct window_node *feedback_node;
};

static char *mouse_mod_str[] =
{
    [MOUSE_MOD_NONE]  = "none",
    [MOUSE_MOD_ALT]   = "alt",
    [MOUSE_MOD_SHIFT] = "shift",
    [MOUSE_MOD_CMD]   = "cmd",
    [MOUSE_MOD_CTRL]  = "ctrl",
    [MOUSE_MOD_FN]    = "fn",
};

//
// LOCAL PATCH: allow mouse_modifier to be a combination, e.g.
//     yabai -m config mouse_modifier cmd+ctrl+alt+shift
//
// The flags are already a bitmask and mouse_handler() compares them with
// exact equality, so a combination works unchanged there -- only the config
// parser was restricted to a single value. This makes it possible to use a
// hyper key (cmd+ctrl+alt+shift, as produced by Karabiner-Elements) as the
// drag modifier.
//
// Note mouse_mod_str[] above is indexed BY FLAG VALUE and so cannot render a
// combination (index 0x1E is an uninitialised slot); mouse_mod_to_string()
// below replaces it for read-back.
//

static const struct { uint8_t flag; const char *name; int length; } mouse_mod_map[] =
{
    { MOUSE_MOD_NONE,  "none",  4 },
    { MOUSE_MOD_ALT,   "alt",   3 },
    { MOUSE_MOD_SHIFT, "shift", 5 },
    { MOUSE_MOD_CMD,   "cmd",   3 },
    { MOUSE_MOD_CTRL,  "ctrl",  4 },
    { MOUSE_MOD_FN,    "fn",    2 },
};

#define MOUSE_MOD_MAP_COUNT (sizeof(mouse_mod_map) / sizeof(mouse_mod_map[0]))

// Parse a '+'-separated modifier list. Returns 0 if any component is unknown
// (0 is not a valid modifier -- MOUSE_MOD_NONE is 0x01 -- so it is safe as a
// failure value). text is NOT null-terminated; length bounds it.
static inline uint8_t mouse_mod_from_string(const char *text, int length)
{
    uint8_t result = 0;

    for (int i = 0; i < length; ) {
        int start = i;
        while (i < length && text[i] != '+') ++i;

        int      part_length = i - start;
        const char *part     = text + start;
        uint8_t  flag        = 0;

        for (int j = 0; j < (int)MOUSE_MOD_MAP_COUNT; ++j) {
            if (part_length == mouse_mod_map[j].length &&
                memcmp(part, mouse_mod_map[j].name, part_length) == 0) {
                flag = mouse_mod_map[j].flag;
                break;
            }
        }

        if (!flag) return 0;
        result |= flag;

        if (i < length && text[i] == '+') ++i;
    }

    return result;
}

// Render a (possibly combined) modifier back to "cmd+ctrl+alt+shift" form,
// in the same order as mouse_mod_map.
static inline void mouse_mod_to_string(uint8_t modifier, char *buffer, int size)
{
    int n = 0;
    buffer[0] = '\0';

    for (int i = 0; i < (int)MOUSE_MOD_MAP_COUNT; ++i) {
        if (!(modifier & mouse_mod_map[i].flag)) continue;
        n += snprintf(buffer + n, size - n, "%s%s", n ? "+" : "", mouse_mod_map[i].name);
        if (n >= size) return;
    }

    if (n == 0) snprintf(buffer, size, "none");
}

static char *mouse_mode_str[] =
{
    [MOUSE_MODE_NONE]   = "none",
    [MOUSE_MODE_MOVE]   = "move",
    [MOUSE_MODE_RESIZE] = "resize",
    [MOUSE_MODE_SWAP]   = "swap",
    [MOUSE_MODE_STACK]  = "stack"
};

void mouse_window_info_populate(struct mouse_state *ms, struct mouse_window_info *info);
enum mouse_drop_action mouse_determine_drop_action(struct mouse_state *ms, struct window_node *src_node, struct window *dst_window, CGPoint point);
void mouse_drop_action_stack(struct window_manager *wm, struct view *src_view, struct window *src_window, struct view *dst_view, struct window *dst_window);
void mouse_drop_action_swap(struct window_manager *wm, struct view *src_view, struct window_node *src_node, struct window *src_window, struct view *dst_view, struct window_node *dst_node, struct window *dst_window);
void mouse_drop_action_warp(struct window_manager *wm, struct view *src_view, struct window_node *src_node, struct window *src_window, struct view *dst_view, struct window_node *dst_node, struct window *dst_window, enum window_node_split split, enum window_node_child child);
void mouse_drop_no_target(struct space_manager *sm, struct window_manager *wm, struct view *src_view, struct view *dst_view, struct window *window, struct window_node *node);
void mouse_drop_try_adjust_bsp_grid(struct window_manager *wm, struct view *view, struct window *window, struct mouse_window_info *info);

void mouse_state_init(struct mouse_state *mouse_state);
bool mouse_handler_begin(struct mouse_state *mouse_state, uint32_t mask);
void mouse_handler_end(struct mouse_state *mouse_state);

#endif
