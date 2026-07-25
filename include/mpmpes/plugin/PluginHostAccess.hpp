#pragma once

/* Runtime hooks filled by Server so native plugins can call host helpers
 * without PluginManager depending on Server.hpp. */

namespace mpmpes::plugin {

struct PluginHostAccess {
  void* ctx = nullptr; // Server*
  void (*broadcast)(void* ctx, const char* msg) = nullptr;
  int (*send_message)(void* ctx, const char* username, const char* msg) = nullptr;
  int (*player_count)(void* ctx) = nullptr;
  int (*get_player_pos)(void* ctx, const char* username, float* x, float* y, float* z,
                        char* world_out, int world_out_len) = nullptr;
  int (*set_block)(void* ctx, const char* world, int x, int y, int z, int id, int meta) = nullptr;
  int (*get_block)(void* ctx, const char* world, int x, int y, int z) = nullptr;
  int (*set_sign_text)(void* ctx, const char* world, int x, int y, int z, const char* t1,
                       const char* t2, const char* t3, const char* t4) = nullptr;
  int (*kick_player)(void* ctx, const char* username, const char* reason) = nullptr;
};

void setPluginHostAccess(const PluginHostAccess& access);
const PluginHostAccess& pluginHostAccess();

} // namespace mpmpes::plugin
