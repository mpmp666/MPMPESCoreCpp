/* C ABI for native (C/C++ .so) plugins.
 * Keep this header pure C so all languages can FFI.
 * API v3: richer host callbacks + sign_change / player_chat_message helpers.
 */
#ifndef MPMPES_PLUGIN_API_H
#define MPMPES_PLUGIN_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MPMPES_PLUGIN_API_VERSION 3

typedef struct MpmpesPluginHost MpmpesPluginHost;

typedef struct MpmpesPluginInfo {
  const char* name;
  const char* version;
  int api_version;
} MpmpesPluginInfo;

/* Host callbacks provided to plugin on init.
 * New fields may be NULL on older hosts; check before call.
 * Plugins compiled against API <= host API version still load.
 */
typedef struct MpmpesHostFns {
  void (*log_info)(const char* msg);
  void (*log_warn)(const char* msg);
  void (*log_error)(const char* msg);
  /* Broadcast a chat-like message to all players + log */
  void (*broadcast)(const char* msg);
  void* user_data;

  /* --- API v3 host helpers (optional; may be NULL) --- */
  /* Send system text to one online player by exact username. Returns 1 if sent. */
  int (*send_message)(const char* username, const char* msg);
  /* Online player count */
  int (*player_count)(void);
  /* Get player position/world; returns 1 if found. world_out must be >= world_out_len. */
  int (*get_player_pos)(const char* username, float* x, float* y, float* z, char* world_out,
                        int world_out_len);
  /* Set block in a world by name (default world if world is NULL/empty). Returns 1 on ok. */
  int (*set_block)(const char* world, int32_t x, int32_t y, int32_t z, int32_t id, int32_t meta);
  /* Get block id; returns -1 if world missing / OOB */
  int (*get_block)(const char* world, int32_t x, int32_t y, int32_t z);
  /* Set sign text (all 4 lines). Returns 1 if sign tile exists or was created at sign block. */
  int (*set_sign_text)(const char* world, int32_t x, int32_t y, int32_t z, const char* t1,
                       const char* t2, const char* t3, const char* t4);
  /* Kick player by name with reason. Returns 1 if found. */
  int (*kick_player)(const char* username, const char* reason);
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
  int32_t action; /* 0 start break, 1 abort, 2 stop/break, 100 place */
  int32_t face;
} MpmpesEventBlock;

typedef struct MpmpesEventWorldLoad {
  const char* name;
  int32_t generator; /* 0 flat 1 void 2 normal */
  int32_t seed;
} MpmpesEventWorldLoad;

/* Sign edit (BlockEntityData) — plugins may rewrite lines or cancel */
typedef struct MpmpesEventSignChange {
  const char* username;
  int32_t x, y, z;
  const char* text1;
  const char* text2;
  const char* text3;
  const char* text4;
  /* plugin may set cancelled; host re-reads text* only if plugin rewrote via
   * mutable buffers — see PluginManager which uses writable char arrays */
  int cancelled;
  /* writable line buffers (API v3): host provides non-const storage; max 256 each */
  char* line1;
  char* line2;
  char* line3;
  char* line4;
  int line_cap; /* capacity of each line buffer */
} MpmpesEventSignChange;

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
void mpmpes_on_sign_change(MpmpesEventSignChange* ev);

#ifdef __cplusplus
}
#endif

#endif /* MPMPES_PLUGIN_API_H */
