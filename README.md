# 原生 REP 播放器

用于离线播放 DNF 客户端中的技能 REP 录像，采用 C++20、Win32 和 D3D11。提供中英搜索、逐帧查看、IMG 显示控制，以及 MOV、MP4 和 PNG 序列导出。声音关闭。

## 下载与运行

从 [GitHub Releases](https://github.com/PhysicalWorldDo/rep-player/releases) 下载 `rep-player-v1.0.0-windows-x64.zip`，**完整解压**到一个可写目录，然后双击 `Start.cmd`。也可以运行 `build\rep_player.exe`。

运行需要 Windows 10 / 11 x64、支持 D3D11 的硬件 GPU，以及用户自行准备的完整客户端。FreeType、zlib 和 C++ 运行支持已静态链接，FFmpeg 与播放器资源随包提供，无需另装 VC++ 运行库、Python 或编译器。请保留 `build`、`assets`、`ui_design` 等目录的相对位置，不能单独搬走 EXE。

首次运行点击顶部“选择客户端”，选择包含以下内容的**客户端根目录**，例如 `D:\MyDNFClient`：

```text
MyDNFClient\
├─ Replay\SkillReplay\    REP 录像
├─ ImagePacks2\           NPK / IMG 图像资源
├─ Fonts\                录像使用的字体
├─ Video\                录像使用的视频
└─ bink2w64.dll           客户端 Bink 视频解码器（使用 Bink 时需要）
```

不要只选择 `Replay\SkillReplay` 或 `ImagePacks2`。录像、图像、字体和视频均从同一个客户端根目录读取；缺少相应资源会影响画面。客户端原始文件保持只读，播放器不会启动游戏。

选择后，右侧上方按职业和子目录显示播放列表，左侧显示播放画面，右侧下方显示 IMG 资源。默认中文，顶部可切换 EN。客户端选择、语言和面板布局保存在程序目录下的 `runtime`，缓存也写入 `runtime`；默认导出目录为 `exports`。

## 播放与查看

| 操作 | 行为 |
| --- | --- |
| 点击任意 REP 条目整行 | 立即切换并自动播放；第二行文件名和行内空白也可点击 |
| 再点当前 REP | 从头重播 |
| 播放 / 暂停、停止、重播 | 使用底部按钮；播放结束保留最后一帧 |
| 空格 | 播放 / 暂停；列表、IMG 和普通按钮获得焦点时同样有效，按住只切换一次 |
| 搜索框内的空格 | 正常输入，不触发播放控制 |
| 前帧 / 后帧 | 按 REP 记录序号查看相邻记录，切换后自动暂停 |
| 点击画面后按 ← / → | 前帧 / 后帧；相同时间戳的记录也能分别查看 |
| 拖动竖分隔线 | 调整画面与右栏宽度 |
| 拖动右栏横分隔线 | 调整播放列表与 IMG 面板高度 |

搜索同时匹配中文技能名、英文 REP 文件名和已登记的 VP 中文别名。切换语言保留搜索条件和当前选择；未登记中文名称的文件显示原名。

实时播放按录像时钟选择场景，追帧时可能跳过中间记录。逐帧查看按记录顺序执行，适合检查同时间戳记录和局部画面。

## IMG 显示控制

“当前画面”列出当前执行调用的完整 IMG 路径、帧号和依赖角色；“全部 IMG”列出当前 REP 整段资源，并区分注册、时间线调用和实际 GPU 绘制。取消勾选会隐藏该完整路径的所有帧和实例。两个页签、播放预览和素材导出共用隐藏状态。

三个批量按钮作用于**当前 REP 整段资源**，每次点击执行一次隐藏：

| 按钮 | 常见路径规则 |
| --- | --- |
| 移除背景 | `map` / `background` |
| 移除怪物 | `monster`，尽量保留其 `effect` / `effects` 特效目录 |
| 移除人物 | `character` / `npc`，尽量保留其 `effect` / `effects` 特效目录 |

路径分类是近似规则，可能需要手动调整。可重新勾选任意资源，或点击“全部显示”恢复。切换 REP 会清除隐藏状态。透明清屏不会自动移除地图背景；导出透明效果前，先用“移除背景”或手动勾选处理画面中不需要的资源。

## 导出素材

选好 REP 和 IMG 显示状态后，点击底部“导出素材”。输出为整个 REP 画面，使用录像原始尺寸，无界面、无声音；可选择 30 / 60 FPS、透明底、输出目录和文件名。输出目录须位于解压后的程序根目录内，默认使用 `exports`。

| 格式 | 用途与透明度 |
| --- | --- |
| MOV / ProRes 4444 | 默认格式，支持 Alpha |
| MP4 / H.264 | 不透明视频 |
| PNG / RGBA 序列 | 逐帧无损图像，支持 Alpha |

导出使用独立 D3D11 执行器和固定时间轴，不受预览实时追帧影响。采样包含到达末场景后的最后一帧，输出时长可能比录像记录时长多不到两帧。同名输出不会直接覆盖；请修改文件名。取消或出错留下的 `.partial` 输出不属于完成素材。

普通 RGBA 素材不能携带客户端的加法、乘法等混合操作。导出会尽量保留效果的可见颜色，但把透明素材合成到其他背景上时，部分特效无法完全复现原生混合结果。不是所有第三方软件都支持 ProRes 4444 Alpha；需要透明度时请在支持该格式的编辑软件中检查，或使用 PNG 序列。

## 命令行

以下命令在解压后的程序根目录中执行。将客户端和录像路径替换为自己的路径：

```powershell
.\build\rep_player.exe --client "D:\MyDNFClient" --open "D:\MyDNFClient\Replay\SkillReplay\Swordman\BloodyRave.rep"
```

命令行导出示例：

```powershell
.\build\rep_export.exe --client "D:\MyDNFClient" --replay "D:\MyDNFClient\Replay\SkillReplay\Swordman\BloodyRave.rep" --format mov --fps 60 --alpha 1 --output ".\exports" --name "BloodyRave"
```

`--format` 支持 `mov`、`mp4`、`png`；`--fps` 支持 `30`、`60`；`--alpha` 使用 `0` / `1`，MP4 始终不透明。可重复传入 `--hide "完整 IMG 逻辑路径"` 指定隐藏资源。

## 常见问题

- **选择客户端提示目录不正确：** 选择同时包含 `Replay\SkillReplay` 和 `ImagePacks2` 的上一级目录。
- **EXE 提示缺少 DLL，或无法找到 FFmpeg / shader：** 重新完整解压发布包，保持目录结构；不要只复制 EXE。
- **部分画面出现占位图或视频缺失：** 检查该版本客户端的图像、字体和视频是否齐全。不同客户端版本的 REP 与资源不保证相互兼容。
- **中文路径或字体：** 已支持宽字符路径读取字体；优先使用客户端原字体，缺少韩版字体名称时使用客户端内现有的中文 Noto 字体。仍需保留客户端 `Fonts`。
- **导出有背景或效果不完整：** 检查 IMG 勾选状态、格式和透明选项；按路径移除时可手动勾回误隐藏的素材。
- **导出提示目录范围错误：** 使用程序目录内的 `exports` 或其他子目录。

## 源码、构建与验证

源码仓库：[PhysicalWorldDo/rep-player](https://github.com/PhysicalWorldDo/rep-player)。构建准备、编译命令和验证边界见 [BUILDING.md](BUILDING.md)，本版本功能及证据见 [RELEASE_NOTES.md](RELEASE_NOTES.md)。第三方组件与许可见 [THIRD_PARTY.txt](THIRD_PARTY.txt) 和 `licenses`。

仓库和发布包不包含客户端原始 REP、NPK、IMG、Fonts、Video、游戏 EXE、Bink DLL，也不包含用户设置、缓存或导出素材。播放器使用的 shader 资源保留客户端来源记录，详见 `assets\shaders\manifest.json` 与第三方说明；它们的原始权利归各自权利人。

`README.txt` 和 `HANDOFF.md` 保留本地研发期间的详细历史。历史记录中的机器路径和旧基准不属于普通用户运行前提。
