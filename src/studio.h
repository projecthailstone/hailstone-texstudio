#pragma once

// Hailstone Texture Studio's state (README.md): the asset folder and its scan, the models and the device they
// run on, what to build and into which pack, the build itself, the preview and the speed test. The window draws it
// through the `studio` Lua table (studio_lua.cpp) in the modern UI's look (ui/modern/tools/texstudio.lua). All of it
// is used from the main thread; the scan, the GPU and the build run on threads of their own.

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "core/config.h"
#include "texpack/model_catalog.h"
#include "ncnn_model.h"
#include "texpack/pack_job.h"
#include "texpack/texpack.h"

namespace hailstone::texstudio {

// The presets the window offers (Custom: the player's own choice of model, scale and maps).
struct Preset {
    const char* id;
    const char* title;
    const char* note;
    std::uint32_t scale;
    bool maps;
};
const std::vector<Preset>& presets();

// A group of archives to build or leave out: a zone (its archives' names before the first '_' or '.', the zone's
// long name when known) or "shared" (characters, equipment and the rest every zone loads).
struct Group {
    std::string key, title;
    std::size_t textures = 0;
    double texels = 0;
};
// The group of an archive file name (lower case): "global", "gequip" and "*_chr" outside a zone are shared.
std::string group_of(std::string_view archive);
std::string group_title(std::string_view key);

// The players' packs (data folder's textures/<name>/, those with a pack.ini or a by-hash/ folder).
struct PackEntry {
    std::string name;
    std::optional<texpack::PackInfo> info;
};
std::vector<PackEntry> list_packs(const std::filesystem::path& textures_dir);
// A name a pack folder may have: letters, digits, '-', '_' and '.', not starting with '.', at most 64 long.
bool valid_pack_name(std::string_view name);

class Studio {
public:
    explicit Studio(std::filesystem::path data_dir);
    ~Studio();
    Studio(const Studio&) = delete;
    Studio& operator=(const Studio&) = delete;

    // Once a frame: takes in what the threads finished, saves changed settings now and then.
    void update();

    // ---- the asset folder and its scan
    std::string assets_path() const { return assets_path_; }
    std::string assets_problem() const { return assets_problem_; }
    const std::vector<std::string>& recent_assets() const { return recent_assets_; }
    void set_assets_path(const std::string& path); // scans again
    bool include_servers() const { return include_servers_; }
    void set_include_servers(bool on);
    std::size_t server_folders() const { return server_folders_.size(); }
    void rescan(bool ignore_cache = false);

    enum class ScanState : std::uint8_t { Idle, Scanning, Ready, Failed };
    ScanState scan_state() const { return scan_state_; }
    std::size_t scan_done() const { return scan_done_; }
    std::size_t scan_total() const { return scan_total_; }
    std::size_t archives() const { return archives_.size(); }
    const std::vector<texpack::TextureInfo>& textures() const { return textures_; }
    const std::vector<std::string>& scan_errors() const { return scan_errors_; }
    std::string scan_message() const { return scan_message_; }

    // ---- models and device
    const std::vector<texpack::GpuDevice>& devices() const { return devices_; }
    int device() const { return device_; } // index into devices()
    void set_device(int index);
    const std::vector<texpack::ModelEntry>& models() const { return models_; }
    std::string preset() const { return preset_; }
    void set_preset(const std::string& id);
    std::string color_model() const { return color_model_; }
    void set_color_model(const std::string& id);
    std::uint32_t scale() const { return scale_; }
    void set_scale(std::uint32_t scale);
    bool maps() const { return maps_; }
    void set_maps(bool on);
    bool maps_available() const; // the normal and roughness models are there

    enum class WorkerState : std::uint8_t { None, Loading, Ready, Failed };
    WorkerState worker_state() const;
    std::string worker_error() const;

    // The speed test: source texels per second for each file made, on this device and these models (0: not known).
    double rate() const;
    bool measuring() const { return measuring_; }
    void measure();

    // ---- what to build
    bool build_all() const { return build_all_; }
    void set_build_all(bool all);
    const std::vector<Group>& groups() const { return groups_; }
    std::uint64_t groups_revision() const { return groups_revision_; }
    bool group_selected(const std::string& key) const { return selected_.contains(key); }
    void set_group_selected(const std::string& key, bool on);
    void select_no_groups();

    struct Estimate {
        std::size_t textures = 0, todo = 0, complete = 0, files = 0;
        double texels = 0; // to make
        double bytes = 0;  // the pack's size once built (all its files)
        double todo_bytes = 0; // of those, the files still to write
        double seconds = 0; // 0: not known (no speed test yet)
        double free_bytes = -1; // free space where the pack goes (-1: not known)
    };
    const Estimate& estimate() const { return estimate_; }

    // ---- the pack
    std::string pack_name() const { return pack_name_; }
    void set_pack_name(const std::string& name);
    std::filesystem::path pack_dir() const;
    std::optional<texpack::PackInfo> pack_info() const { return pack_info_; }
    // The pack exists and was built with other settings: a build would mix them.
    bool pack_mismatch() const;
    std::string suggest_pack_name() const;
    const std::vector<PackEntry>& packs() const { return packs_; }
    std::string active_pack() const; // hailstone.ini display.texture_folder ("" textures/default/, "off")
    bool use_pack(const std::string& name); // sets display.texture_folder
    bool clear_pack(); // deletes the pack's by-hash/ and pack.ini (the pack folder stays)

    // ---- the build
    bool start(std::string* why);
    void pause();
    void resume();
    void stop();
    bool low_impact() const { return low_impact_; }
    void set_low_impact(bool on);
    std::optional<texpack::JobStatus> job_status() const;

    // ---- the preview
    struct PreviewState {
        enum class State : std::uint8_t { None, Working, Ready, Failed } state = State::None;
        std::uint64_t generation = 0;
        std::string name, archives, error, stored;
        std::uint32_t width = 0, height = 0, scale = 0;
        double seconds = 0;
        std::filesystem::path source, color, normal, rough; // PNGs (empty: not made)
        bool data = false; // only Lanczos-scaled (texpack::uses_models false)
    };
    const PreviewState& preview() const { return preview_; }
    // A texture whose name holds `query` (any case), the next match after the one shown; "" a random picture.
    bool preview_texture(const std::string& query);

    std::filesystem::path data_dir() const { return data_dir_; }
    // Off: choices made here are not saved to texstudio.ini (the command line's --build).
    void set_persist(bool on) { persist_ = on; }
    bool gpu_found() const;

private:
    void load_settings();
    void save_settings();
    void apply_preset();
    void rebuild_worker();
    void rebuild_groups();
    void refresh_packs();
    void refresh_estimate();
    void finish_scan();
    std::string rate_key() const;
    std::vector<std::filesystem::path> asset_folders() const;
    std::vector<texpack::TextureInfo> selected_textures() const;

    std::filesystem::path data_dir_;
    std::filesystem::path settings_file_, scan_cache_, preview_dir_;
    Config settings_;
    bool settings_dirty_ = false;
    bool persist_ = true;
    double settings_saved_at_ = 0;

    std::string assets_path_, assets_problem_;
    std::vector<std::string> recent_assets_;
    bool include_servers_ = true;
    std::vector<std::filesystem::path> server_folders_;

    std::atomic<ScanState> scan_state_{ScanState::Idle};
    std::atomic<std::size_t> scan_done_{0}, scan_total_{0};
    std::atomic<bool> scan_cancel_{false};
    std::thread scan_thread_;
    std::mutex scan_mutex_;
    std::optional<texpack::ScanResult> scan_result_; // from the scan thread, taken by update()
    bool scan_from_cache_ = false;
    std::vector<std::filesystem::path> archives_;
    std::vector<texpack::TextureInfo> textures_;
    std::vector<std::string> scan_errors_;
    std::string scan_message_;

    std::vector<texpack::GpuDevice> devices_;
    int device_ = 0;
    std::vector<texpack::ModelEntry> models_;
    std::string preset_ = "standard", color_model_;
    std::uint32_t scale_ = 2;
    bool maps_ = true;
    std::unique_ptr<texpack::GpuWorker> worker_;
    std::string worker_key_; // what the worker was made for (device and models)

    std::atomic<bool> measuring_{false};
    std::shared_ptr<std::atomic<double>> measured_; // the running test's result (< 0: failed)
    std::map<std::string, double> rates_;           // rate_key -> texels a second

    bool build_all_ = true;
    std::set<std::string> selected_;
    std::vector<Group> groups_;
    std::uint64_t groups_revision_ = 0;
    std::vector<std::vector<std::size_t>> texture_groups_; // texture index -> its groups' indexes

    Estimate estimate_;
    bool estimate_dirty_ = true;
    double estimate_at_ = -1;
    std::set<std::string> pack_contents_;
    std::filesystem::path pack_contents_for_;

    std::string pack_name_ = "hd";
    std::optional<texpack::PackInfo> pack_info_;
    std::vector<PackEntry> packs_;
    double packs_at_ = -1;

    std::unique_ptr<texpack::PackJob> job_;
    bool low_impact_ = false;
    bool job_finished_seen_ = false;

    PreviewState preview_;
    std::shared_ptr<std::mutex> preview_mutex_ = std::make_shared<std::mutex>();
    // The GPU thread's finished preview, taken by update() (shared with the task, which may outlive a rebuilt worker)
    std::shared_ptr<std::shared_ptr<PreviewState>> preview_slot_ = std::make_shared<std::shared_ptr<PreviewState>>();
    std::size_t preview_cursor_ = 0;
    std::uint64_t preview_seed_ = 0x9e3779b97f4a7c15ull;
};

} // namespace hailstone::texstudio
