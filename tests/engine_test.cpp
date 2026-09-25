// Build: g++ -std=c++17 -O2 -I../Source engine_test.cpp ../Source/AnalysisEngine.cpp ../Source/ReportBuilder.cpp -o engine_test
#include "AnalysisEngine.h"
#include "ReportBuilder.h"
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

using namespace pa;
static const double PI = 3.14159265358979323846;

static Metrics run (const std::vector<float>& L, const std::vector<float>& R, double fs)
{
    AnalysisEngine e;
    e.prepare (fs);
    for (size_t i = 0; i < L.size(); i += 512)
    {
        int n = (int) std::min<size_t> (512, L.size() - i);
        e.process (L.data() + i, R.data() + i, n);
    }
    return e.finalize();
}

static void pinkNoise (std::vector<float>& out, std::mt19937& rng, float gain)
{
    std::normal_distribution<float> nd (0.f, 1.f);
    double b0=0,b1=0,b2=0,b3=0,b4=0,b5=0,b6=0;
    for (auto& s : out)
    {
        double w = nd (rng);
        b0 = 0.99886*b0 + w*0.0555179; b1 = 0.99332*b1 + w*0.0750759; b2 = 0.96900*b2 + w*0.1538520;
        b3 = 0.86650*b3 + w*0.3104856; b4 = 0.55000*b4 + w*0.5329522; b5 = -0.7616*b5 - w*0.0168980;
        s = (float) ((b0+b1+b2+b3+b4+b5+b6 + w*0.5362) * 0.11 * gain);
        b6 = w * 0.115926;
    }
}

int main()
{
    const double fs = 48000; const int N = (int) fs * 30;
    std::mt19937 rng (1);

    // 1) 997 Hz sine, -20 dBFS both channels -> expect ~ -20 LUFS, crest ~3 dB
    {
        std::vector<float> L (N), R (N);
        for (int i = 0; i < N; ++i) L[i] = R[i] = (float) (0.1 * std::sin (2 * PI * 997 * i / fs));
        auto m = run (L, R, fs);
        std::printf ("[sine] LUFS %.2f  TP %.2f  crest %.2f  corr %.3f monoLoss %.2f\n",
                     m.integratedLufs, m.truePeakDb, m.crestDb, m.correlation, m.monoLossDb);
    }
    // 2) pink noise, uncorrelated -> tilt ~0, corr ~0, mono loss ~-3
    {
        std::vector<float> L (N), R (N);
        pinkNoise (L, rng, 0.3f); pinkNoise (R, rng, 0.3f);
        auto m = run (L, R, fs);
        std::printf ("[pink] tilt %.2f dB/oct corr %.3f monoLoss %.2f flat %.3f holes %zu res %zu\n",
                     m.tiltDbPerOct, m.correlation, m.monoLossDb, m.midFlatness, m.holeFreqs.size(), m.resonances.size());
    }
    // 3) inverted polarity
    {
        std::vector<float> L (N), R (N);
        pinkNoise (L, rng, 0.3f);
        for (int i = 0; i < N; ++i) R[i] = -L[i];
        auto m = run (L, R, fs);
        std::printf ("[invert] corr %.3f monoLoss %.2f neg%% %.1f\n", m.correlation, m.monoLossDb, m.negCorrTimePct);
    }
    // 4) kick (55 Hz) every 0.5s + sustained bass 55 Hz vs 98 Hz + pink bed
    for (double bassF : { 55.0, 98.0 })
    {
        std::vector<float> L (N), R (N), bed (N);
        pinkNoise (bed, rng, 0.05f);
        double ph = 0.0;
        for (int i = 0; i < N; ++i)
        {
            double t = i / fs, tk = std::fmod (t, 0.5);
            if (tk < 1.0 / fs) ph = 0.0;
            double fk = 55.0 + 120.0 * std::exp (-tk * 40.0);
            ph += 2 * PI * fk / fs;
            double kick = 0.7 * std::exp (-tk * 12.0) * std::sin (ph);
            double bass = 0.25 * std::sin (2 * PI * bassF * t);
            L[i] = R[i] = (float) (kick + bass + bed[i]);
        }
        auto m = run (L, R, fs);
        std::printf ("[kick/bass %.0f] kicks %d det %d kickF %.1f bassF %.1f punch %.2f\n",
                     bassF, m.kickCount, (int) m.kickDetected, m.kickFreqHz, m.bassFreqHz, m.kickPunchDb);
    }
    // 5) clipping
    {
        std::vector<float> L (N), R (N);
        for (int i = 0; i < N; ++i) { float s = (float) (1.4 * std::sin (2 * PI * 200 * i / fs)); s = std::max (-1.f, std::min (1.f, s)); L[i] = R[i] = s; }
        auto m = run (L, R, fs);
        std::printf ("[clip] events %lld overs %lld isp %lld flat %lld\n",
                     (long long) m.clipEvents, (long long) m.oversFloat, (long long) m.interSampleOvers, (long long) m.flatTopRuns);
        auto rep = buildReport (m, 0);
        std::printf ("score %d\n%s\n", rep.score, reportToText (rep, m, 0).c_str());
    }
    return 0;
}
