#pragma once
// ============================================================================
//  AnalysisService
//  - Audio thread pushes samples into a lock-free FIFO (pushAudio)
//  - A worker thread drains it into AnalysisEngine (never blocks audio)
//  - Can also analyse an audio file offline (WAV/AIFF/FLAC/MP3/OGG)
//  - Holds the latest Metrics + Report for the editor to read
// ============================================================================
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <deque>

#include "AnalysisEngine.h"
#include "ReportBuilder.h"

class AnalysisService : private juce::Thread
{
public:
    enum class State { Idle = 0, Capturing, AnalyzingFile };

    AnalysisService();
    ~AnalysisService() override;

    // ---- audio thread ---------------------------------------------------------
    void setHostSampleRate (double sr) noexcept { hostSampleRate.store (sr); }
    bool isCapturing() const noexcept           { return capturing.load(); }
    void pushAudio (const float* left, const float* right, int numSamples) noexcept;

    // ---- message thread -------------------------------------------------------
    void startCapture();
    void stopCapture();
    void analyzeFile (const juce::File& file);
    void cancelFileAnalysis() noexcept { cancelFlag.store (true); }

    void setProfile (int index);
    int  getProfile() const noexcept { return profile.load(); }

    State  getState() const noexcept            { return state.load(); }
    double getCapturedSeconds() const noexcept  { return engine.getProcessedSeconds(); }
    float  getFileProgress() const noexcept     { return fileProgress.load(); }
    float  getLivePeakDb() const noexcept       { return engine.getLivePeakDb(); }
    float  getLiveLufs() const noexcept         { return engine.getLiveMomentaryLufs(); }
    float  getLiveCorrelation() const noexcept  { return engine.getLiveCorrelation(); }
    int    getDroppedBlocks() const noexcept    { return droppedBlocks.load(); }

    int  getReportVersion() const noexcept { return reportVersion.load(); }
    bool getReport (pa::Report& r, pa::Metrics& m) const;
    juce::String getStatusMessage() const;
    juce::String getSourceName() const;

private:
    enum class Cmd { Start, Stop, File };
    struct Command { Cmd type; juce::File file; };

    void run() override;
    void handle (const Command& c);
    void drainFifo (bool feed);
    void runFileAnalysis (const juce::File& f);
    void finish (const juce::String& sourceName);
    void setStatus (const juce::String& s);

    pa::AnalysisEngine engine;

    static constexpr int kFifoSize = 1 << 19;   // ~11 s at 48 kHz
    juce::AbstractFifo fifo { kFifoSize };
    std::vector<float> fifoL, fifoR;

    std::atomic<bool>   capturing { false }, cancelFlag { false };
    std::atomic<State>  state { State::Idle };
    std::atomic<double> hostSampleRate { 48000.0 };
    std::atomic<float>  fileProgress { 0.0f };
    std::atomic<int>    droppedBlocks { 0 }, profile { 0 }, reportVersion { 0 };

    juce::CriticalSection queueLock;
    std::deque<Command> queue;

    mutable juce::CriticalSection reportLock;
    pa::Metrics  metrics;
    pa::Report   report;
    juce::String statusMessage, sourceName;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnalysisService)
};
