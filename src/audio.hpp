#pragma once
#include "audio_resources.hpp"
#include <functional>
#include <chrono>

namespace rep {
// One sample clock and one event interpreter serve device playback and export.
// Rendering pictures, rebuilding cameras and reading back pixels never emit sound.
class AudioTrack {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    static constexpr int SampleRate=48000;
    AudioTrack(const std::filesystem::path& client,const std::filesystem::path& cache,
               Replay& replay,const std::function<bool()>& cancelled={},AudioResources::IndexPtr index={},std::shared_ptr<RetainedCache> cacheStore={});
    ~AudioTrack();
    AudioTrack(const AudioTrack&)=delete;AudioTrack& operator=(const AudioTrack&)=delete;
    bool seek(int64_t milliseconds,const std::function<bool()>& cancelled={});
    void render(std::span<float> interleavedStereo);
    bool renderPrepared(std::span<float> interleavedStereo);
    bool prepareRender(int64_t horizon,const std::function<bool()>& cancelled={});
    void prepare(int64_t milliseconds,int64_t horizon=2000,const std::function<bool()>& cancelled={});
    bool hasEvents()const;
    uint64_t eventCount()const;
    uint64_t missingResources()const;
    int64_t positionMilliseconds()const;
    int64_t durationMilliseconds()const;
    std::vector<std::string> diagnostics()const;
    const void* eventIdentity()const;
    uint64_t decoderProcesses()const;
};

class AudioPlayer {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    explicit AudioPlayer(std::unique_ptr<AudioTrack> track);
    ~AudioPlayer();
    AudioPlayer(const AudioPlayer&)=delete;AudioPlayer& operator=(const AudioPlayer&)=delete;
    void play(int64_t milliseconds=0);
    void pause();
    void resume(int64_t milliseconds);
    void stop();
    uint64_t seek(int64_t milliseconds,bool playing,const std::function<bool()>& cancelled={});
    uint64_t appliedSeekSerial()const;
    uint64_t pcmReadySerial()const;
    struct Statistics {
        uint64_t appliedSerial=0,pcmReadySerial=0,deviceFrames=0,underruns=0;
        int64_t bufferedMilliseconds=0;
        bool buffering=false,forcedBuffering=false,clockRunning=false;
        uint64_t bufferingEvents=0;
    };
    Statistics stats()const;
    // stats().buffering acknowledges the device has actually stopped.
    void pauseBuffering(bool paused);
    // Gate optional far-future work independently of near/current PCM.
    void setRemainingPrefetchEnabled(bool enabled);
    bool resumePrepared(uint64_t seekSerial);
    void volume(float value,bool muted);
    void setPlaybackRate(double rate);
    bool available()const;
    std::string error()const;
    std::vector<std::string> diagnostics()const;
    uint64_t missingResources()const;
    uint64_t eventCount()const;
    int64_t positionMilliseconds()const;
};
}
