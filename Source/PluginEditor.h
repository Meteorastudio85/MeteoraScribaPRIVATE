// Scriba - by Meteora Plugins
// Copyright (c) 2026 Meteora Plugins. All rights reserved.
// Proprietary and confidential: copying, distributing or modifying this file, in whole or in part,
// by any means, without the written permission of the copyright holder is prohibited. See LICENSE.txt.

#pragma once

#include <array>
#include <deque>
#include <functional>
#include <memory>
#include <vector>
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

// Look & feel: blu notte con accento ciano.
class ScribaLookAndFeel : public juce::LookAndFeel_V4
{
public:
    ScribaLookAndFeel();

    void drawComboBox (juce::Graphics&, int, int, bool, int, int, int, int, juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float minSliderPos, float maxSliderPos, juce::Slider::SliderStyle, juce::Slider&) override;
    void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos, juce::Rectangle<int> parentArea) override;
};

// The big button: ANALYZE / STOP & ANALYZE / ANALYZING 42% ...
class PrimaryButton : public juce::Button
{
public:
    enum class Mode { Ready, Waiting, Recording, Analysing };
    PrimaryButton() : juce::Button ("primary") {}

    void setMode (Mode m, const juce::String& label, float progress);
    void paintButton (juce::Graphics&, bool over, bool down) override;
    float phase = 0.0f;

private:
    Mode mode = Mode::Ready;
    float progress = 0.0f;
};

// Secondary buttons with an icon (SAVE TXT, DRAG MIDI)
class ActionButton : public juce::Button
{
public:
    enum class Icon { Text, Midi };
    explicit ActionButton (Icon i) : juce::Button ({}), icon (i) {}
    void paintButton (juce::Graphics&, bool over, bool down) override;

private:
    Icon icon;
};

// Pulsante che si puo' cliccare (salva) o trascinare fuori dal plugin (rilascia il file nella DAW).
class MidiDragButton : public ActionButton
{
public:
    MidiDragButton() : ActionButton (Icon::Midi) {}
    std::function<void()> onDragOut;
    bool wasDragged = false;

    void mouseDown (const juce::MouseEvent& e) override
    {
        wasDragged = false;
        ActionButton::mouseDown (e);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        ActionButton::mouseDrag (e);
        if (! wasDragged && isEnabled() && e.getDistanceFromDragStart() > 8 && onDragOut)
        {
            wasDragged = true;
            onDragOut();
        }
    }
};

// Small rounded button: LISTEN (toggle), - / + / 0 / SET for the snare pitch
class ChipButton : public juce::Button
{
public:
    explicit ChipButton (const juce::String& label) : juce::Button (label) { setButtonText (label); }
    void paintButton (juce::Graphics&, bool over, bool down) override;
    bool accent = false;   // filled cyan (the SET button)
};

// A chip that can also be dragged out of the plugin (WHOLE SONG)
class DragChip : public ChipButton
{
public:
    explicit DragChip (const juce::String& label) : ChipButton (label) {}
    std::function<void()> onDragOut;
    bool wasDragged = false;
    void mouseDown (const juce::MouseEvent& e) override { wasDragged = false; ChipButton::mouseDown (e); }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        ChipButton::mouseDrag (e);
        if (! wasDragged && isEnabled() && e.getDistanceFromDragStart() > 8 && onDragOut)
        {
            wasDragged = true;
            onDragOut();
        }
    }
};

// One chord of the key: click = listen, click and drag = the MIDI of the chord into the DAW
class ChordPad : public juce::Button
{
public:
    ChordPad() : juce::Button ("chord") { setRepaintsOnMouseActivity (true); }
    void set (const scriba::ChordDef& c, bool tonic, bool inSong);
    const scriba::ChordDef& getChord() const { return chord; }
    void paintButton (juce::Graphics&, bool over, bool down) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    std::function<void (const scriba::ChordDef&)> onPress, onDragOut;

private:
    scriba::ChordDef chord;
    bool tonic = false, inSong = false, dragged = false;
};

// Five big buttons with an icon: Bass / Guitar / Piano / Kick / Snare
class InstrumentSelector : public juce::Component
{
public:
    explicit InstrumentSelector (juce::RangedAudioParameter& param);
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    int getSelected() const { return selected; }

private:
    int indexAt (juce::Point<float>) const;
    juce::ParameterAttachment attachment;
    int selected = 1, hover = -1;
};

class ScribaEditor : public juce::AudioProcessorEditor,
                     private juce::Timer
{
public:
    explicit ScribaEditor (ScribaProcessor&);
    ~ScribaEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void setPitch (float semitones);
    juce::Rectangle<float> meterArea() const;
    bool overThresholdLine (juce::Point<float>) const;
    void setGateFromY (float y);
    void refresh();
    void mainButtonClicked();
    void exportText();
    void saveMidi();
    void dragMidi();
    bool writeMidi (const juce::File&);
    juce::String baseFileName() const;
    void flash (const juce::String& message);

    void paintHeader (juce::Graphics&);
    void paintKeyCard (juce::Graphics&, juce::Rectangle<float>);
    void paintWheel (juce::Graphics&, juce::Rectangle<float>);
    void paintSnareCard (juce::Graphics&, juce::Rectangle<float>);
    void paintKickCard (juce::Graphics&, juce::Rectangle<float>);
    void paintLyricsCard (juce::Graphics&, juce::Rectangle<float>);
    void paintControls (juce::Graphics&);
    void paintWave (juce::Graphics&);
    void paintChords (juce::Graphics&);
    void setTab (bool chords);
    void dragChord (const scriba::ChordDef&, juce::Component* source);
    void dragSong();
    bool dragBytes (const std::vector<juce::uint8>& bytes, const juce::String& fileName, juce::Component* source);

    juce::String statusText() const;
    bool showResults() const;    // the results on screen belong to the selected instrument

    ScribaProcessor& proc;
    ScribaLookAndFeel lnf;

    InstrumentSelector instrumentSelector;
    juce::Slider sensitivitySlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sensitivityAttach;
    juce::Slider gateSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gateAttach;
    ChipButton listenButton { "LISTEN" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> listenAttach;
    ChipButton pitchDown { juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")) }, pitchUp { "+" }, pitchZero { "0" }, pitchSet { "SET" };
    juce::ComboBox songKeyBox, languageBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> languageAttach;
    ChipButton modelButton { "DOWNLOAD VOICE MODEL" }, folderButton { "OPEN FOLDER" };
    scriba::LyricsResult lyricsShown;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> songKeyAttach;
    PrimaryButton mainButton;
    ActionButton txtButton { ActionButton::Icon::Text };
    MidiDragButton midiButton;
    // bottom card: WAVE / CHORDS
    ChipButton waveTab { "WAVE" }, chordsTab { "CHORDS" };
    bool chordsView = false;
    int shownChordKey = -2;
    std::array<ChordPad, 8> pads;
    int padCount = 0;
    DragChip songButton { "DRAG WHOLE SONG" };
    juce::ComboBox chordTypeBox, chordStyleBox, chordVoicingBox, chordLengthBox;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>> chordAttach;
    std::array<juce::Rectangle<float>, 4> chordLabelAreas;
    juce::Rectangle<float> padsArea;
    std::vector<std::pair<int, int>> songChordShapes;   // (root, third) of the chords played in the song
    bool songHasChords = false;
    int songScanCountdown = 0;
    juce::TooltipWindow tooltip { this, 600 };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Image logo, logoCache;

    // geometria (calcolata in resized)
    float scale = 1.0f;
    juce::Rectangle<float> headerArea, leftCard, rightArea, waveCard;
    juce::Rectangle<float> instLabelArea, sensLabelArea, sensHintArea, keyRowArea, sliderRow, pitchRowArea;

    // stato visivo
    ScribaProcessor::State state = ScribaProcessor::State::Idle;
    scriba::Instrument selected = scriba::Instrument::Guitar, analysed = scriba::Instrument::Guitar;
    bool resultsOk = false, audioKept = false;
    scriba::KeyResult result;
    std::vector<scriba::Note> notes;
    std::vector<float> overview;
    scriba::DrumResult drum;
    scriba::SongKeyInfo songKey;
    scriba::SnareTuning tuning;
    std::array<float, 24> shown {};
    std::array<float, 12> shownChroma {};
    std::deque<float> history;
    std::deque<float> detHistory;     // live gate detector (dB)
    bool draggingGate = false;
    float lastGate = 1000.0f;
    int laidOutFor = -1;
    float smoothLevel = 0.0f;
    float phase = 0.0f;
    int reanalyseCountdown = 0;
    int lastSelected = -1;
    int seenResultsVersion = -1;
    juce::String flashText;
    juce::uint32 flashUntil = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScribaEditor)
};
