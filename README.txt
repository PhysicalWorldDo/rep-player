当前发布：v1.0.2（2026-10-03）。现行使用方法以 README.md 为准，构建方法见 BUILDING.md，版本与支持边界见 RELEASE_NOTES.md。
完整解压 rep-player-v1.0.2-windows-x64.zip 后，直接双击最外层 rep_player.exe。
以下保留旧版发布与本地研发记录，其中的 Start.cmd、旧目录布局和旧版本号不作为当前运行包指引。

v1.0.0 正式发布说明（2026-10-03）

最新使用方法请优先阅读 README.md，构建方法见 BUILDING.md。
完整解压 rep-player-v1.0.0-windows-x64.zip 后运行 Start.cmd，选择自己完整客户端根目录。
本版 FreeType/zlib/C++ 支持已静态链接，FFmpeg 随包提供，无需 Anaconda 或 VC++ 运行库。
下文保留本地研发期间的历史记录，包含旧构建路径与依赖说明；本版依赖以 THIRD_PARTY.txt 为准。

原生 REP 播放器

在本机运行（当前批准界面）

双击 D:\DNF115us\rep_player\Start.cmd，或直接启动 build\rep_player.exe。
左侧播放画面，右侧上方为目录分组播放列表与搜索，下方为 IMG 资源面板。
顶部“选择客户端”使用 Windows 文件夹选择器，指定整个客户端根目录。
Replay\SkillReplay、ImagePacks2、Fonts、Video/Bink 全部从该根解析。
设置和缓存保存在 runtime，默认输出为 exports。默认中文，可切换 EN。
搜索同时匹配中文技能名、英文 REP 文件名与 VP 中文别名，语言切换保留搜索和选择。
名称来源 ui_design\data\skill-name-map.json，按相对职业目录和文件键精确匹配；
未匹配文件保留原名，不猜翻译。默认客户端是 D:\115us\client。

单击 REP 自动播放，底部支持播放/暂停、停止和重播；结束保持最后一帧。
REP 条目整行（含第二行文件名和行内空白）均可点击；播放中点击另一项立即切换，
再次点击当前项从头播放。空格键控制播放/暂停，在播放列表、IMG 列表和普通按钮
获得焦点时同样有效；按住空格只切换一次，搜索框内仍正常输入空格。
底部“前帧 / 后帧”按 REP 记录顺序切换相邻帧，切换后自动暂停，并显示当前帧数。
点击播放画面后也可用 ← / → 切帧；时间戳相同的记录仍能逐帧查看。
拖动画面与右栏之间的竖分隔线可调右栏宽度，拖动播放列表与 IMG 之间的横分隔线
可调上下高度。布局保存在 runtime\layout.txt，下次启动恢复。
右栏采用深色细滚动条；职业/子目录使用展开箭头、独立标题、数量和技能缩进。
IMG “当前画面”显示真实执行调用的完整路径、帧号和依赖角色；隐藏后仍可恢复。
“全部 IMG”是当前 REP 整段目录，并区分注册、时间线调用和实际 GPU 绘制。
取消勾选隐藏完整路径的全部帧/实例，两个页签及导出共用状态。
IMG 页签下的“移除背景 / 移除怪物 / 移除人物”按常见路径一次性批量取消勾选，
作用于当前 REP 整段资源；人物和怪物目录中的 effect/effects 特效尽量保留。
分类采用近似路径规则，仍可手动勾回单项或点击“全部显示”恢复；切换 REP 时重置。
透明清屏保留 Alpha，地图画面需通过“移除背景”或 IMG 勾选隐藏。
字体优先使用客户端原有注册文件；中文版客户端缺少韩版字体名时使用现有中文
Noto 字体。字体通过宽字符路径读入内存，支持中文客户端目录。

“导出素材”导出整个 REP 画面（无 UI、无声音、原始尺寸）：
  MOV / ProRes 4444：默认，支持 Alpha；
  MP4 / H.264：不透明；
  PNG / RGBA 序列：逐帧无损，支持 Alpha。
可选 60 或 30 FPS、透明底、输出目录与文件名；本任务输出范围限 rep_player。
导出使用独立原生 D3D11 执行器和固定时间轴，不受实时追帧影响。
已有同名输出会提示修改文件名；错误或取消的 .partial 输出不视为完成素材。

也可直接打开指定文件：
  .\build\rep_player.exe --open "D:\115us\client\Replay\SkillReplay\ATPriest\LausDeAngelus.rep"
通过命令行导出（输出目录在本工程内）：
  .\build\rep_export.exe --client "D:\115us\client" --replay "D:\115us\client\Replay\SkillReplay\Swordman\BloodyRave.rep" --format mov --fps 60 --alpha 1 --output "D:\DNF115us\rep_player\exports" --name "BloodyRave"

播放器需要 Windows x64、D3D11 硬件 GPU 和对应客户端资源。
完整保留 build\ 下的 DLL、ffmpeg.exe 与 assets\shaders，不能单独搬走 exe。
运行时不需要 Python；播放和合成使用 C++20 / D3D11 GPU，不整片 CPU 预渲染。
声音关闭，原客户端和旧工程只读保留。

构建

在 PowerShell 中执行：
  Set-Location D:\DNF115us\rep_player
  .\build.ps1

已提供便携 LLVM MinGW / Clang 22.1.8、zlib 静态库和 FreeType 导入库/头文件。
脚本增量编译 src\，生成 build\rep_player.exe、rep_validate.exe、rep_resources.exe
及 rep_gpu.exe。编译器只用于构建，播放器不依赖编译器。原生依赖的来源和许可见
THIRD_PARTY.txt、licenses\ 和工具链自带许可文件。

执行方式和覆盖

protocol.cpp 校验容器长度/CRC，立即解开共享字典，保留压缩时间线并按 128 KiB
流式解压；版本 1.0–1.7 的指令 0–64、迁移和辅助流均按原生边界解释。
实时模式遵循客户端时间选择：当前 timestamp >= elapsed 时复用，否则继续读取
直到 timestamp >= elapsed，只绘制最终选中的场景；追帧时允许跳过中间场景。
逐场景全消费是独立验证模式。重播重置时间线并复用资源缓存。

GPU 使用原始 DXBC VS/PS、常量、完整图集、采样器、混合、模板和离屏纹理。
shader factory 0–66 的 59 个对象分支和 8 个空分支均有处理；type22 是对象存在但
program 为空，空 push/pop 保留 phantom 语义。58 种像素输出类型及其内部分支、
StaticBranch 512 种低位组合、全部 Metal/Shiny 等级与 Glow 选择、12 种 Dissolve
预设变体、字体 R/G/B/A 通道均有像素验证。
169 个导出 program 全部在硬件设备上创建；其中有些属于屏幕后处理和非 REP
材质。1,068 个 REP 着色器用例覆盖 91 个 program，不能把创建成功等同于逐像素验证。

相机跨场景持久，新/旧绘制的 registry 和 layer 选择分别处理。actor207 的 12 个
独立目标保持透明。字体保留四位覆盖量化、普通/描边配对、固定 bitmap、反向 UV、
渐变及文字发光第二遍绘制。AVI 通过原生 FFmpeg 流式解码，Bink 通过已安装的
客户端 DLL 解码；缺失视频/空指针跳过，缺失图像使用 interface/base.img[91] 占位。

验证结果

完整协议差分：3,064/3,064 文件精确 EOF，无失败；1,135,988 场景，336,534,193
次引用，461,419,048 字节辅助流。每文件比较 9 个字段，另有全部 65 指令和版本
边界的构造测试。报告：validation\protocol_library_differential.json。

真实 GPU 全消费：40/40 代表文件、8,174 场景通过，包含血剑、天罚和回归狙击；
报告和末帧图在 validation\representatives\。全消费不按实时时钟等待。
1,068/1,068 着色器用例通过，实际最大 RGBA8 单通道误差为 1，所有像素均满足 <=2
的验收界限。报告：validation\shader_pixel_differential.json。
上次完整 65 项 unittest 全绿，包含版本/指令/纹理/字体、双语目录、IMG 隐藏与 Alpha
导出，以及原生 UI 起播、暂停、切换、重播、末帧冻结、错误恢复和本次界面修订。
新增检查覆盖相邻记录切帧、相同时间戳、前后切帧恢复持久状态、隐藏状态保留，
以及两条分隔线拖动、最小化恢复、目录展开、滚动到末项和文字背景匹配。
本次日志与汇总：validation\ui_revision\；真实 BloodyRave / LausDeAngelus 切帧
像素结果与完整顺序执行一致，证据在 validation\frame_step\real_steps_summary.json。
上次 UI / 导出交付及 alternate_client 的证据保留在 validation\native_ui_v2\。
本次整行点击与空格修复后，两项新增回归和四项原有原生窗口用例均已通过。
新用例在修复前有效失败，覆盖播放中切换、快速连续点击、当前项重播，以及列表/
IMG/按钮焦点下的空格、长按抑制和搜索输入。日志与汇总：validation\ui_input\。
中文客户端字体与 IMG 批量移除修订后，6 项字体回归、6 项原有窗口回归和 1 项
批量移除窗口回归共 13 项通过。用户当前中文客户端的 DarkFlameSlash_VP2.rep
完成全部 215 个场景并精确 EOF，无 FreeType 错误；原字体字形/图集/布局检查保持通过。
批量移除检查覆盖 32 条匹配路径、技能特效保留、预览变化、手动勾回、全部恢复，
以及中英标签、最窄侧栏和切 REP 清除状态。证据：validation\img_filters\acceptance.json。
桌面工具能读取原生控件并通过键盘打开目录选择器，但截图捕获超时，且无法定位
其子对话框输入控件。因此本次不宣称完整人工文件夹选择、导出弹窗及视觉布局已验收。

性能

实测硬件为 NVIDIA GeForce RTX 4070 Ti SUPER。最终起播、帧提交和记录时长对比
写在 validation\native_ui_v2\ui_benchmark.json。
该计时来自上次 UI / 导出交付，本次界面修订未重复性能测量。
新进程首幅画面 0.809 秒，首次打开天罚 0.582 秒，
热重播 0.0315 秒；切换回归狙击 0.147 秒。
天罚记录 9.793 秒，实际播放 9.8078 秒；热重播
实际播放 9.8053 秒。首次实时时钟绘制 547/557 场景，追帧跳过
10 个中间场景。帧耗时 p50 6.61 ms、p95 10.41 ms、
p99 17.86 ms、最大 155.41 ms。
“进程首次画面”包含本程序 UI/树/设备初始化到第一次 Present；“首次打开”从
选中 REP 到 Present；“热重播”在同进程复用缓存。未清空操作系统磁盘缓存，
因此这里的新进程测量不等于系统断电后的冷磁盘测量。
帧统计测量 CPU 执行加 Present 的耗时，不是 GPU timestamp query。峰值加载
会触发原生追帧语义；录像仍按记录时钟播放，不强制显示每一条中间场景。

复验命令

以下 Python 只用于只读旧基准和生成验证输出；所有输出在本目录 validation\：
  python -m unittest discover -s tests -v
  python tools\validate_library.py
  python tools\validate_shader_pixels.py
  python tools\validate_representatives.py
  python tools\collect_acceptance.py
需要本机已有 Python、NumPy/Pillow 和 D:\DNF115us\skill_player 只读基准。
原生工具也可独立执行：
  .\build\rep_gpu.exe --smoke
  .\build\rep_validate.exe --dump "完整 REP 路径"
  .\build\rep_gpu.exe --consume "完整 REP 路径" "本目录内报告.json"
  .\build\rep_player.exe --ui-benchmark "D:\DNF115us\rep_player\validation\ui_benchmark.json"
--ui-benchmark 会打开实际原生窗口，播放天罚、热重播及回归狙击后自行关闭。

取证和验证的明确边界

MaskBlur 原导出 VS 的 POSITION 不是 SV_POSITION。为使该分支在 D3D11 光栅化，
程序使用客户端已有的通用 VS，添加一个 GPU 几何阶段桥接像素坐标，保留原 PS。
这是本播放器的兼容重建；原始导出文件仍保留，未宣称找到了客户端同样的修复。
离屏捕获与模板的合成也使用本播放器的小型 GPU 工具 shader。

小图纹理按原生最小 16 / 二次幂尺寸上传，保持逻辑尺寸和图集 UV；新增空白区域
在本播放器中初始化为零。客户端原池复用区的内容未被静态取证证明总是零。
未启动游戏进行动态逐帧对照；正确性证据来自客户端只读取证、旧基准差分、
构造用例、原字节码 GPU 像素结果和本播放器实际 UI 测量。

隔离 oracle 保存在 validation\oracle；旧项目没有改写。验证端修正了原模拟器
整数 literal 位模式、MAD 单次舍入、双线性权重精度、图集/纹理池投影、辅助槽
调用顺序和 Gauge 的 float32 截断；这些修正不会参与运行时播放。

BloodyRave 位置修复（2026-10-03）

build\rep_player.exe 已修复红色吸引漩涡落在左下 NPC 附近的问题，仍通过 Start.cmd 启动。
根因是客户端 DX9/DX11 矩阵 API 会先取负记录角度，旧实现直接使用记录角度。
engine.cpp 的通用 affine 已统一主旋转和二次缩放轴的角度约定；没有技能专属偏移。
同一原始 BloodyRave.rep 的第 65 场景（1116 ms）中，cunsume/cunsume2 已回到手前和目标周围。
修复前后原生 800×600 对照：validation\bloodyrave_fix\final\before_after.png。

新增 3 项合成测试含 26 个 GPU 子用例，以及 1 项真实 BloodyRave 场景回归。
这些用例在修复前失败；修复后完整 39 项 unittest 通过，40 个代表录像的 8,174 场景通过。
BloodyRave 原版 197、VP1 476、VP2 385 场景均完成 GPU 全消费，精确 EOF，无 IMG 占位回退。
UI 自动播放、切换、重播、末帧冻结和错误恢复检查通过；声音继续关闭。
完整验证日志、原生旋转指令取证、资源/命令追踪和修复画面保存在 validation\bloodyrave_fix\。
汇总：validation\bloodyrave_fix\summary.json。

LausDeAngelus 武器修复（2026-10-03）

build\rep_player.exe 已修复花盘武器缩小、杖身脱离人物手部的问题。
根因是 Effect 16 的 StaticBranch_Draw 在没有 preset 尾部参数时误选 program 116；
原生 146F7FB30 对 selector 116 始终请求内置 program 4。bindings.cpp 已补齐该选择，
由默认绘制保留 IMG 的纹理补齐/图集映射，没有调整人物或武器几何坐标。
原始 LausDeAngelus.rep、NPK/IMG 和旧 skill_player 保持只读。

新增 3 项测试含 12 个 GPU 子用例，修复前全部有效失败，修复后通过。
完整 42 项 unittest、1,068 个着色器用例、40 个代表录像的 8,174 场景均通过；
之前的 BloodyRave 场景位置、UI 自动播放/切换/重播/末帧冻结/错误恢复也通过。
原始 LausDeAngelus 的 1,119 场景完成 GPU 全消费、精确 EOF，资源回退为零。
已查看初始、施法和结束场景；隔离武器 Effect 16 与默认绘制的 RGBA 完全一致，
原始初始场景与正确武器绘制诊断也完全一致，两组最大通道误差均为零。

修复前后：validation\lausdeangelus_fix\final\before_after_scene20.png。
武器和手部放大：validation\lausdeangelus_fix\final\weapon_hand_before_after_4x.png。
取证、日志和实际画面：validation\lausdeangelus_fix\。
汇总：validation\lausdeangelus_fix\summary.json。

素材导出实测（2026-10-03）

当前 BloodyRave 导出完整消费 197 个场景，原始尺寸 800×600，记录时长 3282 ms。
MOV / ProRes 4444 和 PNG / RGBA 在 60 FPS 下各有 198 帧；MP4 / H.264 在
30 FPS 下有 100 帧。MOV、MP4 全段解码通过，视频尺寸、帧率、帧数和无音频均已校验。
隐藏两条地图 IMG 后，同一采样帧的 MOV/PNG 各有 397,983 个完全透明像素和
82,017 个可见像素；MP4 解码后全部像素 Alpha 为 255。
报告与素材：validation\new_ui_export\bloodyrave_20261003_122019\summary.json。

采样包含到达末场景后的最后一帧，所以文件时长会比记录时长多不到两帧：本例
60 FPS 为 3.300 秒，30 FPS 为 3.333 秒。透明 PNG 合成黑底与同帧不透明渲染的
最大 RGB 通道差为 1，没有差超过 2 的像素。

普通 RGBA 素材不能携带客户端的加法、乘法等混合操作。GPU 导出转换会在需要时
提高 Alpha 覆盖度，保留原生加法效果的可见颜色能量；未使用色键。黑底结果已按
上述方式验证，但在其他背景上用普通 Alpha 合成，不能完全复现所有原生混合模式。
