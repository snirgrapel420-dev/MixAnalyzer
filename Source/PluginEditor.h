#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "Components.h"

class ProductionAnalyzerEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit ProductionAnalyzerEditor (ProductionAnalyzerProcessor&);
    ~ProductionAnalyzerEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void onCaptureClicked();
    void onLoadFileClicked();
    void onExportClicked();
    void onCopyClicked();
    void selectTab (ui::ContentView::Mode m);
    void refreshReport();
    void updateTabLabels();
    juce::String currentReportText() const;

    ProductionAnalyzerProcessor& proc;
    AnalysisService& service;
    theme::LookAndFeel lnf;

    juce::TextButton captureBtn, fileBtn, exportBtn, copyBtn;
    juce::ToggleButton playingOnlyToggle;
    juce::ComboBox profileBox;

    ui::LiveMeters meters;
    ui::ScoreDial  dial;
    ui::ToneChart  chart;

    juce::TextButton tabFindings, tabPriorities, tabChecklist;
    juce::Viewport   viewport;
    ui::ContentView  content;

    std::unique_ptr<juce::FileChooser> chooser;

    pa::Report  report;
    pa::Metrics metrics;
    int lastVersion = -1;
    juce::String statusText, flashText;
    juce::uint32 flashUntil = 0;
    void flash (const juce::String& msg);
    float progress = 0.0f;
    juce::Rectangle<int> headerArea, statusArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProductionAnalyzerEditor)
};
