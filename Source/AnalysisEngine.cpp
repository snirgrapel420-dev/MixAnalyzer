#include "AnalysisEngine.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace pa
{
namespace
{
constexpr double kPi = 3.14159265358979323846;

double powToDb (double p) { return p > 1e-30 ? 10.0 * std::log10 (p) : -300.0; }
double linToDb (double a) { return a > 1e-9  ? 20.0 * std::log10 (a) : -180.0; }
double lufsOf  (double z) { return -0.691 + powToDb (z); }

double percentile (std::vector<double> v, double p)
{
    if (v.empty()) return 0.0;
    std::sort (v.begin(), v.end());
    const double idx = p * (double) (v.size() - 1);
    const size_t i0  = (size_t) std::floor (idx);
    const size_t i1  = std::min (v.size() - 1, i0 + 1);
    const double t   = idx - (double) i0;
    return v[i0] * (1.0 - t) + v[i1] * t;
}
} // namespace

// ============================================================================
//  FFT (iterative radix-2)
// ============================================================================
void AnalysisEngine::Fft::init (int order)
{
    n = 1 << order;
    rev.assign ((size_t) n, 0);
    for (int i = 0; i < n; ++i)
    {
        int r = 0;
        for (int b = 0; b < order; ++b)
            if (i & (1 << b)) r |= 1 << (order - 1 - b);
        rev[(size_t) i] = r;
    }
    twiddle.resize ((size_t) n / 2);
    for (int k = 0; k < n / 2; ++k)
        twiddle[(size_t) k] = std::polar (1.0, -2.0 * kPi * k / n);
}

void AnalysisEngine::Fft::forward (std::vector<std::complex<double>>& a) const
{
    for (int i = 0; i < n; ++i)
        if (i < rev[(size_t) i]) std::swap (a[(size_t) i], a[(size_t) rev[(size_t) i]]);

    for (int len = 2; len <= n; len <<= 1)
    {
        const int half = len / 2, step = n / len;
        for (int i = 0; i < n; i += len)
            for (int j = 0; j < half; ++j)
            {
                const auto u = a[(size_t) (i + j)];
                const auto v = a[(size_t) (i + j + half)] * twiddle[(size_t) (j * step)];
                a[(size_t) (i + j)]        = u + v;
                a[(size_t) (i + j + half)] = u - v;
            }
    }
}

// ============================================================================
//  True peak (4x polyphase windowed-sinc interpolation)
// ============================================================================
AnalysisEngine::TruePeak::TruePeak()
{
    constexpr int N = kPhases * kTaps;
    const double c = (N - 1) * 0.5;
    for (int nIdx = 0; nIdx < N; ++nIdx)
    {
        const double t    = (nIdx - c) / kPhases;
        const double sinc = std::abs (t) < 1e-12 ? 1.0 : std::sin (kPi * t) / (kPi * t);
        const double w    = 0.42 - 0.5 * std::cos (2.0 * kPi * nIdx / (N - 1))
                                 + 0.08 * std::cos (4.0 * kPi * nIdx / (N - 1));
        coef[nIdx % kPhases][nIdx / kPhases] = sinc * w;
    }
    // normalise each phase to unity DC gain
    for (auto& phase : coef)
    {
        double s = 0.0;
        for (double v : phase) s += v;
        for (double& v : phase) v /= s;
    }
    reset();
}

void AnalysisEngine::TruePeak::reset()
{
    for (auto& h : hist) std::fill (std::begin (h), std::end (h), 0.0f);
}

float AnalysisEngine::TruePeak::push (int ch, float x) noexcept
{
    float* h = hist[ch];
    for (int k = kTaps - 1; k > 0; --k) h[k] = h[k - 1];
    h[0] = x;

    double m = 0.0;
    for (int p = 0; p < kPhases; ++p)
    {
        double y = 0.0;
        for (int k = 0; k < kTaps; ++k) y += h[k] * coef[p][k];
        m = std::max (m, std::abs (y));
    }
    return (float) m;
}

// ============================================================================
AnalysisEngine::AnalysisEngine()
{
    fft.init (kFftOrder);
    prepare (48000.0);
}

AnalysisEngine::Biquad AnalysisEngine::makeLowpass (double sr, double f, double q)
{
    Biquad b;
    const double w0 = 2.0 * kPi * f / sr, cw = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
    const double a0 = 1.0 + alpha;
    b.b0 = (1.0 - cw) * 0.5 / a0;
    b.b1 = (1.0 - cw) / a0;
    b.b2 = b.b0;
    b.a1 = -2.0 * cw / a0;
    b.a2 = (1.0 - alpha) / a0;
    return b;
}

void AnalysisEngine::prepare (double sampleRate)
{
    fs      = sampleRate > 1000.0 ? sampleRate : 48000.0;
    binHz   = fs / kFftSize;
    numBins = kFftSize / 2 + 1;

    window.resize (kFftSize);
    for (int i = 0; i < kFftSize; ++i)
        window[(size_t) i] = 0.5 - 0.5 * std::cos (2.0 * kPi * i / kFftSize);

    fftBuf.assign (kFftSize, {});
    ringL.assign (kFftSize, 0.0f);
    ringR.assign (kFftSize, 0.0f);
    ringPos = ringFilled = hopCounter = 0;
    accPL.assign ((size_t) numBins, 0.0);
    accPR.assign ((size_t) numBins, 0.0);
    accC.assign ((size_t) numBins, 0.0);
    nonSilentFrames = 0;

    lowBins = std::min (numBins, (int) std::ceil (320.0 / binHz) + 1);
    frameLow.clear();  frameCenters.clear();
    frameCorr.clear(); frameBal.clear(); framePres.clear(); frameFlat.clear();

    totalSamples = 0;
    sumSq[0] = sumSq[1] = sum[0] = sum[1] = 0.0;
    peakLin = truePeakLin = 0.0f;
    truePeak.reset();
    clipEvents = clippedSamples = oversFloat = interSampleOvers = flatTopRuns = 0;
    clipRun[0] = clipRun[1] = flatRun[0] = flatRun[1] = 0;
    lastSample[0] = lastSample[1] = 0.0f;

    // ---- K-weighting (ITU-R BS.1770), coefficients derived for any sample rate
    {
        const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
        const double K = std::tan (kPi * f0 / fs);
        const double Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
        const double a0 = 1.0 + K / Q + K * K;
        Biquad pre;
        pre.b0 = (Vh + Vb * K / Q + K * K) / a0;
        pre.b1 = 2.0 * (K * K - Vh) / a0;
        pre.b2 = (Vh - Vb * K / Q + K * K) / a0;
        pre.a1 = 2.0 * (K * K - 1.0) / a0;
        pre.a2 = (1.0 - K / Q + K * K) / a0;

        const double f1 = 38.13547087602444, Q1 = 0.5003270373238773;
        const double K1 = std::tan (kPi * f1 / fs);
        const double d  = 1.0 + K1 / Q1 + K1 * K1;
        Biquad rlb;
        rlb.b0 = 1.0; rlb.b1 = -2.0; rlb.b2 = 1.0;
        rlb.a1 = 2.0 * (K1 * K1 - 1.0) / d;
        rlb.a2 = (1.0 - K1 / Q1 + K1 * K1) / d;

        kPre[0] = kPre[1] = pre;
        kRlb[0] = kRlb[1] = rlb;
    }
    subLen = std::max (1, (int) std::lround (0.1 * fs));
    subCount = 0;
    subAcc = rawSubAcc = 0.0;
    subBlocks.clear();
    rawSubBlocks.clear();

    // ---- kick onset detector
    onsetLp1 = makeLowpass (fs, 130.0, 0.7071);
    onsetLp2 = onsetLp1;
    onsetBlock = std::max (64, (int) std::lround (fs * 0.005));
    onsetCount = 0;
    onsetAcc = slowMean = peakEnv = 0.0;
    peakDecay = std::pow (0.25, onsetBlock / (0.3 * fs));   // -6 dB (power /4) per 300 ms
    std::fill (std::begin (onsetHist), std::end (onsetHist), 0.0);
    onsetHistPos = 0;
    lastOnsetSample = -((int64_t) 1 << 40);
    onsets.clear();

    processedSeconds.store (0.0);
    livePeakDb.store (-120.0f);
    liveMomentary.store (-120.0f);
    liveCorrelation.store (1.0f);
}

// ============================================================================
void AnalysisEngine::checkClip (int ch, float x) noexcept
{
    const float a = std::abs (x);
    if (a > 1.0f) ++oversFloat;

    if (a >= 0.9999f)
    {
        if (++clipRun[ch] == 3)      { ++clipEvents; clippedSamples += 3; }
        else if (clipRun[ch] > 3)    { ++clippedSamples; }
    }
    else clipRun[ch] = 0;

    // plateau of identical samples well above noise, but below full scale
    if (x == lastSample[ch] && a > 0.25f && a < 0.9999f)
    {
        if (++flatRun[ch] == 3) ++flatTopRuns;       // 4 identical samples in a row
    }
    else flatRun[ch] = 0;

    lastSample[ch] = x;
}

void AnalysisEngine::process (const float* left, const float* right, int numSamples)
{
    if (left == nullptr || numSamples <= 0) return;
    if (right == nullptr) right = left;
    const bool same = (left == right);

    float blockPeak = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        float l = left[i], r = right[i];
        if (! std::isfinite (l)) l = 0.0f;
        if (! std::isfinite (r)) r = 0.0f;
        ++totalSamples;

        // ---- time domain -------------------------------------------------
        sumSq[0] += (double) l * l;  sumSq[1] += (double) r * r;
        sum[0]   += l;               sum[1]   += r;
        const float al = std::abs (l), ar = std::abs (r), am = std::max (al, ar);
        peakLin   = std::max (peakLin, am);
        blockPeak = std::max (blockPeak, am);

        checkClip (0, l);
        if (! same) checkClip (1, r);

        const float tpL = truePeak.push (0, l);
        const float tpR = same ? tpL : truePeak.push (1, r);
        const float tpm = std::max (tpL, tpR);
        truePeakLin = std::max (truePeakLin, tpm);
        if (tpm > 1.0f && am <= 1.0f) ++interSampleOvers;

        // ---- loudness ----------------------------------------------------
        const double kl = kRlb[0].process (kPre[0].process (l));
        const double kr = same ? kl : kRlb[1].process (kPre[1].process (r));
        subAcc    += kl * kl + kr * kr;
        rawSubAcc += 0.5 * ((double) l * l + (double) r * r);
        if (++subCount >= subLen)
        {
            subBlocks.push_back (subAcc / subLen);
            rawSubBlocks.push_back (rawSubAcc / subLen);
            subAcc = rawSubAcc = 0.0;
            subCount = 0;

            const size_t n = subBlocks.size();
            if (n >= 4)
            {
                const double z = (subBlocks[n - 1] + subBlocks[n - 2] + subBlocks[n - 3] + subBlocks[n - 4]) * 0.25;
                liveMomentary.store ((float) std::max (-120.0, lufsOf (z)));
            }
        }

        // ---- kick onsets (low band energy) --------------------------------
        const double lo = onsetLp2.process (onsetLp1.process (0.5 * ((double) l + r)));
        onsetAcc += lo * lo;
        if (++onsetCount >= onsetBlock) handleOnsetBlock();

        // ---- FFT ring ----------------------------------------------------
        ringL[(size_t) ringPos] = l;
        ringR[(size_t) ringPos] = r;
        ringPos = (ringPos + 1) & (kFftSize - 1);
        if (ringFilled < kFftSize) ++ringFilled;
        if (++hopCounter >= kHop)
        {
            hopCounter = 0;
            if (ringFilled >= kFftSize) processFrame();
        }
    }

    processedSeconds.store ((double) totalSamples / fs);
    const float prev = livePeakDb.load();
    const float now  = (float) linToDb (blockPeak);
    livePeakDb.store (std::max (now, prev - 0.8f));
}

void AnalysisEngine::handleOnsetBlock()
{
    const double e = onsetAcc / std::max (1, onsetCount);
    onsetAcc = 0.0;
    onsetCount = 0;

    double hist = 0.0;
    for (double v : onsetHist) hist += v;
    hist /= kOnsetHist;

    slowMean = (slowMean <= 0.0) ? e : slowMean * 0.998 + e * 0.002;

    const double prevBlock = onsetHist[(onsetHistPos + kOnsetHist - 1) % kOnsetHist];
    const int64_t blockStart = totalSamples - onsetBlock;
    if (e > 1e-7 && e > 2.5 * hist && e > 1.8 * prevBlock && e > 0.35 * slowMean
        && e > 0.4 * peakEnv
        && (blockStart - lastOnsetSample) > (int64_t) (0.12 * fs))
    {
        onsets.push_back ((double) blockStart / fs);
        lastOnsetSample = blockStart;
    }

    peakEnv = std::max (e, peakEnv * peakDecay);
    onsetHist[onsetHistPos] = e;
    onsetHistPos = (onsetHistPos + 1) % kOnsetHist;
}

void AnalysisEngine::processFrame()
{
    double ms = 0.0;
    for (int i = 0; i < kFftSize; ++i)
    {
        const int idx = (ringPos + i) & (kFftSize - 1);
        const double l = ringL[(size_t) idx], r = ringR[(size_t) idx];
        ms += l * l + r * r;
        fftBuf[(size_t) i] = { l * window[(size_t) i], r * window[(size_t) i] };   // pack L=re, R=im
    }
    ms /= (2.0 * kFftSize);
    if (ms < 1e-7) return;   // below -70 dBFS: treat as silence

    const double centerSec = (double) (totalSamples - kFftSize / 2) / fs;
    fft.forward (fftBuf);

    const bool store = frameCenters.size() < kMaxFrames;
    const size_t lowOffset = frameLow.size();
    if (store) frameLow.resize (lowOffset + (size_t) lowBins, 0.0f);

    double sPL = 0, sPR = 0, sC = 0, pres = 0, body = 0, logSum = 0, linSum = 0;
    int flatCount = 0;
    const std::complex<double> minusHalfI (0.0, -0.5);

    for (int k = 0; k < numBins; ++k)
    {
        const auto z  = fftBuf[(size_t) k];
        const auto zn = std::conj (fftBuf[(size_t) ((kFftSize - k) & (kFftSize - 1))]);
        const auto L  = (z + zn) * 0.5;
        const auto R  = (z - zn) * minusHalfI;

        const double pl = std::norm (L), pr = std::norm (R), c = (L * std::conj (R)).real();
        accPL[(size_t) k] += pl;
        accPR[(size_t) k] += pr;
        accC[(size_t) k]  += c;

        const double f = k * binHz;
        if (k > 0) { sPL += pl; sPR += pr; sC += c; }

        const double pm = std::max (0.0, 0.25 * (pl + pr + 2.0 * c));
        if (store && k < lowBins) frameLow[lowOffset + (size_t) k] = (float) pm;

        const double pt = pl + pr;
        if (f >= 200.0 && f < 2000.0)       body += pt;
        else if (f >= 2000.0 && f < 5000.0) pres += pt;

        if (f >= 250.0 && f < 4000.0)
        {
            logSum += std::log (pm + 1e-20);
            linSum += pm;
            ++flatCount;
        }
    }

    ++nonSilentFrames;
    const double corr = (sPL > 0 && sPR > 0) ? sC / std::sqrt (sPL * sPR) : 1.0;
    liveCorrelation.store ((float) corr);

    if (store)
    {
        frameCenters.push_back (centerSec);
        frameCorr.push_back ((float) corr);
        frameBal.push_back ((float) powToDb ((sPL + 1e-20) / (sPR + 1e-20)));
        framePres.push_back ((float) powToDb ((pres + 1e-20) / (body + 1e-20)));
        const double flat = (flatCount > 0 && linSum > 0)
                              ? std::exp (logSum / flatCount) / (linSum / flatCount) : 0.0;
        frameFlat.push_back ((float) flat);
    }
}

// ============================================================================
//  Finalize: turn accumulators into Metrics
// ============================================================================
Metrics AnalysisEngine::finalize() const
{
    Metrics m;
    m.sampleRate  = fs;
    m.durationSec = (double) totalSamples / fs;
    if (totalSamples < (int64_t) fs || nonSilentFrames < 4) return m;
    m.valid = true;

    // ---------------------------------------------------------------- levels
    m.samplePeakDb = linToDb (peakLin);
    m.truePeakDb   = linToDb (std::max (truePeakLin, peakLin));

    {   // gated RMS (ignore silence and very quiet passages)
        double s = 0.0; int c = 0;
        for (double z : rawSubBlocks) if (powToDb (z) > -60.0) { s += z; ++c; }
        if (c > 0)
        {
            const double relGate = powToDb (s / c) - 20.0;
            double s2 = 0.0; int c2 = 0;
            for (double z : rawSubBlocks) if (powToDb (z) > -60.0 && powToDb (z) > relGate) { s2 += z; ++c2; }
            m.rmsDb = powToDb (c2 > 0 ? s2 / c2 : s / c);
        }
        else m.rmsDb = powToDb ((sumSq[0] + sumSq[1]) / (2.0 * (double) totalSamples));
        m.crestDb = m.samplePeakDb - m.rmsDb;
    }

    m.clipEvents       = clipEvents;
    m.clippedSamples   = clippedSamples;
    m.oversFloat       = oversFloat;
    m.interSampleOvers = interSampleOvers;
    m.flatTopRuns      = flatTopRuns;
    m.dcOffsetDb = linToDb (std::max (std::abs (sum[0]), std::abs (sum[1])) / (double) totalSamples);

    // -------------------------------------------------------------- loudness
    {
        const size_t S = subBlocks.size();
        std::vector<double> prefix (S + 1, 0.0);
        for (size_t i = 0; i < S; ++i) prefix[i + 1] = prefix[i] + subBlocks[i];

        // integrated: 400 ms blocks, 75% overlap, abs gate -70, rel gate -10
        std::vector<double> mom;
        for (size_t j = 0; j + 4 <= S; ++j) mom.push_back ((prefix[j + 4] - prefix[j]) / 4.0);
        double s = 0.0; int c = 0;
        for (double z : mom) if (lufsOf (z) > -70.0) { s += z; ++c; }
        if (c > 0)
        {
            const double rel = lufsOf (s / c) - 10.0;
            double s2 = 0.0; int c2 = 0;
            for (double z : mom) if (lufsOf (z) > -70.0 && lufsOf (z) > rel) { s2 += z; ++c2; }
            m.integratedLufs = c2 > 0 ? lufsOf (s2 / c2) : lufsOf (s / c);
        }

        // short-term 3 s, LRA (EBU Tech 3342)
        std::vector<double> st;
        for (size_t j = 0; j + 30 <= S; ++j) st.push_back ((prefix[j + 30] - prefix[j]) / 30.0);
        double ss = 0.0; int sc = 0;
        for (double z : st)
        {
            m.maxShortTermLufs = std::max (m.maxShortTermLufs, lufsOf (z));
            if (lufsOf (z) > -70.0) { ss += z; ++sc; }
        }
        if (sc > 0)
        {
            const double rel = lufsOf (ss / sc) - 20.0;
            std::vector<double> gated;
            for (double z : st) if (lufsOf (z) > -70.0 && lufsOf (z) > rel) gated.push_back (lufsOf (z));
            if (gated.size() >= 2) m.lra = percentile (gated, 0.95) - percentile (gated, 0.10);
        }
        m.plr = m.truePeakDb - m.integratedLufs;
    }

    // ------------------------------------------------ averaged power spectra
    const double nf = (double) nonSilentFrames;
    std::vector<double> P ((size_t) numBins), prefixP ((size_t) numBins + 1, 0.0);
    for (int k = 0; k < numBins; ++k)
    {
        P[(size_t) k] = 0.5 * (accPL[(size_t) k] + accPR[(size_t) k]) / nf;
        prefixP[(size_t) k + 1] = prefixP[(size_t) k] + P[(size_t) k];
    }

    auto rangeMean = [&] (double lo, double hi, int kCentre)
    {
        int a = std::max (1, (int) std::ceil (lo / binHz));
        int b = std::min (numBins - 1, (int) std::floor (hi / binHz));
        a = std::min (a, kCentre);
        b = std::max (b, kCentre);
        return (prefixP[(size_t) b + 1] - prefixP[(size_t) a]) / (b - a + 1);
    };

    // 1/3-octave band energies (density x bandwidth, so pink noise is flat)
    for (int n = -17; n <= 13; ++n)
    {
        const double fc = 1000.0 * std::pow (2.0, n / 3.0);
        const double lo = fc * std::pow (2.0, -1.0 / 6.0), hi = fc * std::pow (2.0, 1.0 / 6.0);
        if (hi > fs * 0.5 * 0.98) break;
        const int kc = std::max (1, std::min (numBins - 1, (int) std::lround (fc / binHz)));
        const double density = rangeMean (lo, hi, kc) / binHz;
        m.bandFreqs.push_back (fc);
        m.bandDb.push_back (powToDb (density * (hi - lo)));
    }

    // tonal trend: least-squares line over 100 Hz - 10 kHz (x = octaves re 1 kHz)
    {
        double sx = 0, sy = 0, sxx = 0, sxy = 0; int c = 0;
        for (size_t i = 0; i < m.bandFreqs.size(); ++i)
        {
            const double f = m.bandFreqs[i];
            if (f < 99.0 || f > 10100.0) continue;
            const double x = std::log2 (f / 1000.0), y = m.bandDb[i];
            sx += x; sy += y; sxx += x * x; sxy += x * y; ++c;
        }
        double slope = 0.0, icpt = c > 0 ? sy / c : 0.0;
        if (c >= 3)
        {
            slope = (c * sxy - sx * sy) / (c * sxx - sx * sx);
            icpt  = (sy - slope * sx) / c;
        }
        m.tiltDbPerOct = slope;
        for (size_t i = 0; i < m.bandFreqs.size(); ++i)
            m.bandDevDb.push_back (m.bandDb[i] - (icpt + slope * std::log2 (m.bandFreqs[i] / 1000.0)));

        auto regionDev = [&] (double lo, double hi)
        {
            double s = 0.0; int cnt = 0;
            for (size_t i = 0; i < m.bandFreqs.size(); ++i)
                if (m.bandFreqs[i] >= lo && m.bandFreqs[i] <= hi) { s += m.bandDevDb[i]; ++cnt; }
            return cnt > 0 ? s / cnt : 0.0;
        };
        m.subDevDb       = regionDev (25.0, 60.0);
        m.bassDevDb      = regionDev (62.0, 126.0);
        m.lowMidDevDb    = regionDev (158.0, 400.0);
        m.presenceDevDb  = regionDev (1990.0, 5100.0);
        m.sibilanceDevDb = regionDev (6000.0, 10100.0);
        m.airDevDb       = regionDev (12000.0, 16100.0);

        for (size_t i = 0; i < m.bandFreqs.size(); ++i)
            if (m.bandFreqs[i] >= 60.0 && m.bandFreqs[i] <= 12000.0 && m.bandDevDb[i] < -6.0)
                m.holeFreqs.push_back (m.bandFreqs[i]);
    }

    // spectral centroid
    {
        double num = 0.0, den = 0.0;
        for (int k = 1; k < numBins; ++k)
        {
            const double f = k * binHz;
            if (f < 20.0) continue;
            num += f * P[(size_t) k];
            den += P[(size_t) k];
        }
        m.centroidHz = den > 0 ? num / den : 0.0;
    }

    // ------------------------------------------------------------ resonances
    {
        const int kMin = std::max (2, (int) std::ceil (60.0 / binHz));
        const int kMax = std::min (numBins - 2, (int) std::floor (12000.0 / binHz));
        std::vector<double> ex ((size_t) numBins, 0.0);
        for (int k = kMin; k <= kMax; ++k)
        {
            const double f = k * binHz;
            const double fine  = rangeMean (f * std::pow (2.0, -1.0 / 12.0), f * std::pow (2.0, 1.0 / 12.0), k);
            const double broad = rangeMean (f * std::pow (2.0, -0.5), f * std::pow (2.0, 0.5), k);
            ex[(size_t) k] = powToDb ((fine + 1e-30) / (broad + 1e-30));
        }
        std::vector<std::pair<double, int>> cand;
        for (int k = kMin + 1; k < kMax; ++k)
            if (ex[(size_t) k] >= 5.0 && ex[(size_t) k] >= ex[(size_t) k - 1] && ex[(size_t) k] >= ex[(size_t) k + 1])
                cand.push_back ({ ex[(size_t) k], k });
        std::sort (cand.begin(), cand.end(), [] (auto& a, auto& b) { return a.first > b.first; });

        for (auto& c : cand)
        {
            // refine: strongest bin within +-1/12 octave
            const double f0 = c.second * binHz;
            int best = c.second;
            const int a = std::max (1, (int) std::floor (f0 * std::pow (2.0, -1.0 / 12.0) / binHz));
            const int b = std::min (numBins - 1, (int) std::ceil (f0 * std::pow (2.0, 1.0 / 12.0) / binHz));
            for (int k = a; k <= b; ++k) if (P[(size_t) k] > P[(size_t) best]) best = k;
            const double f = best * binHz;

            bool tooClose = false;
            for (auto& r : m.resonances)
                if (std::abs (std::log2 (f / r.freqHz)) < 1.0 / 6.0) { tooClose = true; break; }
            if (tooClose) continue;
            m.resonances.push_back ({ f, c.first });
            if (m.resonances.size() >= 6) break;
        }
    }

    // ---------------------------------------------------------------- stereo
    {
        auto sums = [&] (double lo, double hi, double& el, double& er, double& ec)
        {
            el = er = ec = 0.0;
            for (int k = 1; k < numBins; ++k)
            {
                const double f = k * binHz;
                if (f < lo || f >= hi) continue;
                el += accPL[(size_t) k]; er += accPR[(size_t) k]; ec += accC[(size_t) k];
            }
        };
        auto corrOf = [] (double el, double er, double ec) { return (el > 0 && er > 0) ? ec / std::sqrt (el * er) : 1.0; };
        auto monoLoss = [] (double el, double er, double ec)
        {
            if (el + er <= 0) return 0.0;
            return std::max (-40.0, powToDb (std::max (1e-12, (el + er + 2.0 * ec)) / (2.0 * (el + er))));
        };
        auto bal  = [] (double el, double er) { return (el > 0 && er > 0) ? powToDb (el / er) : 0.0; };
        auto s2m  = [] (double el, double er, double ec)
        {
            const double s = std::max (0.0, el + er - 2.0 * ec), mm = std::max (1e-30, el + er + 2.0 * ec);
            return std::max (-120.0, powToDb (s / mm + 1e-30));
        };

        double el, er, ec;
        sums (20.0, fs * 0.5, el, er, ec);
        m.correlation = corrOf (el, er, ec); m.monoLossDb = monoLoss (el, er, ec); m.balanceDb = bal (el, er);

        sums (20.0, 150.0, el, er, ec);
        m.lowCorrelation = corrOf (el, er, ec); m.lowMonoLossDb = monoLoss (el, er, ec); m.lowBalanceDb = bal (el, er);

        sums (150.0, 4000.0, el, er, ec);
        m.midCorrelation = corrOf (el, er, ec); m.midMonoLossDb = monoLoss (el, er, ec); m.midBalanceDb = bal (el, er);

        sums (4000.0, fs * 0.5, el, er, ec);
        m.highCorrelation = corrOf (el, er, ec); m.highMonoLossDb = monoLoss (el, er, ec); m.highBalanceDb = bal (el, er);

        sums (20.0, 120.0, el, er, ec);   m.lowSideToMidDb = s2m (el, er, ec);
        sums (250.0, 4000.0, el, er, ec); m.midSideToMidDb = s2m (el, er, ec);
    }

    // ------------------------------------------------------ per-frame stats
    if (! frameCorr.empty())
    {
        const double n = (double) frameCorr.size();
        int neg = 0, imb = 0;
        std::vector<double> corrs, pres;
        corrs.reserve (frameCorr.size());
        pres.reserve (framePres.size());
        double flatSum = 0.0;
        for (size_t i = 0; i < frameCorr.size(); ++i)
        {
            if (frameCorr[i] < 0.0f) ++neg;
            if (std::abs (frameBal[i]) > 3.0f) ++imb;
            corrs.push_back (frameCorr[i]);
            pres.push_back (framePres[i]);
            flatSum += frameFlat[i];
        }
        m.negCorrTimePct   = 100.0 * neg / n;
        m.imbalanceTimePct = 100.0 * imb / n;
        m.corrP5           = percentile (corrs, 0.05);
        m.midFlatness      = flatSum / n;

        const double med = percentile (pres, 0.5);
        int spikes = 0;
        for (double p : pres) if (p > med + 5.0) ++spikes;
        m.harshSpikePct = 100.0 * spikes / n;
    }

    // ------------------------------------------------------------ kick / bass
    m.kickCount = (int) onsets.size();
    if (onsets.size() >= 8 && frameCenters.size() >= 20)
    {
        std::vector<double> kick ((size_t) lowBins, 0.0), bass ((size_t) lowBins, 0.0);
        int nk = 0, nb = 0;
        for (size_t i = 0; i < frameCenters.size(); ++i)
        {
            const double c = frameCenters[i];
            bool isKick = false, nearAny = false;
            for (auto it = std::lower_bound (onsets.begin(), onsets.end(), c - 0.25);
                 it != onsets.end() && *it <= c + 0.1; ++it)
            {
                const double d = *it - c;
                if (d >= -0.06 && d <= 0.01) isKick = true;
                if (d >= -0.20 && d <= 0.09) nearAny = true;
            }
            const float* spec = &frameLow[i * (size_t) lowBins];
            if (isKick)        { for (int k = 0; k < lowBins; ++k) kick[(size_t) k] += spec[k]; ++nk; }
            else if (! nearAny){ for (int k = 0; k < lowBins; ++k) bass[(size_t) k] += spec[k]; ++nb; }
        }

        if (nk >= 8 && nb >= 8)
        {
            for (auto& v : kick) v /= nk;
            for (auto& v : bass) v /= nb;

            int kBest = -1, bBest = -1;
            double kv = 0.0, bv = 0.0, ek = 0.0, eb = 0.0;
            for (int k = 1; k < lowBins; ++k)
            {
                const double f = k * binHz;
                const double excess = kick[(size_t) k] - bass[(size_t) k];
                if (f >= 35.0 && f <= 150.0 && excess > kv) { kv = excess; kBest = k; }
                if (f >= 30.0 && f <= 250.0 && bass[(size_t) k] > bv) { bv = bass[(size_t) k]; bBest = k; }
                if (f >= 30.0 && f <= 150.0) { ek += kick[(size_t) k]; eb += bass[(size_t) k]; }
            }
            if (kBest > 0)
            {
                m.kickDetected = true;
                m.kickFreqHz  = kBest * binHz;
                m.bassFreqHz  = bBest > 0 ? bBest * binHz : 0.0;
                m.kickPunchDb = std::min (40.0, powToDb ((ek + 1e-30) / (eb + 1e-30)));
            }
        }
    }

    return m;
}

} // namespace pa
