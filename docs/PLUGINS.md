# MPMPESCoreCpp 插件系统

支持 **C / C++ / Rust / Go**（原生 `.so` 或进程）以及 **Python / PHP / Node.js**（子进程 JSON 行协议）。

## 目录结构

```
plugins/
  MyPlugin/
    plugin.yml      # 必填
    main...         # 见 language
    data/           # 插件数据目录（自动创建）
```

## plugin.yml

```yaml
name: MyPlugin
version: 0.1.0
api: 1.0.0
language: python    # native|c|cpp|go|rust|python|php|nodejs
main: main.py       # .so 文件名 或 可执行文件 或 脚本
author: you
description: hello
```

| language | main 含义 |
|----------|-----------|
| `c` / `cpp` / `native` | 共享库 `.so`，导出 C ABI（见 `include/mpmpes/plugin/PluginApi.h`） |
| `go` / `rust` | **若 main 以 `.so` 结尾** → 原生；否则 → 可执行文件进程模式 |
| `python` | `python3 main` |
| `php` | `php main` |
| `nodejs` | `node main` |

## 原生插件（C ABI）

必须导出：

- `mpmpes_plugin_info`
- `mpmpes_plugin_init`
- `mpmpes_plugin_shutdown`

可选事件：

- `mpmpes_on_server_start`
- `mpmpes_on_session_open`
- `mpmpes_on_session_close`
- `mpmpes_on_player_login`

头文件：`include/mpmpes/plugin/PluginApi.h`（纯 C，Go/Rust FFI 可用）。

编译示例（C）：

```bash
gcc -shared -fPIC -I../../include -o libhello_c.so hello_c.c
```

## 进程插件（JSON 行协议）

- **stdin**：主机 → 插件，每行一个 JSON  
- **stdout**：插件 → 主机，每行一个 JSON  
- **stderr**：自由调试输出  

### 主机 → 插件

```json
{"op":"init","api":1}
{"op":"event","name":"server_start","data":{"motd":"...","port":19132}}
{"op":"event","name":"session_open","data":{"address":"1.2.3.4","port":12345,"client_id":1}}
{"op":"event","name":"session_close","data":{"address":"...","port":1,"reason":"..."}}
{"op":"event","name":"player_login","data":{"username":"Steve","protocol":70,"client_id":1,"address":"...","port":1}}
{"op":"shutdown"}
```

### 插件 → 主机

```json
{"op":"ok"}
{"op":"error","msg":"..."}
{"op":"log","level":"info","msg":"..."}
```

`level`: `info` | `warn` | `warning` | `error` | `notice`

## 配置

`server.properties`：

```
plugins-dir=plugins
enable-plugins=on
```

CLI：

```
./mpmpes --config server.properties --plugins-dir plugins
./mpmpes --no-plugins
```

## 示例插件

| 目录 | 语言 | 模式 |
|------|------|------|
| HelloC | C | `.so` 原生 |
| HelloCpp | C++ | `.so` 原生（需自行编译） |
| HelloPython | Python | 进程 |
| HelloPHP | PHP | 进程 |
| HelloNode | Node.js | 进程 |
| HelloGo | Go | 进程（`go build -o hello_go`） |
| HelloRust | Rust | 进程（`cargo build --release`） |

CMake 会自动编 HelloC 并复制到 `plugins/HelloC/libhello_c.so`。

## 事件一览（API v2）

| 事件 | 原生符号 | 进程 `name` |
|------|----------|-------------|
| 服务启动 | `mpmpes_on_server_start` | `server_start` |
| 会话开/关 | `mpmpes_on_session_open/close` | `session_open` / `session_close` |
| 登录 | `mpmpes_on_player_login` | `player_login` |
| 进服完成 | `mpmpes_on_player_join` | `player_join` |
| 退出 | `mpmpes_on_player_quit` | `player_quit` |
| 聊天 | `mpmpes_on_chat`（可 `cancelled`） | `chat` |
| 命令 | `mpmpes_on_command`（可 `handled`） | `command` |
| 移动 | `mpmpes_on_move` | （进程侧默认不推，防刷屏） |
| 方块动作 | `mpmpes_on_block` | `block` |
| 世界加载 | `mpmpes_on_world_load` | `world_load` |

## 说明

- 进程插件需系统已安装对应运行时（python3 / php / node / 自编二进制）。  
- 原生插件与核心同进程，崩溃会影响服务端；脚本插件隔离更好。  
- 多世界见根目录 `worlds.txt`（`name:generator:seed`）。  
