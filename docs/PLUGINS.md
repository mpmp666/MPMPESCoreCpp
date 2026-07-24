# MPMPESCoreCpp 插件系统

仅支持：

| 模式 | language | main | 说明 |
|------|----------|------|------|
| **原生** | `c` / `cpp` / `native` | `*.so` | 同进程 `dlopen`，C ABI（见 `PluginApi.h`） |
| **Python** | `python` / `py` | `main.py` 等 | 子进程 JSON 行协议 |

已移除：PHP / Node.js / Go / Rust 示例与加载路径（避免多余运行时与子进程拖慢服务端）。

## 目录结构

```
plugins/
  MyPlugin/
    plugin.yml      # 必填
    main...         # .so 或 .py
    data/           # 插件数据目录（自动创建）
```

## plugin.yml

```yaml
name: MyPlugin
version: 0.1.0
api: 1.0.0
language: python    # native|c|cpp|python
main: main.py       # .so 或 python 脚本
author: you
description: hello
```

## 性能约定（不拖慢主循环）

1. **原生 `.so`**：同进程，热路径（move/block）可回调，但示例插件**不要**对 dig/place 打日志。
2. **Python 进程**：
   - **不推送** `move`（高频）
   - `block` / `session_open` / `session_close`：**fire-and-forget**（不等待子进程回复）
   - 仅 `chat` / `command` 等可取消事件短等 ACK
3. 生产环境若不用插件：`enable-plugins=off` 或 `./mpmpes --no-plugins`

## 原生插件（C ABI）

必须导出：

- `mpmpes_plugin_info`
- `mpmpes_plugin_init`
- `mpmpes_plugin_shutdown`

可选事件：`mpmpes_on_server_start`、`session_*`、`player_*`、`chat`、`command`、`move`、`block`、`world_load`。

头文件：`include/mpmpes/plugin/PluginApi.h`。

```bash
gcc -shared -fPIC -I../../include -o libhello_c.so hello_c.c
```

CMake 会自动编 HelloC / HelloCpp 到 `plugins/HelloC|HelloCpp/`。

## Python 进程插件（JSON 行协议）

- **stdin**：主机 → 插件，每行一个 JSON  
- **stdout**：插件 → 主机，每行一个 JSON  
- **stderr**：调试  

### 主机 → 插件

```json
{"op":"init","api":2}
{"op":"event","name":"server_start","data":{"motd":"...","port":19132}}
{"op":"event","name":"player_join","data":{"username":"Steve","world":"world","x":0,"y":64,"z":0}}
{"op":"shutdown"}
```

### 插件 → 主机

```json
{"op":"ok"}
{"op":"error","msg":"..."}
{"op":"log","level":"info","msg":"..."}
```

`level`: `info` | `warn` | `warning` | `error` | `notice`

需要系统已安装 `python3`。

## 配置

```
plugins-dir=plugins
enable-plugins=on
```

```
./mpmpes --config server.properties --plugins-dir plugins
./mpmpes --no-plugins
```

## 示例插件

| 目录 | 语言 | 模式 |
|------|------|------|
| HelloC | C | `.so` 原生 |
| HelloCpp | C++ | `.so` 原生 |
| HelloPython | Python | 进程 |

## 事件一览（API v2）

| 事件 | 原生符号 | 进程 `name` | 进程侧 |
|------|----------|-------------|--------|
| 服务启动 | `mpmpes_on_server_start` | `server_start` | 推送 |
| 会话开/关 | `mpmpes_on_session_open/close` | `session_open` / `session_close` | 推送、不等待 |
| 登录/进服/退出 | `mpmpes_on_player_*` | `player_*` | 推送 |
| 聊天/命令 | `mpmpes_on_chat` / `command` | `chat` / `command` | 短等（可取消/处理） |
| 移动 | `mpmpes_on_move` | — | **不推送** |
| 方块 | `mpmpes_on_block` | `block` | 推送、不等待 |
| 世界加载 | `mpmpes_on_world_load` | `world_load` | 推送 |

## 说明

- 原生插件与核心同进程：崩溃会影响服务端；Python 子进程隔离更好但有 IPC 成本。  
- 不要在热路径（移动/挖掘）里同步阻塞或刷屏日志。  
- 多世界见根目录 `worlds.txt`。
