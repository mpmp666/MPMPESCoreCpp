/* C ABI for native (C/C++/Rust/Go c-shared) plugins.
 * Keep this header pure C so all languages can FFI.
 */
#ifndef MPMPES_PLUGIN_API_H
#define MPMPES_PLUGIN_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MPMPES_PLUGIN_API_VERSION 2

typedef struct MpmpesPluginHost MpmpesPluginHost;

typedef struct MpmpesPluginInfo {
  const char* name;
  const char* version;
  int api_version;
} MpmpesPluginInfo;

/* Host callbacks provided to plugin on init */
typedef struct MpmpesHostFns {
  void (*log_info)(const char* msg);
  void (*log_warn)(const char* msg);
  void (*log_error)(const char* msg);
  /* Broadcast a chat-like message to all players + log */
  void (*broadcast)(const char* msg);
  void* user_data;
} MpmpesHostFns;

typedef struct MpmpesEventSessionOpen {
  const char* address;
  uint16_t port;
  int64_t client_id;
} MpmpesEventSessionOpen;

typedef struct MpmpesEventSessionClose {
  const char* address;
  uint16_t port;
  const char* reason;
} MpmpesEventSessionClose;

typedef struct MpmpesEventPlayerLogin {
  const char* address;
  uint16_t port;
  const char* username;
  int32_t protocol;
  int64_t client_id;
  const char* world;
} MpmpesEventPlayerLogin;

typedef struct MpmpesEventServerStart {
  const char* motd;
  uint16_t port;
} MpmpesEventServerStart;

typedef struct MpmpesEventPlayerJoin {
  const char* username;
  const char* world;
  float x, y, z;
} MpmpesEventPlayerJoin;

typedef struct MpmpesEventPlayerQuit {
  const char* username;
  const char* reason;
} MpmpesEventPlayerQuit;

typedef struct MpmpesEventChat {
  const char* username;
  const char* message;
  int cancelled; /* plugins may set non-zero to cancel */
} MpmpesEventChat;

typedef struct MpmpesEventCommand {
  const char* username;
  const char* command; /* without leading / */
  const char* args;
  int handled; /* set non-zero if plugin handled */
} MpmpesEventCommand;

typedef struct MpmpesEventMove {
  const char* username;
  float x, y, z;
  float yaw, pitch;
} MpmpesEventMove;

typedef struct MpmpesEventBlock {
  const char* username;
  int32_t x, y, z;
  int32_t action; /* 0 start break, 1 abort, 2 stop/break */
  int32_t face;
} MpmpesEventBlock;

typedef struct MpmpesEventWorldLoad {
  const char* name;
  int32_t generator; /* 0 flat 1 void 2 normal */
  int32_t seed;
} MpmpesEventWorldLoad;

/* Required exports from each .so/.dll */
MpmpesPluginInfo mpmpes_plugin_info(void);
int mpmpes_plugin_init(const MpmpesHostFns* host);
void mpmpes_plugin_shutdown(void);

/* Optional event handlers — if missing, host skips */
void mpmpes_on_server_start(const MpmpesEventServerStart* ev);
void mpmpes_on_session_open(const MpmpesEventSessionOpen* ev);
void mpmpes_on_session_close(const MpmpesEventSessionClose* ev);
void mpmpes_on_player_login(const MpmpesEventPlayerLogin* ev);
void mpmpes_on_player_join(const MpmpesEventPlayerJoin* ev);
void mpmpes_on_player_quit(const MpmpesEventPlayerQuit* ev);
/* may set cancelled/handled */
void mpmpes_on_chat(MpmpesEventChat* ev);
void mpmpes_on_command(MpmpesEventCommand* ev);
void mpmpes_on_move(const MpmpesEventMove* ev);
void mpmpes_on_block(const MpmpesEventBlock* ev);
void mpmpes_on_world_load(const MpmpesEventWorldLoad* ev);

#ifdef __cplusplus
}
#endif

#endif /* MPMPES_PLUGIN_API_H */
