#include "mpmpes/plugin/PluginManager.hpp"

#include "mpmpes/util/Logger.hpp"

#include <cerrno>
#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <sstream>

namespace mpmpes::plugin {
namespace fs = std::filesystem;
namespace {
using mpmpes::util::Logger;

std::string jsonEscape(std::string_view s) {
  std::string o;
  o.reserve(s.size() + 8);
  for (unsigned char c : s) {
    switch (c) {
      case '"': o += "\\\""; break;
      case '\\': o += "\\\\"; break;
      case '\n': o += "\\n"; break;
      case '\r': o += "\\r"; break;
      case '\t': o += "\\t"; break;
      default:
        if (c < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", c);
          o += buf;
        } else {
          o.push_back(static_cast<char>(c));
        }
    }
  }
  return o;
}

std::string jsonGetString(std::string_view json, std::string_view key) {
  std::string pat = "\"";
  pat += key;
  pat += "\"";
  auto p = json.find(pat);
  if (p == std::string_view::npos) return {};
  p = json.find(':', p + pat.size());
  if (p == std::string_view::npos) return {};
  p = json.find('"', p + 1);
  if (p == std::string_view::npos) return {};
  ++p;
  std::string out;
  while (p < json.size()) {
    char c = json[p++];
    if (c == '\\' && p < json.size()) {
      char n = json[p++];
      if (n == 'n') out.push_back('\n');
      else if (n == 'r') out.push_back('\r');
      else if (n == 't') out.push_back('\t');
      else out.push_back(n);
    } else if (c == '"') {
      break;
    } else {
      out.push_back(c);
    }
  }
  return out;
}

bool jsonGetBool(std::string_view json, std::string_view key) {
  std::string pat = "\"";
  pat += key;
  pat += "\"";
  auto p = json.find(pat);
  if (p == std::string_view::npos) return false;
  p = json.find(':', p + pat.size());
  if (p == std::string_view::npos) return false;
  auto rest = json.substr(p + 1);
  while (!rest.empty() && (rest[0] == ' ' || rest[0] == '\t')) rest.remove_prefix(1);
  return rest.starts_with("true") || rest.starts_with("1");
}

void hostLogInfo(const char* msg) {
  Logger::instance().info("[plugin] ", msg ? msg : "");
}
void hostLogWarn(const char* msg) {
  Logger::instance().warning("[plugin] ", msg ? msg : "");
}
void hostLogError(const char* msg) {
  Logger::instance().error("[plugin] ", msg ? msg : "");
}
void hostBroadcast(const char* msg) {
  Logger::instance().notice("[broadcast] ", msg ? msg : "");
}

MpmpesHostFns makeHostFns() {
  MpmpesHostFns h{};
  h.log_info = hostLogInfo;
  h.log_warn = hostLogWarn;
  h.log_error = hostLogError;
  h.broadcast = hostBroadcast;
  h.user_data = nullptr;
  return h;
}

class NativePlugin : public IPlugin {
public:
  explicit NativePlugin(config::PluginManifest m) : manifest_(std::move(m)) {}
  ~NativePlugin() override { disable(); }

  const config::PluginManifest& manifest() const override { return manifest_; }

  bool enable(std::string& error) override {
    if (handle_) return true;
    const auto path = (fs::path(manifest_.base_dir) / manifest_.main).string();
    handle_ = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle_) {
      error = std::string("dlopen failed: ") + (dlerror() ? dlerror() : "?");
      return false;
    }
    auto* info_fn = reinterpret_cast<MpmpesPluginInfo (*)()>(dlsym(handle_, "mpmpes_plugin_info"));
    auto* init_fn = reinterpret_cast<int (*)(const MpmpesHostFns*)>(dlsym(handle_, "mpmpes_plugin_init"));
    shutdown_ = reinterpret_cast<void (*)()>(dlsym(handle_, "mpmpes_plugin_shutdown"));
    on_start_ = reinterpret_cast<void (*)(const MpmpesEventServerStart*)>(
        dlsym(handle_, "mpmpes_on_server_start"));
    on_open_ = reinterpret_cast<void (*)(const MpmpesEventSessionOpen*)>(
        dlsym(handle_, "mpmpes_on_session_open"));
    on_close_ = reinterpret_cast<void (*)(const MpmpesEventSessionClose*)>(
        dlsym(handle_, "mpmpes_on_session_close"));
    on_login_ = reinterpret_cast<void (*)(const MpmpesEventPlayerLogin*)>(
        dlsym(handle_, "mpmpes_on_player_login"));
    on_join_ = reinterpret_cast<void (*)(const MpmpesEventPlayerJoin*)>(
        dlsym(handle_, "mpmpes_on_player_join"));
    on_quit_ = reinterpret_cast<void (*)(const MpmpesEventPlayerQuit*)>(
        dlsym(handle_, "mpmpes_on_player_quit"));
    on_chat_ = reinterpret_cast<void (*)(MpmpesEventChat*)>(dlsym(handle_, "mpmpes_on_chat"));
    on_command_ =
        reinterpret_cast<void (*)(MpmpesEventCommand*)>(dlsym(handle_, "mpmpes_on_command"));
    on_move_ = reinterpret_cast<void (*)(const MpmpesEventMove*)>(dlsym(handle_, "mpmpes_on_move"));
    on_block_ =
        reinterpret_cast<void (*)(const MpmpesEventBlock*)>(dlsym(handle_, "mpmpes_on_block"));
    on_world_ = reinterpret_cast<void (*)(const MpmpesEventWorldLoad*)>(
        dlsym(handle_, "mpmpes_on_world_load"));
    if (!info_fn || !init_fn) {
      error = "missing mpmpes_plugin_info/init";
      dlclose(handle_);
      handle_ = nullptr;
      return false;
    }
    auto info = info_fn();
    if (info.api_version > MPMPES_PLUGIN_API_VERSION) {
      error = "plugin API too new";
      dlclose(handle_);
      handle_ = nullptr;
      return false;
    }
    host_ = makeHostFns();
    if (init_fn(&host_) != 0) {
      error = "mpmpes_plugin_init failed";
      dlclose(handle_);
      handle_ = nullptr;
      return false;
    }
    enabled_ = true;
    Logger::instance().info("Native plugin enabled: ", manifest_.name, " v", manifest_.version,
                            " (", path, ")");
    return true;
  }

  void disable() override {
    if (!handle_) return;
    if (enabled_ && shutdown_) {
      try {
        shutdown_();
      } catch (...) {
      }
    }
    dlclose(handle_);
    handle_ = nullptr;
    enabled_ = false;
    shutdown_ = nullptr;
    on_start_ = nullptr;
    on_open_ = nullptr;
    on_close_ = nullptr;
    on_login_ = nullptr;
    on_join_ = nullptr;
    on_quit_ = nullptr;
    on_chat_ = nullptr;
    on_command_ = nullptr;
    on_move_ = nullptr;
    on_block_ = nullptr;
    on_world_ = nullptr;
  }

  void onServerStart(const ServerStartEvent& ev) override {
    if (!on_start_) return;
    MpmpesEventServerStart e{ev.motd.c_str(), ev.port};
    on_start_(&e);
  }
  void onSessionOpen(const SessionOpenEvent& ev) override {
    if (!on_open_) return;
    MpmpesEventSessionOpen e{ev.address.c_str(), ev.port, ev.client_id};
    on_open_(&e);
  }
  void onSessionClose(const SessionCloseEvent& ev) override {
    if (!on_close_) return;
    MpmpesEventSessionClose e{ev.address.c_str(), ev.port, ev.reason.c_str()};
    on_close_(&e);
  }
  void onPlayerLogin(const PlayerLoginEvent& ev) override {
    if (!on_login_) return;
    MpmpesEventPlayerLogin e{ev.address.c_str(), ev.port, ev.username.c_str(), ev.protocol,
                             ev.client_id, ev.world.c_str()};
    on_login_(&e);
  }
  void onPlayerJoin(const PlayerJoinEvent& ev) override {
    if (!on_join_) return;
    MpmpesEventPlayerJoin e{ev.username.c_str(), ev.world.c_str(), ev.x, ev.y, ev.z};
    on_join_(&e);
  }
  void onPlayerQuit(const PlayerQuitEvent& ev) override {
    if (!on_quit_) return;
    MpmpesEventPlayerQuit e{ev.username.c_str(), ev.reason.c_str()};
    on_quit_(&e);
  }
  void onChat(ChatEvent& ev) override {
    if (!on_chat_) return;
    MpmpesEventChat e{ev.username.c_str(), ev.message.c_str(), ev.cancelled ? 1 : 0};
    on_chat_(&e);
    if (e.cancelled) ev.cancelled = true;
  }
  void onCommand(CommandEvent& ev) override {
    if (!on_command_) return;
    MpmpesEventCommand e{ev.username.c_str(), ev.command.c_str(), ev.args.c_str(),
                         ev.handled ? 1 : 0};
    on_command_(&e);
    if (e.handled) ev.handled = true;
  }
  void onMove(const MoveEvent& ev) override {
    if (!on_move_) return;
    MpmpesEventMove e{ev.username.c_str(), ev.x, ev.y, ev.z, ev.yaw, ev.pitch};
    on_move_(&e);
  }
  void onBlock(const BlockEvent& ev) override {
    if (!on_block_) return;
    MpmpesEventBlock e{ev.username.c_str(), ev.x, ev.y, ev.z, ev.action, ev.face};
    on_block_(&e);
  }
  void onWorldLoad(const WorldLoadEvent& ev) override {
    if (!on_world_) return;
    MpmpesEventWorldLoad e{ev.name.c_str(), ev.generator, ev.seed};
    on_world_(&e);
  }

private:
  config::PluginManifest manifest_;
  void* handle_ = nullptr;
  bool enabled_ = false;
  MpmpesHostFns host_{};
  void (*shutdown_)() = nullptr;
  void (*on_start_)(const MpmpesEventServerStart*) = nullptr;
  void (*on_open_)(const MpmpesEventSessionOpen*) = nullptr;
  void (*on_close_)(const MpmpesEventSessionClose*) = nullptr;
  void (*on_login_)(const MpmpesEventPlayerLogin*) = nullptr;
  void (*on_join_)(const MpmpesEventPlayerJoin*) = nullptr;
  void (*on_quit_)(const MpmpesEventPlayerQuit*) = nullptr;
  void (*on_chat_)(MpmpesEventChat*) = nullptr;
  void (*on_command_)(MpmpesEventCommand*) = nullptr;
  void (*on_move_)(const MpmpesEventMove*) = nullptr;
  void (*on_block_)(const MpmpesEventBlock*) = nullptr;
  void (*on_world_)(const MpmpesEventWorldLoad*) = nullptr;
};

class ProcessPlugin : public IPlugin {
public:
  explicit ProcessPlugin(config::PluginManifest m, std::vector<std::string> argv)
      : manifest_(std::move(m)), argv_(std::move(argv)) {}
  ~ProcessPlugin() override { disable(); }

  const config::PluginManifest& manifest() const override { return manifest_; }

  bool enable(std::string& error) override {
    if (pid_ > 0) return true;
    int to_child[2];
    int from_child[2];
    if (pipe(to_child) != 0 || pipe(from_child) != 0) {
      error = "pipe failed";
      return false;
    }
    pid_t pid = fork();
    if (pid < 0) {
      error = "fork failed";
      close(to_child[0]);
      close(to_child[1]);
      close(from_child[0]);
      close(from_child[1]);
      return false;
    }
    if (pid == 0) {
      dup2(to_child[0], STDIN_FILENO);
      dup2(from_child[1], STDOUT_FILENO);
      close(to_child[0]);
      close(to_child[1]);
      close(from_child[0]);
      close(from_child[1]);
      std::vector<char*> args;
      for (auto& s : argv_) args.push_back(s.data());
      args.push_back(nullptr);
      if (chdir(manifest_.base_dir.c_str()) != 0) {
        std::perror("chdir");
      }
      execvp(args[0], args.data());
      std::fprintf(stderr, "execvp failed for '%s': %s\n", args[0], std::strerror(errno));
      _exit(127);
    }
    close(to_child[0]);
    close(from_child[1]);
    pid_ = pid;
    wfd_ = to_child[1];
    rfd_ = from_child[0];
    int flags = fcntl(rfd_, F_GETFL, 0);
    fcntl(rfd_, F_SETFL, flags | O_NONBLOCK);

    if (!sendLine(R"({"op":"init","api":2})")) {
      error = "failed to send init";
      disable();
      return false;
    }
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    bool ok = false;
    while (std::chrono::steady_clock::now() < deadline) {
      drain();
      if (init_ok_) {
        ok = true;
        break;
      }
      if (init_err_) {
        error = init_err_msg_;
        disable();
        return false;
      }
      usleep(20000);
    }
    if (!ok) {
      int st = 0;
      if (waitpid(pid_, &st, WNOHANG) == pid_) {
        error = "plugin process exited during init (missing runtime?)";
        pid_ = -1;
        closeFds();
        return false;
      }
      Logger::instance().warning("Plugin ", manifest_.name, " did not ACK init; continuing");
    }
    Logger::instance().info("Process plugin enabled: ", manifest_.name, " v", manifest_.version,
                            " lang=", manifest_.language, " pid=", pid_);
    return true;
  }

  void disable() override {
    if (pid_ <= 0) return;
    sendLine(R"({"op":"shutdown"})");
    usleep(50000);
    kill(pid_, SIGTERM);
    int st = 0;
    for (int i = 0; i < 20; ++i) {
      if (waitpid(pid_, &st, WNOHANG) == pid_) break;
      usleep(20000);
    }
    if (waitpid(pid_, &st, WNOHANG) == 0) {
      kill(pid_, SIGKILL);
      waitpid(pid_, &st, 0);
    }
    pid_ = -1;
    closeFds();
  }

  void onServerStart(const ServerStartEvent& ev) override {
    emitEvent("server_start",
              std::string(R"({"motd":")") + jsonEscape(ev.motd) + R"(","port":)" +
                  std::to_string(ev.port) + "}");
  }
  void onSessionOpen(const SessionOpenEvent& ev) override {
    emitEvent("session_open",
              std::string(R"({"address":")") + jsonEscape(ev.address) + R"(","port":)" +
                  std::to_string(ev.port) + R"(,"client_id":)" + std::to_string(ev.client_id) +
                  "}");
  }
  void onSessionClose(const SessionCloseEvent& ev) override {
    emitEvent("session_close",
              std::string(R"({"address":")") + jsonEscape(ev.address) + R"(","port":)" +
                  std::to_string(ev.port) + R"(,"reason":")" + jsonEscape(ev.reason) + "\"}");
  }
  void onPlayerLogin(const PlayerLoginEvent& ev) override {
    emitEvent("player_login",
              std::string(R"({"address":")") + jsonEscape(ev.address) + R"(","port":)" +
                  std::to_string(ev.port) + R"(,"username":")" + jsonEscape(ev.username) +
                  R"(","protocol":)" + std::to_string(ev.protocol) + R"(,"client_id":)" +
                  std::to_string(ev.client_id) + R"(,"world":")" + jsonEscape(ev.world) + "\"}");
  }
  void onPlayerJoin(const PlayerJoinEvent& ev) override {
    emitEvent("player_join",
              std::string(R"({"username":")") + jsonEscape(ev.username) + R"(","world":")" +
                  jsonEscape(ev.world) + R"(","x":)" + std::to_string(ev.x) + R"(,"y":)" +
                  std::to_string(ev.y) + R"(,"z":)" + std::to_string(ev.z) + "}");
  }
  void onPlayerQuit(const PlayerQuitEvent& ev) override {
    emitEvent("player_quit",
              std::string(R"({"username":")") + jsonEscape(ev.username) + R"(","reason":")" +
                  jsonEscape(ev.reason) + "\"}");
  }
  void onChat(ChatEvent& ev) override {
    last_chat_cancel_ = false;
    emitEvent("chat",
              std::string(R"({"username":")") + jsonEscape(ev.username) + R"(","message":")" +
                  jsonEscape(ev.message) + "\"}",
              80);
    if (last_chat_cancel_) ev.cancelled = true;
  }
  void onCommand(CommandEvent& ev) override {
    last_cmd_handled_ = false;
    emitEvent("command",
              std::string(R"({"username":")") + jsonEscape(ev.username) + R"(","command":")" +
                  jsonEscape(ev.command) + R"(","args":")" + jsonEscape(ev.args) + "\"}",
              80);
    if (last_cmd_handled_) ev.handled = true;
  }
  void onMove(const MoveEvent& ev) override {
    // high frequency — skip process plugins for moves (native only)
    (void)ev;
  }
  void onBlock(const BlockEvent& ev) override {
    // fire-and-forget: wait_ms=0 (default 50ms × N process plugins made dig/place lag)
    emitEvent("block",
              std::string(R"({"username":")") + jsonEscape(ev.username) + R"(","x":)" +
                  std::to_string(ev.x) + R"(,"y":)" + std::to_string(ev.y) + R"(,"z":)" +
                  std::to_string(ev.z) + R"(,"action":)" + std::to_string(ev.action) +
                  R"(,"face":)" + std::to_string(ev.face) + "}",
              0);
  }
  void onWorldLoad(const WorldLoadEvent& ev) override {
    emitEvent("world_load",
              std::string(R"({"name":")") + jsonEscape(ev.name) + R"(","generator":)" +
                  std::to_string(ev.generator) + R"(,"seed":)" + std::to_string(ev.seed) + "}");
  }

private:
  void emitEvent(const char* name, const std::string& data_json, int wait_ms = 50) {
    std::ostringstream os;
    os << R"({"op":"event","name":")" << name << R"(","data":)" << data_json << "}";
    sendLine(os.str());
    drainWithWait(wait_ms);
  }

  void closeFds() {
    if (wfd_ >= 0) {
      close(wfd_);
      wfd_ = -1;
    }
    if (rfd_ >= 0) {
      close(rfd_);
      rfd_ = -1;
    }
  }

  bool sendLine(const std::string& line) {
    if (wfd_ < 0) return false;
    std::string msg = line;
    if (msg.empty() || msg.back() != '\n') msg.push_back('\n');
    const char* p = msg.data();
    std::size_t left = msg.size();
    while (left > 0) {
      ssize_t n = write(wfd_, p, left);
      if (n < 0) {
        if (errno == EINTR) continue;
        return false;
      }
      p += n;
      left -= static_cast<std::size_t>(n);
    }
    return true;
  }

  void drain() {
    if (rfd_ < 0) return;
    char buf[4096];
    for (;;) {
      ssize_t n = read(rfd_, buf, sizeof(buf));
      if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
        if (errno == EINTR) continue;
        break;
      }
      if (n == 0) break;
      rbuf_.append(buf, static_cast<std::size_t>(n));
      for (;;) {
        auto pos = rbuf_.find('\n');
        if (pos == std::string::npos) break;
        auto line = rbuf_.substr(0, pos);
        rbuf_.erase(0, pos + 1);
        handleLine(line);
      }
    }
  }

  void drainWithWait(int ms) {
    if (ms <= 0) {
      drain(); // fire-and-forget: no sleep (critical for dig/place)
      return;
    }
    const int steps = std::max(1, ms / 10);
    for (int i = 0; i < steps; ++i) {
      drain();
      usleep(10000);
    }
    drain();
  }

  void handleLine(const std::string& line) {
    if (line.empty()) return;
    auto op = jsonGetString(line, "op");
    if (op == "ok") {
      init_ok_ = true;
      return;
    }
    if (op == "error") {
      init_err_ = true;
      init_err_msg_ = jsonGetString(line, "msg");
      if (init_err_msg_.empty()) init_err_msg_ = line;
      return;
    }
    if (op == "log") {
      auto level = jsonGetString(line, "level");
      auto msg = jsonGetString(line, "msg");
      if (msg.empty()) msg = line;
      if (level == "error") Logger::instance().error("[", manifest_.name, "] ", msg);
      else if (level == "warn" || level == "warning")
        Logger::instance().warning("[", manifest_.name, "] ", msg);
      else if (level == "notice")
        Logger::instance().notice("[", manifest_.name, "] ", msg);
      else
        Logger::instance().info("[", manifest_.name, "] ", msg);
      return;
    }
    if (op == "cancel_chat" || (op == "result" && jsonGetBool(line, "cancelled"))) {
      last_chat_cancel_ = true;
      return;
    }
    if (op == "handle_command" || (op == "result" && jsonGetBool(line, "handled"))) {
      last_cmd_handled_ = true;
      return;
    }
  }

  config::PluginManifest manifest_;
  std::vector<std::string> argv_;
  pid_t pid_ = -1;
  int wfd_ = -1;
  int rfd_ = -1;
  std::string rbuf_;
  bool init_ok_ = false;
  bool init_err_ = false;
  std::string init_err_msg_;
  bool last_chat_cancel_ = false;
  bool last_cmd_handled_ = false;
};

std::vector<std::string> buildProcessArgv(const config::PluginManifest& m) {
  const auto& main_rel = m.main;
  const auto lang = m.language;
  if (lang == "python" || lang == "py") return {"python3", main_rel};
  if (lang == "php") return {"php", main_rel};
  if (lang == "nodejs" || lang == "node" || lang == "js") return {"node", main_rel};
  if (!main_rel.empty() && main_rel[0] != '/' && main_rel.find('/') == std::string::npos) {
    return {"./" + main_rel};
  }
  return {main_rel};
}

bool isNativeSharedLib(const config::PluginManifest& m) {
  const auto lang = m.language;
  if (lang == "native" || lang == "c" || lang == "cpp" || lang == "c++" || lang == "rust" ||
      lang == "go") {
    return m.main.size() >= 3 && m.main.substr(m.main.size() - 3) == ".so";
  }
  return false;
}

} // namespace

PluginManager::~PluginManager() { disableAll(); }

std::unique_ptr<IPlugin> PluginManager::tryLoadDir(const std::string& dir) {
  const auto yml = (fs::path(dir) / "plugin.yml").string();
  config::PluginManifest m;
  std::string err;
  if (!config::loadPluginManifest(yml, m, err)) {
    Logger::instance().warning("Skip plugin dir ", dir, ": ", err);
    return nullptr;
  }
  std::error_code ec;
  m.base_dir = fs::absolute(fs::path(dir), ec).string();
  if (ec) m.base_dir = dir;
  m.data_folder = (fs::path(m.base_dir) / "data").string();
  fs::create_directories(m.data_folder, ec);

  const auto main_abs = (fs::path(m.base_dir) / m.main).string();
  if (!fs::exists(main_abs, ec)) {
    Logger::instance().warning("Skip plugin ", m.name, ": main not found: ", main_abs,
                               " (build it first; see docs/PLUGINS.md)");
    return nullptr;
  }

  if (isNativeSharedLib(m)) {
    return std::make_unique<NativePlugin>(std::move(m));
  }
  auto argv = buildProcessArgv(m);
  return std::make_unique<ProcessPlugin>(std::move(m), std::move(argv));
}

void PluginManager::loadAll(std::string_view plugins_dir) {
  plugins_.clear();
  std::error_code ec;
  fs::path root{std::string(plugins_dir)};
  if (!fs::exists(root, ec)) {
    fs::create_directories(root, ec);
    Logger::instance().info("Created plugins dir: ", root.string());
    return;
  }
  for (auto& ent : fs::directory_iterator(root, ec)) {
    if (!ent.is_directory()) continue;
    if (auto p = tryLoadDir(ent.path().string())) {
      plugins_.push_back(std::move(p));
    }
  }
  Logger::instance().info("Discovered ", plugins_.size(), " plugin(s) in ", root.string());
}

void PluginManager::enableAll() {
  for (auto& p : plugins_) {
    std::string err;
    if (!p->enable(err)) {
      Logger::instance().error("Failed to enable plugin ", p->manifest().name, ": ", err);
    }
  }
}

void PluginManager::disableAll() {
  for (auto it = plugins_.rbegin(); it != plugins_.rend(); ++it) {
    (*it)->disable();
  }
}

void PluginManager::fireServerStart(const ServerStartEvent& ev) {
  for (auto& p : plugins_) p->onServerStart(ev);
}
void PluginManager::fireSessionOpen(const SessionOpenEvent& ev) {
  for (auto& p : plugins_) p->onSessionOpen(ev);
}
void PluginManager::fireSessionClose(const SessionCloseEvent& ev) {
  for (auto& p : plugins_) p->onSessionClose(ev);
}
void PluginManager::firePlayerLogin(const PlayerLoginEvent& ev) {
  for (auto& p : plugins_) p->onPlayerLogin(ev);
}
void PluginManager::firePlayerJoin(const PlayerJoinEvent& ev) {
  for (auto& p : plugins_) p->onPlayerJoin(ev);
}
void PluginManager::firePlayerQuit(const PlayerQuitEvent& ev) {
  for (auto& p : plugins_) p->onPlayerQuit(ev);
}
void PluginManager::fireChat(ChatEvent& ev) {
  for (auto& p : plugins_) p->onChat(ev);
}
void PluginManager::fireCommand(CommandEvent& ev) {
  for (auto& p : plugins_) p->onCommand(ev);
}
void PluginManager::fireMove(const MoveEvent& ev) {
  for (auto& p : plugins_) p->onMove(ev);
}
void PluginManager::fireBlock(const BlockEvent& ev) {
  for (auto& p : plugins_) p->onBlock(ev);
}
void PluginManager::fireWorldLoad(const WorldLoadEvent& ev) {
  for (auto& p : plugins_) p->onWorldLoad(ev);
}

} // namespace mpmpes::plugin
