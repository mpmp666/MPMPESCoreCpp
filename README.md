# MPMPESCoreCpp

**MPMPESCore** 的 C++ 重写（进行中）。

- 目标：兼容 Minecraft: 基岩版 **0.14.x**（协议 **70**，与 PHP 版 `MPMPESCore` 一致）
- 来源：`~/MPMPESCore`（Genisys / PocketMine-MP 分支）
- 许可：与上游一致，LGPL-3.0（衍生）

## 状态（v0.5 — dig/place + 创造 + 合成 + 生物 AI + 铁轨/红石 + 漏斗/矿车容器）

已实现：

- RakLib 可靠会话（Phase 0–1）
- **`server.properties`** + CLI + **多语言插件**（`docs/PLUGINS.md`）
- **进服路径**：Login → PlayStatus → StartGame → FullChunk → PLAYER_SPAWN
- **地图生成**：flat / void / normal-stub 列式 chunk
- **多世界**：`worlds.txt` + `/worlds` `/goto <world>` `/spawn`
- **挖放方块**：`UpdateBlock` 世界同步 + `LevelEvent` 破坏粒子 / 点击音
- **创造背包** + 玩家物品栏 + `MobEquipment` 热键
- **合成 stub**：`CraftingData` 基础配方 + `CraftingEvent` 接受结果
- **生物 AI**：猪/鸡/牛/羊/僵尸（游荡 + 重力），`AddEntity`/`MoveEntity`/`RemoveEntity`
- **铁轨自动合并**：普通轨/动力轨/探测轨/激活轨放置与破坏时按邻居重算 meta（直轨/弯轨/坡轨）
- **矿车/铁路**：物品 328 放轨生成实体 84，右键乘坐，`SetEntityLink`，轨上滑动；攻击矿车掉落
- **红石子集**：红石粉强度 BFS、拉杆、红石火把、中继器、红石块、红石灯、动力轨供电；比较器 149/150（读箱子填充；0.14 客户端可能显示异常）
- 命令：`/help` `/list` `/me` `/worlds` `/goto` `/spawn` `/gm` `/give` `/spawnmob` `/clear`
- 插件事件：join/quit/chat/command/move/block/world_load + 告示牌 API v3

未实现 / 粗糙处：

- 客户端 Batch zlib 解压
- 完整合成校验 / 熔炉 UI 计时
- 路径寻路、仇恨 AI、死亡掉落实体
- 完整红石准连接 / 中继器 tick 延迟仿真 / 活塞
- 区块落盘、完整地形噪声

## 构建

```bash
cd ~/MPMPESCoreCpp
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

### 预编译发行版（GitHub Actions）

打 tag `v*` 会自动编译并上传 Release：

| 产物 | 架构 |
|------|------|
| `mpmpes-linux-x86_64` | Linux x86_64 (glibc) |
| `mpmpes-linux-x86_64-static` | Linux x86_64 全静态 |
| `mpmpes-linux-aarch64` | Linux ARM64 |
| `mpmpes-linux-armv7` | Linux ARMv7 hard-float |
| `mpmpes-linux-s390x` | Linux IBM Z (s390x) |
| `mpmpes-linux-ppc64le` | Linux POWER LE |
| `mpmpes-linux-riscv64` | Linux RISC-V 64 |
| `mpmpes-macos-x86_64` / `arm64` | macOS Intel / Apple Silicon |

> Windows 暂未出包（源码仍为 POSIX socket/`dlfcn`）；请用 WSL 或 Linux 二进制。

```bash
# 示例：本机发版
git tag v0.5.0 && git push origin v0.5.0
# 或 Actions → Build & Release → Run workflow
```

## 运行

```bash
cd ~/MPMPESCoreCpp
cp -n server.properties.example server.properties  # 首次
./build/mpmpes --config server.properties
# 或
./build/mpmpes --port 19132 --motd 金安卓 --plugins-dir plugins
./build/mpmpes --no-plugins
```

默认端口 `19132`。`server.properties` 里 `gamemode=` 控制**新玩家**默认模式（`0`/`survival` 或 `1`/`creative`）；老玩家读 `players/*.dat`。

### 客户端小贴士（0.14）

| 操作 | 说明 |
|------|------|
| 挖 | 创造：`START_BREAK` 立即挖；生存：`STOP_BREAK`/`RemoveBlock` |
| 放 | 手持方块右键 → `UseItem` |
| 创造栏 | 登录时下发 `ContainerSetContent(0x79)` |
| 刷怪 | 手持刷怪蛋右键，或 `/spawnmob pig\|zombie` |
| 模式 | `/gm 0` 生存 / `/gm 1` 创造 |

## 插件

见 **[docs/PLUGINS.md](docs/PLUGINS.md)**。

| 示例 | 语言 | 依赖 |
|------|------|------|
| HelloC / HelloCpp | C/C++ `.so` 原生 | CMake 自动编 |
| HelloPython | Python 子进程 | `python3` |

仅保留 **C/C++/native** 与 **Python**。PHP / Node / Go / Rust 示例与加载路径已删除，避免额外运行时拖慢服务端。热路径约定见 `docs/PLUGINS.md`。

## 与 PHP 版对照

| 模块 | PHP 路径 | C++ 路径 |
|------|----------|----------|
| Binary | `src/raklib/Binary.php` | `include/mpmpes/binary/` |
| RakLib | `src/raklib/` | `include/mpmpes/raklib/` + `src/raklib/` |
| Protocol | `src/pocketmine/network/protocol/` | `include/mpmpes/protocol/` |
| Level/Chunk | `src/pocketmine/level/` | `include/mpmpes/level/` |
| Item | `src/pocketmine/item/` | `include/mpmpes/item/Item.hpp` |
| Entity | `src/pocketmine/entity/` | `include/mpmpes/entity/Entity.hpp` |
| Server | `src/pocketmine/Server.php` | `src/server/Server.cpp` |

## 架构原则

1. **先网络后玩法**：RakLib → MCPE packet → Player → Level / Entity
2. **协议常量对齐** PHP `Info.php`（CURRENT_PROTOCOL=70）
3. **不依赖 PHP / pthreads**；单进程 + 可选后续工作线程
4. s390x / x86_64 均可编（无架构特化汇编）

## 许可

见 `LICENSE`（待从 PHP 核心复制 LGPL 文本）。上游致谢：PocketMine-MP、iTXTech Genisys、mpmpes。
