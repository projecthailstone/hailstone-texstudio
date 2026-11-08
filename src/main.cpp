// Hailstone Texture Studio (README.md): builds HD texture packs of the player's own game files on their GPU, in a
// window drawn in the modern UI's look (ui/modern/tools/texstudio.lua over the modern pack's lib/). A tool add-on: it
// is installed in the Hailstone folder's tools/texstudio/ and works on that folder (the data folder): the packs go
// to textures/<pack>/, the settings to texstudio/, the log to logs/texstudio.log. The game's folder is only read.
//
//   texstudio [--data-dir DIR]          the Hailstone folder to work on (the client passes it when it starts the
//                                       studio; without it, the folder two up from tools/texstudio/ when that is a
//                                       Hailstone folder, else the studio's own)
//   texstudio [--frames N [--screenshot FILE]]   quit after N frames (a smoke test), the last one saved
//   texstudio --build [--pack NAME] [--preset fast|standard|high] [--zones KEY,...] [--device N|cpu]
//             [--assets DIR] [--low-impact]
//       builds without a window, with the window's settings unless given (and saves none of them); --zones takes
//       zone short names and "shared"
//   texstudio --devices                 lists the devices --device numbers
//   texstudio --version

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <thread>
#include <vector>
#include <algorithm>

#include <imgui.h>

#include "app/window_icon.h"
#include "core/build_info.h"
#include "core/game_dir.h"
#include "core/log.h"
#include "platform/crash_log.h"
#include "platform/folder_dialog.h"
#include "platform/message_box.h"
#include "platform/paths.h"
#include "platform/process.h"
#include "platform/window.h"
#include "render/vk_context.h"
#include "studio.h"
#include "studio_lua.h"
#include "ncnn_model.h"
#include "ui/imgui_layer.h"
#include "ui/lua/lua_host.h"
#include "ui/screen_pictures.h"
#include "ui/ui_textures.h"

namespace fs = std::filesystem;
using namespace hailstone;

namespace {

constexpr const char* kTitle = "Hailstone Texture Studio";
constexpr const char* kVersion = TEXSTUDIO_VERSION;
constexpr const char* kPack = "modern";
constexpr const char* kScript = "tools/texstudio.lua";

// The studio's script settings start from the modern pack's palette and font, so it looks like the player's client.
void follow_client_look(const fs::path& client_file, const fs::path& own_file) {
    auto read = [](const fs::path& f) {
        std::map<std::string, std::string> out;
        std::ifstream in(f);
        for (std::string line; std::getline(in, line);) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (auto eq = line.find('='); eq != std::string::npos) out[line.substr(0, eq)] = line.substr(eq + 1);
        }
        return out;
    };
    const auto client = read(client_file);
    auto own = read(own_file);
    bool changed = false;
    for (const char* key : {"palette", "font_face"}) {
        auto it = client.find(key);
        if (it != client.end() && own[key] != it->second) {
            own[key] = it->second;
            changed = true;
        }
    }
    if (!changed) return;
    std::error_code ec;
    fs::create_directories(own_file.parent_path(), ec);
    std::ofstream out(own_file, std::ios::trunc);
    for (const auto& [k, v] : own) out << k << '=' << v << '\n';
}

int run(int frames, const fs::path& screenshot) {
    platform::create_data_dirs();
    platform::Window window({.title = kTitle, .width = 1440, .height = 900});
    app::apply_window_icon(window);
    render::VkContext vk(window, false);
    render::SceneOptions scene;
    scene.msaa = 1;
    scene.shadow_size = 0;
    scene.light_shadows = 0;
    scene.ambient_occlusion = false;
    scene.distance_blur = false;
    scene.bloom = 0.0f;
    scene.color_grade = false;
    vk.set_scene_options(scene, platform::shaders_dir());
    ui::ImGuiLayer imgui(window, vk, {});
    const GameDir no_game; // (the studio's pictures are files of its own, never the game's UI art)
    ui::UiTextures textures(vk, no_game);

    // The client's pre-game pictures behind the window.
    std::optional<fs::path> backdrop;
    if (auto pictures = ui::screen_pictures(platform::data_dir() / "ui", {}, platform::pictures_dir()); !pictures.empty())
        backdrop = pictures[static_cast<std::size_t>(std::chrono::steady_clock::now().time_since_epoch().count()) %
                            pictures.size()];

    {
        texstudio::Studio studio(platform::data_dir());
        platform::FolderDialog dialog;
        texstudio::StudioLuaContext context{&studio, &textures, &dialog, &window, kVersion};

        const fs::path pack = platform::ui_packs_dir() / kPack;
        if (!fs::is_regular_file(pack / kScript)) throw std::runtime_error((pack / kScript).string() + " is missing");
        const fs::path settings = platform::ui_settings_file("texstudio");
        follow_client_look(platform::ui_settings_file(kPack), settings);
        ui::lua::LuaUi lua(pack, settings);
        lua.set_entry_scripts({kScript});
        lua.add_eq_api([&](lua_State* L) { texstudio::register_studio_api(L, &context); });

        for (int frame = 0; window.poll_events(); ++frame) {
            if (frames && frame >= frames) break;
            if (auto chosen = dialog.poll()) studio.set_assets_path(chosen->string());
            studio.update();
            imgui.new_frame();
            if (backdrop)
                if (const ui::UiTextures::Texture* t = textures.picture(*backdrop))
                    ui::draw_screen_picture(ImGui::GetBackgroundDrawList(), {t->id, t->width, t->height});
            lua.frame();
            auto f = vk.begin_frame_commands();
            if (!f) {
                imgui.end_frame_without_render();
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
                continue;
            }
            if (!screenshot.empty() && frame == frames - 1) vk.request_screenshot(screenshot);
            vk.begin_scene(*f, glm::vec4(0.02f, 0.025f, 0.03f, 1.0f));
            imgui.render(*f);
            vk.end_frame(*f);
        }
        vk.wait_idle();
        log::info("texture studio: closing");
    }
    texpack::shutdown_devices();
    vk.wait_idle();
    return 0;
}

struct BuildArgs {
    std::string pack, preset, assets, device;
    std::vector<std::string> zones;
    bool low_impact = false;
};

std::vector<std::string> split_list(const std::string& s) {
    std::vector<std::string> out;
    std::size_t p = 0;
    while (p <= s.size()) {
        std::size_t q = std::min(s.find(',', p), s.size());
        if (q > p) out.push_back(s.substr(p, q - p));
        p = q + 1;
    }
    return out;
}

std::string clock_text(double secs) {
    const long long t = static_cast<long long>(secs + 0.5);
    char buf[32];
    std::snprintf(buf, sizeof buf, "%lld:%02lld:%02lld", t / 3600, t / 60 % 60, t % 60);
    return buf;
}

int list_devices_cli() {
    platform::create_data_dirs();
    const auto devices = texpack::list_devices();
    for (std::size_t i = 0; i < devices.size(); ++i)
        std::printf("%zu  %s (%s, %u MB)\n", i + 1, devices[i].name.c_str(), devices[i].type.c_str(), devices[i].memory_mb);
    texpack::shutdown_devices();
    return 0;
}

int run_headless(const BuildArgs& a) {
    using namespace std::chrono_literals;
    platform::create_data_dirs();
    int code = 0;
    {
        texstudio::Studio studio(platform::data_dir());
        studio.set_persist(false);
        if (!a.assets.empty()) studio.set_assets_path(a.assets);
        if (!a.preset.empty()) studio.set_preset(a.preset);
        if (!a.pack.empty()) {
            if (!texstudio::valid_pack_name(a.pack)) {
                std::fprintf(stderr, "texstudio: '%s' cannot be a pack's name\n", a.pack.c_str());
                return 2;
            }
            studio.set_pack_name(a.pack);
        }
        if (!a.device.empty()) {
            int n = a.device == "cpu" ? static_cast<int>(studio.devices().size()) : std::atoi(a.device.c_str());
            if (n < 1 || static_cast<std::size_t>(n) > studio.devices().size()) {
                std::fprintf(stderr, "texstudio: no device %s (--devices lists them)\n", a.device.c_str());
                return 2;
            }
            studio.set_device(n - 1);
        }
        if (!a.zones.empty()) {
            studio.set_build_all(false);
            studio.select_no_groups();
            for (const std::string& z : a.zones) studio.set_group_selected(z, true);
        }
        studio.set_low_impact(a.low_impact);
        while (studio.scan_state() == texstudio::Studio::ScanState::Scanning ||
               studio.worker_state() == texstudio::Studio::WorkerState::Loading) {
            studio.update();
            std::this_thread::sleep_for(50ms);
        }
        studio.update();
        if (studio.scan_state() != texstudio::Studio::ScanState::Ready) {
            std::fprintf(stderr, "texstudio: %s\n", studio.assets_problem().c_str());
            return 1;
        }
        std::printf("%s\n", studio.scan_message().c_str());
        for (const std::string& z : a.zones)
            if (std::none_of(studio.groups().begin(), studio.groups().end(), [&](const texstudio::Group& g) { return g.key == z; }))
                std::fprintf(stderr, "texstudio: no archives of '%s' (zone short names, or \"shared\")\n", z.c_str());
        const auto& dev = studio.devices()[static_cast<std::size_t>(studio.device())];
        std::printf("pack %s, %ux%s, on %s\n", studio.pack_dir().string().c_str(), studio.scale(),
                    studio.maps() ? " with maps" : "", dev.name.c_str());
        std::string why;
        for (int i = 0; i < 20; ++i) { // (the estimate is worked out on the next updates)
            studio.update();
            std::this_thread::sleep_for(30ms);
        }
        if (!studio.start(&why)) {
            std::fprintf(stderr, "texstudio: %s\n", why.c_str());
            return 1;
        }
        auto last = std::chrono::steady_clock::now() - 1h;
        for (;;) {
            studio.update();
            auto st = studio.job_status();
            if (!st) break;
            const bool done = st->state == texpack::JobStatus::State::Done ||
                              st->state == texpack::JobStatus::State::Cancelled ||
                              st->state == texpack::JobStatus::State::Failed;
            if (done || (st->total > 0 && std::chrono::steady_clock::now() - last > 5s)) {
                last = std::chrono::steady_clock::now();
                std::printf("%zu/%zu textures (%zu already made), %zu failed, %s elapsed, %s left\n", st->done, st->total,
                            st->complete, st->failures, clock_text(st->seconds).c_str(),
                            st->eta() > 0 ? clock_text(st->eta()).c_str() : "?");
                std::fflush(stdout);
            }
            if (done) {
                for (const std::string& e : st->errors) std::fprintf(stderr, "  %s\n", e.c_str());
                if (st->state == texpack::JobStatus::State::Failed) std::fprintf(stderr, "texstudio: %s\n", st->fatal.c_str());
                code = st->state == texpack::JobStatus::State::Done && st->failures == 0 ? 0 : 1;
                std::printf("%s: %llu files, %.1f MB\n", std::string(texpack::state_name(st->state)).c_str(),
                            static_cast<unsigned long long>(st->files_written), static_cast<double>(st->bytes_written) / 1e6);
                break;
            }
            std::this_thread::sleep_for(100ms);
        }
    }
    texpack::shutdown_devices();
    return code;
}

// The Hailstone folder this copy is installed in: <data>/tools/<name>/ (the add-on manager's place for a tool), when
// the folder two up holds a Hailstone's settings or add-ons.
std::optional<fs::path> hailstone_folder_around(fs::path install) {
    install = install.lexically_normal();
    if (!install.has_filename()) install = install.parent_path(); // (SDL's base path ends in a separator)
    const fs::path tools = install.parent_path();
    if (tools.filename() != "tools") return std::nullopt;
    const fs::path data = tools.parent_path();
    std::error_code ec;
    if (fs::is_regular_file(data / "hailstone.ini", ec) || fs::is_directory(data / "addons", ec)) return data;
    return std::nullopt;
}

} // namespace

int main(int argc, char** argv) {
    int frames = 0;
    fs::path screenshot;
    bool build = false, devices = false;
    BuildArgs b;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--build") build = true;
        else if (a == "--devices") devices = true;
        else if (a == "--pack") b.pack = next();
        else if (a == "--preset") b.preset = next();
        else if (a == "--zones") b.zones = split_list(next());
        else if (a == "--device") b.device = next();
        else if (a == "--assets") b.assets = next();
        else if (a == "--low-impact") b.low_impact = true;
        else if (a == "--frames" && i + 1 < argc) frames = std::atoi(argv[++i]);
        else if (a == "--screenshot" && i + 1 < argc) screenshot = argv[++i];
        else if (a == "--data-dir" && i + 1 < argc) {
            // UTF-8 (on Windows too: the program's manifest makes UTF-8 its code page, cmake/utf8.manifest)
            const std::string dir = argv[++i];
            platform::use_data_dir(fs::path(std::u8string(dir.begin(), dir.end())), "--data-dir");
        }
        else if (a == "--version") {
            std::printf("%s %s (Hailstone %s)\n", kTitle, kVersion, build_version());
            return 0;
        }
    }
    if (platform::data_dir_reason() != "--data-dir")
        if (auto data = hailstone_folder_around(platform::install_dir()))
            platform::use_data_dir(*data, "the Hailstone folder it is installed in");
    if (build || devices) platform::attach_parent_console();
    try {
        if (std::string problem = platform::data_dir_problem(); !problem.empty()) {
            platform::show_error_box(kTitle, problem);
            return 1;
        }
        if (!log::open_file(platform::logs_dir() / "texstudio.log")) log::warn("could not open the log");
        platform::install_crash_handler(platform::logs_dir());
        log::info("{} {} (Hailstone {}); data folder {} ({})", kTitle, kVersion, build_version(),
                  platform::data_dir().string(), platform::data_dir_reason());
        if (devices) return list_devices_cli();
        if (build) return run_headless(b);
        return run(frames, screenshot);
    } catch (const std::exception& e) {
        log::error("{}", e.what());
        platform::show_error_box(kTitle, std::string("The texture studio could not start:\n\n") + e.what() +
                                             "\n\nThe log is logs/texstudio.log in the Hailstone folder.");
        return 1;
    }
}
