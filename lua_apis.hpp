#include "twitch_api.hpp"
#include <lua.hpp>

namespace LuaTwitchApi {
    inline int set_category(lua_State* L) {
        if (lua_gettop(L) != 2) {lua_pushliteral(L, "set_category; Expected 2 values"); lua_error(L); return 1;}
        TwitchApi* api = static_cast<TwitchApi*>(lua_touserdata(L, lua_upvalueindex(1)));
        std::string room_id = luaL_checkstring(L, 1);
        std::string category = luaL_checkstring(L, 2);
        std::string category_id = api->get_category_id(category);
        if (category_id.empty()) {lua_pushliteral(L, "Category id not found"); return 1;}
        api->set_stream(room_id, category_id, "");
        lua_pushliteral(L, ""); return 1;
    }
    inline int set_title(lua_State* L) {
        if (lua_gettop(L) != 2) {lua_pushliteral(L, "set_title; Expected 2 values"); lua_error(L); return 1;}
        TwitchApi* api = static_cast<TwitchApi*>(lua_touserdata(L, lua_upvalueindex(1)));
        std::string room_id = luaL_checkstring(L, 1);
        std::string title = luaL_checkstring(L, 2);
        api->set_stream(room_id, "", title);
        lua_pushliteral(L, ""); return 1;
    }
    inline constexpr luaL_Reg functions[] = {
        {"set_category", set_category},
        {"set_title", set_title},
        {nullptr, nullptr}
    };
    constexpr int func_count = std::size(functions)-1;
};
