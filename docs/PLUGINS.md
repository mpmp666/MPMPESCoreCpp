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
   - 仅 `chat` / `command` / `sign_change` 等可取消事件短等 ACK
3. 生产环境若不用插件：`enable-plugins=off` 或 `./mpmpes --no-plugins`

## 原生插件（C ABI v3）

必须导出：

- `mpmpes_plugin_info`
- `mpmpes_plugin_init`
- `mpmpes_plugin_shutdown`

可选事件：`mpmpes_on_server_start`、`session_*`、`player_*`、`chat`、`command`、`move`、`block`、`sign_change`、`world_load`。

头文件：`include/mpmpes/plugin/PluginApi.h`（`MPMPES_PLUGIN_API_VERSION = 3`）。

### Host 回调（init 时传入）

| 字段 | 说明 |
|------|------|
| `log_info` / `log_warn` / `log_error` | 写日志 |
| `broadcast` | 全服系统消息 |
| `send_message` | 给指定玩家发系统消息（v3） |
| `player_count` | 在线人数（v3） |
| `get_player_pos` | 玩家坐标/世界名（v3） |
| `set_block` / `get_block` | 改/读方块（v3） |
| `set_sign_text` | 改告示牌四行文字（v3） |
| `kick_player` | 踢人（v3） |

v3 新增字段可能在极老宿主上为 NULL；调用前检查指针。

### 告示牌事件

`mpmpes_on_sign_change(MpmpesEventSignChange* ev)`：

- 可读 `username` / `x,y,z` / `text1..4`（或 `line1..4` 可写缓冲）
- 设 `cancelled != 0` 取消改字
- 改写 `line1..line4`（容量 `line_cap`）可替换最终文字

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
{"op":"init","api":3}
{"op":"event","name":"server_start","data":{"motd":"...","port":19132}}
{"op":"event","name":"player_join","data":{"username":"Steve","world":"world","x":0,"y":64,"z":0}}
{"op":"event","name":"sign_change","data":{"username":"Steve","x":1,"y":5,"z":2,"text1":"a","text2":"","text3":"","text4":""}}
{"op":"shutdown"}
```

### 插件 → 主机

```json
{"op":"ok"}
{"op":"error","msg":"..."}
{"op":"log","level":"info","msg":"..."}
{"op":"cancel_chat"}
{"op":"handle_command"}
{"op":"cancel_sign"}
{"op":"rewrite_sign","text1":"L1","text2":"L2","text3":"L3","text4":"L4"}
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

## 事件一览（API v3）

| 事件 | 原生符号 | 进程 `name` | 进程侧 |
|------|----------|-------------|--------|
| 服务启动 | `mpmpes_on_server_start` | `server_start` | 推送 |
| 会话开/关 | `mpmpes_on_session_open/close` | `session_open` / `session_close` | 推送、不等待 |
| 登录/进服/退出 | `mpmpes_on_player_*` | `player_*` | 推送 |
| 聊天/命令 | `mpmpes_on_chat` / `command` | `chat` / `command` | 短等（可取消/处理） |
| 移动 | `mpmpes_on_move` | — | **不推送** |
| 方块 | `mpmpes_on_block` | `block` | 推送、不等待 |
| 告示牌改字 | `mpmpes_on_sign_change` | `sign_change` | 短等（可取消/改写） |
| 世界加载 | `mpmpes_on_world_load` | `world_load` | 推送 |

## 告示牌（游戏内）

- 物品 `323` SIGN：顶面放 **站立告示牌**（方块 63），侧面放 **墙上告示牌**（方块 68）
- 放置后创建 tile（Text1–4 + Creator）；客户端 `BlockEntityData (0xbd)` 改字
- 仅放置者可改；每行 UTF-8 码点 ≤16；破坏掉落物品 323
- 持久化：`worlds/<name>/signs.dat`（SGN1）；区块发送时附带 tile NBT

## 说明

- 原生插件与主机同进程：崩溃会影响服务端，谨慎使用。
- Python 插件隔离更好，但不要在热路径做重活。
