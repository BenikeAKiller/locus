#include "MainComponent.h"

using namespace locus::ui;

namespace
{
    constexpr int gap = Metrics::grid;
    constexpr int refreshHz = 30;
}

const juce::StringArray MainComponent::tabNames { "Source", "Axis", "Omni", "Room", "Speakers" };

MainComponent::MainComponent (AppController& c, std::function<void()> settingsChanged)
    : controller (c), onSettingsChanged (std::move (settingsChanged)),
      topBar (c), viewRow (c), canvas (c), speakerEditor (c), sources (c), output (c)
{
    setLookAndFeel (&lookAndFeel);
    setOpaque (true);

    pages.push_back (std::make_unique<SourceTab> (c));
    pages.push_back (std::make_unique<EqTab> (c, locus::SourceParam::axisLow, Colours::accentSource));
    pages.push_back (std::make_unique<EqTab> (c, locus::SourceParam::omniLow, Colours::accentRoom));
    pages.push_back (std::make_unique<RoomTab> (c));
    pages.push_back (std::make_unique<SpeakersTab> (c));

    for (auto* comp : std::initializer_list<juce::Component*> { &topBar, &viewRow, &canvas, &sources, &tabs, &output })
        addAndMakeVisible (comp);
    for (auto& p : pages)
        addChildComponent (*p);
    addChildComponent (speakerEditor);

    tabs.onSelect = [this] (int i) { showTab (i); };
    topBar.settings.onClick = [this] { openSettings(); };
    topBar.load.onClick = [this] { loadSession(); };
    topBar.save.onClick = [this] { saveSession(); };

    showTab (0);
    setSize (defaultWidth, defaultHeight);
    refreshAll();
    startTimerHz (refreshHz);
}

MainComponent::~MainComponent()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void MainComponent::showTab (int index)
{
    tabs.setSelected (index);
    for (int i = 0; i < (int) pages.size(); ++i)
        pages[(size_t) i]->setVisible (i == index);
}

void MainComponent::refreshAll()
{
    // The Speaker Editor takes the canvas's place while it is open.
    if (controller.isEditingSpeakers() != showingEditor)
    {
        showingEditor = controller.isEditingSpeakers();
        if (showingEditor)
            speakerEditor.open();

        speakerEditor.setVisible (showingEditor);
        canvas.setVisible (! showingEditor);
        viewRow.setVisible (! showingEditor);
        resized();
    }

    topBar.refresh();
    viewRow.refresh();
    sources.refresh();
    output.refresh();

    for (auto& p : pages)
        if (p->isVisible())
            p->refresh();

    canvas.repaint();
}

void MainComponent::openSettings()
{
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (new SettingsPanel (controller, [this] { if (onSettingsChanged) onSettingsChanged(); }));
    options.dialogTitle = "Settings";
    options.dialogBackgroundColour = Colours::bg;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.launchAsync();
}

void MainComponent::saveSession()
{
    chooser = std::make_unique<juce::FileChooser> ("Save session", juce::File(), juce::String ("*") + locus::sessionFileExtension);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        if (file == juce::File())
            return;

        juce::String error;
        if (! locus::writeJsonFile (file.withFileExtension (locus::sessionFileExtension),
                                    locus::sessionToJson (controller.captureSession()), error))
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Save session", error);
    });
}

void MainComponent::loadSession()
{
    chooser = std::make_unique<juce::FileChooser> ("Load session", juce::File(), juce::String ("*") + locus::sessionFileExtension);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        if (file == juce::File())
            return;

        juce::String error;
        if (auto json = locus::readJsonFile (file, error))
            if (auto session = locus::sessionFromJson (*json, error))
            {
                controller.applySession (*session);
                refreshAll();
                return;
            }

        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Load session", error);
    });
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (Colours::bg);

    g.setColour (Colours::panel);
    g.fillRect (topBar.getBounds());
    g.fillRect (output.getBounds());
    g.fillRoundedRectangle (panelArea.toFloat(), (float) Metrics::panelCorner);

    g.setColour (Colours::stroke);
    g.drawHorizontalLine (topBar.getBottom() - 1, 0.0f, (float) getWidth());
    g.drawHorizontalLine (output.getY(), 0.0f, (float) getWidth());
}

void MainComponent::resized()
{
    auto area = getLocalBounds();
    topBar.setBounds (area.removeFromTop (Metrics::topBarHeight));
    output.setBounds (area.removeFromBottom (Metrics::outputStripHeight));

    // Right panel: a fixed 400 px column holding a card with 8 px margins.
    panelArea = area.removeFromRight (Metrics::panelWidth).withTrimmedTop (gap).withTrimmedBottom (gap).withTrimmedRight (gap);
    auto panel = panelArea.reduced (gap);

    int tallestPage = 0;
    for (auto& p : pages)
        tallestPage = juce::jmax (tallestPage, p->getPreferredHeight());

    // The source list takes whatever height the tabs leave: at least 2 rows, at most 16.
    const auto forList = panel.getHeight() - (gap + Metrics::tabBarHeight + gap + tallestPage);
    const auto rows = juce::jlimit (2, locus::maxSources, (forList - Metrics::rowHeight) / Metrics::rowHeight);
    sources.setBounds (panel.removeFromTop (Metrics::rowHeight * (rows + 1)));
    panel.removeFromTop (gap);
    tabs.setBounds (panel.removeFromTop (Metrics::tabBarHeight));
    panel.removeFromTop (gap);

    for (auto& p : pages)
        p->setBounds (panel.withHeight (juce::jmin (panel.getHeight(), p->getPreferredHeight())));

    // Canvas, or the Speaker Editor in its place
    auto left = area.reduced (gap);
    if (showingEditor)
    {
        speakerEditor.setBounds (left);
    }
    else
    {
        viewRow.setBounds (left.removeFromTop (Metrics::viewRowHeight));
        left.removeFromTop (gap);
        canvas.setBounds (left);
    }
}
