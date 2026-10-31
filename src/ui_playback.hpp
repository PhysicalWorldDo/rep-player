#pragma once
#include "engine.hpp"
#include "client_protocol.hpp"
#include "canvas.hpp"
#include "audio_resources.hpp"
#include <windows.h>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <optional>

namespace rep::ui {
using Clock = std::chrono::steady_clock;
enum class Phase { Empty, Loading, Playing, Paused, Ended, Stopped, Error, Seeking };
struct PlayerStatus {
    Phase phase=Phase::Empty;
    std::wstring message,path;
    std::filesystem::path client;
    int timestamp=0,duration=0,width=800,height=600;
    int canvasWidth=800,canvasHeight=600,canvasLeft=0,canvasTop=0,canvasRight=0,canvasBottom=0;
    int64_t elapsed=0;
    uint64_t presentationSerial=0;
    uint64_t ordinal=0,frames=0,frameCount=0,skipped=0,captureSerial=0;
    double readySeconds=0,maxFrameMilliseconds=0,processReadySeconds=0,p50Frame=0,p95Frame=0,p99Frame=0,playbackSeconds=0;
    double bufferingMilliseconds=0,postStartMaxFrameMilliseconds=0,resourceWaitMilliseconds=0;
    double presentationGapP50Milliseconds=0,presentationGapP95Milliseconds=0,presentationGapP99Milliseconds=0,presentationGapMaxMilliseconds=0;
    double presentationExcessP95Milliseconds=0,presentationExcessP99Milliseconds=0,presentationExcessMaxMilliseconds=0;
    uint64_t presentationGap50=0,presentationGap100=0;
    uint64_t bufferingCount=0,longFrames50=0,longFrames100=0,audioUnderruns=0,audioBufferingEvents=0;
    uint64_t retainedCacheBytes=0,inactiveCacheBytes=0,activeCacheBytes=0,cacheEvictions=0;
    uint64_t audioPcmReadySerial=0,audioAppliedSerial=0,audioRequestedSerial=0;
    bool buffering=false;
    bool frozen=false,compatibilityIgnored=false;
    bool negativeImgOffsetsEnabled=false;
    bool patchesEnabled=true;
    bool audioAvailable=false,audioMuted=false;
    float audioVolume=1;
    double playbackRate=1;
    std::wstring audioMessage;
    uint64_t missingSoundCount=0,audioEvents=0;
    int64_t audioPosition=0;
    uint32_t finalCrc=0,frameCrc=0;
    std::vector<ImgCall> currentImages,allImages;
    std::vector<std::string> currentOggs,allOggs;
    std::unordered_set<std::string> hiddenOggs;
    std::string auditionPath;
    std::wstring auditionMessage;
    std::unordered_set<std::string> hiddenImages;
    ImageVisibilityPolicy imageVisibility;
    std::string highlightedImage;
    bool highlightAvailable=false;
    bool scrubbing=false;
    uint64_t navigationSerial=0,completedNavigationSerial=0,navigationProgressOrdinal=0;
    int64_t navigationTarget=0;
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
    struct HighlightPresentation {std::string path;bool available=false;};
    std::mutex highlightMutex_;
    std::string highlightSelection_;
    HighlightPresentation highlightPresentation_;
    std::atomic<bool> presentRequested_=false;
    std::atomic<uint64_t> presentationSerial_=0;
    std::atomic<uint64_t> generation_=0;
    uint64_t consumed_=0;
    bool stopRequested_=false,toggleRequested_=false,filterRequested_=false,captureRequested_=false,canvasRequested_=false;
    bool audioSettingsRequested_=false,audioMuted_=false;
    std::unordered_set<std::string> hiddenOggs_;
    std::optional<std::string> auditionRequested_;
    std::atomic<uint64_t> auditionSerial_=0;
    bool negativeImgOffsetsRequested_=false,negativeImgOffsetsEnabled_=false;
    float audioVolume_=1;
    double playbackRate_=1;
    bool playbackRateRequested_=false;
    struct NavigationRequest {
        uint64_t serial=0,ordinal=0;
        int64_t time=0;
        bool byOrdinal=false,last=false,resume=false,redraw=false,preserveState=false,resumeOnly=false,audioOnly=false;
        Phase originalPhase=Phase::Paused;
    };
    std::optional<NavigationRequest> navigationRequested_,activeNavigation_;
    std::atomic<uint64_t> navigationSerial_=0;
    bool scrubChanged_=false,scrubbing_=false,scrubResume_=false;
    Phase scrubOriginalPhase_=Phase::Paused;
    std::unordered_set<std::string> hidden_;
    ImageVisibilityPolicy imageVisibility_;
    Assets::IndexPtr imageIndex_;
    AudioResources::IndexPtr audioIndex_;
    bool patchesEnabled_=true;
    bool refreshImagesRequested_=false;
    PlayerStatus status_;
    void run();
    void queueRefreshLocked();
    void queueNavigationLocked(NavigationRequest request,bool stopAudition=true);
    void cancelAuditionLocked();
public:
    PlaybackController(HWND canvas,std::filesystem::path root,std::filesystem::path client,bool test,Clock::time_point processStart,ClientProtocolSelection protocol={},CanvasSettings settings={},bool patchesEnabled=true);
    ~PlaybackController();
    void open(const std::filesystem::path& path);
    void replay();
    void stop();
    void togglePause();
    void stepFrame(int direction,bool repeated=false);
    void beginScrub();
    void endScrub(std::optional<int64_t> target={},bool endpointLast=false);
    void seekTime(int64_t target,bool endpointLast=false);
    void configureClient(const std::filesystem::path& client);
    void refreshImages(std::optional<bool> patchesEnabled={});
    Assets::IndexPtr imageIndex();
    AudioResources::IndexPtr audioIndex();
    std::optional<bool> appliedPatchesEnabled();
    void setHiddenImages(std::unordered_set<std::string> hidden);
    void setImageVisibility(const ImageVisibilityPolicy& policy);
    void setCanvasSettings(const CanvasSettings& settings);
    void setNegativeImgOffsetsEnabled(bool enabled);
    void setAudioVolume(float volume);
    void setAudioMuted(bool muted);
    void setHiddenOggs(std::unordered_set<std::string> paths);
    void auditionOgg(std::string path);
    void setPlaybackRate(double rate);
    void setHighlightedImage(std::string path);
    void captureFrame();
    PlayerStatus status();
};
}
