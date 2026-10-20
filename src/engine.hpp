#pragma once
#include "bindings.hpp"
#include "fonts.hpp"
#include "canvas.hpp"
#include "image_categories.hpp"
#include <optional>
#include <functional>
#include <chrono>

namespace rep {
struct ImgCall {
    std::string path;
    int frame=-1;
    uint64_t count=0;
    uint64_t timelineCount=0,runtimeCount=0;
    uint32_t layer=0;
    std::string role="draw";
    bool dependency=false,drawn=false,registered=false,hidden=false;
};
struct ReplayInspection {std::vector<ImgCall> images;int32_t durationMilliseconds=0;uint64_t scenes=0;};
// Consumes an independent Replay; records dispatched timeline references without GPU/image decoding.
ReplayInspection inspectReplayImages(Replay& replay,const std::function<bool()>& cancelled={});
struct Camera {float x=0,y=0,width=800,height=600,zoom=1;};
struct ExecutionStats {
    uint64_t scenes=0,references=0,primitives=0,nullResources=0,emptyImages=0,nullCaptures=0,nullCaches=0,audioEvents=0,phantomPushes=0,cameraUpdates=0,actorPoolUpdates=0,gridCells=0,stencilDraws=0,samplerDraws=0;
    uint64_t contextAllocations=0,contextBindings=0,contextReleases=0,compatibilityIgnoredInstructions=0;
    int lastContext=10;
    std::array<uint64_t,67> opcodes{};
    std::array<uint64_t,67> effects{};
};
class Movies;
class Executor {
public:
    struct State;
    struct Sprite;
private:
    Gpu& gpu_;Assets& assets_;Binder binder_;Fonts fonts_;
    Replay* replay_=nullptr;
    Target output_,canvas_,working_,raw_,localMask_,globalMask_;
    CanvasSettings canvasSettings_;
    CanvasLayout canvasLayout_;
    bool localCapture_=false,globalCapture_=false;
    int localTarget_=0,globalTarget_=0;
    std::map<std::pair<int,uint32_t>,Camera> cameras_;
    std::array<std::optional<Target>,12> actors_;
    std::array<std::optional<Target>,12> contexts_;
    std::array<bool,12> contextBound_{};
    int selectedContext_=219;
    std::array<Camera,12> actorCameras_{};
    std::map<int,std::array<float,2>> samplerOffsets_;
    unsigned lastBlend_=0;
    std::unique_ptr<Movies> movies_;
    std::vector<ImgCall> currentImages_,allImages_;
    bool trackHighlights_=false;
    std::shared_ptr<HighlightFrame> highlightFrame_;
    std::unordered_set<std::string> hiddenImages_;
    ImageVisibilityPolicy imageVisibility_;
    bool transparent_=false,localHidden_=false,globalHidden_=false;
    bool negativeImgOffsetsEnabled_=false;
    int32_t durationMilliseconds_=0;
    std::map<std::pair<int,uint32_t>,Camera> sceneEntryCameras_;
    std::map<int,std::array<float,2>> sceneEntryOffsets_;
    unsigned sceneEntryBlend_=0;
    uint32_t sceneEntryRandom_=1;
    bool sceneEntryValid_=false;
    size_t recordImage(std::string path,int frame,uint32_t layer,std::string role,bool dependency,bool drawn=false);
    void markDrawn(size_t call);
    bool hidden(const std::string& path)const;
    bool negativeImgFrameSuppressed(const Frame& frame)const;
    Target& renderTarget(int context);
    void resizeCanvas(const CanvasLayout& layout);
    std::array<int,4> canvasClip()const;
    std::array<int,4> physicalClip(std::array<int,4> clip)const;
    void draw(Sprite sprite,State& state);
    void capture(const Instruction& instruction,std::span<const uint8_t> payload,bool global,State& state);
public:
    ExecutionStats statistics;
    Executor(Gpu& gpu,Assets& assets,const std::filesystem::path& cache,const std::filesystem::path& clientRoot={});
    ~Executor();
    void enableImageHighlightTracking(bool enabled){trackHighlights_=enabled;if(!enabled)highlightFrame_.reset();}
    std::shared_ptr<const HighlightFrame> highlightFrame()const{return highlightFrame_;}
    void attach(Replay& replay);
    void setCanvasSettings(const CanvasSettings& settings);
    const CanvasSettings& canvasSettings()const{return canvasSettings_;}
    const CanvasLayout& canvasLayout()const{return canvasLayout_;}
    void prepare(const std::function<bool()>& cancelled={});
    void execute(const Scene& scene);
    void redraw(const Scene& scene);
    void resetPlaybackState();
    void setHiddenImages(std::unordered_set<std::string> paths);
    void setImageVisibility(const ImageVisibilityPolicy& policy);
    void setNegativeImgOffsetsEnabled(bool enabled){negativeImgOffsetsEnabled_=enabled;}
    bool negativeImgOffsetsEnabled()const{return negativeImgOffsetsEnabled_;}
    std::unordered_set<std::string> hiddenImages()const;
    const auto& currentImages()const{return currentImages_;}
    const auto& allImages()const{return allImages_;}
    void setTransparent(bool transparent){transparent_=transparent;}
    bool transparent()const{return transparent_;}
    void setInspection(ReplayInspection inspection);
    int32_t durationMilliseconds()const{return durationMilliseconds_;}
    Target& output(){return output_;}
    const auto& cameras()const{return cameras_;}
};
class Playback {
    Replay* replay_=nullptr;Executor* executor_=nullptr;Scene scene_;
    bool hasScene_=false,ended_=false,stopped_=true,paused_=false;
    bool sequentialState_=true,contextDependent_=false;
    std::chrono::steady_clock::time_point start_;
    int64_t elapsed_=0,clockElapsed_=0;
    double playbackRate_=1;
    bool rebuildToOrdinal(uint64_t ordinal);
public:
    uint64_t selected=0,skipped=0;
    void attach(Replay& replay,Executor& executor);
    void start();void stop();void pause();void resume();
    void setPlaybackRate(double rate);
    double playbackRate()const{return playbackRate_;}
    bool select(int64_t elapsedMilliseconds);
    bool tick();
    bool seek(int64_t elapsedMilliseconds,bool endpointLast=false,const std::function<bool()>& cancelled={},const std::function<void(uint64_t)>& progress={});
    bool seekOrdinal(uint64_t ordinal,const std::function<bool()>& cancelled={},const std::function<void(uint64_t)>& progress={},bool forceRebuild=false);
    void setElapsedMilliseconds(int64_t elapsed);
    void finishNavigation(int64_t elapsed,bool playing,bool ended=false,bool stopped=false);
    bool step(int direction);
    bool refresh();
    bool setCanvasSettings(const CanvasSettings& settings);
    bool setNegativeImgOffsetsEnabled(bool enabled);
    bool paused()const{return paused_;}
    int64_t elapsedMilliseconds()const;
    bool ended()const{return ended_;}
    bool playing()const{return !stopped_&&!paused_&&!ended_&&replay_;}
    int32_t timestamp()const{return scene_.timestamp;}
    uint64_t ordinal()const{return scene_.ordinal;}
};
}
