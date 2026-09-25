#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include "Theme.h"
#include "ReportBuilder.h"

namespace ui
{
using namespace theme;

// ============================================================================
//  ScoreDial - VU-style readiness gauge + headline + summary
// ============================================================================
class ScoreDial : public juce::Component
{
public:
    void setReport (const pa::Report& r)
    {
        valid    = r.valid;
        score    = r.score;
        headline = str (r.headline);
        summary  = str (r.summary);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (col::panel);
        g.fillRoundedRectangle (b, radius);

        auto area = b.reduced ((float) pad);
        auto dialArea = area.removeFromTop (area.getHeight() * 0.47f);

        // --- gauge geometry
        constexpr float halfSweep = 0.95f;   // radians each side of vertical
        const float rad = juce::jmin (dialArea.getWidth() * 0.5f / std::sin (halfSweep), dialArea.getHeight() * 0.95f);
        const juce::Point<float> pivot (dialArea.getCentreX(), dialArea.getBottom());
        auto angleFor = [&] (float s) { return -halfSweep + 2.0f * halfSweep * juce::jlimit (0.0f, 100.0f, s) / 100.0f; };
        auto pointAt  = [&] (float angle, float r) { return juce::Point<float> (pivot.x + r * std::sin (angle), pivot.y - r * std::cos (angle)); };

        // zones
        struct Zone { float from, to; juce::Colour c; };
        const Zone zones[] = { { 0, 65, col::problem }, { 65, 85, col::warning }, { 85, 100, col::ok } };
        for (auto& z : zones)
        {
            juce::Path p;
            p.addCentredArc (pivot.x, pivot.y, rad, rad, 0.0f, angleFor (z.from), angleFor (z.to), true);
            g.setColour (z.c.withAlpha (valid ? 0.85f : 0.3f));
            g.strokePath (p, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
        }

        // ticks
        for (int t = 0; t <= 100; t += 5)
        {
            const float a = angleFor ((float) t);
            const bool major = t % 25 == 0;
            g.setColour (major ? col::textMuted : col::line);
            g.drawLine (juce::Line<float> (pointAt (a, rad - 8.0f), pointAt (a, rad - (major ? 18.0f : 13.0f))), major ? 1.6f : 1.0f);
        }
        g.setFont (font (size::small));
        g.setColour (col::textMuted);
        for (int t : { 0, 50, 100 })
        {
            const auto p = pointAt (angleFor ((float) t), rad - 30.0f);
            g.drawText (juce::String (t), juce::Rectangle<float> (40.0f, 16.0f).withCentre (p), juce::Justification::centred);
        }

        // needle
        const float a = angleFor (valid ? (float) score : 0.0f);
        g.setColour (valid ? col::accent : col::line);
        g.drawLine (juce::Line<float> (pivot, pointAt (a, rad - 4.0f)), 2.5f);
        g.fillEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre (pivot));

        // --- text
        area.removeFromTop (6.0f);
        auto numRow = area.removeFromTop (48.0f);
        g.setColour (col::text);
        g.setFont (font (size::display, true));
        g.drawText (valid ? juce::String (score) : juce::String ("--"), numRow, juce::Justification::centred);

        const auto headCol = ! valid ? col::textMuted : score >= 85 ? col::ok : score >= 65 ? col::warning : col::problem;
        auto hl = rtl (valid ? headline : HE ("עדיין אין ניתוח"), font (size::heading, true), headCol, area.getWidth(), juce::Justification::centredTop);
        hl.draw (g, area.removeFromTop (hl.getHeight()));
        area.removeFromTop (4.0f);
        auto sm = rtl (valid ? summary : HE ("ציון מוכנות למאסטר, מ-0 עד 100."), font (size::small), col::textMuted, area.getWidth(), juce::Justification::centredTop);
        sm.draw (g, area);
    }

private:
    bool valid = false;
    int score = 0;
    juce::String headline, summary;
};

// ============================================================================
//  ToneChart - 1/3-octave deviation from the mix's own tonal trend
// ============================================================================
class ToneChart : public juce::Component
{
public:
    void setData (const pa::Metrics& m)
    {
        freqs = m.bandFreqs;
        devs  = m.bandDevDb;
        tilt  = m.tiltDbPerOct;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (col::panel);
        g.fillRoundedRectangle (b, radius);

        auto area = b.reduced ((float) pad);
        auto header = area.removeFromTop (22.0f);
        g.setColour (col::text);
        g.setFont (font (size::heading, true));
        g.drawText (HE ("איזון טונאלי"), header, juce::Justification::centredRight);
        g.setColour (col::textMuted);
        g.setFont (font (size::small));
        if (! freqs.empty())
            g.drawText ("Tilt " + juce::String (tilt, 1) + " dB/oct", header, juce::Justification::centredLeft);

        auto sub = rtl (HE ("כל עמודה מראה כמה התחום חורג מהקו הטבעי של המיקס שלכם. מעל האפס זו הצטברות, מתחת זה חוסר."),
                        font (size::small), col::textMuted, area.getWidth());
        sub.draw (g, area.removeFromTop (sub.getHeight()));
        area.removeFromTop (8.0f);

        auto labels = area.removeFromBottom (16.0f);
        auto plot = area;
        const float range = 9.0f;
        auto xOf = [&] (double f) { return plot.getX() + plot.getWidth() * (float) (std::log (f / 20.0) / std::log (1000.0)); };
        auto yOf = [&] (double d) { return plot.getCentreY() - (float) juce::jlimit (-1.0, 1.0, d / range) * plot.getHeight() * 0.5f; };

        // healthy zone +-3 dB and grid
        g.setColour (col::ok.withAlpha (0.07f));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (plot.getX(), yOf (3.0), plot.getRight(), yOf (-3.0)));
        g.setColour (col::line);
        for (double d : { -6.0, 6.0 })
            g.drawHorizontalLine ((int) yOf (d), plot.getX(), plot.getRight());
        g.setColour (col::textMuted.withAlpha (0.6f));
        g.drawHorizontalLine ((int) yOf (0.0), plot.getX(), plot.getRight());

        // region names
        struct Region { double lo, hi; const char* name; };
        const Region regions[] = { { 25, 60, "סאב" }, { 60, 125, "באס" }, { 160, 400, "בוץ" },
                                   { 2000, 5000, "נוכחות" }, { 6000, 10000, "סיבילנס" } };
        g.setFont (font (size::small));
        for (auto& r : regions)
        {
            auto rr = juce::Rectangle<float>::leftTopRightBottom (xOf (r.lo), plot.getY(), xOf (r.hi), plot.getY() + 14.0f);
            g.setColour (col::textMuted.withAlpha (0.8f));
            g.drawText (HE (r.name), rr.expanded (20.0f, 0.0f), juce::Justification::centred);
        }

        // frequency labels
        g.setColour (col::textMuted);
        for (double f : { 50.0, 100.0, 250.0, 500.0, 1000.0, 2500.0, 5000.0, 10000.0 })
        {
            const juce::String t = f >= 1000.0 ? juce::String (f / 1000.0, f >= 2500.0 && f < 5000.0 ? 1 : 0) + "k" : juce::String ((int) f);
            g.drawText (t, juce::Rectangle<float> (40.0f, 16.0f).withCentre ({ xOf (f), labels.getCentreY() }), juce::Justification::centred);
        }

        if (freqs.empty())
        {
            g.setColour (col::textMuted);
            g.setFont (font (size::body));
            g.drawText (HE ("הגרף יופיע אחרי הניתוח"), plot, juce::Justification::centred);
            return;
        }

        for (size_t i = 0; i < freqs.size() && i < devs.size(); ++i)
        {
            if (freqs[i] < 20.0 || freqs[i] > 20000.0) continue;
            const float x0 = xOf (freqs[i] * std::pow (2.0, -1.0 / 6.0)) + 1.5f;
            const float x1 = xOf (freqs[i] * std::pow (2.0,  1.0 / 6.0)) - 1.5f;
            const double d = devs[i];
            const float y0 = yOf (0.0), y1 = yOf (d);
            const auto c = std::abs (d) > 6.0 ? col::problem : std::abs (d) > 3.0 ? col::warning : col::textMuted.withAlpha (0.75f);
            g.setColour (c);
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (x0, juce::jmin (y0, y1), x1, juce::jmax (y0, y1) + 0.5f));
        }
    }

private:
    std::vector<double> freqs, devs;
    double tilt = 0.0;
};

// ============================================================================
//  LiveMeters - time, peak, momentary loudness, correlation
// ============================================================================
class LiveMeters : public juce::Component
{
public:
    void setValues (bool isActive, bool isWaiting, double seconds, float peakDb, float lufs, float corr)
    {
        active = isActive; waiting = isWaiting; secs = seconds; peak = peakDb; lu = lufs; cor = corr;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (col::panel);
        g.fillRoundedRectangle (b, radius);
        auto area = b.reduced (12.0f, 6.0f);
        const float w = area.getWidth() / 4.0f;

        auto label = [&] (juce::Rectangle<float> r, const juce::String& t)
        {
            g.setColour (col::textMuted);
            g.setFont (font (size::small));
            g.drawText (t, r.removeFromTop (14.0f), juce::Justification::centredRight);
        };
        const float dim = active ? 1.0f : 0.35f;

        // (RTL) 1: time
        auto c1 = area.removeFromRight (w).reduced (8.0f, 0.0f);
        label (c1, HE ("זמן"));
        c1.removeFromTop (14.0f);
        {
            const bool blinkOn = (juce::Time::getMillisecondCounter() / 500) % 2 == 0;
            auto dot = c1.removeFromRight (14.0f).withSizeKeepingCentre (9.0f, 9.0f);
            g.setColour (active && ! waiting && blinkOn ? col::problem : col::line);
            g.fillEllipse (dot);
            const int s = (int) secs;
            g.setColour (col::text.withAlpha (dim));
            g.setFont (font (size::heading, true));
            g.drawText (juce::String::formatted ("%02d:%02d", s / 60, s % 60), c1.withTrimmedRight (6.0f), juce::Justification::centredRight);
        }

        // 2: peak bar (-60..+3)
        auto c2 = area.removeFromRight (w).reduced (8.0f, 0.0f);
        label (c2, HE ("פיק (dBFS)"));
        c2.removeFromTop (16.0f);
        {
            auto bar = c2.removeFromTop (8.0f);
            g.setColour (col::bg);
            g.fillRoundedRectangle (bar, 3.0f);
            const float norm = juce::jlimit (0.0f, 1.0f, (peak + 60.0f) / 63.0f);
            const auto c = peak > -1.0f ? col::problem : peak > -3.0f ? col::warning : col::ok;
            g.setColour (c.withAlpha (dim));
            g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * norm), 3.0f);
            g.setColour (col::text.withAlpha (dim));
            g.setFont (font (size::small));
            g.drawText (juce::String (peak, 1), c2, juce::Justification::centredLeft);
        }

        // 3: LUFS momentary
        auto c3 = area.removeFromRight (w).reduced (8.0f, 0.0f);
        label (c3, HE ("עוצמה (LUFS-M)"));
        c3.removeFromTop (14.0f);
        g.setColour (col::text.withAlpha (dim));
        g.setFont (font (size::heading, true));
        g.drawText (lu > -99.0f ? juce::String (lu, 1) : juce::String ("--"), c3, juce::Justification::centredRight);

        // 4: correlation -1..+1
        auto c4 = area.reduced (8.0f, 0.0f);
        label (c4, HE ("קורלציה"));
        c4.removeFromTop (16.0f);
        {
            auto bar = c4.removeFromTop (8.0f);
            g.setColour (col::bg);
            g.fillRoundedRectangle (bar, 3.0f);
            const float mid = bar.getCentreX();
            const float x = mid + bar.getWidth() * 0.5f * juce::jlimit (-1.0f, 1.0f, cor);
            g.setColour ((cor < 0.0f ? col::problem : col::ok).withAlpha (dim));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (juce::jmin (mid, x), bar.getY(), juce::jmax (mid, x), bar.getBottom()));
            g.setColour (col::textMuted);
            g.drawVerticalLine ((int) mid, bar.getY() - 2.0f, bar.getBottom() + 2.0f);
            g.setColour (col::text.withAlpha (dim));
            g.setFont (font (size::small));
            g.drawText (juce::String (cor, 2), c4, juce::Justification::centredLeft);
        }
    }

private:
    bool active = false, waiting = false;
    double secs = 0.0;
    float peak = -120.0f, lu = -120.0f, cor = 1.0f;
};

// ============================================================================
//  ContentView - findings cards / priorities / checklist / how-to
// ============================================================================
class ContentView : public juce::Component
{
public:
    enum class Mode { Findings, Priorities, Checklist };

    void setReport (const pa::Report& r)
    {
        report = r;
        expanded.assign (r.findings.size(), false);
        for (size_t i = 0; i < r.findings.size(); ++i)
            expanded[i] = (int) r.findings[i].status >= (int) pa::Status::Warning;
        relayout();
    }
    void setMode (Mode m)            { mode = m; relayout(); }
    void setLayoutWidth (int w)      { width = juce::jmax (200, w); relayout(); }

    void paint (juce::Graphics& g) override
    {
        for (auto& c : cards)  paintCard (g, c);
        for (auto& r : rows)   paintRow (g, r);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (mode != Mode::Findings || ! report.valid) return;
        for (size_t i = 0; i < cards.size(); ++i)
            if (cards[i].bounds.contains (e.position) && cards[i].index >= 0)
            {
                expanded[(size_t) cards[i].index] = ! expanded[(size_t) cards[i].index];
                relayout();
                return;
            }
    }

private:
    struct Card
    {
        int index = -1;
        juce::Rectangle<float> bounds;
        pa::Status status = pa::Status::Ok;
        juce::TextLayout title, value, expl, critLabel, crit, hint;
        std::vector<juce::TextLayout> tips;
        float yTitle = 0, yValue = 0, yExpl = 0, yCrit = 0, critH = 0, yTips = 0, yHint = 0, titleH = 0;
        bool showCrit = false, showTips = false, showHint = false;
    };
    enum class RowKind { Heading, Numbered, Check, Plain };
    struct Row
    {
        RowKind kind = RowKind::Plain;
        juce::Rectangle<float> bounds;
        juce::TextLayout text;
        pa::Status status = pa::Status::Info;
        int number = 0;
    };

    // --------------------------------------------------------------- layout
    void relayout()
    {
        cards.clear();
        rows.clear();
        float y = 0.0f;

        if (! report.valid)                   y = layoutHowTo();
        else if (mode == Mode::Findings)      y = layoutFindings();
        else if (mode == Mode::Priorities)    y = layoutPriorities();
        else                                  y = layoutChecklist();

        setSize (width, (int) std::ceil (y + 8.0f));
        repaint();
    }

    float layoutFindings()
    {
        float y = 0.0f;
        const float W = (float) width;
        const float innerW = W - 2.0f * pad - 6.0f;

        for (size_t i = 0; i < report.findings.size(); ++i)
        {
            const auto& f = report.findings[i];
            Card c;
            c.index = (int) i;
            c.status = f.status;
            float cy = (float) pad;

            c.title  = rtl (str (f.title), font (size::heading, true), col::text, innerW - 110.0f);
            c.yTitle = cy;
            c.titleH = juce::jmax (24.0f, c.title.getHeight());
            cy += c.titleH + 4.0f;

            c.value  = ltr (str (f.value), font (size::small), col::accent.withAlpha (0.9f), innerW);
            c.yValue = cy;
            cy += c.value.getHeight() + 8.0f;

            c.expl  = rtl (str (f.explanation), font (size::body), col::text.withAlpha (0.92f), innerW);
            c.yExpl = cy;
            cy += c.expl.getHeight() + 10.0f;

            const bool open = expanded[i];
            const bool hasMore = ! f.criticalTip.empty() || ! f.tips.empty();
            if (open && ! f.criticalTip.empty())
            {
                c.showCrit  = true;
                c.critLabel = rtl (HE ("טיפ קריטי"), font (size::small, true), col::accent, innerW - 24.0f);
                c.crit      = rtl (str (f.criticalTip), font (size::body), col::text, innerW - 24.0f);
                c.yCrit     = cy;
                c.critH     = 10.0f + c.critLabel.getHeight() + 4.0f + c.crit.getHeight() + 10.0f;
                cy += c.critH + 10.0f;
            }
            if (open && ! f.tips.empty())
            {
                c.showTips = true;
                c.yTips = cy;
                for (auto& t : f.tips)
                {
                    c.tips.push_back (rtl (juce::String::fromUTF8 ("•  ") + str (t), font (size::body), col::textMuted.brighter (0.25f), innerW));
                    cy += c.tips.back().getHeight() + 6.0f;
                }
            }
            if (hasMore)
            {
                c.showHint = true;
                c.hint  = rtl (open ? HE ("לחצו כדי לסגור") : HE ("לחצו להצגת הטיפים"), font (size::small), col::textMuted, innerW);
                c.yHint = cy;
                cy += c.hint.getHeight() + 4.0f;
            }
            cy += (float) pad - 6.0f;

            c.bounds = { 0.0f, y, W, cy };
            y += cy + (float) gap;
            cards.push_back (std::move (c));
        }
        return y;
    }

    float addRow (float y, RowKind kind, const juce::String& text, pa::Status s = pa::Status::Info, int number = 0)
    {
        Row r;
        r.kind = kind;
        r.status = s;
        r.number = number;
        const float W = (float) width;
        const float indent = (kind == RowKind::Numbered || kind == RowKind::Check) ? 40.0f : 0.0f;
        const auto f = kind == RowKind::Heading ? font (size::heading, true) : font (size::body);
        const auto c = kind == RowKind::Heading ? col::text : col::text.withAlpha (0.92f);
        r.text = rtl (text, f, c, W - 2.0f * pad - indent);
        const float h = juce::jmax (26.0f, r.text.getHeight()) + 16.0f;
        r.bounds = { 0.0f, y, W, h };
        rows.push_back (std::move (r));
        return y + h + 4.0f;
    }

    float layoutPriorities()
    {
        float y = addRow (0.0f, RowKind::Heading, HE ("מה לתקן קודם (לפי השפעה על המאסטר)"));
        if (report.priorities.empty())
            return addRow (y, RowKind::Plain, HE ("אין בעיות דחופות. אפשר לעבור לרשימת המסירה."));
        int n = 1;
        for (auto& p : report.priorities) y = addRow (y, RowKind::Numbered, str (p), pa::Status::Problem, n++);
        return y;
    }

    float layoutChecklist()
    {
        float y = addRow (0.0f, RowKind::Heading, HE ("רשימת מסירה למאסטר"));
        for (auto& c : report.checklist) y = addRow (y, RowKind::Check, str (c.text), c.status);
        return addRow (y, RowKind::Plain, HE ("עיגול ריק: לבדוק ידנית. ירוק: עבר. אדום: לתקן לפני השליחה."));
    }

    float layoutHowTo()
    {
        float y = addRow (0.0f, RowKind::Heading, HE ("איך משתמשים"));
        const char* steps[] = {
            "שימו את Production Analyzer אחרון בשרשרת של ערוץ ה-Master. אם יש לימיטר להאזנה בלבד, עקפו אותו כדי לנתח את מה שבאמת יישלח למאסטר.",
            "בחרו סגנון מוזיקלי למעלה. הוא משנה את הספים של הלואו-אנד והדינמיקה.",
            "לחצו 'התחל ניתוח' ונגנו את הטראק מההתחלה ועד הסוף. ההקלטה רצה רק כש-Play פועל.",
            "לחצו 'עצור והפק דוח'. הממצאים ממוינים לפי חומרה, ולכל אחד יש טיפ קריטי וטיפים מעשיים.",
            "אפשרות נוספת: 'טען קובץ אודיו' מנתח קובץ WAV/AIFF/FLAC/MP3 של המיקס, בלי לנגן.",
        };
        int n = 1;
        for (auto* s : steps) y = addRow (y, RowKind::Numbered, HE (s), pa::Status::Info, n++);
        return y;
    }

    // ---------------------------------------------------------------- paint
    void paintCard (juce::Graphics& g, const Card& c)
    {
        const auto r = c.bounds;
        const auto sc = statusColour (c.status);
        g.setColour (col::panel);
        g.fillRoundedRectangle (r, radius);
        if (c.status == pa::Status::Problem)
        {
            g.setColour (sc.withAlpha (0.35f));
            g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
        }
        // status stripe on the right (RTL reading start)
        g.setColour (sc);
        g.fillRoundedRectangle (juce::Rectangle<float> (r.getRight() - 6.0f, r.getY(), 6.0f, r.getHeight()), 3.0f);

        const float left = r.getX() + pad;
        const float innerW = r.getWidth() - 2.0f * pad - 6.0f;

        // title + pill
        c.title.draw (g, { left + 110.0f, r.getY() + c.yTitle, innerW - 110.0f, c.titleH });
        auto pill = juce::Rectangle<float> (left, r.getY() + c.yTitle + 1.0f, 96.0f, 22.0f);
        g.setColour (sc.withAlpha (0.16f));
        g.fillRoundedRectangle (pill, radiusSmall);
        g.setColour (sc);
        g.setFont (font (size::small, true));
        g.drawText (juce::String::fromUTF8 (pa::statusName (c.status)), pill, juce::Justification::centred);

        c.value.draw (g, { left, r.getY() + c.yValue, innerW, c.value.getHeight() });
        c.expl.draw  (g, { left, r.getY() + c.yExpl,  innerW, c.expl.getHeight() });

        if (c.showCrit)
        {
            auto box = juce::Rectangle<float> (left, r.getY() + c.yCrit, innerW, c.critH);
            g.setColour (col::accent.withAlpha (0.09f));
            g.fillRoundedRectangle (box, radiusSmall);
            g.setColour (col::accent);
            g.fillRect (juce::Rectangle<float> (box.getRight() - 3.0f, box.getY() + 6.0f, 3.0f, box.getHeight() - 12.0f));
            auto inner = box.reduced (12.0f, 10.0f);
            c.critLabel.draw (g, inner.removeFromTop (c.critLabel.getHeight()));
            inner.removeFromTop (4.0f);
            c.crit.draw (g, inner);
        }
        if (c.showTips)
        {
            float ty = r.getY() + c.yTips;
            for (auto& t : c.tips)
            {
                t.draw (g, { left, ty, innerW, t.getHeight() });
                ty += t.getHeight() + 6.0f;
            }
        }
        if (c.showHint)
            c.hint.draw (g, { left, r.getY() + c.yHint, innerW, c.hint.getHeight() });
    }

    void paintRow (juce::Graphics& g, const Row& r)
    {
        const auto b = r.bounds;
        if (r.kind != RowKind::Heading)
        {
            g.setColour (col::panel);
            g.fillRoundedRectangle (b, radius);
        }
        const float right = b.getRight() - pad;
        const float indent = (r.kind == RowKind::Numbered || r.kind == RowKind::Check) ? 40.0f : 0.0f;
        const float textW = b.getWidth() - 2.0f * pad - indent;
        const float ty = b.getY() + 8.0f;

        if (r.kind == RowKind::Numbered)
        {
            auto circ = juce::Rectangle<float> (right - 26.0f, ty, 26.0f, 26.0f);
            g.setColour (col::accent.withAlpha (0.16f));
            g.fillEllipse (circ);
            g.setColour (col::accent);
            g.setFont (font (size::body, true));
            g.drawText (juce::String (r.number), circ, juce::Justification::centred);
        }
        else if (r.kind == RowKind::Check)
        {
            auto circ = juce::Rectangle<float> (right - 24.0f, ty + 1.0f, 24.0f, 24.0f);
            const auto c = statusColour (r.status);
            if (r.status == pa::Status::Ok)
            {
                g.setColour (c);
                g.fillEllipse (circ);
                juce::Path tick;
                tick.startNewSubPath (circ.getX() + 6.5f, circ.getCentreY() + 0.5f);
                tick.lineTo (circ.getX() + 10.5f, circ.getCentreY() + 4.5f);
                tick.lineTo (circ.getRight() - 6.0f, circ.getY() + 7.5f);
                g.setColour (col::bg);
                g.strokePath (tick, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            else if (r.status == pa::Status::Problem)
            {
                g.setColour (c);
                g.fillEllipse (circ);
                g.setColour (col::bg);
                auto x = circ.reduced (7.5f);
                g.drawLine (juce::Line<float> (x.getTopLeft(), x.getBottomRight()), 2.2f);
                g.drawLine (juce::Line<float> (x.getTopRight(), x.getBottomLeft()), 2.2f);
            }
            else
            {
                g.setColour (col::textMuted);
                g.drawEllipse (circ.reduced (1.0f), 1.6f);
            }
        }
        r.text.draw (g, { b.getX() + pad, ty + 2.0f, textW, r.text.getHeight() });
    }

    pa::Report report;
    Mode mode = Mode::Findings;
    int width = 600;
    std::vector<bool> expanded;
    std::vector<Card> cards;
    std::vector<Row> rows;
};

} // namespace ui
