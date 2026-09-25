#include "AnalysisService.h"

#define HE(literal) juce::String::fromUTF8 (literal)

AnalysisService::AnalysisService() : juce::Thread ("ProductionAnalyzer worker")
{
    fifoL.assign ((size_t) kFifoSize, 0.0f);
    fifoR.assign ((size_t) kFifoSize, 0.0f);
    statusMessage = HE ("מוכן. לחצו 'התחל ניתוח' ונגנו את הטראק, או טענו קובץ.");
    startThread();
}

AnalysisService::~AnalysisService()
{
    capturing.store (false);
    cancelFlag.store (true);
    stopThread (4000);
}

// ---------------------------------------------------------------- audio thread
void AnalysisService::pushAudio (const float* left, const float* right, int numSamples) noexcept
{
    if (! capturing.load() || numSamples <= 0) return;
    if (right == nullptr) right = left;

    const auto scope = fifo.write (numSamples);
    if (scope.blockSize1 > 0)
    {
        std::copy (left,  left  + scope.blockSize1, fifoL.data() + scope.startIndex1);
        std::copy (right, right + scope.blockSize1, fifoR.data() + scope.startIndex1);
    }
    if (scope.blockSize2 > 0)
    {
        std::copy (left  + scope.blockSize1, left  + scope.blockSize1 + scope.blockSize2, fifoL.data() + scope.startIndex2);
        std::copy (right + scope.blockSize1, right + scope.blockSize1 + scope.blockSize2, fifoR.data() + scope.startIndex2);
    }
    if (scope.blockSize1 + scope.blockSize2 < numSamples)
        droppedBlocks.fetch_add (1);
}

// ------------------------------------------------------------- message thread
void AnalysisService::startCapture()
{
    const juce::ScopedLock sl (queueLock);
    queue.push_back ({ Cmd::Start, {} });
    notify();
}

void AnalysisService::stopCapture()
{
    capturing.store (false);   // stop the audio thread pushing immediately
    const juce::ScopedLock sl (queueLock);
    queue.push_back ({ Cmd::Stop, {} });
    notify();
}

void AnalysisService::analyzeFile (const juce::File& file)
{
    const juce::ScopedLock sl (queueLock);
    queue.push_back ({ Cmd::File, file });
    notify();
}

void AnalysisService::setProfile (int index)
{
    profile.store (juce::jlimit (0, pa::getNumProfiles() - 1, index));
    const juce::ScopedLock sl (reportLock);
    if (metrics.valid)
    {
        report = pa::buildReport (metrics, profile.load());
        reportVersion.fetch_add (1);
    }
}

bool AnalysisService::getReport (pa::Report& r, pa::Metrics& m) const
{
    const juce::ScopedLock sl (reportLock);
    r = report;
    m = metrics;
    return metrics.valid;
}

juce::String AnalysisService::getStatusMessage() const
{
    const juce::ScopedLock sl (reportLock);
    return statusMessage;
}

juce::String AnalysisService::getSourceName() const
{
    const juce::ScopedLock sl (reportLock);
    return sourceName;
}

void AnalysisService::setStatus (const juce::String& s)
{
    const juce::ScopedLock sl (reportLock);
    statusMessage = s;
}

// -------------------------------------------------------------- worker thread
void AnalysisService::run()
{
    while (! threadShouldExit())
    {
        bool has = false;
        Command cmd { Cmd::Stop, {} };
        {
            const juce::ScopedLock sl (queueLock);
            if (! queue.empty())
            {
                cmd = queue.front();
                queue.pop_front();
                has = true;
            }
        }
        if (has) handle (cmd);

        if (state.load() == State::Capturing)
            drainFifo (true);

        wait (15);
    }
}

void AnalysisService::drainFifo (bool feed)
{
    for (;;)
    {
        const int ready = fifo.getNumReady();
        if (ready <= 0) break;
        const auto scope = fifo.read (ready);
        if (feed)
        {
            if (scope.blockSize1 > 0)
                engine.process (fifoL.data() + scope.startIndex1, fifoR.data() + scope.startIndex1, scope.blockSize1);
            if (scope.blockSize2 > 0)
                engine.process (fifoL.data() + scope.startIndex2, fifoR.data() + scope.startIndex2, scope.blockSize2);
        }
    }
}

void AnalysisService::handle (const Command& c)
{
    switch (c.type)
    {
        case Cmd::Start:
        {
            if (state.load() != State::Idle) return;
            drainFifo (false);                       // discard anything stale
            engine.prepare (hostSampleRate.load());
            droppedBlocks.store (0);
            state.store (State::Capturing);
            capturing.store (true);
            setStatus (HE ("מקליט לניתוח... נגנו את הטראק מההתחלה ועד הסוף, ואז לחצו 'עצור והפק דוח'."));
            break;
        }
        case Cmd::Stop:
        {
            if (state.load() != State::Capturing) return;
            capturing.store (false);
            drainFifo (true);
            finish (HE ("הקלטה מאבלטון"));
            state.store (State::Idle);
            break;
        }
        case Cmd::File:
        {
            if (state.load() != State::Idle) return;
            runFileAnalysis (c.file);
            state.store (State::Idle);
            break;
        }
    }
}

void AnalysisService::runFileAnalysis (const juce::File& file)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples <= 0)
    {
        setStatus (HE ("לא ניתן לפתוח את הקובץ. נתמכים: WAV, AIFF, FLAC, MP3, OGG."));
        return;
    }

    state.store (State::AnalyzingFile);
    cancelFlag.store (false);
    fileProgress.store (0.0f);
    engine.prepare (reader->sampleRate);
    setStatus (HE ("מנתח קובץ: ") + file.getFileName());

    constexpr int chunk = 65536;
    juce::AudioBuffer<float> buf (2, chunk);
    const juce::int64 len = reader->lengthInSamples;
    const bool mono = reader->numChannels < 2;

    for (juce::int64 pos = 0; pos < len; pos += chunk)
    {
        if (threadShouldExit() || cancelFlag.load())
        {
            setStatus (HE ("ניתוח הקובץ בוטל."));
            fileProgress.store (0.0f);
            return;
        }
        const int n = (int) juce::jmin ((juce::int64) chunk, len - pos);
        reader->read (&buf, 0, n, pos, true, true);
        const float* l = buf.getReadPointer (0);
        const float* r = mono ? l : buf.getReadPointer (1);
        engine.process (l, r, n);
        fileProgress.store ((float) ((double) (pos + n) / (double) len));
    }
    finish (file.getFileName());
}

void AnalysisService::finish (const juce::String& source)
{
    const double secs = engine.getProcessedSeconds();
    if (secs < 3.0)
    {
        setStatus (HE ("ההקלטה קצרה מדי. צריך לפחות 3 שניות של אודיו (מומלץ את כל הטראק)."));
        return;
    }

    pa::Metrics m = engine.finalize();
    pa::Report  r = pa::buildReport (m, profile.load());
    {
        const juce::ScopedLock sl (reportLock);
        metrics = m;
        report = r;
        sourceName = source;
        if (! m.valid)
            statusMessage = HE ("לא נמצא מספיק אודיו שאינו שקט. בדקו שהטראק באמת התנגן.");
        else
        {
            statusMessage = HE ("הדוח מוכן: ") + juce::String (secs, 0) + HE (" שניות נותחו.");
            if (droppedBlocks.load() > 0)
                statusMessage << HE (" (חלק מהאודיו לא נקלט בגלל עומס. לתוצאה מדויקת, נתחו קובץ.)");
        }
    }
    reportVersion.fetch_add (1);
}
