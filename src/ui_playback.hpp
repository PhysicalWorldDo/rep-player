#pragma once
#include "engine.hpp"
#include "client_protocol.hpp"
#include "canvas.hpp"
#include <windows.h>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>

namespace rep::ui {
using Clock = std::chrono::steady_clock;
enum class Phase { Empty, Loading, Playing, Paused, Ended, Stopped, Error };
struct PlayerStatus {
    Phase phase=Phase::Empty;
    std::wstring message,path;
    std::filesystem::path client;
    int timestamp=0,duration=0,width=800,height=600;
    int canvasWidth=800,canvasHeight=600,canvasLeft=0,canvasTop=0,canvasRight=0,canvasBottom=0;
    int64_t elapsed=0;
    uint64_t ordinal=0,frames=0,frameCount=0,skipped=0,captureSerial=0;
    double readySeconds=0,maxFrameMilliseconds=0,processReadySeconds=0,p50Frame=0,p95Frame=0,p99Frame=0,playbackSeconds=0;
    bool frozen=false,compatibilityIgnored=false;
    uint32_t finalCrc=0,frameCrc=0;
    std::vector<ImgCall> currentImages,allImages;
    std::unordered_set<std::string> hiddenImages;
};

// UI commands cross this boundary; all immediate-context and replay access stays
// on the render thread. Resource inspection has its own Replay stream.
class PlaybackController {
    HWND canvas_;
    std::filesystem::path root_,client_,requested_;
    ClientProtocolSelection protocol_;
    CanvasSettings canvasSettings_;
    bool test_;
    Clock::time_point processStart_,requestedAt_;
    std::mutex mutex_;
    std::condition_variable condition_;
    std::thread thread_;
    std::atomic<bool> quitting_=false;
    std::atomic<uint64_t> generation_=0;
    uint64_t consumed_=0;
    bool stopRequested_=false,toggleRequested_=false,filterRequested_=false,captureRequested_=false,canvasRequested_=false;
    std::vector<int> stepsRequested_;
    std::unordered_set<std::string> hidden_;
    PlayerStatus status_;
    void run();
public:
    PlaybackController(HWND canvas,std::filesystem::path root,std::filesystem::path client,bool test,Clock::time_point processStart,ClientProtocolSelection protocol={},CanvasSettings settings={});
    ~PlaybackController();
    void open(const std::filesystem::path& path);
    void replay();
    void stop();
    void togglePause();
    void stepFrame(int direction);
    void configureClient(const std::filesystem::path& client);
    void setHiddenImages(std::unordered_set<std::string> hidden);
    void setCanvasSettings(const CanvasSettings& settings);
    void captureFrame();
    PlayerStatus status();
};
}
