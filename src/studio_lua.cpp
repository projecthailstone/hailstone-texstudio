// The `studio` table the texture studio's window (ui/modern/tools/texstudio.lua) draws from and acts through. Its
// functions are the window's only way to the studio (README.md "The window").

#include "studio_lua.h"

#include <cmath>

#include <lua.hpp>

#include "platform/folder_dialog.h"
#include "platform/open_folder.h"
#include "platform/paths.h"
#include "platform/window.h"
#include "texpack/texpack.h"
#include "ui/ui_textures.h"

namespace hailstone::texstudio {

namespace {

StudioLuaContext& ctx(lua_State* L) { return *static_cast<StudioLuaContext*>(lua_touserdata(L, lua_upvalueindex(1))); }

void set_string(lua_State* L, const char* key, const std::string& v) {
    lua_pushlstring(L, v.data(), v.size());
    lua_setfield(L, -2, key);
}
void set_number(lua_State* L, const char* key, double v) {
    lua_pushnumber(L, v);
    lua_setfield(L, -2, key);
}
void set_int(lua_State* L, const char* key, lua_Integer v) {
    lua_pushinteger(L, v);
    lua_setfield(L, -2, key);
}
void set_bool(lua_State* L, const char* key, bool v) {
    lua_pushboolean(L, v);
    lua_setfield(L, -2, key);
}
std::string arg_string(lua_State* L, int i) {
    std::size_t n = 0;
    const char* s = luaL_optlstring(L, i, "", &n);
    return std::string(s, n);
}

const char* scan_state(Studio::ScanState s) {
    switch (s) {
    case Studio::ScanState::Idle: return "idle";
    case Studio::ScanState::Scanning: return "scanning";
    case Studio::ScanState::Ready: return "ready";
    case Studio::ScanState::Failed: return "failed";
    }
    return "idle";
}

const char* worker_state(Studio::WorkerState s) {
    switch (s) {
    case Studio::WorkerState::None: return "none";
    case Studio::WorkerState::Loading: return "loading";
    case Studio::WorkerState::Ready: return "ready";
    case Studio::WorkerState::Failed: return "failed";
    }
    return "none";
}

void push_pack_info(lua_State* L, const texpack::PackInfo& i) {
    lua_newtable(L);
    set_string(L, "title", i.title);
    set_string(L, "colorModel", i.color_model);
    set_int(L, "scale", i.scale);
    set_bool(L, "maps", i.maps);
    set_number(L, "textures", static_cast<double>(i.textures));
    set_number(L, "files", static_cast<double>(i.files));
    set_number(L, "bytes", static_cast<double>(i.bytes));
    set_string(L, "built", i.built);
    set_string(L, "toolVersion", i.tool_version);
}

// state = studio.State(): everything the window shows, made fresh each call.
int State(lua_State* L) {
    StudioLuaContext& c = ctx(L);
    Studio& s = *c.studio;
    lua_newtable(L);
    set_string(L, "assetsPath", s.assets_path());
    set_string(L, "assetsProblem", s.assets_problem());
    lua_newtable(L);
    for (std::size_t i = 0; i < s.recent_assets().size(); ++i) {
        lua_pushstring(L, s.recent_assets()[i].c_str());
        lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
    }
    lua_setfield(L, -2, "recent");
    set_bool(L, "includeServers", s.include_servers());
    set_int(L, "serverFolders", static_cast<lua_Integer>(s.server_folders()));
    set_bool(L, "browsing", c.folder_dialog && c.folder_dialog->is_open());

    lua_newtable(L);
    set_string(L, "state", scan_state(s.scan_state()));
    set_int(L, "done", static_cast<lua_Integer>(s.scan_done()));
    set_int(L, "total", static_cast<lua_Integer>(s.scan_total()));
    set_int(L, "textures", static_cast<lua_Integer>(s.textures().size()));
    set_int(L, "archives", static_cast<lua_Integer>(s.archives()));
    set_int(L, "errors", static_cast<lua_Integer>(s.scan_errors().size()));
    set_string(L, "message", s.scan_message());
    lua_setfield(L, -2, "scan");

    lua_newtable(L);
    for (std::size_t i = 0; i < s.devices().size(); ++i) {
        const auto& d = s.devices()[i];
        lua_newtable(L);
        set_string(L, "name", d.name);
        set_string(L, "type", d.type);
        set_int(L, "memoryMb", d.memory_mb);
        lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
    }
    lua_setfield(L, -2, "devices");
    set_int(L, "device", s.device() + 1);

    lua_newtable(L);
    int n = 0;
    for (const auto& m : s.models()) {
        if (m.kind != texpack::ModelKind::Color) continue;
        lua_newtable(L);
        set_string(L, "id", m.id);
        set_string(L, "title", m.title);
        set_string(L, "note", m.note);
        set_string(L, "license", m.license);
        set_string(L, "source", m.source);
        set_int(L, "scale", m.scale);
        set_bool(L, "user", m.user);
        lua_rawseti(L, -2, ++n);
    }
    lua_setfield(L, -2, "colorModels");
    set_bool(L, "mapsAvailable", s.maps_available());

    lua_newtable(L);
    n = 0;
    for (const Preset& p : presets()) {
        lua_newtable(L);
        set_string(L, "id", p.id);
        set_string(L, "title", p.title);
        set_string(L, "note", p.note);
        lua_rawseti(L, -2, ++n);
    }
    lua_setfield(L, -2, "presets");
    set_string(L, "preset", s.preset());
    set_string(L, "colorModel", s.color_model());
    set_int(L, "scale", s.scale());
    set_bool(L, "maps", s.maps());

    lua_newtable(L);
    set_string(L, "state", worker_state(s.worker_state()));
    set_string(L, "error", s.worker_error());
    lua_setfield(L, -2, "worker");
    set_number(L, "rate", s.rate());
    set_bool(L, "measuring", s.measuring());

    const Studio::Estimate& e = s.estimate();
    lua_newtable(L);
    set_int(L, "textures", static_cast<lua_Integer>(e.textures));
    set_int(L, "todo", static_cast<lua_Integer>(e.todo));
    set_int(L, "complete", static_cast<lua_Integer>(e.complete));
    set_int(L, "files", static_cast<lua_Integer>(e.files));
    set_number(L, "bytes", e.bytes);
    set_number(L, "seconds", e.seconds);
    set_number(L, "freeBytes", e.free_bytes);
    set_number(L, "todoBytes", e.todo_bytes);
    lua_setfield(L, -2, "estimate");
    set_bool(L, "gpuFound", s.gpu_found());

    set_bool(L, "buildAll", s.build_all());
    set_string(L, "packName", s.pack_name());
    set_string(L, "packDir", s.pack_dir().string());
    set_bool(L, "packMismatch", s.pack_mismatch());
    set_string(L, "suggestedName", s.suggest_pack_name());
    if (auto info = s.pack_info()) {
        push_pack_info(L, *info);
        lua_setfield(L, -2, "packInfo");
    }
    set_string(L, "activePack", s.active_pack());
    lua_newtable(L);
    for (std::size_t i = 0; i < s.packs().size(); ++i) {
        const PackEntry& p = s.packs()[i];
        if (p.info) push_pack_info(L, *p.info);
        else lua_newtable(L);
        set_string(L, "name", p.name);
        lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
    }
    lua_setfield(L, -2, "packs");

    set_bool(L, "lowImpact", s.low_impact());
    if (auto j = s.job_status()) {
        lua_newtable(L);
        set_string(L, "state", std::string(texpack::state_name(j->state)));
        set_int(L, "total", static_cast<lua_Integer>(j->total));
        set_int(L, "done", static_cast<lua_Integer>(j->done));
        set_int(L, "complete", static_cast<lua_Integer>(j->complete));
        set_int(L, "failures", static_cast<lua_Integer>(j->failures));
        set_number(L, "filesWritten", static_cast<double>(j->files_written));
        set_number(L, "bytesWritten", static_cast<double>(j->bytes_written));
        set_number(L, "fraction", j->texels_total > 0 ? j->texels_done / j->texels_total : (j->total ? 0.0 : 1.0));
        set_number(L, "seconds", j->seconds);
        set_number(L, "eta", j->eta());
        set_string(L, "current", j->current);
        set_string(L, "fatal", j->fatal);
        lua_newtable(L);
        for (std::size_t i = 0; i < j->errors.size(); ++i) {
            lua_pushstring(L, j->errors[i].c_str());
            lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
        }
        lua_setfield(L, -2, "errors");
        lua_setfield(L, -2, "job");
    }

    const Studio::PreviewState& p = s.preview();
    lua_newtable(L);
    static const char* const kPreview[] = {"none", "working", "ready", "failed"};
    set_string(L, "state", kPreview[static_cast<int>(p.state)]);
    set_number(L, "generation", static_cast<double>(p.generation));
    set_string(L, "name", p.name);
    set_string(L, "archives", p.archives);
    set_string(L, "stored", p.stored);
    set_string(L, "error", p.error);
    set_int(L, "width", p.width);
    set_int(L, "height", p.height);
    set_int(L, "scale", p.scale);
    set_number(L, "seconds", p.seconds);
    set_bool(L, "data", p.data);
    set_string(L, "source", p.source.string());
    set_string(L, "color", p.color.string());
    set_string(L, "normal", p.normal.string());
    set_string(L, "rough", p.rough.string());
    lua_setfield(L, -2, "preview");
    set_string(L, "version", c.version);
    return 1;
}

// revision, groups = studio.Groups(): { key, title, textures, selected } in the order shown (shared first).
int Groups(lua_State* L) {
    Studio& s = *ctx(L).studio;
    lua_pushinteger(L, static_cast<lua_Integer>(s.groups_revision()));
    lua_newtable(L);
    for (std::size_t i = 0; i < s.groups().size(); ++i) {
        const Group& g = s.groups()[i];
        lua_newtable(L);
        set_string(L, "key", g.key);
        set_string(L, "title", g.title);
        set_int(L, "textures", static_cast<lua_Integer>(g.textures));
        set_bool(L, "selected", s.group_selected(g.key));
        lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
    }
    return 2;
}

// studio.Set(key, value): one of the window's choices.
int Set(lua_State* L) {
    Studio& s = *ctx(L).studio;
    const std::string key = luaL_checkstring(L, 1);
    if (key == "assetsPath") s.set_assets_path(arg_string(L, 2));
    else if (key == "includeServers") s.set_include_servers(lua_toboolean(L, 2) != 0);
    else if (key == "device") s.set_device(static_cast<int>(luaL_checkinteger(L, 2)) - 1);
    else if (key == "preset") s.set_preset(arg_string(L, 2));
    else if (key == "colorModel") s.set_color_model(arg_string(L, 2));
    else if (key == "scale") s.set_scale(static_cast<std::uint32_t>(std::clamp<lua_Integer>(luaL_checkinteger(L, 2), 1, 4)));
    else if (key == "maps") s.set_maps(lua_toboolean(L, 2) != 0);
    else if (key == "buildAll") s.set_build_all(lua_toboolean(L, 2) != 0);
    else if (key == "packName") s.set_pack_name(arg_string(L, 2));
    else if (key == "lowImpact") s.set_low_impact(lua_toboolean(L, 2) != 0);
    else if (key == "noGroups") s.select_no_groups();
    else if (key.starts_with("group:")) s.set_group_selected(key.substr(6), lua_toboolean(L, 2) != 0);
    else return luaL_error(L, "studio.Set: unknown key '%s'", key.c_str());
    return 0;
}

int Rescan(lua_State* L) {
    ctx(L).studio->rescan(lua_toboolean(L, 1) != 0);
    return 0;
}

// ok, why = studio.Start()
int Start(lua_State* L) {
    std::string why;
    const bool ok = ctx(L).studio->start(&why);
    lua_pushboolean(L, ok);
    lua_pushstring(L, why.c_str());
    return 2;
}
int Pause(lua_State* L) {
    ctx(L).studio->pause();
    return 0;
}
int Resume(lua_State* L) {
    ctx(L).studio->resume();
    return 0;
}
int Stop(lua_State* L) {
    ctx(L).studio->stop();
    return 0;
}
int Measure(lua_State* L) {
    ctx(L).studio->measure();
    return 0;
}
// ok = studio.UsePack(name): hailstone.ini display.texture_folder ("off": none).
int UsePack(lua_State* L) {
    lua_pushboolean(L, ctx(L).studio->use_pack(arg_string(L, 1)));
    return 1;
}
int ClearPack(lua_State* L) {
    lua_pushboolean(L, ctx(L).studio->clear_pack());
    return 1;
}
// studio.OpenFolder(which): "pack", "textures", "models" or "logs" in the system's file manager.
int OpenFolder(lua_State* L) {
    Studio& s = *ctx(L).studio;
    const std::string which = arg_string(L, 1);
    std::filesystem::path p;
    if (which == "pack") p = s.pack_dir();
    else if (which == "textures") p = platform::textures_dir();
    else if (which == "models") p = s.data_dir() / "texstudio" / "models";
    else if (which == "logs") p = platform::logs_dir();
    else return luaL_error(L, "studio.OpenFolder: unknown folder '%s'", which.c_str());
    std::error_code ec;
    std::filesystem::create_directories(p, ec);
    lua_pushboolean(L, platform::open_folder(p));
    return 1;
}
// studio.Browse(): the system's folder picker for the asset folder; its answer is applied when it comes.
int Browse(lua_State* L) {
    StudioLuaContext& c = ctx(L);
    if (c.folder_dialog && c.window) c.folder_dialog->open(*c.window, c.studio->assets_path());
    return 0;
}
// ok = studio.Preview(query): "" a random picture, else the next texture whose name holds query.
int Preview(lua_State* L) {
    lua_pushboolean(L, ctx(L).studio->preview_texture(arg_string(L, 1)));
    return 1;
}
// texture, w, h = studio.Picture(path): a PNG of the preview as an ImGui texture (nil when it cannot be read).
int Picture(lua_State* L) {
    StudioLuaContext& c = ctx(L);
    const std::string path = arg_string(L, 1);
    if (path.empty() || !c.textures) return 0;
    const ui::UiTextures::Texture* t = c.textures->picture(path);
    if (!t) return 0;
    lua_pushlightuserdata(L, t->id);
    lua_pushinteger(L, t->width);
    lua_pushinteger(L, t->height);
    return 3;
}

} // namespace

void register_studio_api(lua_State* L, StudioLuaContext* context) {
    static const luaL_Reg fns[] = {
        {"State", State},   {"Groups", Groups},       {"Set", Set},         {"Rescan", Rescan},
        {"Start", Start},   {"Pause", Pause},         {"Resume", Resume},   {"Stop", Stop},
        {"Measure", Measure}, {"UsePack", UsePack},   {"ClearPack", ClearPack}, {"OpenFolder", OpenFolder},
        {"Browse", Browse}, {"Preview", Preview},     {"Picture", Picture}, {nullptr, nullptr},
    };
    lua_newtable(L);
    lua_pushlightuserdata(L, context);
    luaL_setfuncs(L, fns, 1);
    lua_setglobal(L, "studio");
}

} // namespace hailstone::texstudio
