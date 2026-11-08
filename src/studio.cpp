#include "studio.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "core/asset_folder.h"
#include "core/asset_folder_choice.h"
#include "core/log.h"
#include "core/png_write.h"
#include "game/zone_names.h"
#include "platform/paths.h"

namespace hailstone::texstudio {

namespace fs = std::filesystem;
using texpack::FileKind;
using texpack::TextureInfo;

namespace {

double now_seconds() {
    using namespace std::chrono;
    static const auto start = steady_clock::now();
    return duration<double>(steady_clock::now() - start).count();
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::vector<std::string> split(std::string_view s, char sep) {
    std::vector<std::string> out;
    std::size_t p = 0;
    while (p <= s.size()) {
        std::size_t q = std::min(s.find(sep, p), s.size());
        if (q > p) out.emplace_back(s.substr(p, q - p));
        p = q + 1;
    }
    return out;
}

bool write_png(const fs::path& path, const assets::Image& img) {
    return write_png_rgba(path, img.mips[0], img.width, img.height);
}

} // namespace

const std::vector<Preset>& presets() {
    static const std::vector<Preset> list = {
        {"fast", "Fast", "Sharper textures at twice the size, without the material maps. The quickest to build.", 2,
         false},
        {"standard", "Standard",
         "Twice the size, with normal and roughness maps for the modern lighting: the pack Hailstone was tested with.",
         2, true},
        {"high", "High",
         "Four times the size, with the maps. Takes about four times the video memory and disk of Standard.", 4,
         true},
        {"custom", "Custom", "Your own choice of model, size and maps.", 0, false},
    };
    return list;
}

std::string group_of(std::string_view archive) {
    std::string name(archive);
    if (auto dot = name.rfind('.'); dot != std::string::npos) name.resize(dot);
    if (name.starts_with("global") || name.starts_with("gequip")) return "shared";
    std::string key = name.substr(0, name.find('_'));
    if (game::zone_id_by_short_name(key) != 0) return key;
    return "shared";
}

std::string group_title(std::string_view key) {
    if (key == "shared") return "Shared: characters, equipment, sky and spells";
    if (std::uint32_t id = game::zone_id_by_short_name(key)) {
        std::string long_name = game::zone_long_name(id);
        if (!long_name.empty()) return long_name;
    }
    return std::string(key);
}

bool valid_pack_name(std::string_view name) {
    if (name.empty() || name.size() > 64 || name.front() == '.') return false;
    return std::all_of(name.begin(), name.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
               c == '.';
    });
}

std::vector<PackEntry> list_packs(const fs::path& textures_dir) {
    std::vector<PackEntry> out;
    std::error_code ec;
    for (fs::directory_iterator it(textures_dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_directory(ec)) continue;
        PackEntry p;
        p.name = it->path().filename().string();
        p.info = texpack::read_pack_info(it->path());
        if (!p.info && !fs::is_directory(it->path() / "by-hash", ec)) continue;
        out.push_back(std::move(p));
    }
    std::sort(out.begin(), out.end(), [](const PackEntry& a, const PackEntry& b) { return a.name < b.name; });
    return out;
}

// ---- Studio --------------------------------------------------------------------------------------------------------

Studio::Studio(fs::path data_dir) : data_dir_(std::move(data_dir)) {
    settings_file_ = data_dir_ / "texstudio" / "texstudio.ini";
    scan_cache_ = platform::cache_dir() / "texstudio" / "scan.txt";
    preview_dir_ = platform::cache_dir() / "texstudio" / "preview";
    std::error_code ec;
    fs::remove_all(preview_dir_, ec); // (the last run's)
    fs::create_directories(preview_dir_, ec);
    fs::create_directories(data_dir_ / "texstudio" / "models", ec);

    devices_ = texpack::list_devices();
    models_ = texpack::list_models({platform::install_dir() / "models", data_dir_ / "texstudio" / "models"});
    for (const auto& d : devices_) log::info("texture studio: device {} ({}, {} MB)", d.name, d.type, d.memory_mb);
    for (const auto& m : models_) log::info("texture studio: model {} ({})", m.id, texpack::model_kind_name(m.kind));

    for (fs::directory_iterator it(platform::servers_dir(), ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (fs::is_directory(it->path() / "assets", e2)) server_folders_.push_back(it->path() / "assets");
    }
    std::sort(server_folders_.begin(), server_folders_.end());

    load_settings();
    refresh_packs();
    rebuild_worker();
    rescan();
}

Studio::~Studio() {
    scan_cancel_ = true;
    if (scan_thread_.joinable()) scan_thread_.join();
    job_.reset();
    worker_.reset();
    save_settings();
}

void Studio::load_settings() {
    settings_.load(settings_file_);
    Config client;
    client.load(default_config_path());
    recent_assets_ = recent_asset_folders(client);
    assets_path_ = settings_.get_string("assets", client.get_string("assets.path"));
    include_servers_ = settings_.get_bool("include_servers", true);
    preset_ = settings_.get_string("preset", "standard");
    if (std::none_of(presets().begin(), presets().end(), [&](const Preset& p) { return preset_ == p.id; })) preset_ = "standard";
    color_model_ = settings_.get_string("color_model", texpack::kPbrifyColor);
    scale_ = static_cast<std::uint32_t>(std::clamp(settings_.get_int("scale", 2), 1, 4));
    maps_ = settings_.get_bool("maps", true);
    build_all_ = settings_.get_bool("build_all", true);
    for (auto& g : split(settings_.get_string("groups"), ',')) selected_.insert(g);
    pack_name_ = settings_.get_string("pack", "hd");
    if (!valid_pack_name(pack_name_)) pack_name_ = "hd";
    low_impact_ = settings_.get_bool("low_impact", false);
    const std::string device = settings_.get_string("device");
    device_ = 0;
    for (std::size_t i = 0; i < devices_.size(); ++i)
        if (devices_[i].name == device) device_ = static_cast<int>(i);
    for (const auto& [k, v] : settings_.entries())
        if (k.starts_with("rate.")) {
            try {
                rates_[k.substr(5)] = std::stod(v);
            } catch (const std::exception&) {
            }
        }
    if (preset_ != "custom") apply_preset();
    pack_info_ = texpack::read_pack_info(pack_dir());
}

void Studio::save_settings() {
    if (!settings_dirty_ || !persist_) return;
    settings_dirty_ = false;
    settings_.set("assets", assets_path_);
    settings_.set_bool("include_servers", include_servers_);
    settings_.set("preset", preset_);
    settings_.set("color_model", color_model_);
    settings_.set_int("scale", static_cast<int>(scale_));
    settings_.set_bool("maps", maps_);
    settings_.set_bool("build_all", build_all_);
    std::string groups;
    for (const auto& g : selected_) groups += (groups.empty() ? "" : ",") + g;
    settings_.set("groups", groups);
    settings_.set("pack", pack_name_);
    settings_.set_bool("low_impact", low_impact_);
    if (device_ >= 0 && static_cast<std::size_t>(device_) < devices_.size()) settings_.set("device", devices_[static_cast<std::size_t>(device_)].name);
    for (const auto& [k, v] : rates_) settings_.set("rate." + k, std::to_string(v));
    if (!settings_.save(settings_file_)) log::warn("texture studio: could not save {}", settings_file_.string());
    settings_saved_at_ = now_seconds();
}

void Studio::update() {
    // The scan.
    {
        std::optional<texpack::ScanResult> done;
        {
            std::lock_guard lock(scan_mutex_);
            done.swap(scan_result_);
        }
        if (done) {
            if (scan_thread_.joinable()) scan_thread_.join();
            if (done->cancelled) {
                scan_state_ = ScanState::Idle;
            } else {
                archives_ = texpack::list_archives(asset_folders());
                textures_ = std::move(done->textures);
                scan_errors_ = std::move(done->errors);
                finish_scan();
            }
        }
    }
    // The speed test.
    if (measuring_ && measured_ && *measured_ != 0) {
        const double r = *measured_;
        measuring_ = false;
        measured_.reset();
        if (r > 0) {
            rates_[rate_key()] = r;
            settings_dirty_ = true;
            estimate_dirty_ = true;
            log::info("texture studio: {:.0f} texels a second ({})", r, rate_key());
        }
    }
    if (!measuring_ && scan_state_ == ScanState::Ready && worker_ && worker_->ready() && rate() == 0 && !job_)
        measure();
    // The preview.
    {
        std::shared_ptr<PreviewState> done;
        {
            std::lock_guard lock(*preview_mutex_);
            done.swap(*preview_slot_);
        }
        if (done && done->generation == preview_.generation) preview_ = std::move(*done);
    }
    // The build: once it ends, the pack is read again.
    if (job_ && job_->finished() && !job_finished_seen_) {
        job_finished_seen_ = true;
        auto s = job_->status();
        log::info("texture studio: build {}: {} of {} textures, {} files, {} failures", texpack::state_name(s.state),
                  s.done, s.total, s.files_written, s.failures);
        pack_info_ = texpack::read_pack_info(pack_dir());
        pack_contents_for_.clear();
        estimate_dirty_ = true;
        packs_at_ = -1;
    }
    if (job_ && !job_->finished()) estimate_dirty_ = false; // (the estimate is the job's status then)
    const double t = now_seconds();
    if (packs_at_ < 0 || t - packs_at_ > 5.0) refresh_packs();
    if (estimate_dirty_ && t - estimate_at_ > 0.25) refresh_estimate();
    if (settings_dirty_ && t - settings_saved_at_ > 1.0) save_settings();
}

// ---- asset folder and scan -----------------------------------------------------------------------------------------

std::vector<fs::path> Studio::asset_folders() const {
    std::vector<fs::path> out;
    if (!assets_path_.empty()) out.emplace_back(assets_path_);
    if (include_servers_) out.insert(out.end(), server_folders_.begin(), server_folders_.end());
    return out;
}

void Studio::set_assets_path(const std::string& path) {
    if (path == assets_path_) return;
    assets_path_ = path;
    settings_dirty_ = true;
    rescan();
}

void Studio::set_include_servers(bool on) {
    if (on == include_servers_) return;
    include_servers_ = on;
    settings_dirty_ = true;
    rescan();
}

void Studio::rescan(bool ignore_cache) {
    if (job_ && !job_->finished()) return;
    scan_cancel_ = true;
    if (scan_thread_.joinable()) scan_thread_.join();
    {
        std::lock_guard lock(scan_mutex_);
        scan_result_.reset();
    }
    scan_cancel_ = false;
    textures_.clear();
    groups_.clear();
    texture_groups_.clear();
    ++groups_revision_;
    estimate_ = {};
    assets_problem_.clear();
    scan_message_.clear();
    if (assets_path_.empty()) {
        assets_problem_ = "Choose the folder of your EverQuest files (the one Hailstone plays from).";
        scan_state_ = ScanState::Failed;
        return;
    }
    AssetFolderCheck check = validate_asset_folder(assets_path_);
    if (!check.is_directory) {
        assets_problem_ = "There is no folder there.";
        scan_state_ = ScanState::Failed;
        return;
    }
    if (std::string conflict = data_folder_conflict(assets_path_, platform::data_dir()); !conflict.empty()) {
        assets_problem_ = conflict;
        scan_state_ = ScanState::Failed;
        return;
    }
    if (!check.ok) assets_problem_ = describe(check); // (still scanned: its archives may be all a pack needs)
    scan_state_ = ScanState::Scanning;
    scan_done_ = 0;
    scan_total_ = 0;
    const auto folders = asset_folders();
    const fs::path cache = scan_cache_;
    scan_thread_ = std::thread([this, folders, cache, ignore_cache] {
        const auto archives = texpack::list_archives(folders);
        scan_total_ = archives.size();
        texpack::ScanResult r;
        std::optional<std::vector<TextureInfo>> cached;
        if (!ignore_cache) cached = texpack::load_scan(cache, archives);
        if (cached) {
            r.textures = std::move(*cached);
            r.archives = archives.size();
            scan_done_ = archives.size();
        } else {
            texpack::ScanOptions o;
            o.cancelled = [this] { return scan_cancel_.load(); };
            o.progress = [this](std::size_t d, std::size_t) { scan_done_ = d; };
            r = texpack::scan(archives, o);
            if (!r.cancelled && !texpack::save_scan(cache, archives, r.textures))
                log::warn("texture studio: could not save the scan to {}", cache.string());
        }
        std::lock_guard lock(scan_mutex_);
        scan_result_ = std::move(r);
    });
}

void Studio::finish_scan() {
    scan_state_ = archives_.empty() ? ScanState::Failed : ScanState::Ready;
    if (archives_.empty() && assets_problem_.empty()) assets_problem_ = "There are no .s3d or .eqg files in that folder.";
    char buf[160];
    std::snprintf(buf, sizeof buf, "%zu textures in %zu archives", textures_.size(), archives_.size());
    scan_message_ = buf;
    if (!scan_errors_.empty())
        scan_message_ += " (" + std::to_string(scan_errors_.size()) + " left out: formats Hailstone does not draw)";
    log::info("texture studio: {}", scan_message_);
    for (std::size_t i = 0; i < scan_errors_.size() && i < 40; ++i) log::warn("texture studio: {}", scan_errors_[i]);
    rebuild_groups();
    estimate_dirty_ = true;
}

void Studio::rebuild_groups() {
    std::map<std::string, std::size_t> index;
    groups_.clear();
    texture_groups_.assign(textures_.size(), {});
    for (std::size_t i = 0; i < textures_.size(); ++i) {
        std::set<std::string> keys;
        for (const std::string& a : textures_[i].archives) keys.insert(group_of(a));
        for (const std::string& k : keys) {
            auto [it, fresh] = index.try_emplace(k, groups_.size());
            if (fresh) groups_.push_back({k, group_title(k)});
            Group& g = groups_[it->second];
            ++g.textures;
            g.texels += static_cast<double>(textures_[i].width) * textures_[i].height;
            texture_groups_[i].push_back(it->second);
        }
    }
    // Shared first, then the zones by name; the indexes in texture_groups_ follow.
    std::vector<std::size_t> order(groups_.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        const bool sa = groups_[a].key == "shared", sb = groups_[b].key == "shared";
        if (sa != sb) return sa;
        return lower(groups_[a].title) < lower(groups_[b].title);
    });
    std::vector<std::size_t> where(groups_.size());
    std::vector<Group> sorted;
    for (std::size_t i = 0; i < order.size(); ++i) {
        where[order[i]] = i;
        sorted.push_back(std::move(groups_[order[i]]));
    }
    groups_ = std::move(sorted);
    for (auto& list : texture_groups_)
        for (std::size_t& g : list) g = where[g];
    ++groups_revision_;
}

// ---- models and device ---------------------------------------------------------------------------------------------

void Studio::set_device(int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= devices_.size() || index == device_) return;
    device_ = index;
    settings_dirty_ = true;
    rebuild_worker();
}

void Studio::apply_preset() {
    for (const Preset& p : presets())
        if (preset_ == p.id && p.scale) {
            color_model_ = texpack::kPbrifyColor;
            scale_ = p.scale;
            maps_ = p.maps && maps_available();
        }
}

void Studio::set_preset(const std::string& id) {
    if (std::none_of(presets().begin(), presets().end(), [&](const Preset& p) { return id == p.id; })) return;
    preset_ = id;
    apply_preset();
    settings_dirty_ = true;
    estimate_dirty_ = true;
    pack_info_ = texpack::read_pack_info(pack_dir());
    rebuild_worker();
}

void Studio::set_color_model(const std::string& id) {
    const texpack::ModelEntry* m = texpack::find_model(models_, id);
    if (!m || m->kind != texpack::ModelKind::Color || id == color_model_) return;
    color_model_ = id;
    preset_ = "custom";
    settings_dirty_ = true;
    estimate_dirty_ = true;
    rebuild_worker();
}

void Studio::set_scale(std::uint32_t scale) {
    scale = std::clamp<std::uint32_t>(scale, 1, 4);
    if (scale == scale_) return;
    scale_ = scale;
    preset_ = "custom";
    settings_dirty_ = true;
    estimate_dirty_ = true;
}

void Studio::set_maps(bool on) {
    on = on && maps_available();
    if (on == maps_) return;
    maps_ = on;
    preset_ = "custom";
    settings_dirty_ = true;
    estimate_dirty_ = true;
    rebuild_worker();
}

bool Studio::maps_available() const {
    return texpack::find_model(models_, texpack::kPbrifyNormal) && texpack::find_model(models_, texpack::kPbrifyRough);
}

void Studio::rebuild_worker() {
    if (job_ && !job_->finished()) return;
    const texpack::ModelEntry* color = texpack::find_model(models_, color_model_);
    const std::string key = std::to_string(device_) + "|" + color_model_ + "|" + (maps_ ? "maps" : "");
    if (worker_ && key == worker_key_) return;
    worker_.reset();
    worker_key_ = key;
    measuring_ = false;
    measured_.reset();
    if (!color || devices_.empty()) return;
    std::optional<texpack::ModelEntry> normal, rough;
    if (maps_) {
        if (auto* n = texpack::find_model(models_, texpack::kPbrifyNormal)) normal = *n;
        if (auto* r = texpack::find_model(models_, texpack::kPbrifyRough)) rough = *r;
    }
    const texpack::GpuDevice& d = devices_[static_cast<std::size_t>(device_)];
    const unsigned cpu = std::max(1u, std::thread::hardware_concurrency() / 2);
    worker_ = std::make_unique<texpack::GpuWorker>(texpack::ncnn_model_set(*color, normal, rough, d.index, cpu));
}

Studio::WorkerState Studio::worker_state() const {
    if (!worker_) return WorkerState::None;
    if (worker_->failed()) return WorkerState::Failed;
    return worker_->ready() ? WorkerState::Ready : WorkerState::Loading;
}

std::string Studio::worker_error() const {
    if (!worker_) {
        if (!texpack::find_model(models_, color_model_)) return "The model " + color_model_ + " is not installed.";
        return {};
    }
    return worker_->error();
}

std::string Studio::rate_key() const {
    std::string device = device_ >= 0 && static_cast<std::size_t>(device_) < devices_.size()
                             ? devices_[static_cast<std::size_t>(device_)].name
                             : "none";
    for (char& c : device)
        if (c == '=' || c == '\n') c = ' ';
    return device + "|" + color_model_ + "|" + std::to_string(scale_) + (maps_ ? "|maps" : "");
}

double Studio::rate() const {
    auto it = rates_.find(rate_key());
    return it == rates_.end() ? 0.0 : it->second;
}

void Studio::measure() {
    if (!worker_ || measuring_ || textures_.empty()) return;
    // Ten ordinary pictures of 128 to 512 texels a side, the same ones every time (by hash).
    std::vector<TextureInfo> sample;
    for (const TextureInfo& t : textures_) {
        const std::uint32_t side = std::max(t.width, t.height);
        if (texpack::uses_models(t) && side >= 128 && side <= 512) sample.push_back(t);
        if (sample.size() == 10) break;
    }
    if (sample.size() < 3) return;
    measuring_ = true;
    auto result = std::make_shared<std::atomic<double>>(0.0);
    measured_ = result;
    texpack::UpscaleSettings settings{scale_, maps_};
    worker_->post(
        [sample, settings, result](const texpack::Models& models) {
            if (!models.color) {
                *result = -1;
                return;
            }
            try {
                std::vector<texpack::Sources> sources;
                for (const TextureInfo& t : sample) {
                    auto s = texpack::read_sources(t, nullptr);
                    if (!s) {
                        *result = -1;
                        return;
                    }
                    sources.push_back(std::move(*s));
                }
                // The first one warms the models up (ncnn makes its pipelines then): not timed.
                double texels = 0;
                std::chrono::steady_clock::time_point start;
                for (std::size_t i = 0; i < sample.size(); ++i) {
                    if (i == 1) start = std::chrono::steady_clock::now();
                    auto files = texpack::pack_files(sample[i], settings.maps);
                    texpack::upscale_texture(sample[i], sources[i], models, settings, files);
                    if (i >= 1) texels += static_cast<double>(sample[i].width) * sample[i].height * static_cast<double>(files.size());
                }
                const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
                *result = secs > 0 ? texels / secs : -1;
            } catch (const std::exception&) {
                *result = -1;
            }
        },
        true);
}

// ---- what to build -------------------------------------------------------------------------------------------------

void Studio::set_build_all(bool all) {
    if (all == build_all_) return;
    build_all_ = all;
    settings_dirty_ = true;
    estimate_dirty_ = true;
}

void Studio::set_group_selected(const std::string& key, bool on) {
    if (on == selected_.contains(key)) return;
    if (on) selected_.insert(key);
    else selected_.erase(key);
    settings_dirty_ = true;
    estimate_dirty_ = true;
    ++groups_revision_;
}

void Studio::select_no_groups() {
    if (selected_.empty()) return;
    selected_.clear();
    settings_dirty_ = true;
    estimate_dirty_ = true;
    ++groups_revision_;
}

std::vector<TextureInfo> Studio::selected_textures() const {
    if (build_all_) return textures_;
    std::vector<bool> chosen(groups_.size());
    for (std::size_t g = 0; g < groups_.size(); ++g) chosen[g] = selected_.contains(groups_[g].key);
    std::vector<TextureInfo> out;
    for (std::size_t i = 0; i < textures_.size(); ++i)
        if (std::any_of(texture_groups_[i].begin(), texture_groups_[i].end(), [&](std::size_t g) { return chosen[g]; }))
            out.push_back(textures_[i]);
    return out;
}

void Studio::refresh_estimate() {
    estimate_dirty_ = false;
    estimate_at_ = now_seconds();
    if (pack_contents_for_ != pack_dir()) {
        pack_contents_ = texpack::pack_contents(pack_dir());
        pack_contents_for_ = pack_dir();
    }
    Estimate e;
    std::vector<bool> chosen(groups_.size());
    for (std::size_t g = 0; g < groups_.size(); ++g) chosen[g] = build_all_ || selected_.contains(groups_[g].key);
    const double area = static_cast<double>(scale_) * scale_;
    for (std::size_t i = 0; i < textures_.size(); ++i) {
        if (!build_all_ &&
            std::none_of(texture_groups_[i].begin(), texture_groups_[i].end(), [&](std::size_t g) { return chosen[g]; }))
            continue;
        const TextureInfo& t = textures_[i];
        ++e.textures;
        const double texels = static_cast<double>(t.width) * t.height;
        for (FileKind k : texpack::pack_files(t, maps_)) e.bytes += texels * area * texpack::bytes_per_texel(k);
        auto missing = texpack::missing_files(t, maps_, pack_contents_);
        if (missing.empty()) {
            ++e.complete;
            continue;
        }
        ++e.todo;
        e.files += missing.size();
        for (FileKind k : missing) e.todo_bytes += texels * area * texpack::bytes_per_texel(k);
        e.texels += texels * static_cast<double>(missing.size());
    }
    if (double r = rate(); r > 0) e.seconds = e.texels / r;
    std::error_code ec;
    fs::path where = pack_dir();
    while (!where.empty() && !fs::exists(where, ec) && where.has_parent_path() && where.parent_path() != where)
        where = where.parent_path();
    if (auto space = fs::space(where, ec); !ec) e.free_bytes = static_cast<double>(space.available);
    estimate_ = e;
}

// ---- packs ---------------------------------------------------------------------------------------------------------

fs::path Studio::pack_dir() const { return platform::textures_dir() / pack_name_; }

void Studio::set_pack_name(const std::string& name) {
    if (name == pack_name_ || !valid_pack_name(name) || (job_ && !job_->finished())) return;
    pack_name_ = name;
    settings_dirty_ = true;
    estimate_dirty_ = true;
    pack_info_ = texpack::read_pack_info(pack_dir());
}

bool Studio::pack_mismatch() const {
    if (!pack_info_) return false;
    texpack::PackInfo now;
    now.color_model = color_model_;
    now.normal_model = maps_ ? texpack::kPbrifyNormal : "";
    now.rough_model = maps_ ? texpack::kPbrifyRough : "";
    now.scale = scale_;
    now.maps = maps_;
    return !pack_info_->same_settings(now);
}

std::string Studio::suggest_pack_name() const {
    std::string base = preset_ == "custom" ? "hd-custom" : "hd-" + preset_;
    std::string name = base;
    std::error_code ec;
    for (int n = 2; fs::exists(platform::textures_dir() / name, ec) && n < 100; ++n) name = base + "-" + std::to_string(n);
    return name;
}

void Studio::refresh_packs() {
    packs_ = list_packs(platform::textures_dir());
    packs_at_ = now_seconds();
}

std::string Studio::active_pack() const {
    Config c;
    c.load(default_config_path());
    return c.get_string("display.texture_folder", "");
}

bool Studio::use_pack(const std::string& name) {
    if (!valid_pack_name(name) && name != "off") return false;
    Config c;
    c.load(default_config_path());
    c.set("display.texture_folder", name == "default" ? "" : name);
    const bool ok = c.save(default_config_path());
    if (ok) log::info("texture studio: Hailstone uses the pack {}", name);
    return ok;
}

bool Studio::clear_pack() {
    if (job_ && !job_->finished()) return false;
    std::error_code ec;
    fs::remove_all(pack_dir() / "by-hash", ec);
    fs::remove(pack_dir() / texpack::kPackInfoFile, ec);
    pack_info_.reset();
    pack_contents_for_.clear();
    estimate_dirty_ = true;
    packs_at_ = -1;
    log::info("texture studio: cleared {}", pack_dir().string());
    return !ec;
}

// ---- build ---------------------------------------------------------------------------------------------------------

bool Studio::start(std::string* why) {
    auto fail = [&](std::string w) {
        if (why) *why = std::move(w);
        return false;
    };
    if (job_ && !job_->finished()) return fail("A build is running.");
    if (scan_state_ != ScanState::Ready) return fail("The game's files are not read yet.");
    if (!worker_ || worker_->failed()) return fail(worker_error().empty() ? "The models are not loaded." : worker_error());
    if (!valid_pack_name(pack_name_)) return fail("The pack's name may hold only letters, digits, '-', '_' and '.'.");
    if (pack_mismatch()) return fail("This pack was built with other settings.");
    // (a gigabyte to spare: the system and Hailstone need room too)
    if (estimate_.free_bytes >= 0 && estimate_.todo > 0 && estimate_.free_bytes < estimate_.todo_bytes + 1e9)
        return fail("There is not enough free disk space for it: choose fewer zones, or a smaller size.");
    texpack::JobOptions o;
    o.pack_dir = pack_dir();
    o.textures = selected_textures();
    o.settings = {scale_, maps_};
    o.low_impact = low_impact_;
    o.info.title = pack_name_;
    o.info.tool_version = "texstudio " TEXSTUDIO_VERSION;
    o.info.color_model = color_model_;
    o.info.normal_model = maps_ ? texpack::kPbrifyNormal : "";
    o.info.rough_model = maps_ ? texpack::kPbrifyRough : "";
    o.info.scale = scale_;
    o.info.maps = maps_;
    if (pack_info_) {
        o.info.textures = pack_info_->textures;
        o.info.files = pack_info_->files;
        o.info.bytes = pack_info_->bytes;
        o.info.built = pack_info_->built;
    }
    // Its settings go in at once, so a pack stopped half way still says how it was made.
    std::error_code ec;
    fs::create_directories(o.pack_dir, ec);
    texpack::write_pack_info(o.pack_dir, o.info);
    pack_info_ = o.info;
    log::info("texture studio: building {} ({} textures, scale {}, maps {})", o.pack_dir.string(), o.textures.size(),
              scale_, maps_);
    measuring_ = false;
    job_.reset();
    job_ = std::make_unique<texpack::PackJob>(std::move(o), *worker_);
    job_finished_seen_ = false;
    return true;
}

bool Studio::gpu_found() const {
    return std::any_of(devices_.begin(), devices_.end(), [](const texpack::GpuDevice& d) { return d.index >= 0; });
}

void Studio::pause() {
    if (job_) job_->pause();
}
void Studio::resume() {
    if (job_) job_->resume();
}
void Studio::stop() {
    if (job_) job_->cancel();
}

void Studio::set_low_impact(bool on) {
    low_impact_ = on;
    settings_dirty_ = true;
    if (job_) job_->set_low_impact(on);
}

std::optional<texpack::JobStatus> Studio::job_status() const {
    if (!job_) return std::nullopt;
    return job_->status();
}

// ---- preview -------------------------------------------------------------------------------------------------------

bool Studio::preview_texture(const std::string& query) {
    if (!worker_ || textures_.empty()) return false;
    std::optional<std::size_t> pick;
    if (query.empty()) {
        // xorshift: a different picture each click, among those the models run on.
        std::vector<std::size_t> candidates;
        for (std::size_t i = 0; i < textures_.size(); ++i)
            if (texpack::uses_models(textures_[i]) && std::max(textures_[i].width, textures_[i].height) >= 64) candidates.push_back(i);
        if (candidates.empty()) return false;
        preview_seed_ ^= preview_seed_ << 13;
        preview_seed_ ^= preview_seed_ >> 7;
        preview_seed_ ^= preview_seed_ << 17;
        pick = candidates[preview_seed_ % candidates.size()];
    } else {
        const std::string q = lower(query);
        for (std::size_t k = 1; k <= textures_.size() && !pick; ++k) {
            std::size_t i = (preview_cursor_ + k) % textures_.size();
            for (const std::string& n : textures_[i].names)
                if (n.find(q) != std::string::npos) {
                    pick = i;
                    break;
                }
        }
        if (!pick) return false;
    }
    preview_cursor_ = *pick;
    const TextureInfo info = textures_[*pick];
    PreviewState next;
    next.state = PreviewState::State::Working;
    next.generation = preview_.generation + 1;
    next.name = *info.names.begin();
    for (const std::string& a : info.archives) next.archives += (next.archives.empty() ? "" : ", ") + a;
    next.width = info.width;
    next.height = info.height;
    next.stored = info.stored;
    next.scale = scale_;
    next.data = !texpack::uses_models(info);
    preview_ = next;
    auto mutex = preview_mutex_;
    auto out = preview_slot_;
    const fs::path dir = preview_dir_;
    const texpack::UpscaleSettings settings{scale_, maps_};
    worker_->post(
        [info, next, dir, settings, mutex, out](const texpack::Models& models) {
            auto r = std::make_shared<PreviewState>(next);
            const auto start = std::chrono::steady_clock::now();
            std::string why;
            auto sources = texpack::read_sources(info, nullptr, &why);
            if (!models.color) {
                r->state = PreviewState::State::Failed;
                r->error = "The models are not loaded.";
            } else if (!sources) {
                r->state = PreviewState::State::Failed;
                r->error = why;
            } else {
                try {
                    auto files = texpack::pack_files(info, settings.maps);
                    texpack::Outputs o = texpack::upscale_texture(info, *sources, models, settings, files);
                    r->seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
                    const std::string base = std::to_string(next.generation) + "-";
                    r->source = dir / (base + "source.png");
                    write_png(r->source, sources->color);
                    if (o.color) write_png(r->color = dir / (base + "color.png"), *o.color);
                    if (o.normal) write_png(r->normal = dir / (base + "normal.png"), *o.normal);
                    if (o.rough) write_png(r->rough = dir / (base + "rough.png"), *o.rough);
                    r->state = PreviewState::State::Ready;
                } catch (const std::exception& e) {
                    r->state = PreviewState::State::Failed;
                    r->error = e.what();
                }
            }
            std::lock_guard lock(*mutex);
            *out = std::move(r);
        },
        true);
    return true;
}

} // namespace hailstone::texstudio
