#include "PluginEditor.h"

using namespace theme;

ProductionAnalyzerEditor::ProductionAnalyzerEditor (ProductionAnalyzerProcessor& p)
    : AudioProcessorEditor (&p), proc (p), service (p.getService())
{
    setLookAndFeel (&lnf);

    // ---- transport ---------------------------------------------------------
    captureBtn.onClick = [this] { onCaptureClicked(); };
    fileBtn.setButtonText (HE ("טען קובץ אודיו"));
    fileBtn.onClick = [this] { onLoadFileClicked(); };
    exportBtn.setButtonText (HE ("ייצא דוח"));
    exportBtn.onClick = [this] { onExportClicked(); };
    copyBtn.setButtonText (HE ("העתק דוח"));
    copyBtn.onClick = [this] { onCopyClicked(); };

    playingOnlyToggle.setButtonText (HE ("הקלט רק בזמן נגינה"));
    playingOnlyToggle.setToggleState (proc.onlyWhilePlaying.load(), juce::dontSendNotification);
    playingOnlyToggle.setColour (juce::ToggleButton::textColourId, col::textMuted);
    playingOnlyToggle.setColour (juce::ToggleButton::tickColourId, col::accent);
    playingOnlyToggle.setColour (juce::ToggleButton::tickDisabledColourId, col::line);
    playingOnlyToggle.onClick = [this] { proc.onlyWhilePlaying.store (playingOnlyToggle.getToggleState()); };

    for (auto* c : { (juce::Component*) &captureBtn, (juce::Component*) &fileBtn, (juce::Component*) &exportBtn,
                     (juce::Component*) &copyBtn, (juce::Component*) &playingOnlyToggle })
        addAndMakeVisible (c);

    // ---- profile -------------------------------------------------------------
    for (int i = 0; i < pa::getNumProfiles(); ++i)
        profileBox.addItem (str (pa::getProfileName (i)), i + 1);
    profileBox.setSelectedId (service.getProfile() + 1, juce::dontSendNotification);
    profileBox.setJustificationType (juce::Justification::centredRight);
    profileBox.onChange = [this] { service.setProfile (profileBox.getSelectedId() - 1); };
    addAndMakeVisible (profileBox);

    // ---- panels --------------------------------------------------------------
    addAndMakeVisible (meters);
    addAndMakeVisible (dial);
    addAndMakeVisible (chart);

    // ---- tabs ----------------------------------------------------------------
    auto setupTab = [this] (juce::TextButton& b, ui::ContentView::Mode m)
    {
        b.setClickingTogglesState (true);
        b.setRadioGroupId (4201);
        b.onClick = [this, m] { selectTab (m); };
        addAndMakeVisible (b);
    };
    setupTab (tabFindings,   ui::ContentView::Mode::Findings);
    setupTab (tabPriorities, ui::ContentView::Mode::Priorities);
    setupTab (tabChecklist,  ui::ContentView::Mode::Checklist);
    tabFindings.setToggleState (true, juce::dontSendNotification);
    updateTabLabels();

    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (8);
    addAndMakeVisible (viewport);

    setResizable (true, true);
    setResizeLimits (860, 740, 1800, 1600);
    setSize (1000, 860);

    refreshReport();
    timerCallback();
    startTimerHz (20);
}

ProductionAnalyzerEditor::~ProductionAnalyzerEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

// ============================================================================
void ProductionAnalyzerEditor::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);

    // header (RTL: product name on the right)
    auto h = headerArea.toFloat();
    auto titleArea = h.removeFromRight (h.getWidth() * 0.55f);
    g.setColour (col::text);
    g.setFont (font (size::title + 4.0f, true));
    g.drawText ("Production Analyzer", titleArea.removeFromTop (30.0f), juce::Justification::centredRight);
    g.setColour (col::textMuted);
    g.setFont (font (size::body));
    g.drawText (HE ("בדיקת מיקס לפני שליחה למאסטר"), titleArea, juce::Justification::topRight);

    // status line + progress
    auto s = statusArea.toFloat();
    if (service.getState() == AnalysisService::State::AnalyzingFile)
    {
        auto bar = s.removeFromLeft (180.0f).withSizeKeepingCentre (180.0f, 6.0f);
        g.setColour (col::panel);
        g.fillRoundedRectangle (bar, 3.0f);
        g.setColour (col::accent);
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * progress), 3.0f);
        s.removeFromLeft (12.0f);
    }
    auto st = rtl (statusText, font (size::small), col::textMuted, s.getWidth(), juce::Justification::centredRight);
    st.draw (g, s);
}

void ProductionAnalyzerEditor::resized()
{
    auto area = getLocalBounds().reduced (pad);

    headerArea = area.removeFromTop (54);
    {
        auto left = headerArea.withWidth (juce::jmin (330, headerArea.getWidth() / 2));
        profileBox.setBounds (left.withSizeKeepingCentre (left.getWidth(), 34).withX (left.getX()));
    }
    area.removeFromTop (6);

    auto transport = area.removeFromTop (40);
    captureBtn.setBounds (transport.removeFromRight (210));
    transport.removeFromRight (gap);
    fileBtn.setBounds (transport.removeFromRight (160));
    transport.removeFromRight (gap);
    playingOnlyToggle.setBounds (transport.removeFromRight (190));
    exportBtn.setBounds (transport.removeFromLeft (120));
    transport.removeFromLeft (gap);
    copyBtn.setBounds (transport.removeFromLeft (120));

    area.removeFromTop (6);
    statusArea = area.removeFromTop (22);
    area.removeFromTop (6);

    meters.setBounds (area.removeFromTop (50));
    area.removeFromTop (gap);

    auto top = area.removeFromTop (262);
    dial.setBounds (top.removeFromRight (320));
    top.removeFromRight (gap);
    chart.setBounds (top);
    area.removeFromTop (gap + 4);

    auto tabs = area.removeFromTop (36);
    const int tw = juce::jmin (200, tabs.getWidth() / 3);
    tabFindings.setBounds (tabs.removeFromRight (tw));
    tabs.removeFromRight (6);
    tabPriorities.setBounds (tabs.removeFromRight (tw));
    tabs.removeFromRight (6);
    tabChecklist.setBounds (tabs.removeFromRight (tw));
    area.removeFromTop (gap);

    viewport.setBounds (area);
    content.setLayoutWidth (area.getWidth() - viewport.getScrollBarThickness() - 4);
}

// ============================================================================
void ProductionAnalyzerEditor::timerCallback()
{
    const auto state = service.getState();
    const bool capturing = state == AnalysisService::State::Capturing;
    const bool fileBusy  = state == AnalysisService::State::AnalyzingFile;
    const bool waiting   = capturing && proc.onlyWhilePlaying.load() && ! proc.hostIsPlaying.load();

    captureBtn.setButtonText (capturing ? HE ("עצור והפק דוח") : fileBusy ? HE ("בטל ניתוח קובץ") : HE ("התחל ניתוח"));
    captureBtn.setColour (juce::TextButton::buttonColourId, capturing ? col::problem : fileBusy ? col::panelRaised : col::accent);
    captureBtn.setColour (juce::TextButton::textColourOffId, capturing || ! fileBusy ? col::bg : col::text);
    fileBtn.setEnabled (state == AnalysisService::State::Idle);
    profileBox.setEnabled (! fileBusy);
    exportBtn.setEnabled (report.valid && ! capturing);
    copyBtn.setEnabled (report.valid && ! capturing);

    meters.setValues (capturing || fileBusy, waiting, service.getCapturedSeconds(),
                      service.getLivePeakDb(), service.getLiveLufs(), service.getLiveCorrelation());

    juce::String s = service.getStatusMessage();
    if (juce::Time::getMillisecondCounter() < flashUntil) s = flashText;
    else if (waiting) s = HE ("ממתין לנגינה. לחצו Play באבלטון (ההקלטה רצה רק בזמן נגינה).");
    else if (report.valid && ! capturing && ! fileBusy && service.getSourceName().isNotEmpty())
        s << "  |  " << HE ("מקור: ") << service.getSourceName();
    const float prog = service.getFileProgress();
    if (s != statusText || std::abs (prog - progress) > 0.002f)
    {
        statusText = s;
        progress = prog;
        repaint (statusArea);
    }

    if (service.getReportVersion() != lastVersion) refreshReport();
}

void ProductionAnalyzerEditor::refreshReport()
{
    lastVersion = service.getReportVersion();
    service.getReport (report, metrics);
    dial.setReport (report);
    chart.setData (metrics);
    content.setReport (report);
    updateTabLabels();
}

void ProductionAnalyzerEditor::updateTabLabels()
{
    int problems = 0, warnings = 0, open = 0;
    for (auto& f : report.findings)
    {
        if (f.status == pa::Status::Problem) ++problems;
        if (f.status == pa::Status::Warning) ++warnings;
    }
    for (auto& c : report.checklist) if (c.status == pa::Status::Problem) ++open;

    juce::String f = HE ("ממצאים");
    if (report.valid) f << " (" << (problems + warnings) << ")";
    tabFindings.setButtonText (f);
    tabPriorities.setButtonText (HE ("סדר עדיפויות"));
    juce::String c = HE ("רשימת מסירה");
    if (report.valid && open > 0) c << " (" << open << ")";
    tabChecklist.setButtonText (c);
}

void ProductionAnalyzerEditor::selectTab (ui::ContentView::Mode m)
{
    content.setMode (m);
    viewport.setViewPosition (0, 0);
}

// ============================================================================
void ProductionAnalyzerEditor::onCaptureClicked()
{
    switch (service.getState())
    {
        case AnalysisService::State::Idle:          service.startCapture(); break;
        case AnalysisService::State::Capturing:     service.stopCapture(); break;
        case AnalysisService::State::AnalyzingFile: service.cancelFileAnalysis(); break;
    }
}

void ProductionAnalyzerEditor::onLoadFileClicked()
{
    chooser = std::make_unique<juce::FileChooser> (HE ("בחרו את קובץ המיקס"), juce::File(),
                                                   "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (f.existsAsFile()) service.analyzeFile (f);
                          });
}

juce::String ProductionAnalyzerEditor::currentReportText() const
{
    return str (pa::reportToText (report, metrics, service.getProfile()));
}

void ProductionAnalyzerEditor::onExportClicked()
{
    if (! report.valid) return;
    const auto text = currentReportText();
    auto def = juce::File::getSpecialLocation (juce::File::userDesktopDirectory).getChildFile ("Mix Report.txt");
    chooser = std::make_unique<juce::FileChooser> (HE ("שמירת הדוח"), def, "*.txt");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this, text] (const juce::FileChooser& fc)
                          {
                              auto f = fc.getResult();
                              if (f == juce::File()) return;
                              if (! f.hasFileExtension ("txt")) f = f.withFileExtension ("txt");
                              if (f.replaceWithText (text, false, false, "\n"))
                                  flash (HE ("הדוח נשמר: ") + f.getFullPathName());
                              else
                                  flash (HE ("לא ניתן לשמור בתיקייה הזו. נסו מיקום אחר."));
                          });
}

void ProductionAnalyzerEditor::onCopyClicked()
{
    if (! report.valid) return;
    juce::SystemClipboard::copyTextToClipboard (currentReportText());
    flash (HE ("הדוח הועתק. אפשר להדביק אותו במייל למהנדס המאסטר."));
}

void ProductionAnalyzerEditor::flash (const juce::String& msg)
{
    flashText  = msg;
    flashUntil = juce::Time::getMillisecondCounter() + 4000;
}
