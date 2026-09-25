#include "ReportBuilder.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>

namespace pa
{
namespace
{
// ----------------------------------------------------------------------------
//  Genre profiles - tweak thresholds here
// ----------------------------------------------------------------------------
struct Profile
{
    const char* name;
    double lowAllowDb;     // how far sub+bass may sit above the mix trend
    double plrWarn, plrProblem;
    double lraLow, lraHigh;
    double crestWarn, crestProblem;
};

const Profile kProfiles[] = {
    { "אלקטרוני / EDM / האוס / טכנו", 5.0, 10.0, 7.5, 3.0, 12.0, 10.0, 7.5 },
    { "היפ-הופ / טראפ",                6.0, 10.0, 7.5, 3.0, 12.0, 10.0, 7.5 },
    { "פופ / רוק",                      3.0, 11.0, 8.5, 4.0, 14.0, 11.0, 8.5 },
    { "אקוסטי / ג'אז / אורקסטרלי",     1.0, 13.0, 10.0, 5.0, 20.0, 13.0, 10.0 },
};
constexpr int kNumProfiles = (int) (sizeof (kProfiles) / sizeof (kProfiles[0]));

// ----------------------------------------------------------------------------
std::string fmt (double v, int d = 1)
{
    char b[64];
    std::snprintf (b, sizeof (b), "%.*f", d, v);
    return b;
}
std::string fmtS (double v, int d = 1)
{
    char b[64];
    std::snprintf (b, sizeof (b), "%+.*f", d, v);
    return b;
}
// Hebrew-friendly number inside RTL sentences ("מינוס 3.2" instead of "-3.2")
std::string he (double v, int d = 1)
{
    if (v < 0.0) return "מינוס " + fmt (-v, d);
    return fmt (v, d);
}
std::string hz (double f)
{
    if (f >= 1000.0) return fmt (f / 1000.0, f >= 10000.0 ? 1 : 2) + "kHz";
    return fmt (f, 0) + "Hz";
}
std::string noteName (double f)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    if (f <= 0.0) return "-";
    const double midi = 69.0 + 12.0 * std::log2 (f / 440.0);
    const int n = (int) std::lround (midi);
    const int idx = ((n % 12) + 12) % 12;
    return std::string (names[idx]) + std::to_string (n / 12 - 1);
}
double clamp01 (double x) { return std::max (0.0, std::min (1.0, x)); }
Status worst (Status a, Status b) { return (int) a >= (int) b ? a : b; }

void addSentence (std::string& s, const std::string& add)
{
    if (add.empty()) return;
    if (! s.empty()) s += " ";
    s += add;
}
void addTip (Finding& f, const std::string& t)
{
    for (auto& e : f.tips) if (e == t) return;
    f.tips.push_back (t);
}
void setCritical (Finding& f, const std::string& t)
{
    if (f.criticalTip.empty()) f.criticalTip = t;
}

// ============================================================================
//  1. Headroom & loudness
// ============================================================================
Finding checkHeadroom (const Metrics& m)
{
    Finding f;
    f.id = "headroom";
    f.title = "Headroom ועוצמה לפני מאסטר";
    f.value = "True Peak " + fmt (m.truePeakDb) + " dBTP  |  Integrated " + fmt (m.integratedLufs)
            + " LUFS  |  Max short-term " + fmt (m.maxShortTermLufs) + " LUFS";

    const double tp = m.truePeakDb;
    if (tp > -1.0)
    {
        f.status = Status::Problem;
        addSentence (f.explanation, "הפיקים מגיעים ל-" + he (tp) + " dBTP, כמעט בלי מרווח. מהנדס המאסטר צריך headroom כדי לעבוד עם EQ, קומפרסיה וסאטורציה בלי להיכנס לעיוות.");
        setCritical (f, "הורידו את ה-Master (או Utility אחרון בשרשרת) עד שהפיקים יהיו בין מינוס 6 למינוס 3 dBFS. לא להוריד את הפיידרים של הערוצים אחד-אחד, כדי לא לשנות את האיזון.");
    }
    else if (tp > -3.0)
    {
        f.status = Status::Warning;
        addSentence (f.explanation, "הפיקים ב-" + he (tp) + " dBTP. זה עובד, אבל המרווח צר. רוב מהנדסי המאסטר מבקשים פיקים סביב מינוס 6 dBFS.");
        setCritical (f, "הורידו 2–3 dB ב-Utility בסוף שרשרת המאסטר לפני הייצוא.");
    }
    else if (tp < -18.0)
    {
        f.status = Status::Info;
        addSentence (f.explanation, "המיקס שקט מאוד (פיקים ב-" + he (tp) + " dBTP). בייצוא 24 ביט או 32 float זה תקין טכנית, אבל עדיף להגיש סביב מינוס 6 dBFS.");
        addTip (f, "העלו את ה-Master עד שהפיקים יהיו בערך מינוס 6 dBFS. בייצוא float אין בזה סיכון.");
    }
    else
    {
        f.status = Status::Ok;
        addSentence (f.explanation, "יש מרווח טוב: פיקים ב-" + he (tp) + " dBTP. בדיוק מה שמהנדס מאסטר רוצה לקבל.");
    }

    if (m.integratedLufs > -11.0)
    {
        f.status = worst (f.status, Status::Problem);
        addSentence (f.explanation, "העוצמה הממוצעת (" + he (m.integratedLufs) + " LUFS) היא כבר עוצמה של מאסטר מוגמר. כמעט בטוח שיש לימיטר, קליפר או קומפרסיה כבדה על ה-Master.");
        f.criticalTip = "עקפו (Bypass) את הלימיטר/קליפר/Glue על ה-Master וייצאו בלעדיו. את העוצמה הסופית משיגים במאסטר, לא במיקס.";
    }
    else if (m.integratedLufs > -14.0)
    {
        f.status = worst (f.status, Status::Warning);
        addSentence (f.explanation, "העוצמה הממוצעת (" + he (m.integratedLufs) + " LUFS) גבוהה למיקס לפני מאסטר. בדקו מה יושב על ערוץ ה-Master.");
        addTip (f, "אם הלימיטר על ה-Master משמש רק להאזנה, עקפו אותו בייצוא. אם הוא חלק מהסאונד, ספרו על זה למהנדס ושלחו גם גרסה בלעדיו.");
    }

    addTip (f, "ייצוא: WAV או AIFF, ב-24 ביט או 32 float, באותו Sample Rate של הפרויקט. בלי Normalize ובלי Dither (ב-32 float).");
    addTip (f, "השאירו 1–2 שניות שקט בהתחלה ובסוף, ואל תעשו Fade out על הזנב של הרוורב. את הפייד עושים במאסטר.");
    return f;
}

// ============================================================================
//  2. Clipping & technical
// ============================================================================
Finding checkClipping (const Metrics& m)
{
    Finding f;
    f.id = "clipping";
    f.title = "קליפינג ובעיות טכניות";
    f.value = "Clip events " + std::to_string (m.clipEvents) + "  |  Over 0dBFS " + std::to_string (m.oversFloat)
            + "  |  ISP " + std::to_string (m.interSampleOvers) + "  |  Flat-tops " + std::to_string (m.flatTopRuns)
            + "  |  DC " + fmt (m.dcOffsetDb, 0) + " dB";

    const double minutes = std::max (0.25, m.durationSec / 60.0);
    const double flatPerMin = (double) m.flatTopRuns / minutes;

    if (m.clipEvents > 0 || m.oversFloat > 0)
    {
        f.status = Status::Problem;
        addSentence (f.explanation, "זוהו " + std::to_string (m.clipEvents) + " אירועי קליפינג (רצף של לפחות 3 דגימות על 0dBFS).");
        if (m.oversFloat > 0)
            addSentence (f.explanation, "חלק מהדגימות עוברות את 0dBFS. בתוך אבלטון (32-bit float) זה לא נשמע, אבל בקובץ 24 ביט הן ייחתכו לעיוות קשה שאי אפשר לתקן במאסטר.");
        f.criticalTip = "אל תסתירו את זה עם לימיטר. הורידו את ה-Master או את ה-Group שמזין אותו, עד שאין אף דגימה מעל מינוס 1 dBFS.";
        addTip (f, "כדי למצוא את המקור: עברו על מדי הערוצים וה-Groups באבלטון בזמן הקטע הרועש ביותר, וחפשו אדום.");
        addTip (f, "אם הקליפינג מכוון (קליפר כצבע סאונד), שימו אותו על הערוץ או ה-Group, לא על ה-Master, ותשאירו headroom אחריו.");
    }
    else if (m.interSampleOvers > 0)
    {
        f.status = Status::Warning;
        addSentence (f.explanation, "אין דגימות מעל 0dBFS, אבל יש " + std::to_string (m.interSampleOvers) + " מקרים שבהם הגל המשוחזר עובר 0dB בין הדגימות (Inter-sample peaks). בהמרה ל-MP3/AAC ובסטרימינג זה עלול לעוות.");
        setCritical (f, "הורידו 1–2 dB ב-Master כדי שה-True Peak יהיה מתחת למינוס 1 dBTP.");
    }

    if (flatPerMin > 15.0)
    {
        f.status = worst (f.status, Status::Warning);
        addSentence (f.explanation, "זוהו " + std::to_string (m.flatTopRuns) + " קטעי 'ראש שטוח': צורת גל שנחתכה במקום כלשהו בשרשרת ורק אחר כך הונמכה.");
        addTip (f, "בדקו סמפלים של קיק וסנר. חלק מהספריות מגיעות כבר חתוכות, ולפעמים זה מכוון.");
        addTip (f, "בדקו פלאגינים עם Drive או Clip (Saturator, Drum Buss, Roar, קליפרים) ובדקו שהם עושים את מה שאתם רוצים.");
    }

    if (m.dcOffsetDb > -50.0)
    {
        f.status = worst (f.status, Status::Warning);
        addSentence (f.explanation, "יש DC offset (" + he (m.dcOffsetDb, 0) + " dB): הגל לא ממורכז סביב האפס. זה מבזבז headroom ויכול לגרום לקליקים בחיתוכים.");
        addTip (f, "High-pass עדין (10–20Hz) על הערוץ החשוד, או על ה-Master. בדרך כלל המקור הוא סמפל או סינת עם DC.");
    }

    if (f.status == Status::Ok)
        f.explanation = "לא זוהו קליפינג, Inter-sample peaks, צורות גל חתוכות או DC offset. נקי טכנית.";
    return f;
}

// ============================================================================
//  3. Crest factor
// ============================================================================
Finding checkCrest (const Metrics& m, const Profile& p)
{
    Finding f;
    f.id = "crest";
    f.title = "קרסט פקטור (Crest Factor)";
    f.value = "Crest " + fmt (m.crestDb) + " dB  |  Peak " + fmt (m.samplePeakDb) + " dBFS  |  RMS " + fmt (m.rmsDb) + " dBFS";

    const std::string base = "קרסט פקטור הוא ההפרש בין הפיק לרמה הממוצעת (RMS). ערך גבוה אומר טרנזיינטים חיים ומיקס שנושם. ערך נמוך אומר מיקס דחוס ושטוח.";

    if (m.crestDb < p.crestProblem)
    {
        f.status = Status::Problem;
        f.explanation = base + " כאן הוא רק " + fmt (m.crestDb) + " dB: המיקס דחוס מאוד. במאסטר לא יישאר מקום להעלות עוצמה בלי להרוס את הפאנץ'.";
        f.criticalTip = "עברו על הקומפרסורים ב-Groups וב-Master (במיוחד Glue עם Ratio גבוה ו-Makeup) והורידו אותם בחצי. בדקו שהתופים חוזרים לנשום.";
        addTip (f, "Attack מהיר מדי בקומפרסור של התופים חונק את הטרנזיינט. נסו 10–30ms.");
        addTip (f, "קומפרסיה מקבילית (Dry/Wet 30–50%) שומרת גם על עוצמה וגם על פאנץ'.");
    }
    else if (m.crestDb < p.crestWarn)
    {
        f.status = Status::Warning;
        f.explanation = base + " כאן " + fmt (m.crestDb) + " dB: דחוס במידה. זה סביר לז'אנרים צפופים, אבל כדאי לוודא שהקומפרסיה לא עושה את העבודה של המאסטר.";
        addTip (f, "השוו A/B עם הקומפרסורים על ה-Groups עקופים, באותה עוצמה נשמעת (Utility). האם הדחיסה באמת מוסיפה משהו?");
    }
    else if (m.crestDb > 20.0)
    {
        f.status = Status::Info;
        f.explanation = base + " כאן " + fmt (m.crestDb) + " dB: דינמי מאוד. זה בסדר, אבל ייתכן שיש פיקים בודדים חדים (סנר, פרקשן, קליק) שיגבילו את העוצמה במאסטר.";
        addTip (f, "חפשו פיקים חריגים ספציפיים ושקלו קליפר או לימיטר עדין על הערוץ עצמו (1–2 dB), לא על ה-Master.");
    }
    else
    {
        f.status = Status::Ok;
        f.explanation = base + " כאן " + fmt (m.crestDb) + " dB: טווח בריא למיקס לפני מאסטר.";
    }
    return f;
}

// ============================================================================
//  4. Dynamic range (PLR / LRA)
// ============================================================================
Finding checkDynamics (const Metrics& m, const Profile& p)
{
    Finding f;
    f.id = "dynamics";
    f.title = "טווח דינמי (PLR / LRA)";
    f.value = "PLR " + fmt (m.plr) + " dB  |  LRA " + fmt (m.lra) + " LU";
    f.explanation = "PLR הוא ההפרש בין ה-True Peak לעוצמה הממוצעת. LRA מודד כמה העוצמה משתנה בין חלקי השיר (בית, פזמון, ברייק, דרופ).";

    if (m.plr < p.plrProblem)
    {
        f.status = Status::Problem;
        addSentence (f.explanation, "PLR של " + fmt (m.plr) + " dB נמוך מאוד למיקס. אין למאסטר מקום לעבוד.");
        f.criticalTip = "בטלו לימיטר או קומפרסיה כבדה על ה-Master לפני הייצוא. PLR של 10 dB ומעלה הוא נקודת פתיחה טובה.";
    }
    else if (m.plr < p.plrWarn)
    {
        f.status = Status::Warning;
        addSentence (f.explanation, "PLR של " + fmt (m.plr) + " dB קצת נמוך. המיקס כבר מוחץ במידה מסוימת.");
        addTip (f, "בדקו כמה Gain Reduction יש על קומפרסורי ה-Groups. יותר מ-3–4 dB באופן קבוע זה הרבה לפני מאסטר.");
    }

    if (m.lra < p.lraLow)
    {
        f.status = worst (f.status, Status::Warning);
        addSentence (f.explanation, "LRA של " + fmt (m.lra) + " LU: כל חלקי השיר כמעט באותה עוצמה. לרוב זה אומר שאין מספיק ניגוד בין הברייק לדרופ או לפזמון.");
        addTip (f, "אוטומציה: הורידו 1–3 dB בבתים ובברייקים, או דללו אותם, כדי שהדרופ או הפזמון ירגישו גדולים יותר. המאסטר לא יכול ליצור ניגוד שלא קיים.");
    }
    else if (m.lra > p.lraHigh)
    {
        f.status = worst (f.status, Status::Info);
        addSentence (f.explanation, "LRA של " + fmt (m.lra) + " LU: יש הבדלי עוצמה גדולים בין החלקים. ודאו שהחלקים השקטים לא הולכים לאיבוד בהאזנה בסביבה רועשת.");
        addTip (f, "האזינו לחלקים השקטים ברמה נמוכה. אם משהו נעלם, העלו אותו באוטומציה במקום לדחוס את כל המיקס.");
    }

    if (f.status == Status::Ok)
        addSentence (f.explanation, "PLR של " + fmt (m.plr) + " dB ו-LRA של " + fmt (m.lra) + " LU: דינמיקה בריאה עם מקום לעבודה במאסטר.");
    return f;
}

// ============================================================================
//  5. Low end
// ============================================================================
Finding checkLowEnd (const Metrics& m, const Profile& p)
{
    Finding f;
    f.id = "lowend";
    f.title = "הצטברות בתדרים נמוכים (Low-end)";
    const double lowDev = 0.5 * (m.subDevDb + m.bassDevDb);
    f.value = "Sub 25–60Hz " + fmtS (m.subDevDb) + " dB  |  Bass 60–125Hz " + fmtS (m.bassDevDb)
            + " dB  |  Low-mids 160–400Hz " + fmtS (m.lowMidDevDb) + " dB  (vs. mix trend)";

    const double over = lowDev - p.lowAllowDb;
    if (over > 4.0)
    {
        f.status = Status::Problem;
        addSentence (f.explanation, "הסאב והבאס (30–125Hz) חזקים בכ-" + fmt (over) + " dB מעבר למה שמקובל בסגנון הזה. זה יגרום למאסטר 'לנשום' לפי הבאס, יקטין את העוצמה האפשרית, וישמע עמוס במערכות גדולות.");
        f.criticalTip = "השוו לטראק רפרנס מאותו ז'אנר באותה עוצמה נשמעת. בדרך כלל הורדה של 2–3 dB לקבוצת הבאס/סאב פותרת את רוב הבעיה.";
    }
    else if (over > 0.0)
    {
        f.status = Status::Warning;
        addSentence (f.explanation, "הלואו-אנד קצת כבד ביחס לשאר הספקטרום (כ-" + fmt (over) + " dB מעל הטווח המקובל לסגנון).");
    }
    else if (over < -8.0)
    {
        f.status = Status::Warning;
        addSentence (f.explanation, "הלואו-אנד חלש ביחס לשאר הספקטרום. המיקס עלול להישמע דק, במיוחד במועדון או ברכב.");
        addTip (f, "בדקו שהבאס או הסאב לא נחתכים ב-High-pass, ושאין להם בעיית פאזה (ראו סעיף פאזה ומונו).");
        addTip (f, "סאטורציה עדינה על הבאס מוסיפה הרמוניות שנשמעות גם ברמקולים קטנים.");
    }

    if (over > 0.0)
    {
        addTip (f, "High-pass על כל מה שלא צריך סאב: פאדים, לידים, ווקאל, FX ו-Returns של רוורב. אפילו 100–150Hz זה בסדר.");
        addTip (f, "Multiband או Dynamic EQ על הבאס, כדי לשלוט בתווים בודדים שקופצים חזק מהשאר.");
        addTip (f, "בחדר לא מטופל הלואו-אנד מטעה. בדקו גם באוזניות ובעוצמה נמוכה.");
    }

    if (m.lowMidDevDb > 3.0)
    {
        f.status = worst (f.status, Status::Problem);
        addSentence (f.explanation, "יש הצטברות בולטת ב-160–400Hz (כ-" + fmt (m.lowMidDevDb) + " dB מעל הקו הטבעי של המיקס). זה 'הבוץ' שגורם למיקס להישמע עמום ומכוסה.");
        setCritical (f, "חיתוך רחב ועדין (1–3 dB, Q נמוך) סביב 200–400Hz בכלים שמצטברים שם: פאדים, גיטרות, פסנתר, סנר וטומים. לא על ה-Master.");
    }
    else if (m.lowMidDevDb > 1.5)
    {
        f.status = worst (f.status, Status::Warning);
        addSentence (f.explanation, "קצת עומס ב-160–400Hz. כדאי לבדוק שלא כל הכלים 'מחזיקים חום' באותו אזור.");
    }
    if (m.lowMidDevDb > 1.5)
    {
        addTip (f, "Returns של רוורב: High-pass גבוה יותר (200–400Hz) מנקה הרבה בוץ בלי לפגוע בכלים עצמם.");
        addTip (f, "אל תחתכו מכל הכלים. תנו את החום לאחד או שניים שבאמת צריכים אותו (למשל באס וווקאל).");
    }

    if (f.status == Status::Ok)
        f.explanation = "הלואו-אנד מאוזן ביחס לשאר הספקטרום, בלי הצטברות בולטת באזור 200–400Hz.";
    return f;
}

// ============================================================================
//  6. Harshness
// ============================================================================
Finding checkHarshness (const Metrics& m)
{
    Finding f;
    f.id = "harsh";
    f.title = "חדות וצרימה (Harshness)";
    f.value = "2–5kHz " + fmtS (m.presenceDevDb) + " dB  |  6–10kHz " + fmtS (m.sibilanceDevDb)
            + " dB  |  Spikes " + fmt (m.harshSpikePct) + "% of time";

    if (m.presenceDevDb > 3.0)
    {
        f.status = Status::Problem;
        addSentence (f.explanation, "אזור 2–5kHz חזק בכ-" + fmt (m.presenceDevDb) + " dB מעל הקו הטבעי של המיקס. זה האזור שהאוזן הכי רגישה אליו. הוא גורם לעייפות האזנה, והמאסטר רק יחמיר אותו.");
        f.criticalTip = "מצאו את הכלי הבולט (Solo על סינתים, גיטרות, ווקאל ומצלתיים) ושימו Dynamic EQ על 2–5kHz, רק על הכלי הזה.";
    }
    else if (m.presenceDevDb > 1.5)
    {
        f.status = Status::Warning;
        addSentence (f.explanation, "אזור 2–5kHz קצת בולט (" + fmtS (m.presenceDevDb) + " dB). כדאי לבדוק בהאזנה ארוכה בעוצמה בינונית.");
    }
    else if (m.presenceDevDb < -4.0)
    {
        f.status = Status::Info;
        addSentence (f.explanation, "אזור 2–5kHz חלש יחסית. המיקס עלול להישמע עמום או 'רחוק'.");
        addTip (f, "לפני שמגבירים היי, נסו להוריד בוץ ב-200–500Hz. לפעמים זה כל מה שחסר לבהירות.");
    }

    if (m.sibilanceDevDb > 3.0)
    {
        f.status = worst (f.status, Status::Warning);
        addSentence (f.explanation, "גם 6–10kHz בולט (" + fmtS (m.sibilanceDevDb) + " dB): סיבילנטים ('ס', 'ש'), הי-האטים או מצלתיים חדים.");
        addTip (f, "De-esser על הווקאל, ו-De-esser או Dynamic EQ עדין על ה-Group של ההי-האטים והמצלתיים.");
    }

    if (m.harshSpikePct > 6.0)
    {
        f.status = worst (f.status, Status::Warning);
        addSentence (f.explanation, "ב-" + fmt (m.harshSpikePct) + "% מהזמן אזור הנוכחות קופץ פתאום ביותר מ-5 dB. יש קטעים ספציפיים צורמים (לרוב ליד, סינת או ווקאל בקטע הגבוה).");
        addTip (f, "האזינו לקטעים שבהם הכלי המוביל עולה לאוקטבה גבוהה. Dynamic EQ עובד שם טוב יותר מ-EQ קבוע.");
    }

    if ((int) f.status >= (int) Status::Warning)
    {
        addTip (f, "סאטורציה ודיסטורשן מוסיפים הרמוניות עליונות שמצטברות. בדקו כמה Drive יש על כל כלי.");
        addTip (f, "בדיקה מהירה: האזינו 10 דקות ברצף בעוצמה בינונית. אם האוזניים מתעייפות, זה האזור.");
    }

    if (f.status == Status::Ok)
        f.explanation = "אזורי 2–5kHz ו-6–10kHz מאוזנים. אין סימנים לצרימה קבועה או לקפיצות חדות.";
    return f;
}

// ============================================================================
//  7. Masking (estimated from the stereo mix)
// ============================================================================
Finding checkMasking (const Metrics& m, bool kickBassClash)
{
    Finding f;
    f.id = "masking";
    f.title = "מיסוך (Masking)";

    const double mud      = clamp01 ((m.lowMidDevDb + 1.0) / 5.0);
    const double dense    = clamp01 ((m.midFlatness - 0.12) / 0.30);
    const double centered = clamp01 ((-m.midSideToMidDb - 8.0) / 14.0);
    double index = 100.0 * (0.4 * mud + 0.3 * dense + 0.3 * centered);
    if (kickBassClash) index = std::min (100.0, index + 15.0);

    f.value = "Masking index " + fmt (index, 0) + "/100  |  Side/Mid (250Hz–4kHz) " + fmt (m.midSideToMidDb)
            + " dB  |  Flatness " + fmt (m.midFlatness, 2);

    f.explanation = "מיסוך הוא מצב שבו כלים שיושבים באותם תדרים ובאותו מקום בסטריאו מסתירים זה את זה. ההערכה מבוססת על שלושה סימנים במיקס הסופי.";

    if (index > 60.0)      f.status = Status::Problem;
    else if (index > 40.0) f.status = Status::Warning;

    if (mud > 0.6)
    {
        addSentence (f.explanation, "הרבה אנרגיה מצטברת ב-160–400Hz, והכלים נבלעים זה בזה באזור החום.");
        addTip (f, "חלקו תפקידים: לכל כלי 'חלון' תדרים משלו. חיתוך קטן בכלי אחד במקום שבו כלי אחר צריך לבלוט.");
    }
    if (dense > 0.6)
    {
        addSentence (f.explanation, "האמצע מלא ואחיד מאוד: הרבה שכבות שמתחרות על אותו מרחב.");
        addTip (f, "צמצמו שכבות. לא כל סינת צריך את כל הספקטרום. חתכו כל שכבה לתחום שבו היא תורמת.");
    }
    if (centered > 0.6)
    {
        addSentence (f.explanation, "רוב האנרגיה ב-250Hz–4kHz יושבת במרכז (Side חלש מאוד), וכלים רבים נלחמים על אותה נקודה.");
        addTip (f, "פזרו כלים משניים בפאנינג (L/R): פרקשן, קורדים, כלי ליווי. השאירו במרכז קיק, סנר, באס וווקאל ראשי.");
    }
    if (kickBassClash)
        addSentence (f.explanation, "בנוסף, הקיק והבאס יושבים על אותו תדר (ראו סעיף קיק ובאס).");

    if ((int) f.status >= (int) Status::Warning)
    {
        f.criticalTip = "Sidechain EQ: על הכלים המלווים, הורדה של 2–3 dB ב-1–3kHz רק כשהווקאל או הליד מנגנים (Dynamic EQ או Multiband עם Sidechain). הכלי המוביל יבלוט בלי להעלות אותו.";
        addTip (f, "בדיקת מונו: כלים שנעלמים במונו הם בדרך כלל גם אלה שמוסתרים במיקס.");
    }
    addTip (f, "ההערכה מבוססת על מיקס סטריאו בלבד. כדי לדעת בדיוק מי מסתיר את מי, השוו שני ערוצים עם Spectrum של EQ Eight במצב Solo.");

    if (f.status == Status::Ok)
        addSentence (f.explanation, "לא נמצאו סימנים חזקים למיסוך: יש הפרדה סבירה בתדרים ובסטריאו.");
    return f;
}

// ============================================================================
//  8. Phase
// ============================================================================
Finding checkPhase (const Metrics& m)
{
    Finding f;
    f.id = "phase";
    f.title = "פאזה וקורלציה";
    f.value = "Correlation " + fmt (m.correlation, 2) + "  |  Low <150Hz " + fmt (m.lowCorrelation, 2)
            + "  |  Negative " + fmt (m.negCorrTimePct) + "% of time";

    if (m.correlation < 0.0)
    {
        f.status = Status::Problem;
        addSentence (f.explanation, "הקורלציה הכללית שלילית (" + he (m.correlation, 2) + "). כנראה שאחד הערוצים (L או R) הפוך בפולריות, או שיש אפקט סטריאו קיצוני.");
        f.criticalTip = "בדקו אם יש Utility עם Phase invert על ערוץ אחד, או Widener קיצוני. הפעילו Mono על ה-Master ושמעו מה נעלם.";
    }
    else if (m.correlation < 0.2)
    {
        f.status = Status::Warning;
        addSentence (f.explanation, "הקורלציה נמוכה (" + fmt (m.correlation, 2) + "). המיקס רחב מאוד ועלול להתפרק במונו.");
    }

    if (m.lowCorrelation < 0.1)
    {
        f.status = worst (f.status, Status::Problem);
        addSentence (f.explanation, "בתדרים הנמוכים (מתחת ל-150Hz) הערוצים לא בפאזה. זה הורג את הלואו-אנד במועדון ובמונו.");
        setCritical (f, "Utility על ה-Master (או על קבוצת הבאס) עם Bass Mono מתחת ל-120Hz.");
    }
    else if (m.lowCorrelation < 0.6)
    {
        f.status = worst (f.status, Status::Warning);
        addSentence (f.explanation, "בתדרים הנמוכים יש יותר מדי סטריאו או חוסר התאמה בפאזה (קורלציה " + fmt (m.lowCorrelation, 2) + ").");
    }
    if (m.lowCorrelation < 0.6)
    {
        addTip (f, "בדקו Chorus, Unison, Detune ו-Stereo spread על סינת הבאס ועל שכבות הסאב.");
        addTip (f, "שכבות קיק או באס: עשו זום על ההתחלה של שתי השכבות. אם הגלים יוצאים בכיוונים הפוכים, הפכו פאזה לשכבה אחת או הזיזו אותה כמה מילישניות.");
    }

    if (m.negCorrTimePct > 10.0)
    {
        f.status = worst (f.status, Status::Warning);
        addSentence (f.explanation, "ב-" + fmt (m.negCorrTimePct) + "% מהזמן הקורלציה שלילית. יש קטעים עם בעיית פאזה (לרוב Widener, רוורב רחב מאוד או Haas).");
        addTip (f, "עקבו אחרי מד הקורלציה בזמן נגינה ומצאו איפה הוא יורד מתחת לאפס. בדרך כלל זה כלי אחד עם אפקט סטריאו.");
    }

    if (f.status == Status::Ok && m.correlation > 0.95 && m.midSideToMidDb < -25.0)
    {
        f.status = Status::Info;
        f.explanation = "המיקס כמעט מונו. זה תקין לגמרי מבחינת פאזה, אבל אפשר לשקול יותר רוחב בכלים משניים (פאנינג, רוורב סטריאו, דאבל).";
    }
    else if (f.status == Status::Ok)
        f.explanation = "הקורלציה חיובית ויציבה (" + fmt (m.correlation, 2) + "), והלואו-אנד בפאזה. מצוין.";
    return f;
}

// ============================================================================
//  9. Mono compatibility
// ============================================================================
Finding checkMono (const Metrics& m)
{
    Finding f;
    f.id = "mono";
    f.title = "תאימות למונו";
    f.value = "Mono loss " + fmt (m.monoLossDb) + " dB  |  Low " + fmt (m.lowMonoLossDb) + "  |  Mid "
            + fmt (m.midMonoLossDb) + "  |  High " + fmt (m.highMonoLossDb);

    f.explanation = "כשמסכמים למונו (טלפונים, רמקולי בלוטות', הרבה מערכות במועדונים) המיקס מאבד " + fmt (-m.monoLossDb) + " dB בממוצע.";

    if (m.monoLossDb < -3.0)       f.status = Status::Problem;
    else if (m.monoLossDb < -1.5)  f.status = Status::Warning;

    if (m.lowMonoLossDb < -3.0)
    {
        f.status = worst (f.status, Status::Problem);
        addSentence (f.explanation, "הלואו-אנד מאבד " + fmt (-m.lowMonoLossDb) + " dB במונו. הבאס והקיק ייחלשו משמעותית במועדון.");
        f.criticalTip = "Bass Mono מתחת ל-120Hz (Utility באבלטון), ובדיקה שאין Chorus או Unison על הבאס.";
    }
    else if (m.lowMonoLossDb < -1.0)
    {
        f.status = worst (f.status, Status::Warning);
        addSentence (f.explanation, "הלואו-אנד מאבד " + fmt (-m.lowMonoLossDb) + " dB במונו.");
        addTip (f, "Bass Mono מתחת ל-100–120Hz.");
    }
    if (m.midMonoLossDb < -2.5)
    {
        f.status = worst (f.status, Status::Warning);
        addSentence (f.explanation, "גם האמצע (150Hz–4kHz) מאבד " + fmt (-m.midMonoLossDb) + " dB. שם יושבים הווקאל והלידים, וזה מה שנשמע בטלפון.");
        setCritical (f, "הפעילו Utility עם Width 0% על ה-Master והקשיבו: איזה כלי נעלם או נהיה חלול? הוא החשוד.");
    }
    if (m.highMonoLossDb < -3.0)
    {
        f.status = worst (f.status, Status::Info);
        addSentence (f.explanation, "ההיי מאבד " + fmt (-m.highMonoLossDb) + " dB במונו. זה פחות קריטי, אבל רוורבים ומצלתיים ייחלשו.");
    }

    if ((int) f.status >= (int) Status::Warning)
    {
        addTip (f, "Widener, Haas או Chorus רחב על כלים מרכזיים (ווקאל ראשי, ליד) הם החשודים המיידיים. העדיפו רוחב מפאנינג ודאבלים אמיתיים.");
        addTip (f, "בדקו בכל פעם שאתם מוסיפים אפקט סטריאו: Mono במאסטר, שמיעה מהירה, וחזרה.");
    }
    if (f.status == Status::Ok)
        f.explanation = "המיקס שומר על עצמו כמעט לגמרי במונו (איבוד של " + fmt (-m.monoLossDb) + " dB בלבד). הלואו-אנד יציב.";
    return f;
}

// ============================================================================
//  10. Stereo imbalance
// ============================================================================
Finding checkBalance (const Metrics& m)
{
    Finding f;
    f.id = "balance";
    f.title = "איזון סטריאו (שמאל/ימין)";
    f.value = "L/R " + fmtS (m.balanceDb) + " dB  |  Low " + fmtS (m.lowBalanceDb) + "  |  Mid " + fmtS (m.midBalanceDb)
            + "  |  High " + fmtS (m.highBalanceDb) + "  |  Low side " + fmt (m.lowSideToMidDb) + " dB";

    auto side = [] (double db) { return db > 0 ? std::string ("שמאל") : std::string ("ימין"); };

    const double ab = std::abs (m.balanceDb);
    if (ab > 3.0)
    {
        f.status = Status::Problem;
        addSentence (f.explanation, "המיקס נוטה בבירור ל" + side (m.balanceDb) + " (" + fmt (ab) + " dB). במאסטר זה ייתפס כמיקס עקום.");
        f.criticalTip = "בדקו את הפאנינג של הכלים הבולטים, ואת הפאן והבלאנס של ה-Groups ושל ה-Master עצמו.";
    }
    else if (ab > 1.5)
    {
        f.status = Status::Warning;
        addSentence (f.explanation, "נטייה קלה ל" + side (m.balanceDb) + " (" + fmt (ab) + " dB).");
    }

    struct B { const char* name; double db; };
    const B bands[] = { { "בנמוכים", m.lowBalanceDb }, { "באמצע", m.midBalanceDb }, { "בגבוהים", m.highBalanceDb } };
    for (auto& b : bands)
        if (std::abs (b.db) > 2.5)
        {
            f.status = worst (f.status, Status::Warning);
            addSentence (f.explanation, std::string (b.name) + " יש נטייה ל" + side (b.db) + " (" + fmt (std::abs (b.db)) + " dB).");
        }

    if (m.imbalanceTimePct > 25.0)
    {
        f.status = worst (f.status, Status::Warning);
        addSentence (f.explanation, "ב-" + fmt (m.imbalanceTimePct) + "% מהזמן יש הפרש של יותר מ-3 dB בין הצדדים, כנראה בקטעים ספציפיים.");
    }

    if (m.lowSideToMidDb > -6.0)
    {
        f.status = worst (f.status, Status::Problem);
        addSentence (f.explanation, "יש הרבה סטריאו מתחת ל-120Hz (Side ביחס ל-Mid: " + he (m.lowSideToMidDb) + " dB). בוויניל ובמועדונים זו בעיה של ממש.");
        setCritical (f, "Bass Mono מתחת ל-120Hz, ובדיקה אילו ערוצים מכניסים סטריאו לתחתית (Pads, Reverb Returns, Unison).");
    }
    else if (m.lowSideToMidDb > -12.0)
    {
        f.status = worst (f.status, Status::Warning);
        addSentence (f.explanation, "יש מעט סטריאו בתחתית (Side ביחס ל-Mid: " + he (m.lowSideToMidDb) + " dB). עדיף תחתית מונו.");
    }

    if ((int) f.status >= (int) Status::Warning)
    {
        addTip (f, "כלי אחד בולט בצד אחד (הי-האט, גיטרה, שייקר) יכול למשוך את כל המיקס. שימו מולו כלי באנרגיה דומה בצד השני.");
        addTip (f, "בדקו שאין Pan אוטומטי או אוטומציית פאן ששכחתם בקטעים מסוימים.");
    }
    if (f.status == Status::Ok)
        f.explanation = "שמאל וימין מאוזנים (הפרש " + fmt (ab) + " dB), ואין סטריאו מיותר בלואו-אנד.";
    return f;
}

// ============================================================================
//  11. Resonances
// ============================================================================
Finding checkResonances (const Metrics& m)
{
    Finding f;
    f.id = "resonance";
    f.title = "רזוננסים בולטים";

    std::vector<Resonance> rel;
    for (auto& r : m.resonances) if (r.excessDb >= 6.0) rel.push_back (r);

    std::string v;
    for (size_t i = 0; i < rel.size() && i < 4; ++i)
    {
        if (! v.empty()) v += "  |  ";
        v += hz (rel[i].freqHz) + " (" + noteName (rel[i].freqHz) + ") +" + fmt (rel[i].excessDb) + "dB";
    }
    f.value = v.empty() ? "No narrow peaks above 6 dB" : v;

    if (rel.empty())
    {
        f.status = Status::Ok;
        f.explanation = "לא נמצאו תדרים צרים שבולטים יותר מ-6 dB מעל הסביבה שלהם. אין צלצולים קבועים.";
        return f;
    }

    const double maxEx = rel.front().excessDb;
    f.status = maxEx >= 10.0 ? Status::Problem : Status::Warning;

    std::string list;
    for (size_t i = 0; i < rel.size() && i < 4; ++i)
    {
        if (! list.empty()) list += ", ";
        list += hz (rel[i].freqHz) + " (" + noteName (rel[i].freqHz) + ")";
    }
    f.explanation = "זוהו תדרים צרים שבולטים מעל הסביבה שלהם לאורך כל הטראק: " + list
                  + ". תדרים כאלה נשמעים כצלצול, 'בום' או 'אף', והמאסטר מגביר אותם עוד יותר.";
    addSentence (f.explanation, "שימו לב: אם שם התו תואם לטוניקה של השיר, ייתכן שזה פשוט התו המרכזי של הבאס או המלודיה, ואז זה לא בהכרח בעיה.");

    f.criticalTip = "EQ Eight במצב Spectrum על כל כלי חשוד, ב-Solo. כשמוצאים את מקור התדר: חיתוך צר (Q גבוה, 2–5 dB) רק בכלי הזה, לא על ה-Master.";
    addTip (f, "Dynamic EQ או מעבד רזוננסים (בסגנון Soothe) חותכים רק כשהרזוננס מתפרץ, ושומרים על הכלי כשהוא שקט.");
    addTip (f, "מדריך מהיר למקורות: 80–200Hz חדר או באס בום, 200–500Hz קופסתיות של תופים וגיטרות, 800Hz–1.5kHz 'אף', 2–4kHz סינתים וווקאל צורמים.");
    addTip (f, "טכניקת Sweep: בוסט צר של 8–10 dB, העבירו לאט על התדרים, ובמקום שצורם במיוחד הפכו את הבוסט לחיתוך.");
    return f;
}

// ============================================================================
//  12. Spectral density & tonal balance
// ============================================================================
Finding checkDensity (const Metrics& m)
{
    Finding f;
    f.id = "density";
    f.title = "צפיפות ספקטרלית ואיזון טונאלי";
    f.value = "Tilt " + fmtS (m.tiltDbPerOct) + " dB/oct  |  Flatness " + fmt (m.midFlatness, 2) + "  |  Centroid "
            + hz (m.centroidHz) + "  |  Holes " + std::to_string (m.holeFreqs.size());

    if (m.holeFreqs.size() >= 2)
    {
        f.status = Status::Warning;
        std::string list;
        for (size_t i = 0; i < m.holeFreqs.size() && i < 5; ++i)
        {
            if (! list.empty()) list += ", ";
            list += hz (m.holeFreqs[i]);
        }
        addSentence (f.explanation, "יש 'חורים' בספקטרום, אזורים עם מעט מאוד אנרגיה ביחס לשאר: " + list + ".");
        addTip (f, "בדקו אם כלי נחתך באגרסיביות ב-EQ, או שחסר כלי שממלא את התחום (למשל Pad או קורדים באמצע).");
    }

    if (m.midFlatness > 0.45)
    {
        f.status = worst (f.status, Status::Warning);
        addSentence (f.explanation, "האמצע צפוף מאוד ודומה לרעש. הרבה שכבות מדוחסות, ולכן קשה לשמוע הפרדה בין הכלים.");
        addTip (f, "צמצמו שכבות או הקצו לכל שכבה תחום תדרים. פחות שכבות נשמע לרוב גדול יותר.");
    }
    else if (m.midFlatness < 0.03)
    {
        f.status = worst (f.status, Status::Info);
        addSentence (f.explanation, "הספקטרום באמצע דליל מאוד (מעט כלים או צלילים טונאליים מאוד). זה בסדר לסגנונות מינימליסטיים.");
    }

    if (m.tiltDbPerOct > -0.5)
    {
        f.status = worst (f.status, Status::Warning);
        addSentence (f.explanation, "האיזון הטונאלי בהיר מאוד (שיפוע " + he (m.tiltDbPerOct) + " dB לאוקטבה). המיקס עלול להישמע דק וחד אחרי המאסטר.");
        addTip (f, "השוו לרפרנס. לרוב צריך להוריד קצת היי-מיד או להוסיף גוף ב-100–300Hz.");
    }
    else if (m.tiltDbPerOct < -6.0)
    {
        f.status = worst (f.status, Status::Warning);
        addSentence (f.explanation, "האיזון הטונאלי כהה מאוד (שיפוע " + he (m.tiltDbPerOct) + " dB לאוקטבה). המיקס עלול להישמע עמום.");
        addTip (f, "לפני שמגבירים היי, נקו בוץ ב-200–500Hz. בהירות מגיעה לא פעם מחיסור ולא מתוספת.");
    }

    if (f.status == Status::Ok)
        f.explanation = "הספקטרום מלא ורציף, בלי חורים בולטים, עם שיפוע טונאלי טבעי (" + he (m.tiltDbPerOct) + " dB לאוקטבה).";
    addTip (f, "הגרף 'איזון טונאלי' למעלה מראה כל תחום ביחס לקו הטבעי של המיקס שלכם. עמודה גבוהה היא הצטברות, עמודה נמוכה היא חור.");
    return f;
}

// ============================================================================
//  13. Kick / Bass
// ============================================================================
Finding checkKickBass (const Metrics& m, bool& clash)
{
    Finding f;
    f.id = "kickbass";
    f.title = "יחסי קיק ובאס";
    clash = false;

    if (! m.kickDetected)
    {
        f.status = Status::Info;
        f.value = "Kick hits detected: " + std::to_string (m.kickCount);
        f.explanation = "לא זוהה קיק עם מכות ברורות לאורך הטראק. אם יש קיק, ייתכן שהוא רך מאוד או קבור בתוך הבאס, ואז כדאי לבדוק שהוא בולט מספיק.";
        addTip (f, "לניתוח קיק ובאס צריך לפחות 20–30 שניות של קטע שבו שניהם מנגנים יחד (למשל דרופ או פזמון).");
        return f;
    }

    // signed distance: negative = bass below the kick's main energy (inside the kick's pitch-drop tail)
    const double signedSemis = (m.bassFreqHz > 0) ? 12.0 * std::log2 (m.bassFreqHz / m.kickFreqHz) : 99.0;
    const double semis = std::abs (signedSemis);
    f.value = "Kick " + hz (m.kickFreqHz) + " (" + noteName (m.kickFreqHz) + ")  |  Bass " + hz (m.bassFreqHz)
            + " (" + noteName (m.bassFreqHz) + ")  |  Punch " + fmtS (m.kickPunchDb) + " dB  |  Hits " + std::to_string (m.kickCount);

    const bool sameSpot = signedSemis > -8.0 && signedSemis < 3.0;

    if (m.kickPunchDb < 2.0)
    {
        f.status = Status::Problem;
        addSentence (f.explanation, "ברגעי המכה הקיק מעלה את הלואו-אנד רק ב-" + fmt (m.kickPunchDb) + " dB מעל הבאס. הקיק כמעט קבור, והמאסטר יתקשה להוציא ממנו פאנץ'.");
        f.criticalTip = "Sidechain מהקיק לבאס: Compressor על הבאס עם Sidechain מהקיק, Attack 0.1–1ms, Release 50–150ms, והורדה של 4–8 dB. או ducking ב-Auto Pan / LFO Tool.";
        addTip (f, "Transient shaper או שכבת קליק (2–5kHz) לקיק, כדי שיישמע גם בלי להגדיל את הסאב.");
        if (sameSpot) clash = true;
    }
    else if (sameSpot && m.kickPunchDb < 6.0)
    {
        f.status = Status::Problem;
        clash = true;
    }
    else if (sameSpot)
    {
        f.status = Status::Warning;
        clash = true;
    }
    else if (m.kickPunchDb > 14.0)
    {
        f.status = Status::Info;
        addSentence (f.explanation, "הקיק בולט מאוד (" + fmt (m.kickPunchDb) + " dB מעל מה שיש בין המכות). ייתכן שהבאס חלש, או שה-Sidechain עמוק מדי ויוצר 'חורים' בלואו-אנד.");
        addTip (f, "נסו להקטין את עומק ה-Sidechain או לקצר את ה-Release, כדי שהבאס יחזור מהר יותר אחרי כל מכה.");
    }

    if (clash)
    {
        addSentence (f.explanation, "התדר היסודי של הקיק (" + hz (m.kickFreqHz) + ") והתדר הדומיננטי של הבאס (" + hz (m.bassFreqHz) + ") קרובים (" + fmt (semis) + " חצאי טונים). הם נאבקים על אותו מקום, והלואו-אנד מאבד הגדרה.");
        setCritical (f, "החליטו מי מחזיק את הסאב. קיק ארוך וסאבי: הבאס יושב גבוה יותר (80–120Hz). באס סאבי: קיק קצר וטייט עם דגש על 60–100Hz.");
        addTip (f, "כוונו את הקיק לטונאליות של השיר (Transpose ב-Simpler או ב-Drum Rack), כך שיהיה בהרמוניה עם הבאס ולא יתנגש בו.");
        addTip (f, "EQ משלים: חיתוך צר ועדין בבאס בתדר היסודי של הקיק, ולהפך.");
        addTip (f, "Sidechain מהקיק לבאס, אפילו עדין (2–3 dB), מנקה הרבה מההתנגשות.");
    }

    if (f.status == Status::Ok)
        f.explanation = "הקיק והבאס מופרדים היטב: הקיק מעלה את הלואו-אנד ב-" + fmt (m.kickPunchDb) + " dB ברגעי המכה, ויושב על תדר שונה מזה של הבאס (" + fmt (semis) + " חצאי טונים).";
    return f;
}

// ============================================================================
//  Checklist
// ============================================================================
std::vector<ChecklistItem> buildChecklist (const Metrics& m, const Report& r)
{
    auto statusOf = [&] (const std::string& id)
    {
        for (auto& f : r.findings) if (f.id == id) return f.status;
        return Status::Info;
    };
    auto good = [] (bool ok) { return ok ? Status::Ok : Status::Problem; };

    std::vector<ChecklistItem> c;
    c.push_back ({ "פיקים בין מינוס 6 למינוס 3 dBFS (True Peak מתחת למינוס 1)", good (m.truePeakDb <= -1.0 && m.truePeakDb >= -12.0) });
    c.push_back ({ "אין לימיטר או קליפר על ה-Master (עוצמה ממוצעת מתחת למינוס 12 LUFS)", good (m.integratedLufs < -12.0) });
    c.push_back ({ "אין קליפינג ואין דגימות מעל 0dBFS", good (m.clipEvents == 0 && m.oversFloat == 0) });
    c.push_back ({ "הלואו-אנד מונו ובפאזה (מתחת ל-120Hz)", good (m.lowCorrelation >= 0.6 && m.lowSideToMidDb <= -12.0) });
    c.push_back ({ "המיקס עובד במונו", good ((int) statusOf ("mono") < (int) Status::Warning) });
    c.push_back ({ "קיק ובאס לא מתנגשים", statusOf ("kickbass") == Status::Problem ? Status::Problem : Status::Ok });
    c.push_back ({ "ייצוא: WAV או AIFF, 24 ביט או 32 float, Sample Rate של הפרויקט", Status::Info });
    c.push_back ({ "שקט של 1–2 שניות בהתחלה ובסוף, בלי Fade out על הזנב", Status::Info });
    c.push_back ({ "ייצוא מתחילת הטראק ועד סוף הזנב של הרוורב והדיליי", Status::Info });
    c.push_back ({ "שלחו למהנדס טראק רפרנס ורשימת הערות (BPM, סולם, מה חשוב לכם)", Status::Info });
    c.push_back ({ "אם יש על ה-Master עיבוד שהוא חלק מהסאונד, שלחו גם גרסה בלעדיו", Status::Info });
    return c;
}

} // namespace

// ============================================================================
int getNumProfiles() { return kNumProfiles; }

std::string getProfileName (int i)
{
    return kProfiles[std::max (0, std::min (kNumProfiles - 1, i))].name;
}

const char* statusName (Status s)
{
    switch (s)
    {
        case Status::Ok:      return "תקין";
        case Status::Info:    return "לידיעה";
        case Status::Warning: return "לתשומת לב";
        case Status::Problem: return "לתקן";
    }
    return "";
}

Report buildReport (const Metrics& m, int profileIndex)
{
    Report r;
    if (! m.valid)
    {
        r.headline = "אין מספיק אודיו לניתוח";
        r.summary  = "צריך לפחות כמה שניות של אודיו שאינו שקט. נגנו את הטראק (עדיף מההתחלה ועד הסוף) ונסו שוב.";
        return r;
    }
    r.valid = true;
    const Profile& p = kProfiles[std::max (0, std::min (kNumProfiles - 1, profileIndex))];

    bool clash = false;
    Finding kb = checkKickBass (m, clash);

    r.findings.push_back (checkHeadroom (m));
    r.findings.push_back (checkClipping (m));
    r.findings.push_back (kb);
    r.findings.push_back (checkLowEnd (m, p));
    r.findings.push_back (checkPhase (m));
    r.findings.push_back (checkMono (m));
    r.findings.push_back (checkBalance (m));
    r.findings.push_back (checkHarshness (m));
    r.findings.push_back (checkResonances (m));
    r.findings.push_back (checkMasking (m, clash));
    r.findings.push_back (checkDensity (m));
    r.findings.push_back (checkDynamics (m, p));
    r.findings.push_back (checkCrest (m, p));

    std::stable_sort (r.findings.begin(), r.findings.end(),
                      [] (const Finding& a, const Finding& b) { return (int) a.status > (int) b.status; });

    int problems = 0, warnings = 0, score = 100;
    for (auto& f : r.findings)
    {
        if (f.status == Status::Problem) { ++problems; score -= 12; }
        else if (f.status == Status::Warning) { ++warnings; score -= 5; }
    }
    // critical technical issues weigh more
    for (auto& f : r.findings)
        if ((f.id == "clipping" || f.id == "headroom") && f.status == Status::Problem) score -= 8;
    r.score = std::max (0, std::min (100, score));

    if (r.score >= 85)      r.headline = "מוכן למאסטר";
    else if (r.score >= 65) r.headline = "כמעט מוכן. כדאי לטפל בכמה נקודות";
    else                    r.headline = "מומלץ לתקן לפני שליחה למאסטר";

    if (problems == 0 && warnings == 0)
        r.summary = "לא נמצאו בעיות משמעותיות. המיקס נקי טכנית ומאוזן. עברו על רשימת המסירה למטה ושלחו.";
    else
        r.summary = "נמצאו " + std::to_string (problems) + " בעיות לתיקון ו-" + std::to_string (warnings)
                  + " נקודות לתשומת לב. התחילו מהטיפ הקריטי של כל סעיף אדום: הוא נותן את השיפור הגדול ביותר במינימום זמן.";

    for (auto& f : r.findings)
        if (f.status == Status::Problem && r.priorities.size() < 3)
            r.priorities.push_back (f.title + ": " + (f.criticalTip.empty() ? f.explanation : f.criticalTip));
    for (auto& f : r.findings)
        if (f.status == Status::Warning && r.priorities.size() < 3)
            r.priorities.push_back (f.title + ": " + (f.criticalTip.empty() ? (f.tips.empty() ? f.explanation : f.tips.front()) : f.criticalTip));

    r.checklist = buildChecklist (m, r);
    return r;
}

std::string reportToText (const Report& r, const Metrics& m, int profileIndex)
{
    std::string s;
    s += "Production Analyzer | דוח מיקס לפני מאסטר\n";
    {
        std::time_t t = std::time (nullptr);
        char buf[64];
        std::strftime (buf, sizeof (buf), "%Y-%m-%d %H:%M", std::localtime (&t));
        s += std::string ("תאריך: ") + buf + "\n";
    }
    s += "סגנון: " + getProfileName (profileIndex) + "\n";
    s += "משך שנותח: " + fmt (m.durationSec, 0) + " שניות  |  Sample rate: " + fmt (m.sampleRate, 0) + " Hz\n";
    s += "ציון: " + std::to_string (r.score) + "/100  |  " + r.headline + "\n";
    s += r.summary + "\n\n";

    if (! r.priorities.empty())
    {
        s += "סדר עדיפויות:\n";
        for (size_t i = 0; i < r.priorities.size(); ++i)
            s += "  " + std::to_string (i + 1) + ". " + r.priorities[i] + "\n";
        s += "\n";
    }

    for (auto& f : r.findings)
    {
        s += "================================================\n";
        s += "[" + std::string (statusName (f.status)) + "] " + f.title + "\n";
        s += f.value + "\n";
        s += f.explanation + "\n";
        if (! f.criticalTip.empty()) s += "טיפ קריטי: " + f.criticalTip + "\n";
        for (auto& t : f.tips) s += "  - " + t + "\n";
        s += "\n";
    }

    s += "================================================\nרשימת מסירה למאסטר:\n";
    for (auto& c : r.checklist)
    {
        const char* mark = c.status == Status::Ok ? "[V]" : c.status == Status::Problem ? "[X]" : "[ ]";
        s += std::string ("  ") + mark + " " + c.text + "\n";
    }
    s += "\nהערה: הספים הם הערכה מקצועית כללית, לא חוק. השוו תמיד לטראק רפרנס מאותו ז'אנר.\n";
    return s;
}

} // namespace pa
