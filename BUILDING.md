# 构建方法

本工程使用 C++20、Win32 和 D3D11，目标为 Windows x64。用户运行包双击根目录 `rep_player.exe` 即可；以下内容用于源码开发和打包。

## 获取源码

在 PowerShell 中执行，目录名可自行更改：

```powershell
git clone https://github.com/PhysicalWorldDo/rep-player.git
Set-Location .\rep-player
```

源码不包含客户端原始录像、图像、字体、视频、Bink DLL 或游戏程序。播放验证需要自己准备完整客户端，目录结构见 [README.md](README.md)。

## 准备依赖

依赖准备脚本下载固定版本的编译器和源码，构建静态 zlib / FreeType 并放入工程目录：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\setup-dependencies.ps1
```

| 组件 | 版本 / 用途 |
| --- | --- |
| LLVM MinGW | `llvm-mingw-20260616-ucrt-x86_64`，Clang 22.1.8，编译 C++20 和 Windows 资源 |
| zlib | 1.2.11，用于 REP 压缩流 |
| FreeType | 2.12.1，静态构建，用于字体 |

准备后应存在 `toolchain\llvm-mingw-20260616-ucrt-x86_64\bin\clang++.exe`、`vendor\zlib\libz.a` 和 `vendor\freetype\libfreetype.a`。FreeType 保留默认字体模块，通过 `FT_CONFIG_OPTION_SYSTEM_ZLIB` 共用静态 zlib 支持压缩 PCF；关闭外部 PNG、BZip2、Brotli 和 HarfBuzz 集成。脚本无参数，每次重新编译两个静态库，复用已下载的源码和工具链。

FFmpeg 用于 AVI 解码和素材导出，编译播放器本身不需要它。可从 v1.0.1 运行 ZIP 提取同版静态 FFmpeg，供开发目录运行及打包使用：

```powershell
New-Item -ItemType Directory -Path .\dependency-cache -Force | Out-Null
curl.exe --fail --location --output .\dependency-cache\runtime.zip https://github.com/PhysicalWorldDo/rep-player/releases/download/v1.0.1/rep-player-v1.0.1-windows-x64.zip
Expand-Archive -LiteralPath .\dependency-cache\runtime.zip -DestinationPath .\dependency-cache\runtime -Force
New-Item -ItemType Directory -Path .\build -Force | Out-Null
Copy-Item -LiteralPath .\dependency-cache\runtime\resources\ffmpeg.exe -Destination .\build\ffmpeg.exe -Force
```

FFmpeg 版本保持 `n8.1.2-29-g703dcc25b9-20260721`，支持 libx264、ProRes 4444 和 PNG，通过独立进程调用。其构建源码和完整依赖源码均使用上游固定下载链接，本项目 v1.0.1 Release 不重复附带大源码包；来源和重建方法见 [FFMPEG_SOURCE.md](licenses/FFMPEG_SOURCE.md)。

## 编译与开发运行

关闭从本工程启动的播放器后，在工程根目录执行：

```powershell
.\build.ps1
```

如执行策略阻止脚本，可使用单次命令：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1
```

脚本按源码和头文件时间增量编译，生成以下开发产物：

| 输出 | 用途 |
| --- | --- |
| `build\rep_player.exe` | 图形播放器 |
| `build\rep_export.exe` | 命令行素材导出 |
| `build\rep_validate.exe` | REP 协议检查 |
| `build\rep_resources.exe` | 图像资源检查 |
| `build\rep_gpu.exe` | GPU 创建与渲染验证 |
| `build\rep_catalog.exe` | 双语名称与目录检查 |

直接运行 `.\build\rep_player.exe`。程序会识别源码工程的 `build.ps1` / `build` 布局，继续使用工程根目录的 `runtime` 和 `exports`，保留已有本地设置。用户发布包则以根目录 EXE 所在位置作为程序目录。

名称表、173 个注册着色器文件和应用图标由构建嵌入 EXE。源文件仍保留在源码树中用于维护与重建，用户运行包无需 `ui_design`、`assets` 或 `build`。资源生成由 PowerShell 构建流程完成，无需为编译安装 Python。开发命令行与验证工具继续保留，但不进入用户 ZIP。

## 生成用户运行包

编译并准备好 `build\ffmpeg.exe` 后执行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\package-release.ps1 -Version 1.0.2 -FFmpegDirectory .\build
```

FFmpegDirectory 指向包含当前静态 `ffmpeg.exe` 的目录，脚本只复制该 EXE，不复制历史 DLL。脚本按运行清单生成 `dist\rep-player-v1.0.2-windows-x64.zip`，ZIP 内直接包含：

```text
rep_player.exe
resources\
├─ ffmpeg.exe
└─ licenses\
```

不额外套版本名称文件夹。许可证目录提供第三方许可、通知和来源链接。ZIP 不带辅助验证 EXE、开发文档、启动 CMD、设计资料、图标 PNG / ICO、包内清单或预生成的用户设置。打包目录或 ZIP 同名已存在时，脚本会停止；需要保留旧包后使用新的输出版本。

## 基本验证

工程根目录的 GPU 创建检查：

```powershell
.\build\rep_gpu.exe --smoke
```

这验证着色器 program 能在硬件 D3D11 设备上创建，不能替代逐像素或完整录像验证。检查自己的录像：

```powershell
$CLIENT = Read-Host '输入完整客户端根目录'
.\build\rep_validate.exe --dump (Join-Path $CLIENT 'Replay\SkillReplay\Swordman\BloodyRave.rep')
```

请改为客户端中实际存在的录像。

检查DNF来源时用 `--profile dnf-july`，DFO来源用 `--profile dfo`。仅验证结构且不解码字符串时加入 `--structural`；`--dump` 仍保留原字符串字节与使用的profile。`--codepage N` 可覆盖资源字符串代码页。批量检查接受UTF-8路径列表：

```powershell
.\build\rep_validate.exe --profile dnf-july --structural --batch .\validation\paths.txt
```

GUI、`rep_export` 和GPU验证工具也接受 `--profile` / `--codepage`。GUI及导出默认按所选客户端根下的程序名选择profile；验证工具默认DFO。`dnf-july`对应2026-07-09原生证据，不认证其他构建中的未知指令。

开发命令行导出示例：

```powershell
.\build\rep_export.exe --client $CLIENT --replay (Join-Path $CLIENT 'Replay\SkillReplay\Swordman\BloodyRave.rep') --format mov --fps 60 --alpha 1 --output .\exports --name BloodyRave
```

随后完整解压用户 ZIP 到其他可写目录，直接启动根 EXE，检查客户端选择、名称显示、播放、暂停、前后帧、重播和导出。运行依赖须以解压包自身验证。

## 研发测试与历史证据

`tests` 和 `tools` 保留研发验证工具。Python 只用于测试和报告，播放器运行不依赖 Python。完整历史测试还需要 NumPy、Pillow、指定客户端输入及独立的旧 Python 播放器基准，需先配置实际存在的输入再执行：

```powershell
python -m unittest discover -s tests -v
```

历史协议、GPU、UI 和导出证据的范围见 [RELEASE_NOTES.md](RELEASE_NOTES.md) 及 `HANDOFF.md`，其中本地研发记录不是普通用户的运行前提。客户端输入保持只读，新设置、缓存、导出和验证报告放在工程内。Git 不追踪 `build`、`toolchain`、`runtime`、缓存、导出素材或本机验证输出。
