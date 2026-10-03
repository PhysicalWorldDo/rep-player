# FFmpeg 对应源码

运行包中的 `build/ffmpeg.exe` 是未修改的 BtbN Windows x64 静态 GPLv3 构建。播放器通过独立进程调用它。二进制、源代码版本与构建输入对应关系见 [THIRD_PARTY.txt](../THIRD_PARTY.txt)。

[v1.0.0 Release](https://github.com/PhysicalWorldDo/rep-player/releases/tag/v1.0.0) 单独提供两份完整源码附件：

- `Open-Video-Craft-1.0.3-Windows-FFmpeg-Build-Sources.tar.gz`：固定版本 BtbN 构建脚本和 FFmpeg 源码。
- `Open-Video-Craft-1.0.3-Windows-FFmpeg-Dependency-Sources.tar`：全部依赖的源码下载缓存，包括各自许可。
- `SHA256SUMS.txt`：原分发者的校验清单。

普通用户只需要运行 ZIP。源码附件用于查看或重建第三方工具，不需要放入播放器目录。

以下步骤使用 Linux、Bash、Python 3、Git 与 Docker（含 BuildKit）。将两个源码附件放在同一目录，从该目录执行。核心包内实际路径为 `upstream-archives/`；依赖包的顶层路径为 `downloads/`，所以应解压到 BtbN 树的 `.cache`，不能再多套一层 `downloads`。

```sh
set -e
source_dir="$(pwd)"
btbn_commit=8c736b2d6fe5da2a10a8896d01e53bfb0ca4f665
ffmpeg_commit=703dcc25b91eacd2ab8b8b2fe888dc8d7ab4ad6d

printf '%s  %s\n' \
  1999c647b7945d2fddd9ff303dcc133a7740ad1e791c257afa5e8a7211782686 \
  Open-Video-Craft-1.0.3-Windows-FFmpeg-Build-Sources.tar.gz \
  f5375da5ffa4f85a7dac99108a2aa36d3030b8832b0008b6a453f747aa47fdbb \
  Open-Video-Craft-1.0.3-Windows-FFmpeg-Dependency-Sources.tar | sha256sum -c -

mkdir ffmpeg-rebuild
tar -xzf Open-Video-Craft-1.0.3-Windows-FFmpeg-Build-Sources.tar.gz \
  -C ffmpeg-rebuild
cd ffmpeg-rebuild
sha256sum -c SOURCE_SHA256SUMS.txt
tar -xzf "upstream-archives/btbn-ffmpeg-builds-${btbn_commit}.tar.gz"
cd "FFmpeg-Builds-${btbn_commit}"
mkdir -p .cache
tar -xf "$source_dir/Open-Video-Craft-1.0.3-Windows-FFmpeg-Dependency-Sources.tar" \
  -C .cache
test -f .cache/downloads/50-x264.tar.xz
```

原始 `build.sh` 在生成的 Docker 内部构建脚本中克隆 `release/8.1` 分支；不会自动使用附件中的核心 tar。原脚本也把执行当天作为版本后缀。使用以下一次性补丁，在其 `cd ffmpeg` 后、`./configure` 前固定实际源码 commit，并固定版本日期；断言会阻止对不匹配的脚本静默修改。这里保留原 configure 的所有参数。

```sh
python3 - <<'PY'
from pathlib import Path
p = Path("build.sh")
s = p.read_text()
old = "    cd ffmpeg\n\n    ./configure"
new = (
    "    cd ffmpeg\n"
    "    git checkout --detach 703dcc25b91eacd2ab8b8b2fe888dc8d7ab4ad6d\n"
    "    test \"\\$(git rev-parse HEAD)\" = "
    "703dcc25b91eacd2ab8b8b2fe888dc8d7ab4ad6d\n\n"
    "    ./configure"
)
old_date = r'--extra-version="\$(date +%Y%m%d)"'
assert s.count(old) == 1, "Unexpected build.sh source checkout section"
assert s.count(old_date) == 1, "Unexpected build.sh date section"
s = s.replace(old, new).replace(old_date, '--extra-version="20260721"')
p.write_text(s)
PY
bash -n build.sh
```

然后生成并实际构建依赖镜像，再运行 FFmpeg 构建；仅执行 `generate.sh` 会生成 Dockerfile，不会编译依赖。

```sh
export GITHUB_REPOSITORY=btbn/ffmpeg-builds
export DOCKER_BUILDKIT=1
./generate.sh win64 gpl 8.1
docker build --progress=plain \
  -t ghcr.io/btbn/ffmpeg-builds/win64-gpl-8.1:latest .
./build.sh win64 gpl 8.1
```

输出在该 BtbN 树的 `artifacts/`。依赖镜像从公开 `base-win64:latest` 开始，依赖源码从已提供缓存编译；Docker 基础镜像、编译器与工具链的版本可能不同，所以以上是固定应用源码/依赖输入的重建步骤，不保证产物逐字节一致。需要网络获取 Docker 基础镜像和固定 FFmpeg Git commit；附件中的核心源码 tar 也提供了相同源内容。没有在本次 Windows 发布中实际执行整套 Linux/Docker 重编，只核实了附件路径、固定脚本内容和补丁语法。

本包实际分发的未修改 `ffmpeg.exe` SHA-256 为 `070be6f5202e71a5e0bec88312230eebf2708f9b9ee3694596babf20902dddd2`。原 BtbN 历史 Release 已不可获取；我们从 Open Video Craft v1.0.3 的 portable 容器中只提取 FFmpeg，并核验与其固定版本记录一致，没有执行该容器或重新编译分发的 FFmpeg。

出处：[固定版本构建说明](https://raw.githubusercontent.com/Reubencfernandes/Open-Video-Craft/v1.0.3/FFMPEG_WINDOWS_BUILD_NOTES.md)、[固定版本二进制与源码记录](https://raw.githubusercontent.com/Reubencfernandes/Open-Video-Craft/v1.0.3/scripts/prepare-windows-ffmpeg.mjs)、[v1.0.3 源码附件](https://github.com/Reubencfernandes/Open-Video-Craft/releases/tag/v1.0.3)。
