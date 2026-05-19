#include <poll.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cerrno>
#include <cstdint>
#include <ctime>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <wayland-client.h>
#define namespace namespace_
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#undef namespace

#include <cairo/cairo.h>
#include <pango/pangocairo.h>

// Wayland shared-memory buffers are the bridge between our CPU-side drawing
// and the compositor. Cairo renders into `data`, and `wl` is the Wayland
// object attached to the surface for presentation.
struct Buffer {
    wl_buffer* wl = nullptr;
    void* data = nullptr;
    int fd = -1;
    int w = 0, h = 0, stride = 0, size = 0;
};

struct Color {
    double r = 0.0;
    double g = 0.0;
    double b = 0.0;
};

struct App;

// Each physical output needs its own layer-shell surface and backing buffer.
struct Bar {
    App* app = nullptr;
    wl_output* output = nullptr;
    wl_surface* surface = nullptr;
    zwlr_layer_surface_v1* layer_surface = nullptr;
    uint32_t registry_name = 0;

    Buffer buf{};
    int width = 0;
    int height = 20;
    int output_scale = 1;
    int buffer_scale = 1;
    bool configured = false;
    bool redraw = true;
    bool visible = true;
    bool closed = false;
    uint64_t show_at_ms = 0;

    std::string output_name;
    std::string left = "[1] 2 3 4";
    std::string center = "minibar";
    std::string right = "--:--";
};

// `App` keeps all global Wayland objects and the shared text state in one
// place. Per-output rendering state lives in `bars`.
struct App {
    wl_display* display = nullptr;
    wl_registry* registry = nullptr;
    wl_compositor* compositor = nullptr;
    wl_shm* shm = nullptr;
    zwlr_layer_shell_v1* layer_shell = nullptr;

    std::vector<std::unique_ptr<Bar>> bars;

    bool running = true;
    int bar_height = 20;
    int font_size = 10;
    Color background{};
    bool position_top = true;
};

constexpr char kSectionSeparator = '\x1f';
constexpr uint64_t kShowAfterFullscreenDelayMs = 100;
constexpr char kVersion[] = "0.1.0";

enum class ArgsResult {
    Ok,
    ExitSuccess,
    Error,
};

static uint64_t now_ms() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

static void usage() {
    std::cerr << "usage: minibar [options]\n"
              << "\n"
              << "options:\n"
              << "  -h, --help                 show this help and exit\n"
              << "  --version                  show version and exit\n"
              << "  --height N                 bar height in logical pixels (default: 20)\n"
              << "  --font-size N              font size (default: 10)\n"
              << "  --background #RRGGBB       background color (default: #141414)\n"
              << "  --position top|bottom      monitor edge, also accepts up|down (default: top)\n"
              << "\n"
              << "stdin protocol:\n"
              << "  Text updates are read from stdin. Sections are separated by ASCII Unit\n"
              << "  Separator, byte 0x1f. The three-section form updates every bar:\n"
              << "\n"
              << "    left<0x1f>center<0x1f>right\n"
              << "\n"
              << "  The four-section form targets one output and starts with a config section\n"
              << "  containing space-separated key=value pairs:\n"
              << "\n"
              << "    output=eDP-1 visible=1<0x1f>left<0x1f>center<0x1f>right\n"
              << "\n"
              << "  Supported config keys:\n"
              << "    output=<name>             Wayland output name to update\n"
              << "    visible=0|1               hide or show that output's bar\n"
              << "\n"
              << "examples:\n"
              << "  printf 'left\\x1fcenter\\x1fright\\n' | minibar\n"
              << "  printf 'output=eDP-1 visible=1\\x1fleft\\x1fcenter\\x1fright\\n' | minibar\n";
}

static bool parse_positive_int(const char* value, int& out) {
    if (!value || !*value) return false;
    char* end = nullptr;
    long n = std::strtol(value, &end, 10);
    if (*end || n <= 0 || n > 1000) return false;
    out = (int)n;
    return true;
}

static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool parse_hex_color(const char* value, Color& out) {
    if (!value || std::strlen(value) != 7 || value[0] != '#') return false;

    int rgb[3]{};
    for (int i = 0; i < 3; ++i) {
        int hi = hex_digit(value[1 + i * 2]);
        int lo = hex_digit(value[2 + i * 2]);
        if (hi < 0 || lo < 0) return false;
        rgb[i] = hi * 16 + lo;
    }

    out.r = rgb[0] / 255.0;
    out.g = rgb[1] / 255.0;
    out.b = rgb[2] / 255.0;
    return true;
}

static bool option_value(int argc, char** argv, int& i, const char* arg,
                         const char* name, const char*& value) {
    size_t len = std::strlen(name);
    if (std::strncmp(arg, name, len) != 0) return false;

    if (arg[len] == '=') {
        value = arg + len + 1;
        return true;
    }

    if (arg[len] == '\0' && i + 1 < argc) {
        value = argv[++i];
        return true;
    }

    return false;
}

static ArgsResult parse_args(App& a, int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        const char* value = nullptr;

        if (std::strcmp(arg, "--help") == 0 || std::strcmp(arg, "-h") == 0) {
            usage();
            return ArgsResult::ExitSuccess;
        }

        if (std::strcmp(arg, "--version") == 0) {
            std::cout << "minibar " << kVersion << "\n";
            return ArgsResult::ExitSuccess;
        }

        if (option_value(argc, argv, i, arg, "--height", value)) {
            if (parse_positive_int(value, a.bar_height)) continue;
        } else if (option_value(argc, argv, i, arg, "--font-size", value)) {
            if (parse_positive_int(value, a.font_size)) continue;
        } else if (option_value(argc, argv, i, arg, "--fontsize", value)) {
            if (parse_positive_int(value, a.font_size)) continue;
        } else if (option_value(argc, argv, i, arg, "--background", value)) {
            if (parse_hex_color(value, a.background)) continue;
        } else if (option_value(argc, argv, i, arg, "--position", value)) {
            if (std::strcmp(value, "top") == 0 || std::strcmp(value, "up") == 0) {
                a.position_top = true;
                continue;
            }
            if (std::strcmp(value, "bottom") == 0 || std::strcmp(value, "down") == 0) {
                a.position_top = false;
                continue;
            }
        }

        std::cerr << "invalid option: " << arg << "\n";
        usage();
        return ArgsResult::Error;
    }

    return ArgsResult::Ok;
}

static int create_shm_file(size_t size) {
    // Wayland shm buffers need a file descriptor the compositor can also map.
    // `memfd_create` is the cleanest option when available; the mkstemp path is
    // a portable fallback.
#ifdef MFD_CLOEXEC
    {
        int fd = memfd_create("minibar", MFD_CLOEXEC);
        if (fd >= 0) {
            if (ftruncate(fd, (off_t)size) == 0) return fd;
            close(fd);
        }
    }
#endif
    char name[] = "/minibar-XXXXXX";
    int fd = mkstemp(name);
    if (fd < 0) return -1;
    unlink(name);
    if (ftruncate(fd, (off_t)size) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static void destroy_buffer(Buffer& b) {
    if (b.wl) wl_buffer_destroy(b.wl);
    if (b.data) munmap(b.data, b.size);
    if (b.fd >= 0) close(b.fd);
    b = {};
}

static bool make_buffer(App& a, Bar& b, int w, int h) {
    // Recreate the backing store whenever the logical bar size or buffer scale
    // changes. The compositor sees the wl_buffer; Cairo sees the mmap'd memory.
    destroy_buffer(b.buf);
    b.buf.w = w;
    b.buf.h = h;
    b.buf.stride = w * 4;
    b.buf.size = b.buf.stride * h;
    b.buf.fd = create_shm_file(b.buf.size);
    if (b.buf.fd < 0) return false;

    b.buf.data = mmap(nullptr, b.buf.size, PROT_READ | PROT_WRITE, MAP_SHARED, b.buf.fd, 0);
    if (b.buf.data == MAP_FAILED) {
        b.buf.data = nullptr;
        destroy_buffer(b.buf);
        return false;
    }

    wl_shm_pool* pool = wl_shm_create_pool(a.shm, b.buf.fd, b.buf.size);
    b.buf.wl = wl_shm_pool_create_buffer(pool, 0, w, h, b.buf.stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    return b.buf.wl != nullptr;
}

static void draw_text(cairo_t* cr, const std::string& text, int x, int bar_h, int scale,
                       int font_size) {
    // Pango handles text shaping and markup, then Cairo paints the prepared
    // layout into the shared-memory image.
    PangoLayout* layout = pango_cairo_create_layout(cr);
    pango_layout_set_markup(layout, text.c_str(), -1);

    std::string font_str = "NotoSansM Nerd Font " + std::to_string(font_size * scale);
    PangoFontDescription* font = pango_font_description_from_string(font_str.c_str());
    pango_layout_set_font_description(layout, font);

    int th = 0;
    pango_layout_get_pixel_size(layout, nullptr, &th);

    cairo_move_to(cr, x, ((bar_h * scale) - th) / 2);
    pango_cairo_show_layout(cr, layout);

    pango_font_description_free(font);
    g_object_unref(layout);
}

static int text_width(cairo_t* cr, const std::string& text, int scale, int font_size) {
    PangoLayout* layout = pango_cairo_create_layout(cr);
    pango_layout_set_markup(layout, text.c_str(), -1);
    std::string font_str = "NotoSansM Nerd Font " + std::to_string(font_size * scale);
    PangoFontDescription* font = pango_font_description_from_string(font_str.c_str());
    pango_layout_set_font_description(layout, font);
    int tw = 0;
    pango_layout_get_pixel_size(layout, &tw, nullptr);
    pango_font_description_free(font);
    g_object_unref(layout);
    return tw;
}

static int choose_buffer_scale(const Bar& b) {
    // The compositor reports an output scale, but we allow an override because
    // tiny bars often look sharper if rendered into a denser buffer.
    if (const char* env = std::getenv("MINIBAR_BUFFER_SCALE")) {
        int s = std::atoi(env);
        if (s > 0) return s;
    }
    return b.output_scale > 1 ? b.output_scale : 2;
}

static void sync_buffer_scale(Bar& b) {
    int s = choose_buffer_scale(b);
    if (s != b.buffer_scale) {
        b.buffer_scale = s;
        if (b.surface) wl_surface_set_buffer_scale(b.surface, b.buffer_scale);
        b.redraw = true;
    }
}

static void create_layer_surface(App& a, Bar& b);

static void set_bar_visible(App& a, Bar& b, bool visible) {
    if (b.closed || !b.layer_surface || !b.surface || visible == b.visible) return;

    b.visible = visible;

    // The exclusive zone tells the compositor how much screen edge space this
    // layer surface wants to reserve. When hidden, we release that space.
    zwlr_layer_surface_v1_set_exclusive_zone(b.layer_surface, visible ? b.height : 0);

    if (visible) {
        // A null input region means the whole surface is interactive again.
        wl_surface_set_input_region(b.surface, nullptr);
        b.configured = false;
        b.redraw = false;
    } else {
        // An empty input region makes the hidden surface stop receiving input,
        // and detaching the buffer removes the already-drawn contents.
        wl_region* empty = wl_compositor_create_region(a.compositor);
        wl_surface_set_input_region(b.surface, empty);
        wl_region_destroy(empty);

        wl_surface_attach(b.surface, nullptr, 0, 0);
        wl_surface_damage_buffer(b.surface, 0, 0, INT32_MAX, INT32_MAX);
        b.configured = false;
        b.redraw = false;
    }

    wl_surface_commit(b.surface);
}

static void request_bar_visible(App& a, Bar& b, bool visible) {
    if (visible) {
        if (!b.visible && !b.show_at_ms) b.show_at_ms = now_ms() + kShowAfterFullscreenDelayMs;
    } else {
        b.show_at_ms = 0;
        set_bar_visible(a, b, false);
    }
}

static void apply_pending_bar_shows(App& a) {
    uint64_t now = now_ms();
    for (auto& bar : a.bars) {
        if (!bar->show_at_ms || now < bar->show_at_ms) continue;
        bar->show_at_ms = 0;
        set_bar_visible(a, *bar, true);
    }
}

static int pending_bar_show_timeout(const App& a) {
    uint64_t now = now_ms();
    uint64_t next = 0;
    for (const auto& bar : a.bars) {
        if (!bar->show_at_ms) continue;
        if (!next || bar->show_at_ms < next) next = bar->show_at_ms;
    }

    if (!next) return -1;
    return next > now ? (int)(next - now) : 0;
}

static std::vector<std::string> split_fields(const std::string& line) {
    std::vector<std::string> fields;
    size_t start = 0;
    while (true) {
        size_t pos = line.find(kSectionSeparator, start);
        if (pos == std::string::npos) {
            fields.push_back(line.substr(start));
            return fields;
        }

        fields.push_back(line.substr(start, pos - start));
        start = pos + 1;
    }
}

static std::string config_value(const std::string& config, const std::string& key) {
    size_t start = 0;
    while (start < config.size()) {
        while (start < config.size() && config[start] == ' ') ++start;

        size_t end = config.find(' ', start);
        if (end == std::string::npos) end = config.size();

        size_t eq = config.find('=', start);
        if (eq != std::string::npos && eq < end && config.compare(start, eq - start, key) == 0) {
            return config.substr(eq + 1, end - eq - 1);
        }

        start = end + 1;
    }

    return "";
}

static void set_bar_text(Bar& b, const std::string& left,
                         const std::string& center, const std::string& right) {
    b.left = left;
    b.center = center;
    b.right = right;
    b.redraw = true;
}

static void update_bar_text(App& a, const std::string& line) {
    std::vector<std::string> fields = split_fields(line);

    if (fields.size() == 3) {
        for (auto& bar : a.bars) set_bar_text(*bar, fields[0], fields[1], fields[2]);
        return;
    }

    if (fields.size() == 4) {
        std::string output = config_value(fields[0], "output");
        if (!output.empty()) {
            std::string visible = config_value(fields[0], "visible");
            for (auto& bar : a.bars) {
                if (bar->output_name != output) continue;
                if (!visible.empty()) request_bar_visible(a, *bar, visible != "0");
                set_bar_text(*bar, fields[1], fields[2], fields[3]);
            }
            return;
        }
    }
}

static void draw(App& a, Bar& b) {
    if (b.closed || !b.configured || b.width <= 0 || !b.visible) return;

    // The layer surface size is in logical coordinates. The backing buffer may
    // be larger when rendering at scale > 1 for HiDPI output.
    int bw = b.width * b.buffer_scale;
    int bh = b.height * b.buffer_scale;
    if (!b.buf.wl || b.buf.w != bw || b.buf.h != bh) {
        if (!make_buffer(a, b, bw, bh)) {
            std::cerr << "failed to create shm buffer\n";
            a.running = false;
            return;
        }
    }

    cairo_surface_t* s = cairo_image_surface_create_for_data(
        static_cast<unsigned char*>(b.buf.data),
        CAIRO_FORMAT_ARGB32, b.buf.w, b.buf.h, b.buf.stride);
    cairo_t* cr = cairo_create(s);

    cairo_set_source_rgb(cr, a.background.r, a.background.g, a.background.b);
    cairo_paint(cr);

    cairo_set_source_rgb(cr, 0.85, 0.85, 0.85);

    const int pad = 8 * b.buffer_scale;
    const int width = b.width * b.buffer_scale;

    int cw = text_width(cr, b.center, b.buffer_scale, a.font_size);
    int rw = text_width(cr, b.right, b.buffer_scale, a.font_size);

    int lx = pad;
    int cx = (width - cw) / 2;
    int rx = width - rw - pad;

    draw_text(cr, b.left, lx, b.height, b.buffer_scale, a.font_size);
    draw_text(cr, b.center, cx, b.height, b.buffer_scale, a.font_size);
    draw_text(cr, b.right, rx, b.height, b.buffer_scale, a.font_size);
    cairo_destroy(cr);
    cairo_surface_destroy(s);

    wl_surface_attach(b.surface, b.buf.wl, 0, 0);
    wl_surface_damage_buffer(b.surface, 0, 0, b.buf.w, b.buf.h);
    wl_surface_commit(b.surface);
    b.redraw = false;
}

static void layer_surface_configure(void* data, zwlr_layer_surface_v1*, uint32_t serial,
                                    uint32_t width, uint32_t height) {
    auto& b = *static_cast<Bar*>(data);
    // Layer-shell surfaces are configured asynchronously. We cannot rely on the
    // requested size until the compositor sends this event and we ack it.
    zwlr_layer_surface_v1_ack_configure(b.layer_surface, serial);
    if (width > 0) b.width = (int)width;
    if (height > 0) b.height = (int)height;
    b.configured = true;
    b.redraw = true;
}

static void layer_surface_closed(void* data, zwlr_layer_surface_v1*) {
    auto& b = *static_cast<Bar*>(data);
    b.closed = true;
    b.configured = false;
    b.redraw = false;
    b.visible = false;
    b.show_at_ms = 0;
}

static const zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = layer_surface_configure,
    .closed = layer_surface_closed,
};

static void output_geometry(void*, wl_output*, int32_t, int32_t, int32_t, int32_t,
                            int32_t, const char*, const char*, int32_t) {}

static void output_mode(void*, wl_output*, uint32_t, int32_t, int32_t, int32_t) {}

static void output_done(void*, wl_output*) {}

static void output_scale(void* data, wl_output*, int32_t factor) {
    auto& b = *static_cast<Bar*>(data);
    b.output_scale = factor > 0 ? factor : 1;
    sync_buffer_scale(b);
}

static void output_name(void* data, wl_output*, const char* name) {
    auto& b = *static_cast<Bar*>(data);
    b.output_name = name ? name : "";
}

static void output_description(void*, wl_output*, const char*) {}

static const wl_output_listener output_listener = {
    output_geometry,
    output_mode,
    output_done,
    output_scale,
    output_name,
    output_description,
};

static void registry_add(void* data, wl_registry* reg, uint32_t name,
                         const char* iface, uint32_t version) {
    auto& a = *static_cast<App*>(data);

    // The Wayland registry is runtime discovery: the compositor tells us which
    // global interfaces exist, and we bind only the ones this program needs.
    if (strcmp(iface, wl_compositor_interface.name) == 0) {
        a.compositor = static_cast<wl_compositor*>(
            wl_registry_bind(reg, name, &wl_compositor_interface, 4));
    } else if (strcmp(iface, wl_shm_interface.name) == 0) {
        a.shm = static_cast<wl_shm*>(
            wl_registry_bind(reg, name, &wl_shm_interface, 1));
    } else if (strcmp(iface, wl_output_interface.name) == 0) {
        auto bar = std::make_unique<Bar>();
        bar->app = &a;
        bar->registry_name = name;
        bar->height = a.bar_height;
        uint32_t output_version = version < 4 ? version : 4;
        bar->output = static_cast<wl_output*>(
            wl_registry_bind(reg, name, &wl_output_interface, output_version));
        wl_output_add_listener(bar->output, &output_listener, bar.get());
        if (a.compositor && a.layer_shell) create_layer_surface(a, *bar);
        a.bars.push_back(std::move(bar));
    } else if (strcmp(iface, zwlr_layer_shell_v1_interface.name) == 0) {
        a.layer_shell = static_cast<zwlr_layer_shell_v1*>(
            wl_registry_bind(reg, name, &zwlr_layer_shell_v1_interface, 1));
    }
}

static void destroy_bar(Bar& b) {
    destroy_buffer(b.buf);
    if (b.layer_surface) zwlr_layer_surface_v1_destroy(b.layer_surface);
    if (b.surface) wl_surface_destroy(b.surface);
    if (b.output) wl_output_destroy(b.output);
    b.layer_surface = nullptr;
    b.surface = nullptr;
    b.output = nullptr;
}

static void registry_remove(void* data, wl_registry*, uint32_t name) {
    auto& a = *static_cast<App*>(data);
    for (auto it = a.bars.begin(); it != a.bars.end(); ++it) {
        if ((*it)->registry_name != name) continue;
        destroy_bar(**it);
        a.bars.erase(it);
        return;
    }
}

static const wl_registry_listener registry_listener = {
    .global = registry_add,
    .global_remove = registry_remove,
};

static void create_layer_surface(App& a, Bar& b) {
    if (b.layer_surface) return;

    b.closed = false;
    b.surface = wl_compositor_create_surface(a.compositor);
    b.layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        a.layer_shell, b.surface, b.output,
        ZWLR_LAYER_SHELL_V1_LAYER_TOP, "minibar");

    sync_buffer_scale(b);
    wl_surface_set_buffer_scale(b.surface, b.buffer_scale);

    zwlr_layer_surface_v1_add_listener(b.layer_surface, &layer_surface_listener, &b);
    uint32_t edge_anchor = a.position_top
        ? ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP
        : ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
    zwlr_layer_surface_v1_set_anchor(
        b.layer_surface,
        edge_anchor |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    zwlr_layer_surface_v1_set_size(b.layer_surface, 0, b.height);
    zwlr_layer_surface_v1_set_exclusive_zone(b.layer_surface, b.height);
    zwlr_layer_surface_v1_set_margin(b.layer_surface, 0, 0, 0, 0);
    wl_surface_commit(b.surface);
}

int main(int argc, char** argv) {
    App a{};

    ArgsResult args = parse_args(a, argc, argv);
    if (args == ArgsResult::ExitSuccess) return 0;
    if (args == ArgsResult::Error) return 2;

    // Connect to the compositor, discover globals, then create one layer-shell
    // surface anchored to the top edge of each output.
    a.display = wl_display_connect(nullptr);
    if (!a.display) {
        std::cerr << "failed to connect to wayland\n";
        return 1;
    }

    a.registry = wl_display_get_registry(a.display);
    wl_registry_add_listener(a.registry, &registry_listener, &a);
    wl_display_roundtrip(a.display);

    if (!a.compositor || !a.shm || !a.layer_shell) {
        std::cerr << "missing required wayland globals\n";
        return 1;
    }

    if (a.bars.empty()) {
        auto bar = std::make_unique<Bar>();
        bar->app = &a;
        bar->height = a.bar_height;
        a.bars.push_back(std::move(bar));
    }

    for (auto& bar : a.bars) {
        if (!bar->layer_surface) create_layer_surface(a, *bar);
    }
    wl_display_roundtrip(a.display);

    int wl_fd = wl_display_get_fd(a.display);
    int stdin_fd = fileno(stdin);

    while (a.running) {
        apply_pending_bar_shows(a);

        for (auto& bar : a.bars) {
            if (bar->redraw) draw(a, *bar);
        }

        wl_display_flush(a.display);

        // One poll loop drives everything:
        // - Wayland socket: compositor events such as configure/scale
        // - stdin: external text updates for the bar contents
        pollfd fds[2] = {
            { .fd = wl_fd, .events = POLLIN, .revents = 0 },
            { .fd = stdin_fd, .events = POLLIN, .revents = 0 },
        };

        if (poll(fds, 2, pending_bar_show_timeout(a)) < 0) break;

        if (fds[0].revents & POLLIN) {
            if (wl_display_dispatch(a.display) < 0) break;
        } else {
            wl_display_dispatch_pending(a.display);
        }

        if (fds[1].revents & POLLIN) {
            std::string line;
            if (!std::getline(std::cin, line)) {
                a.running = false;
            } else {
                update_bar_text(a, line);
            }
        }
    }

    // Tear down in reverse order of ownership so Wayland objects do not outlive
    // the connection they were created from.
    for (auto& bar : a.bars) {
        destroy_bar(*bar);
    }
    if (a.layer_shell) zwlr_layer_shell_v1_destroy(a.layer_shell);
    if (a.shm) wl_shm_destroy(a.shm);
    if (a.compositor) wl_compositor_destroy(a.compositor);
    if (a.registry) wl_registry_destroy(a.registry);
    if (a.display) wl_display_disconnect(a.display);
    return 0;
}
