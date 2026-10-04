# REP 播放器

离线播放 DNF 客户端中的 REP 录像，支持中英搜索、逐帧查看、IMG 显示控制，以及 MOV、MP4 和 PNG 序列导出。适用于 Windows 10 / 11 x64，声音关闭。

## 下载与运行

从 [v1.0.4 Release](https://github.com/PhysicalWorldDo/rep-player/releases/tag/v1.0.4) 下载 `rep-player-v1.0.4-windows-x64.zip`，完整解压到可写目录，直接双击最外层的 **rep_player.exe**。

```text
解压目录/
├─ rep_player.exe
└─ resources/
   ├─ ffmpeg.exe
   └─ licenses/
```

保留整个 `resources` 目录。无需安装 Python、编译器或 VC++ 运行库。运行需要支持 D3D11 的显卡，以及自行准备的完整客户端。

首次打开后点击顶部“选择客户端”，选择包含 `Replay` 和 `ImagePacks2` 的客户端根目录。录像、图像、字体和视频都从该目录读取；使用字体和视频的录像还需要客户端中的 `Fonts`、`Video`，Bink 视频需要对应的 `bink2w64.dll`。播放器只读取客户端文件。

## 功能

- 按实际目录层级列出整个 `Replay` 目录下的 REP，包括根部文件、多层子目录和大写 `.REP` 扩展名。
- 技能录像显示中英文名称，支持中文技能名、英文文件名、VP 名称和相对路径搜索。包含帝国骑士、女蓝拳的技能及 VP 中文名称；未登记名称的文件显示原名。
- 点击列表条目自动播放，再点当前条目从头重播；播放结束保留末帧。
- 顶部“打开 REP”可选择任意位置的录像。列表内文件会自动展开并定位；列表外文件使用当前所选客户端的资源。
- 中文 / EN 界面切换；可拖动分隔线调整画面、列表和 IMG 面板大小。

| 操作 | 行为 |
| --- | --- |
| 播放 / 暂停、停止、重播 | 控制当前录像 |
| 空格 | 播放 / 暂停；搜索框内正常输入空格 |
| 前帧 / 后帧 | 按记录顺序查看相邻帧并暂停 |
| 点击画面后按 ← / → | 前帧 / 后帧 |

实时播放可能跳过中间记录以跟随录像时间。逐帧查看按记录顺序执行，相同时间戳的记录也可分别查看。

## IMG 显示控制

右侧“当前画面”显示当前调用的 IMG，“全部 IMG”显示整段录像的资源。取消勾选可隐藏对应素材，预览和导出共用隐藏状态。

“移除背景”“移除怪物”“移除人物”按常见资源路径批量隐藏素材，尽量保留特效。可手动勾回任意条目，或点击“全部显示”恢复。切换 REP 会重置隐藏状态。

透明底不会自动移除地图背景；导出透明素材前，可先隐藏背景。

## 导出素材

点击底部“导出素材”，选择格式、30 / 60 FPS、透明底、目录和文件名。输出使用录像原始尺寸，无界面、无声音。

| 格式 | 透明度 |
| --- | --- |
| MOV / ProRes 4444 | 支持 Alpha |
| MP4 / H.264 | 不透明 |
| PNG / RGBA 序列 | 支持 Alpha，逐帧无损 |

默认输出到程序目录中的 `exports`；其他输出目录也须位于程序目录内。同名文件不会覆盖，请使用新的文件名。采样保留末帧，输出时长可能比录像多不到两帧。取消或失败留下的 `.partial` 文件不是完成素材。

普通 RGBA 无法保存所有加法、乘法混合效果，透明素材在其他背景上的合成可能与原画面有差异。透明视频需要支持 ProRes 4444 Alpha 的编辑软件，也可使用 PNG 序列。

## 设置与缓存

客户端、语言和布局设置保存在程序目录的 `runtime` 中。切换到不同 REP 时自动释放旧录像的图像和纹理缓存；同一录像重播保留热缓存。支持 `Neople Video Fil` 和 `Neople Video Stream` 视频封装；解包临时文件在停止使用、切换录像或关闭播放器时回收，避免长期积累。设置和已导出素材会保留。

## 指定录像启动

也可从 PowerShell 指定客户端及录像：

```powershell
$CLIENT = Read-Host '输入完整客户端根目录'
.\rep_player.exe --client $CLIENT --open (Join-Path $CLIENT 'Replay\SkillReplay\Swordman\BloodyRave.rep')
```

将录像相对路径替换为实际文件。播放器会根据客户端的 `DFO.exe` / `DNF.exe` 自动选择协议，支持 REP 1.0–1.8。DFO 使用 `dfo`，DNF 使用 `dnf-compatible`；自建资源目录可用 `--profile` 指定。

DNF 兼容模式仅对 REP 1.8 / minor 6 中完整、独立的 `4200000000` 字典命令进行跳过，窗口会提示“兼容播放”。该 opcode 66 的原生渲染作用尚未确认，画面可能与客户端不同。其他未知指令、非零载荷、嵌入命令及不同版本仍会报错。使用 `--profile dnf-july` 可恢复严格的 7.09 读取合同，保留对 66 的拒绝。

文字显示或解码异常时，可按录像来源指定 `--codepage 949`、`--codepage 936` 或 `--codepage 65001`。

## 常见问题

- **提示客户端目录不正确：** 选择同时包含 `Replay` 和 `ImagePacks2` 的上一级目录。
- **缺少 FFmpeg：** 完整解压发布包，保留 `resources/ffmpeg.exe`。
- **画面出现占位图、字体或视频缺失：** 确认客户端资源齐全，并与录像版本匹配。客户端中的中文 Noto 字体可作为部分韩版字体的替代。
- **导出有背景或效果不完整：** 检查 IMG 勾选、透明选项和格式，可手动恢复误隐藏的素材。
- **导出提示目录范围错误：** 使用程序目录内的 `exports` 或其他子目录。
- **录像提示不支持的指令：** 部分历史或不同客户端版本的录像尚不兼容，可查看错误信息定位具体文件。

## 源码与许可

源码见 [PhysicalWorldDo/rep-player](https://github.com/PhysicalWorldDo/rep-player)，构建方法见 [BUILDING.md](BUILDING.md)，更新记录见 [RELEASE_NOTES.md](RELEASE_NOTES.md)。

第三方来源与许可见 [THIRD_PARTY.txt](THIRD_PARTY.txt) 和 [licenses](licenses)；运行包中的通知位于 `resources/licenses`。FFmpeg 对应源码链接见 [FFMPEG_SOURCE.md](licenses/FFMPEG_SOURCE.md)。

仓库和运行包不包含客户端原始 REP、NPK、IMG、字体、视频、游戏程序、Bink DLL、用户设置、缓存或导出素材。
