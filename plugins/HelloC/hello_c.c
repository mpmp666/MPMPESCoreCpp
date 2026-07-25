#include "mpmpes/plugin/PluginApi.h"

#include <stdio.h>
#include <string.h>

static const MpmpesHostFns* g_host = NULL;

MpmpesPluginInfo mpmpes_plugin_info(void) {
  MpmpesPluginInfo info;
  info.name = "HelloC";
  info.version = "0.1.0";
  info.api_version = MPMPES_PLUGIN_API_VERSION;
  return info;
}

int mpmpes_plugin_init(const MpmpesHostFns* host) {
  g_host = host;
  if (g_host && g_host->log_info) {
    g_host->log_info("HelloC: init OK");
  }
  return 0;
}

void mpmpes_plugin_shutdown(void) {
  if (g_host && g_host->log_info) {
    g_host->log_info("HelloC: shutdown");
  }
  g_host = NULL;
}

void mpmpes_on_server_start(const MpmpesEventServerStart* ev) {
  if (!g_host || !g_host->log_info || !ev) return;
  char buf[256];
  snprintf(buf, sizeof(buf), "HelloC: server_start motd=%s port=%u",
           ev->motd ? ev->motd : "?", (unsigned)ev->port);
  g_host->log_info(buf);
}

void mpmpes_on_session_open(const MpmpesEventSessionOpen* ev) {
  if (!g_host || !g_host->log_info || !ev) return;
  char buf[256];
  snprintf(buf, sizeof(buf), "HelloC: session_open %s:%u id=%lld",
           ev->address ? ev->address : "?", (unsigned)ev->port,
           (long long)ev->client_id);
  g_host->log_info(buf);
}

void mpmpes_on_session_close(const MpmpesEventSessionClose* ev) {
  if (!g_host || !g_host->log_info || !ev) return;
  char buf[256];
  snprintf(buf, sizeof(buf), "HelloC: session_close %s:%u (%s)",
           ev->address ? ev->address : "?", (unsigned)ev->port,
           ev->reason ? ev->reason : "");
  g_host->log_info(buf);
}

void mpmpes_on_player_login(const MpmpesEventPlayerLogin* ev) {
  if (!g_host || !g_host->log_info || !ev) return;
  char buf[256];
  snprintf(buf, sizeof(buf), "HelloC: player_login %s protocol=%d world=%s",
           ev->username ? ev->username : "?", (int)ev->protocol,
           ev->world ? ev->world : "?");
  g_host->log_info(buf);
}

void mpmpes_on_player_join(const MpmpesEventPlayerJoin* ev) {
  if (!g_host || !g_host->log_info || !ev) return;
  char buf[256];
  snprintf(buf, sizeof(buf), "HelloC: player_join %s @ %s (%.1f,%.1f,%.1f)",
           ev->username ? ev->username : "?", ev->world ? ev->world : "?",
           (double)ev->x, (double)ev->y, (double)ev->z);
  g_host->log_info(buf);
  if (g_host->broadcast) {
    char b2[128];
    snprintf(b2, sizeof(b2), "[+]%s joined (HelloC)",
             ev->username ? ev->username : "?");
    g_host->broadcast(b2);
  }
  /* API v3: welcome DM */
  if (g_host->send_message && ev->username) {
    g_host->send_message(ev->username, "Welcome! (HelloC API v3)");
  }
}

void mpmpes_on_player_quit(const MpmpesEventPlayerQuit* ev) {
  if (!g_host || !g_host->log_info || !ev) return;
  char buf[256];
  snprintf(buf, sizeof(buf), "HelloC: player_quit %s (%s)",
           ev->username ? ev->username : "?", ev->reason ? ev->reason : "");
  g_host->log_info(buf);
}

void mpmpes_on_chat(MpmpesEventChat* ev) {
  if (!g_host || !g_host->log_info || !ev) return;
  char buf[256];
  snprintf(buf, sizeof(buf), "HelloC: chat <%s> %s",
           ev->username ? ev->username : "?", ev->message ? ev->message : "");
  g_host->log_info(buf);
}

void mpmpes_on_command(MpmpesEventCommand* ev) {
  if (!g_host || !g_host->log_info || !ev) return;
  char buf[256];
  snprintf(buf, sizeof(buf), "HelloC: command /%s %s",
           ev->command ? ev->command : "?", ev->args ? ev->args : "");
  g_host->log_info(buf);
}

void mpmpes_on_block(const MpmpesEventBlock* ev) {
  /* Example only — do not log every dig/place (spams console / panel). */
  (void)ev;
}

void mpmpes_on_world_load(const MpmpesEventWorldLoad* ev) {
  if (!g_host || !g_host->log_info || !ev) return;
  char buf[256];
  snprintf(buf, sizeof(buf), "HelloC: world_load %s gen=%d seed=%d",
           ev->name ? ev->name : "?", (int)ev->generator, (int)ev->seed);
  g_host->log_info(buf);
}

void mpmpes_on_sign_change(MpmpesEventSignChange* ev) {
  if (!g_host || !g_host->log_info || !ev) return;
  char buf[512];
  snprintf(buf, sizeof(buf), "HelloC: sign_change %s @ %d,%d,%d |%s|%s|%s|%s|",
           ev->username ? ev->username : "?", (int)ev->x, (int)ev->y, (int)ev->z,
           ev->line1 ? ev->line1 : "", ev->line2 ? ev->line2 : "",
           ev->line3 ? ev->line3 : "", ev->line4 ? ev->line4 : "");
  g_host->log_info(buf);
  /* Example: prefix first line with [C] if empty rewrite not needed */
  (void)ev;
}
