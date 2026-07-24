#include "mpmpes/plugin/PluginApi.h"

#include <cstdio>
#include <string>

static const MpmpesHostFns* g_host = nullptr;

extern "C" {

MpmpesPluginInfo mpmpes_plugin_info(void) {
  return MpmpesPluginInfo{"HelloCpp", "0.1.0", MPMPES_PLUGIN_API_VERSION};
}

int mpmpes_plugin_init(const MpmpesHostFns* host) {
  g_host = host;
  if (g_host && g_host->log_info) g_host->log_info("HelloCpp: init OK");
  return 0;
}

void mpmpes_plugin_shutdown(void) {
  if (g_host && g_host->log_info) g_host->log_info("HelloCpp: shutdown");
  g_host = nullptr;
}

void mpmpes_on_server_start(const MpmpesEventServerStart* ev) {
  if (!g_host || !g_host->log_info || !ev) return;
  char buf[256];
  std::snprintf(buf, sizeof(buf), "HelloCpp: server_start motd=%s port=%u",
                ev->motd ? ev->motd : "?", static_cast<unsigned>(ev->port));
  g_host->log_info(buf);
}

void mpmpes_on_session_open(const MpmpesEventSessionOpen* ev) {
  if (!g_host || !g_host->log_info || !ev) return;
  char buf[256];
  std::snprintf(buf, sizeof(buf), "HelloCpp: session_open %s:%u",
                ev->address ? ev->address : "?", static_cast<unsigned>(ev->port));
  g_host->log_info(buf);
}

void mpmpes_on_session_close(const MpmpesEventSessionClose*) {}

void mpmpes_on_player_login(const MpmpesEventPlayerLogin* ev) {
  if (!g_host || !g_host->log_info || !ev) return;
  char buf[256];
  std::snprintf(buf, sizeof(buf), "HelloCpp: player_login %s world=%s",
                ev->username ? ev->username : "?",
                ev->world ? ev->world : "?");
  g_host->log_info(buf);
}

void mpmpes_on_player_join(const MpmpesEventPlayerJoin* ev) {
  if (!g_host || !g_host->log_info || !ev) return;
  char buf[256];
  std::snprintf(buf, sizeof(buf), "HelloCpp: player_join %s @ %s",
                ev->username ? ev->username : "?", ev->world ? ev->world : "?");
  g_host->log_info(buf);
}

void mpmpes_on_player_quit(const MpmpesEventPlayerQuit* ev) {
  if (!g_host || !g_host->log_info || !ev) return;
  char buf[128];
  std::snprintf(buf, sizeof(buf), "HelloCpp: player_quit %s",
                ev->username ? ev->username : "?");
  g_host->log_info(buf);
}

void mpmpes_on_world_load(const MpmpesEventWorldLoad* ev) {
  if (!g_host || !g_host->log_info || !ev) return;
  char buf[256];
  std::snprintf(buf, sizeof(buf), "HelloCpp: world_load %s gen=%d",
                ev->name ? ev->name : "?", static_cast<int>(ev->generator));
  g_host->log_info(buf);
}

} // extern "C"
