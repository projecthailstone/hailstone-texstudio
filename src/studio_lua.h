#pragma once

#include <string>

#include "studio.h"

struct lua_State;

namespace hailstone::platform {
class FolderDialog;
class Window;
} // namespace hailstone::platform
namespace hailstone::ui { class UiTextures; }

namespace hailstone::texstudio {

// What the `studio` functions reach; owned by main, outliving the Lua state.
struct StudioLuaContext {
    Studio* studio = nullptr;
    ui::UiTextures* textures = nullptr;
    platform::FolderDialog* folder_dialog = nullptr;
    platform::Window* window = nullptr;
    std::string version;
};

// The global `studio` table (studio_lua.cpp).
void register_studio_api(lua_State* L, StudioLuaContext* context);

} // namespace hailstone::texstudio
