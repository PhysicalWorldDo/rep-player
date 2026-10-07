#pragma once
#include "protocol.hpp"
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
               Replay& replay,const std::function<bool()>& cancelled={});
    ~AudioTrack();
    AudioTrack(const AudioTrack&)=delete;AudioTrack& operator=(const AudioTrack&)=delete;
    void seek(int64_t milliseconds);
    void render(std::span<float> interleavedStereo);
    void prepare(int64_t milliseconds,int64_t horizon=2000,const std::function<bool()>& cancelled={});
    bool hasEvents()const;
    uint64_t eventCount()const;
    uint64_t missingResources()const;
    int64_t positionMilliseconds()const;
    int64_t durationMilliseconds()const;
    std::vector<std::string> diagnostics()const;
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
    void seek(int64_t milliseconds,bool playing);
    void volume(float value,bool muted);
    bool available()const;
    std::string error()const;
    uint64_t missingResources()const;
    uint64_t eventCount()const;
    int64_t positionMilliseconds()const;
};
}
