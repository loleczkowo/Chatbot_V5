#include "twitch_api.hpp"
#include "types.hpp"
#include <lua.hpp>

namespace LuaTwitchApi {
    struct LuaTwitchApiData {
        TwitchApi& api;
        ChannelContext& channel_context;
        std::string room_id;
    };
    inline int set_category(lua_State* L) {
        if (lua_gettop(L) != 1) {lua_pushliteral(L, "set_category; Expected 1 value"); lua_error(L); return 1;}
        LuaTwitchApiData* data = static_cast<LuaTwitchApiData*>(lua_touserdata(L, lua_upvalueindex(1)));
        std::string category = luaL_checkstring(L, 1);
        std::string category_id = data->api.get_category_id(category);
        if (category_id.empty()) {lua_pushliteral(L, "Category id not found"); return 1;}
        data->api.set_stream(data->room_id, category_id, "");
        lua_pushliteral(L, ""); return 1;
    }
    inline int set_title(lua_State* L) {
        if (lua_gettop(L) != 1) {lua_pushliteral(L, "set_title; Expected 1 value"); lua_error(L); return 1;}
        LuaTwitchApiData* data = static_cast<LuaTwitchApiData*>(lua_touserdata(L, lua_upvalueindex(1)));
        std::string title = luaL_checkstring(L, 1);
        data->api.set_stream(data->room_id, "", title);
        lua_pushliteral(L, ""); return 1;
    }
    inline int run_ads(lua_State* L) {
        if (lua_gettop(L) != 1) {lua_pushliteral(L, "run_ads; Expected 1 value"); lua_error(L); return 1;}
        if (!lua_isinteger(L, 1)) {lua_pushliteral(L, "run_ads; Expected \"length\" to be an int"); return 1;}
        LuaTwitchApiData* data = static_cast<LuaTwitchApiData*>(lua_touserdata(L, lua_upvalueindex(1)));
        unsigned int length = lua_tointeger(L, 1);
        lua_pushboolean(L, data->api.run_ads(data->room_id, length, data->channel_context)); return 1;
    }
    inline constexpr luaL_Reg functions[] = {
        {"set_category", set_category},
        {"set_title", set_title},
        {"run_ads", run_ads},
        {nullptr, nullptr}
    };
    constexpr int func_count = std::size(functions)-1;
};
