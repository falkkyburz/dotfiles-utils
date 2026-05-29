#include <wayland-client.h>

#define namespace namespace_
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#undef namespace

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <limits>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <linux/input-event-codes.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

volatile std::sig_atomic_t running = 1;

struct Options {
    bool include_cursor = false;
    bool interactive = false;
    bool help = false;
    std::string output;
};

struct Image {
    int width = 0;
    int height = 0;
    std::vector<unsigned char> rgb;
};

struct TempFile {
    explicit TempFile(std::string path) : path(std::move(path)) {}
    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;
    ~TempFile() {
        if (!path.empty()) {
            std::remove(path.c_str());
        }
    }

    std::string path;
};

struct Output {
    wl_output* output = nullptr;
    uint32_t registry_name = 0;
    std::string name;
    int32_t scale = 1;
};

struct Buffer {
    wl_buffer* buffer = nullptr;
    void* data = nullptr;
    std::size_t size = 0;
    int fd = -1;
    int width = 0;
    int height = 0;
    int stride = 0;
    bool busy = false;
};

struct Surface {
    wl_surface* surface = nullptr;
    zwlr_layer_surface_v1* layer_surface = nullptr;
    Output* output = nullptr;
    Image image;
    Buffer buffer;
    int configured_width = 0;
    int configured_height = 0;
    bool configured = false;
};

struct WaylandState {
    wl_display* display = nullptr;
    wl_registry* registry = nullptr;
    wl_compositor* compositor = nullptr;
    wl_shm* shm = nullptr;
    wl_seat* seat = nullptr;
    wl_keyboard* keyboard = nullptr;
    zwlr_layer_shell_v1* layer_shell = nullptr;
    std::deque<Output> outputs;
    bool interactive = false;
};

[[noreturn]] void usage(int exit_code) {
    std::ostream& out = exit_code == 0 ? std::cout : std::cerr;
    out << "Usage: screenfreeze [--cursor] [--interactive] [--output NAME]\n\n"
        << "Captures the current Wayland output(s) using grim, then shows the\n"
        << "capture as a fullscreen layer-shell overlay. By default the overlay\n"
        << "does not take input, so tools like slurp can run on top of it.\n\n"
        << "Options:\n"
        << "  --cursor       Include the pointer in the frozen screenshot.\n"
        << "  --interactive  Take keyboard focus and exit on Escape or q.\n"
        << "  --output NAME  Freeze only one output, for example DP-1.\n"
        << "  -h, --help     Show this help text.\n";
    std::exit(exit_code);
}

Options parse_args(int argc, char** argv) {
    Options options;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--cursor") {
            options.include_cursor = true;
        } else if (arg == "--interactive") {
            options.interactive = true;
        } else if (arg == "--output") {
            if (i + 1 >= argc) {
                std::cerr << "screenfreeze: --output requires an output name\n";
                usage(2);
            }
            options.output = argv[++i];
        } else if (arg == "-h" || arg == "--help") {
            options.help = true;
        } else {
            std::cerr << "screenfreeze: unknown option: " << arg << '\n';
            usage(2);
        }
    }

    return options;
}

void handle_signal(int) {
    running = 0;
}

void install_signal_handlers() {
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);
}

TempFile make_temp_file() {
    std::vector<char> path_template{'/', 't', 'm', 'p', '/', 's', 'c', 'r', 'e', 'e', 'n', 'f', 'r', 'e', 'e', 'z', 'e', '-', 'X', 'X', 'X', 'X', 'X', 'X', '.', 'p', 'p', 'm', '\0'};
    const int fd = mkstemps(path_template.data(), 4);
    if (fd == -1) {
        throw std::runtime_error(std::string("failed to create temporary file: ") + std::strerror(errno));
    }

    close(fd);
    return TempFile(path_template.data());
}

int run_process(const std::vector<std::string>& args) {
    const pid_t pid = fork();
    if (pid == -1) {
        throw std::runtime_error(std::string("failed to fork: ") + std::strerror(errno));
    }

    if (pid == 0) {
        std::vector<char*> argv;
        argv.reserve(args.size() + 1);
        for (const std::string& arg : args) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);

        execvp(argv[0], argv.data());
        std::fprintf(stderr, "screenfreeze: failed to run %s: %s\n", argv[0], std::strerror(errno));
        _exit(errno == ENOENT ? 127 : 126);
    }

    int status = 0;
    while (waitpid(pid, &status, 0) == -1) {
        if (errno != EINTR) {
            throw std::runtime_error(std::string("failed to wait for child process: ") + std::strerror(errno));
        }
    }

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }

    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }

    return 1;
}

void capture_screen(const Options& options, const std::string& output_name, const std::string& path) {
    std::vector<std::string> args{"grim", "-t", "ppm"};
    if (options.include_cursor) {
        args.push_back("-c");
    }
    if (!output_name.empty()) {
        args.push_back("-o");
        args.push_back(output_name);
    }
    args.push_back(path);

    const int exit_code = run_process(args);
    if (exit_code != 0) {
        throw std::runtime_error("grim failed with exit code " + std::to_string(exit_code) +
                                 ". Make sure grim is installed and your compositor allows screenshot capture.");
    }
}

std::string read_ppm_token(std::istream& input) {
    std::string token;
    char ch = '\0';

    while (input.get(ch)) {
        const unsigned char byte = static_cast<unsigned char>(ch);
        if (std::isspace(byte)) {
            continue;
        }
        if (ch == '#') {
            input.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            continue;
        }
        token.push_back(ch);
        break;
    }

    while (input.get(ch)) {
        const unsigned char byte = static_cast<unsigned char>(ch);
        if (std::isspace(byte)) {
            break;
        }
        token.push_back(ch);
    }

    if (token.empty()) {
        throw std::runtime_error("invalid PPM file: unexpected end of header");
    }

    return token;
}

int parse_positive_int(const std::string& value, const char* name) {
    std::size_t parsed = 0;
    const long number = std::stol(value, &parsed, 10);
    if (parsed != value.size() || number <= 0 || number > std::numeric_limits<int>::max()) {
        throw std::runtime_error(std::string("invalid PPM file: bad ") + name);
    }
    return static_cast<int>(number);
}

Image load_ppm(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("failed to open captured image: " + path);
    }

    if (read_ppm_token(input) != "P6") {
        throw std::runtime_error("invalid PPM file: expected P6 format");
    }

    Image image;
    image.width = parse_positive_int(read_ppm_token(input), "width");
    image.height = parse_positive_int(read_ppm_token(input), "height");
    const int max_value = parse_positive_int(read_ppm_token(input), "max value");
    if (max_value != 255) {
        throw std::runtime_error("invalid PPM file: only max value 255 is supported");
    }

    const auto pixel_count = static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height);
    if (pixel_count > std::numeric_limits<std::size_t>::max() / 3) {
        throw std::runtime_error("captured image is too large");
    }

    image.rgb.resize(pixel_count * 3);
    input.read(reinterpret_cast<char*>(image.rgb.data()), static_cast<std::streamsize>(image.rgb.size()));
    if (input.gcount() != static_cast<std::streamsize>(image.rgb.size())) {
        throw std::runtime_error("captured image is incomplete");
    }

    return image;
}

int create_anonymous_file(std::size_t size) {
    int fd = -1;
#ifdef MFD_CLOEXEC
    fd = memfd_create("screenfreeze-buffer", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (fd >= 0) {
        if (ftruncate(fd, static_cast<off_t>(size)) == -1) {
            close(fd);
            throw std::runtime_error(std::string("failed to size shared memory buffer: ") + std::strerror(errno));
        }
        return fd;
    }
    if (errno != ENOSYS) {
        throw std::runtime_error(std::string("failed to create shared memory buffer: ") + std::strerror(errno));
    }
#endif

    std::vector<char> path_template{'/', 't', 'm', 'p', '/', 's', 'c', 'r', 'e', 'e', 'n', 'f', 'r', 'e', 'e', 'z', 'e', '-', 's', 'h', 'm', '-', 'X', 'X', 'X', 'X', 'X', 'X', '\0'};
    fd = mkstemp(path_template.data());
    if (fd == -1) {
        throw std::runtime_error(std::string("failed to create shared memory file: ") + std::strerror(errno));
    }
    unlink(path_template.data());

    if (ftruncate(fd, static_cast<off_t>(size)) == -1) {
        close(fd);
        throw std::runtime_error(std::string("failed to size shared memory file: ") + std::strerror(errno));
    }
    return fd;
}

void buffer_release(void* data, wl_buffer*) {
    static_cast<Buffer*>(data)->busy = false;
}

const wl_buffer_listener buffer_listener = {
    buffer_release,
};

void destroy_buffer(Buffer& buffer) {
    if (buffer.buffer) {
        wl_buffer_destroy(buffer.buffer);
        buffer.buffer = nullptr;
    }
    if (buffer.data && buffer.data != MAP_FAILED) {
        munmap(buffer.data, buffer.size);
        buffer.data = nullptr;
    }
    if (buffer.fd >= 0) {
        close(buffer.fd);
        buffer.fd = -1;
    }
}

void draw_image(Buffer& buffer, const Image& image) {
    auto* pixels = static_cast<unsigned char*>(buffer.data);

    for (int y = 0; y < buffer.height; ++y) {
        const int src_y = static_cast<int>((static_cast<long long>(y) * image.height) / buffer.height);
        unsigned char* row = pixels + static_cast<std::size_t>(y) * buffer.stride;
        for (int x = 0; x < buffer.width; ++x) {
            const int src_x = static_cast<int>((static_cast<long long>(x) * image.width) / buffer.width);
            const std::size_t src = (static_cast<std::size_t>(src_y) * image.width + src_x) * 3;
            const std::size_t dst = static_cast<std::size_t>(x) * 4;
            row[dst + 0] = image.rgb[src + 2];
            row[dst + 1] = image.rgb[src + 1];
            row[dst + 2] = image.rgb[src + 0];
            row[dst + 3] = 0xff;
        }
    }
}

void create_buffer(WaylandState& state, Surface& surface) {
    destroy_buffer(surface.buffer);

    const int scale = std::max(1, surface.output ? surface.output->scale : 1);
    const int width = std::max(1, surface.configured_width) * scale;
    const int height = std::max(1, surface.configured_height) * scale;
    const int stride = width * 4;
    const std::size_t size = static_cast<std::size_t>(stride) * height;

    Buffer buffer;
    buffer.fd = create_anonymous_file(size);
    buffer.data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, buffer.fd, 0);
    if (buffer.data == MAP_FAILED) {
        close(buffer.fd);
        throw std::runtime_error(std::string("failed to map shared memory buffer: ") + std::strerror(errno));
    }

    wl_shm_pool* pool = wl_shm_create_pool(state.shm, buffer.fd, static_cast<int32_t>(size));
    buffer.buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);

    buffer.size = size;
    buffer.width = width;
    buffer.height = height;
    buffer.stride = stride;
    wl_buffer_add_listener(buffer.buffer, &buffer_listener, &surface.buffer);

    surface.buffer = buffer;
    draw_image(surface.buffer, surface.image);
}

void commit_surface(WaylandState& state, Surface& surface) {
    create_buffer(state, surface);
    surface.buffer.busy = true;

    const int scale = std::max(1, surface.output ? surface.output->scale : 1);
    wl_surface_set_buffer_scale(surface.surface, scale);
    wl_surface_attach(surface.surface, surface.buffer.buffer, 0, 0);
    wl_surface_damage_buffer(surface.surface, 0, 0, surface.buffer.width, surface.buffer.height);
    wl_surface_commit(surface.surface);
}

void layer_surface_configure(void* data, zwlr_layer_surface_v1* layer_surface, uint32_t serial, uint32_t width, uint32_t height) {
    auto* surface = static_cast<Surface*>(data);
    zwlr_layer_surface_v1_ack_configure(layer_surface, serial);

    surface->configured_width = static_cast<int>(width ? width : surface->image.width);
    surface->configured_height = static_cast<int>(height ? height : surface->image.height);
    surface->configured = true;
}

void layer_surface_closed(void*, zwlr_layer_surface_v1*) {
    running = 0;
}

const zwlr_layer_surface_v1_listener layer_surface_listener = {
    layer_surface_configure,
    layer_surface_closed,
};

void output_geometry(void*, wl_output*, int32_t, int32_t, int32_t, int32_t, int32_t, const char*, const char*, int32_t) {}
void output_mode(void*, wl_output*, uint32_t, int32_t, int32_t, int32_t) {}
void output_done(void*, wl_output*) {}

void output_scale(void* data, wl_output*, int32_t factor) {
    static_cast<Output*>(data)->scale = std::max(1, factor);
}

void output_name(void* data, wl_output*, const char* name) {
    static_cast<Output*>(data)->name = name ? name : "";
}

void output_description(void*, wl_output*, const char*) {}

const wl_output_listener output_listener = {
    output_geometry,
    output_mode,
    output_done,
    output_scale,
    output_name,
    output_description,
};

void keyboard_keymap(void*, wl_keyboard*, uint32_t, int32_t fd, uint32_t) {
    if (fd >= 0) {
        close(fd);
    }
}

void keyboard_enter(void*, wl_keyboard*, uint32_t, wl_surface*, wl_array*) {}
void keyboard_leave(void*, wl_keyboard*, uint32_t, wl_surface*) {}

void keyboard_key(void*, wl_keyboard*, uint32_t, uint32_t, uint32_t key, uint32_t state) {
    if (state == WL_KEYBOARD_KEY_STATE_PRESSED && (key == KEY_ESC || key == KEY_Q)) {
        running = 0;
    }
}

void keyboard_modifiers(void*, wl_keyboard*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) {}
void keyboard_repeat_info(void*, wl_keyboard*, int32_t, int32_t) {}

const wl_keyboard_listener keyboard_listener = {
    keyboard_keymap,
    keyboard_enter,
    keyboard_leave,
    keyboard_key,
    keyboard_modifiers,
    keyboard_repeat_info,
};

void seat_capabilities(void* data, wl_seat* seat, uint32_t capabilities) {
    auto* state = static_cast<WaylandState*>(data);
    const bool has_keyboard = capabilities & WL_SEAT_CAPABILITY_KEYBOARD;

    if (state->interactive && has_keyboard && !state->keyboard) {
        state->keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(state->keyboard, &keyboard_listener, state);
    } else if ((!state->interactive || !has_keyboard) && state->keyboard) {
        wl_keyboard_destroy(state->keyboard);
        state->keyboard = nullptr;
    }
}

void seat_name(void*, wl_seat*, const char*) {}

const wl_seat_listener seat_listener = {
    seat_capabilities,
    seat_name,
};

void registry_global(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version) {
    auto* state = static_cast<WaylandState*>(data);
    const std::string iface = interface;

    if (iface == wl_compositor_interface.name) {
        state->compositor = static_cast<wl_compositor*>(wl_registry_bind(registry, name, &wl_compositor_interface, std::min(version, 4u)));
    } else if (iface == wl_shm_interface.name) {
        state->shm = static_cast<wl_shm*>(wl_registry_bind(registry, name, &wl_shm_interface, 1));
    } else if (iface == zwlr_layer_shell_v1_interface.name) {
        state->layer_shell = static_cast<zwlr_layer_shell_v1*>(wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, std::min(version, 4u)));
    } else if (iface == wl_seat_interface.name) {
        state->seat = static_cast<wl_seat*>(wl_registry_bind(registry, name, &wl_seat_interface, std::min(version, 7u)));
        wl_seat_add_listener(state->seat, &seat_listener, state);
    } else if (iface == wl_output_interface.name) {
        state->outputs.emplace_back();
        Output& output = state->outputs.back();
        output.registry_name = name;
        output.output = static_cast<wl_output*>(wl_registry_bind(registry, name, &wl_output_interface, std::min(version, 4u)));
        wl_output_add_listener(output.output, &output_listener, &output);
    }
}

void registry_global_remove(void* data, wl_registry*, uint32_t name) {
    auto* state = static_cast<WaylandState*>(data);
    for (Output& output : state->outputs) {
        if (output.registry_name == name) {
            output.output = nullptr;
            output.name.clear();
        }
    }
}

const wl_registry_listener registry_listener = {
    registry_global,
    registry_global_remove,
};

class WaylandConnection {
public:
    explicit WaylandConnection(bool interactive) {
        state_.interactive = interactive;
        state_.display = wl_display_connect(nullptr);
        if (!state_.display) {
            throw std::runtime_error("failed to connect to the Wayland display");
        }

        state_.registry = wl_display_get_registry(state_.display);
        wl_registry_add_listener(state_.registry, &registry_listener, &state_);
        wl_display_roundtrip(state_.display);
        wl_display_roundtrip(state_.display);

        if (!state_.compositor) {
            throw std::runtime_error("Wayland compositor global is missing");
        }
        if (!state_.shm) {
            throw std::runtime_error("Wayland wl_shm global is missing");
        }
        if (!state_.layer_shell) {
            throw std::runtime_error("wlr-layer-shell is not available; this app requires a wlroots-compatible compositor");
        }
    }

    WaylandConnection(const WaylandConnection&) = delete;
    WaylandConnection& operator=(const WaylandConnection&) = delete;

    ~WaylandConnection() {
        if (state_.keyboard) {
            wl_keyboard_destroy(state_.keyboard);
        }
        if (state_.seat) {
            wl_seat_destroy(state_.seat);
        }
        for (Output& output : state_.outputs) {
            if (output.output) {
                wl_output_destroy(output.output);
            }
        }
        if (state_.layer_shell) {
            zwlr_layer_shell_v1_destroy(state_.layer_shell);
        }
        if (state_.shm) {
            wl_shm_destroy(state_.shm);
        }
        if (state_.compositor) {
            wl_compositor_destroy(state_.compositor);
        }
        if (state_.registry) {
            wl_registry_destroy(state_.registry);
        }
        if (state_.display) {
            wl_display_disconnect(state_.display);
        }
    }

    WaylandState& state() { return state_; }

private:
    WaylandState state_;
};

std::vector<Output*> selected_outputs(WaylandState& state, const std::string& output_name) {
    std::vector<Output*> selected;
    for (Output& output : state.outputs) {
        if (!output.output || output.name.empty()) {
            continue;
        }
        if (output_name.empty() || output.name == output_name) {
            selected.push_back(&output);
        }
    }

    if (!output_name.empty() && selected.empty()) {
        throw std::runtime_error("output not found: " + output_name);
    }

    return selected;
}

Image capture_output(const Options& options, const std::string& output_name) {
    TempFile screenshot = make_temp_file();
    capture_screen(options, output_name, screenshot.path);
    return load_ppm(screenshot.path);
}

std::vector<Surface> create_surfaces(WaylandState& state, const Options& options) {
    std::vector<Surface> surfaces;
    std::vector<Output*> outputs = selected_outputs(state, options.output);

    if (outputs.empty()) {
        Surface surface;
        surface.image = capture_output(options, "");
        surface.surface = wl_compositor_create_surface(state.compositor);
        surface.layer_surface = zwlr_layer_shell_v1_get_layer_surface(state.layer_shell,
                                                                      surface.surface,
                                                                      nullptr,
                                                                      ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
                                                                      "screenfreeze");
        surfaces.push_back(std::move(surface));
    } else {
        surfaces.reserve(outputs.size());
        for (Output* output : outputs) {
            Surface surface;
            surface.output = output;
            surface.image = capture_output(options, output->name);
            surface.surface = wl_compositor_create_surface(state.compositor);
            surface.layer_surface = zwlr_layer_shell_v1_get_layer_surface(state.layer_shell,
                                                                          surface.surface,
                                                                          output->output,
                                                                          ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
                                                                          "screenfreeze");
            surfaces.push_back(std::move(surface));
        }
    }

    for (Surface& surface : surfaces) {
        zwlr_layer_surface_v1_add_listener(surface.layer_surface, &layer_surface_listener, &surface);
        zwlr_layer_surface_v1_set_anchor(surface.layer_surface,
                                         ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
                                             ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT |
                                             ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                                             ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT);
        zwlr_layer_surface_v1_set_exclusive_zone(surface.layer_surface, -1);
        zwlr_layer_surface_v1_set_keyboard_interactivity(
            surface.layer_surface,
            options.interactive ? ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE
                                : ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);

        if (!options.interactive) {
            wl_region* region = wl_compositor_create_region(state.compositor);
            wl_surface_set_input_region(surface.surface, region);
            wl_region_destroy(region);
        }

        wl_surface_commit(surface.surface);
    }

    while (running) {
        wl_display_roundtrip(state.display);
        bool all_configured = true;
        for (const Surface& surface : surfaces) {
            all_configured = all_configured && surface.configured;
        }
        if (all_configured) {
            break;
        }
    }

    for (Surface& surface : surfaces) {
        commit_surface(state, surface);
    }

    wl_display_flush(state.display);
    return surfaces;
}

void destroy_surfaces(std::vector<Surface>& surfaces) {
    for (Surface& surface : surfaces) {
        destroy_buffer(surface.buffer);
        if (surface.layer_surface) {
            zwlr_layer_surface_v1_destroy(surface.layer_surface);
            surface.layer_surface = nullptr;
        }
        if (surface.surface) {
            wl_surface_destroy(surface.surface);
            surface.surface = nullptr;
        }
    }
}

void event_loop(WaylandState& state) {
    while (running) {
        while (wl_display_prepare_read(state.display) != 0) {
            if (wl_display_dispatch_pending(state.display) == -1) {
                running = 0;
                return;
            }
        }

        if (wl_display_flush(state.display) == -1 && errno != EAGAIN) {
            wl_display_cancel_read(state.display);
            throw std::runtime_error(std::string("failed to flush Wayland display: ") + std::strerror(errno));
        }

        pollfd fd = {wl_display_get_fd(state.display), POLLIN, 0};
        const int poll_result = poll(&fd, 1, 250);
        if (poll_result == -1) {
            wl_display_cancel_read(state.display);
            if (errno == EINTR) {
                continue;
            }
            throw std::runtime_error(std::string("poll failed: ") + std::strerror(errno));
        }

        if (poll_result == 0) {
            wl_display_cancel_read(state.display);
            continue;
        }

        if (wl_display_read_events(state.display) == -1) {
            running = 0;
            return;
        }
        if (wl_display_dispatch_pending(state.display) == -1) {
            running = 0;
            return;
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        install_signal_handlers();

        const Options options = parse_args(argc, argv);
        if (options.help) {
            usage(0);
        }

        WaylandConnection connection(options.interactive);
        std::vector<Surface> surfaces = create_surfaces(connection.state(), options);
        event_loop(connection.state());
        destroy_surfaces(surfaces);
    } catch (const std::exception& error) {
        std::cerr << "screenfreeze: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
