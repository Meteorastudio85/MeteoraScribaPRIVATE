// Scriba - by Meteora Plugins
// Copyright (c) 2026 Meteora Plugins. All rights reserved.
// Proprietary and confidential: copying, distributing or modifying this file, in whole or in part,
// by any means, without the written permission of the copyright holder is prohibited. See LICENSE.txt.

#pragma once

#include <atomic>
#include <vector>
#include <juce_audio_processors/juce_audio_processors.h>
#include "ChordPreview.h"
#include "ChordTheory.h"
#include "DrumAnalyzer.h"
#include "DrumGate.h"
#include "KeyDetector.h"
#include "NoteTranscriber.h"
#include "MidiExport.h"
#include "ReportBuilder.h"
#include "SharedKey.h"
#include "Link/MeteoraLink.h"
#include "SnareTuning.h"

class ScribaProcessor : public juce::AudioProcessor
{
public:
    enum class State { Idle = 0, Armed, Recording, Analysing, Done };

    ScribaProcessor();
    ~ScribaProcessor() override;

    // --- AudioProcessor ---
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void updateTrackProperties (const TrackProperties& properties) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Scriba"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // --- controllo dall'interfaccia (thread messaggi) ---
    void startCapture();   // arma la registrazione: parte con il play dell'host
    void stopCapture();    // ferma e analizza (o annulla se ancora in attesa)
    void reanalyse();      // rifa l'analisi sull'audio gia' registrato (nuovo strumento / sensibilita')

    State getState() const noexcept            { return (State) state.load(); }
    float getLevel() const noexcept            { return level.exchange (0.0f); }
    float getProgress() const noexcept         { return progress.load(); }
    double getRecordedSeconds() const noexcept;
    double getBpm() const noexcept             { return bpm.load(); }
    int getTimeSigNumerator() const noexcept   { return tsNum.load(); }
    int getTimeSigDenominator() const noexcept { return tsDen.load(); }

    scriba::KeyResult getResult() const;           // key of THIS track (bass / guitar / piano)
    std::vector<scriba::Note> getNotes() const;
    std::vector<float> getOverview() const;
    scriba::DrumResult getDrumResult() const;
    bool hasResults() const;                       // the last analysis produced something usable
    bool hasAudio() const noexcept             { return writeIndex.load() > 0; }   // false after reopening a project
    int getResultsVersion() const noexcept     { return resultsVersion.load(); }   // changes every time the results change
    bool hasTextReport() const;                    // false for the kick: MIDI only
    scriba::Instrument getAnalysedInstrument() const;
    scriba::Instrument getSelectedInstrument() const;

    // live gate (kick / snare)
    float takeDetectorDb() const noexcept;             // loudest detector level since the last call (dB)
    bool isGateOpen() const noexcept           { return liveGateOpen.load(); }
    bool isTransportPlaying() const noexcept   { return transportPlaying.load()
                                                        && juce::Time::getMillisecondCounter() - lastBlockMs.load() < 400u; }
    float getGateDb() const                    { return gateParam->load(); }
    float getPitch() const                     { return pitchParam->load(); }

    // the key the snare is tuned to: chosen by hand, or taken from the other Scriba instances
    scriba::SongKeyInfo getSongKey() const;

    // Vocals: the lyrics, and the chords / sections of the other Scriba for the lyric sheet
    scriba::LyricsResult getLyrics() const;
    scriba::LyricsSheetInfo getLyricsSheetInfo() const;
    scriba::SnareTuning getSnareTuning() const;

    // chord buttons: the key they use (this track's key, else the song key of the other Scriba), the chord
    // played in the chosen style, as MIDI, and the whole song progression
    int getChordKey() const;
    std::vector<scriba::ChordDef> getKeyChords() const;
    std::vector<scriba::Note> renderChord (const scriba::ChordDef&) const;
    std::vector<juce::uint8> buildChordMidi (const scriba::ChordDef&) const;
    bool hasSongProgression() const;
    std::vector<juce::uint8> buildProgressionMidi() const;
    void previewChord (const scriba::ChordDef&);
    void stopPreview()                         { preview.stop(); }

    juce::String buildTextReport() const;          // testo UTF-8 pronto da salvare
    std::vector<juce::uint8> buildMidiBytes() const;

    juce::AudioProcessorValueTreeState apvts;

private:
    class AnalysisThread : public juce::Thread
    {
    public:
        explicit AnalysisThread (ScribaProcessor& p) : juce::Thread ("Scriba analysis"), proc (p) {}
        void run() override;
        bool cancelled() const { return threadShouldExit(); }
    private:
        ScribaProcessor& proc;
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void supervise();
    void captureBlock (const juce::AudioBuffer<float>&);
    void processLive (juce::AudioBuffer<float>&);
    void runAnalysis();
    void publishKey();
    /** Meteora Link: tells the other Meteora plugins (Scriba Pitch) the key and chords of this track. */
    void publishMeteoraLink();
    juce::String sourceName() const;
    scriba::ReportInfo makeReportInfo() const;

    const int instanceId = scriba::SharedKeyRegistry::newInstanceId();
    juce::String trackName;   // from the DAW, when it tells (guarded by resultLock)

    meteora::link::Publisher linkPublisher;
    juce::CriticalSection linkLock;
    std::vector<meteora::link::Chord> linkChords;   // guarded by linkLock
    std::atomic<int> linkPublishedSongKey { -2 };

    // registrazione (mono, decimata a ~22 kHz). Il buffer si alloca al primo ANALYZE.
    std::vector<float> captured;
    std::atomic<size_t> writeIndex { 0 };
    std::atomic<size_t> capacity { 0 };
    double hostRate = 44100.0;
    double storedRate = 0.0;
    int decimFactor = 1;
    float accum = 0.0f;
    int accumCount = 0;

    std::atomic<int> state { (int) State::Idle };
    mutable std::atomic<float> level { 0.0f };
    std::atomic<float> progress { 0.0f };
    std::atomic<bool> rerunRequested { false };
    std::atomic<juce::uint32> lastBlockMs { 0 };

    // trasporto dell'host (ultimo valore visto) e valori fissati all'inizio della registrazione
    std::atomic<double> bpm { 0.0 };
    std::atomic<int> tsNum { 0 }, tsDen { 0 };
    std::atomic<double> ppq { 0.0 }, ppqBar { 0.0 };
    std::atomic<bool> hasPpq { false };
    std::atomic<double> recBpm { 0.0 };
    std::atomic<int> recTsNum { 0 }, recTsDen { 0 };
    std::atomic<int> recFirstBar { 1 };          // DAW bar where the recording started
    std::atomic<double> recPhaseQn { 0.0 };      // quarter notes from that bar start to the recording start
    std::atomic<bool> recBarsFromHost { false };
    std::atomic<double> recBarStartQn { 0.0 };   // DAW position (quarter notes) of the bar where the recording started
    std::atomic<int> captureId { 0 };
    std::atomic<bool> tempoVaries { false };
    std::atomic<double> recordedSeconds { 0.0 };
    std::atomic<int> resultsVersion { 0 };

    mutable juce::CriticalSection resultLock;
    scriba::KeyResult result;
    std::vector<scriba::Note> notes;
    scriba::DrumResult drumResult;                 // snare pitch (only for Snare)
    std::vector<scriba::Section> sections;         // Intro / Verse / Chorus ... (not for drums)
    std::vector<float> overview;
    scriba::Instrument analysedInstrument = scriba::Instrument::Guitar;
    float analysedSensitivity = 0.55f;
    bool analysisOk = false;
    scriba::LyricsResult lyrics;
    int lyricsLangUsed = -1, lyricsCapture = -1;

    // live gate and pitch (audio thread)
    std::atomic<float>* gateParam = apvts.getRawParameterValue ("gate");
    std::atomic<float>* gateListenParam = apvts.getRawParameterValue ("gateListen");
    std::atomic<float>* pitchParam = apvts.getRawParameterValue ("pitch");
    std::atomic<float>* lyricsLangParam = apvts.getRawParameterValue ("lyricsLang");
    scriba::ChordPreview preview;
    struct ChordOptions { scriba::ChordType type; scriba::ChordStyle style; scriba::ChordVoicing voicing; double bars; };
    ChordOptions chordOptions() const;
    double chordBpm() const;
    double chordBeatsPerBar() const;
    scriba::DrumGate liveGate;
    scriba::SnareShifter liveShifter;
    mutable std::atomic<float> liveDetectorDb { -120.0f };
    std::atomic<bool> liveGateOpen { false }, transportPlaying { false };
    std::atomic<int> liveHits { 0 };

    AnalysisThread analysisThread { *this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScribaProcessor)
};
