# 构建方法

本工程使用 C++20、Win32 和 D3D11，目标为 Windows x64。发布包可直接运行；只有从源码编译时才需要下列工具和开发依赖。

## 获取源码

在 PowerShell 中执行，目录名可自行更改：

```powershell
git clone https://github.com/PhysicalWorldDo/rep-player.git
Set-Location .\rep-player
```

源码不包含客户端原始 REP、NPK、IMG、字体、视频、Bink DLL 或游戏程序。构建完成后的播放验证需要自己准备完整客户端，目录结构见 [README.md](README.md)。

## 准备依赖

便携编译器不纳入 Git 仓库。依赖准备脚本下载固定版本的编译器和源码，构建静态 zlib / FreeType 并放入工程目录：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\setup-dependencies.ps1
```

本版本构建依赖如下；运行时第三方组件的来源与许可见 [THIRD_PARTY.txt](THIRD_PARTY.txt) 和 `licenses`。

| 组件 | 版本 / 用途 |
| --- | --- |
| LLVM MinGW | `llvm-mingw-20260616-ucrt-x86_64`，Clang 22.1.8，编译 C++20 与 Windows 资源 |
| zlib | 1.2.11 头文件和静态库，用于 REP 压缩流 |
| FreeType | 2.12.1 头文件和静态构建库，用于字体 |

准备后应存在 `toolchain\llvm-mingw-20260616-ucrt-x86_64\bin\clang++.exe`、`vendor\zlib\libz.a` 和 `vendor\freetype\libfreetype.a`。FreeType 保留官方默认字体模块，通过 `FT_CONFIG_OPTION_SYSTEM_ZLIB` 共用静态 zlib 1.2.11 支持压缩 PCF，关闭外部 PNG、BZip2、Brotli 和 HarfBuzz；该构建不需要原 Anaconda FreeType、libpng 或 zlib DLL。脚本无参数，每次运行都重新编译两个静态库；已下载的源码和工具链会复用。

FFmpeg 用于 AVI 解码和视频 / PNG 导出，依赖准备脚本不安装它。播放器源码编译本身不需要 FFmpeg。运行导出前，可以从本项目 v1.0.0 运行 ZIP 提取已核对来源的静态 FFmpeg：

```powershell
curl.exe --fail --location --output .\dependency-cache\runtime.zip https://github.com/PhysicalWorldDo/rep-player/releases/download/v1.0.0/rep-player-v1.0.0-windows-x64.zip
Expand-Archive -LiteralPath .\dependency-cache\runtime.zip -DestinationPath .\dependency-cache\runtime -Force
New-Item -ItemType Directory -Path .\build -Force | Out-Null
Copy-Item -LiteralPath .\dependency-cache\runtime\rep-player-v1.0.0-windows-x64\build\ffmpeg.exe -Destination .\build\ffmpeg.exe -Force
```

版本为 `n8.1.2-29-g703dcc25b9-20260721`，支持 libx264、ProRes 4444 和 PNG。较小的构建源码包作为 Release 附件提供，完整依赖源码缓存链接到上游固定版本；查看 [FFMPEG_SOURCE.md](licenses/FFMPEG_SOURCE.md) 可重建该第三方工具。它通过独立进程调用，未链接入播放器。

## 编译

关闭从本工程 `build\rep_player.exe` 启动的播放器后，在工程根目录执行：

```powershell
.\build.ps1
```

脚本按源码和头文件时间增量编译，链接播放器和辅助工具：

| 输出 | 用途 |
| --- | --- |
| `build\rep_player.exe` | 图形播放器 |
| `build\rep_export.exe` | 命令行素材导出 |
| `build\rep_validate.exe` | REP 协议检查 |
| `build\rep_resources.exe` | 图像资源检查 |
| `build\rep_gpu.exe` | GPU 创建与渲染验证 |
| `build\rep_catalog.exe` | 双语名称与目录检查 |

运行 `Start.cmd` 或 `build\rep_player.exe`。必须保留 FFmpeg、`assets\shaders` 和 `ui_design\data\skill-name-map.json` 的相对位置。播放器运行不需要构建工具链或 Python。

## 生成运行包

在编译并准备好 `build\ffmpeg.exe` 后执行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\package-release.ps1 -Version 1.0.0 -FFmpegDirectory .\build
```

本次正式包使用 `dist\ffmpeg-runtime` 作为 FFmpegDirectory，该目录仅有静态 `ffmpeg.exe`。若本地 `build` 保留历史第三方 DLL，请改用只放当前 FFmpeg 及其必需 DLL 的新目录，避免带入旧 DLL。脚本按清单复制 EXE、运行资源和许可证，输出 `dist\rep-player-v1.0.0-windows-x64.zip`；同名目录或 ZIP 已存在时会停止，请使用新版本名或先保留旧包。

若 PowerShell 执行策略阻止脚本，可对单次命令使用进程级设置：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1
```

## 基本验证

在工程根目录执行 GPU 创建检查：

```powershell
.\build\rep_gpu.exe --smoke
```

该命令验证 shader program 能在硬件 D3D11 设备上创建；创建成功不能替代逐像素或完整录像验证。检查自己客户端的一份 REP：

```powershell
.\build\rep_validate.exe --dump "D:\MyDNFClient\Replay\SkillReplay\Swordman\BloodyRave.rep"
```

随后启动播放器，选择完整客户端根，检查自动播放、暂停、前后帧、切换、重播及素材导出。

## 研发测试与历史证据

`tests` 和 `tools` 包含研发验证工具。Python 仅用于测试和报告生成，播放器运行不依赖 Python。历史完整测试还依赖 NumPy、Pillow、特定本机客户端及独立的旧 Python 播放器基准；其中部分命令使用研发机器路径，不能直接当作全新机器上的构建步骤。

需要匹配测试输入后，可运行：

```powershell
python -m unittest discover -s tests -v
```

历史协议差分、代表录像、着色器像素、UI 和导出证据的范围见 [RELEASE_NOTES.md](RELEASE_NOTES.md) 及 `HANDOFF.md`。客户端输入保持只读；新的设置、缓存、导出和验证报告应放在本工程目录内。

Git 不追踪 `build`、`toolchain`、`runtime`、缓存、导出素材和本机验证输出。发布包包含实际运行依赖，源码仓库通过依赖准备脚本恢复构建环境。
