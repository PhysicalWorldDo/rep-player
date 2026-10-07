#pragma once
#include "engine.hpp"
#include "client_protocol.hpp"
#include <unordered_set>

namespace rep {
enum class ExportFormat { Mov, Mp4, Png };
struct ExportOptions {
    ExportFormat format=ExportFormat::Mov;
    int fps=60;
    bool alpha=true;
    std::filesystem::path outputDirectory;
    std::wstring fileName;
    std::unordered_set<std::string> hiddenImages;
    ClientProtocolSelection protocol;
    CanvasSettings canvas;
};
struct ExportProgress {
    uint64_t completedFrames=0,totalFrames=0;
    int32_t sampledMilliseconds=0;
    std::wstring stage;
};
struct ExportResult {
    std::filesystem::path outputPath;
    int width=0,height=0,fps=0,durationMilliseconds=0;
    uint64_t frames=0,executedScenes=0,hiddenImageCount=0,compatibilityIgnoredInstructions=0;
    bool alpha=false;
};
// Call on the thread that owns gpu/assets. A separate device keeps UI playback live.
// Exceptions report loading/renderer/encoder failures; cancellation also throws Error.
// Samples are n/fps, including the first sample at or beyond the last REP timestamp.
ExportResult exportReplay(Gpu& gpu,Assets& assets,
    const std::filesystem::path& client,const std::filesystem::path& cache,
    const std::filesystem::path& replayPath,const ExportOptions& options,
    const std::function<void(const ExportProgress&)>& progress={},
    const std::function<bool()>& cancelled={});
}
