#pragma once

#include "../model/AppController.h"
#include <uikit/ToggleDot.h>
#include <juce_audio_utils/juce_audio_utils.h>

/** Top bar (spec §9.2): name, session Load/Save, audio device, CPU, Settings. */
class TopBar final : public juce::Component
{
public:
    explicit TopBar (AppController&);
    void refresh();
    void resized() override;

    juce::TextButton load { "Load" }, save { "Save" }, settings { "Settings" };

private:
    AppController& controller;
    juce::Label title { {}, "Locus" }, sessionLabel { {}, "Session" }, device, cpu;
    juce::String deviceText;
};

/** Above the canvas: Top / Top+Front view, Labels, VUs. */
class ViewRow final : public juce::Component
{
public:
    explicit ViewRow (AppController&);
    void refresh();
    void resized() override;

private:
    AppController& controller;
    juce::TextButton top { "Top" }, topFront { "Top+Front" };
    locus::ui::ToggleDot labels, vus;
};

/** Flat text tabs with an underline on the selected one. */
class TabBar final : public juce::Component
{
public:
    explicit TabBar (const juce::StringArray& names);
    void setSelected (int index);
    int getSelected() const noexcept { return selected; }
    void resized() override;

    std::function<void (int)> onSelect;

private:
    struct Tab final : juce::TextButton
    {
        using juce::TextButton::TextButton;
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    };

    std::vector<std::unique_ptr<Tab>> tabs;
    int selected = 0;
};

/** Connected and known sources; clicking a row selects that source. */
class SourceList final : public juce::Component
{
public:
    explicit SourceList (AppController&);
    void refresh();
    void resized() override;

private:
    struct Row final : juce::Component
    {
        Row();
        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override { if (onClick) onClick(); }

        int slot = 0;
        bool selected = false;
        juce::Colour colour;
        juce::String fullName;
        juce::Label number, name, status, underruns;
        std::function<void()> onClick;
    };

    void layoutRows();

    AppController& controller;
    juce::Label heading { {}, "Sources" }, empty { {}, "No Send plugins connected" };
    juce::Viewport viewport;
    juce::Component content;
    std::vector<std::unique_ptr<Row>> rows;
    std::vector<int> shownSlots;
};

/** One thin meter per used output channel, labelled with the channel number and, where room
    allows, the speaker label. */
class MeterBank final : public juce::Component
{
public:
    explicit MeterBank (AppController& c) : controller (c) {}
    void paint (juce::Graphics&) override;

private:
    AppController& controller;
};

/** Bottom strip (spec §9.2): Volume, Limiter, VUs, meters. */
class OutputStrip final : public juce::Component
{
public:
    explicit OutputStrip (AppController&);
    void refresh();
    void resized() override;

private:
    AppController& controller;
    juce::Label volumeLabel { {}, "Volume" }, volumeValue;
    juce::Slider volume { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    locus::ui::ToggleDot limiter, vus;
    MeterBank meters;

    std::unique_ptr<SliderBinding> volumeBinding;
    std::unique_ptr<ButtonBinding> limiterBinding, vusBinding;
};

/** Settings dialog content (spec §9.4): audio device, IPC buffer, canvas default zoom. */
class SettingsPanel final : public juce::Component
{
public:
    SettingsPanel (AppController&, std::function<void()> onChange);
    void paint (juce::Graphics& g) override { g.fillAll (locus::ui::Colours::bg); }
    void resized() override;

private:
    AppController& controller;
    std::function<void()> onChange;
    juce::AudioDeviceSelectorComponent selector;
    juce::Label linkLabel { {}, "Link buffer" }, zoomLabel { {}, "Canvas zoom" };
    juce::ComboBox link, zoom;
};
