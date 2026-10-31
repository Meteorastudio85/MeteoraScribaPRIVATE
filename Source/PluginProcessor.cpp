// Scriba - by Meteora Plugins
// Copyright (c) 2026 Meteora Plugins. All rights reserved.
// Proprietary and confidential: copying, distributing or modifying this file, in whole or in part,
// by any means, without the written permission of the copyright holder is prohibited. See LICENSE.txt.

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "VoiceModel.h"

namespace
{
constexpr double kMaxSeconds = 600.0;   // massimo 10 minuti di registrazione
constexpr double kTargetRate = 22050.0; // frequenza di lavoro

bool bpmValid (double b) { return b >= 20.0 && b <= 400.0; }
}

ScribaProcessor::ScribaProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    analysisThread.startThread();
}

ScribaProcessor::~ScribaProcessor()
{
    scriba::SharedKeyRegistry::get().remove (instanceId);
    analysisThread.stopThread (5000);
}

juce::AudioProcessorValueTreeState::ParameterLayout ScribaProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "instrument", 1 },
                                                              "Instrument",
                                                              juce::StringArray { "Bass", "Guitar", "Piano", "Kick", "Snare", "Vocals" },
                                                              1));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "sensitivity", 1 },
                                                             "Sensitivity",
                                                             juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f),
                                                             0.55f));

    // Kick / Snare: threshold of the gate (dB on the detector), and whether you hear the gated signal
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "gate", 1 }, "Gate",
                                                             juce::NormalisableRange<float> (-60.0f, 0.0f, 0.5f), -30.0f,
                                                             juce::AudioParameterFloatAttributes().withLabel ("dB")));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "gateListen", 1 }, "Gate listen", true));

    // Snare: pitch shift applied to the track by Scriba itself (semitones, 0 = off)
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "pitch", 1 }, "Snare pitch",
                                                             juce::NormalisableRange<float> (-12.0f, 12.0f, 0.01f), 0.0f,
                                                             juce::AudioParameterFloatAttributes().withLabel ("st")));

    // Vocals: language of the lyrics
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "lyricsLang", 1 }, "Lyrics language",
                                                              juce::StringArray { "Auto (IT / EN)", "Italiano", "English" }, 0));

    // Snare: key of the song. 0 = taken from the other Scriba instances, then the 24 keys
    juce::StringArray keys { "Auto (from other Scriba)" };
    for (int pc = 0; pc < 12; ++pc)
    {
        keys.add (scriba::keyName (pc));
        keys.add (scriba::keyName (pc + 12));
    }
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "songKey", 1 }, "Song key", keys, 0));

    // chord buttons: chord type, how it is played, voicing, length
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "chordType", 1 }, "Chord type",
                                                              juce::StringArray { "Triads", "7th", "sus2", "sus4", "add9" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "chordStyle", 1 }, "Chord style",
                                                              juce::StringArray { "Held", "Strum", "Quarters", "Eighths", "Arpeggio up",
                                                                                  "Arpeggio up-down", "Alberti", "Ballad",
                                                                                  "Piano: Pop", "Piano: Ballad arpeggio", "Piano: Gospel",
                                                                                  "Piano: Driving pop", "Piano: Cinematic", "Piano: Neo soul" }, 8));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "chordVoicing", 1 }, "Chord voicing",
                                                              juce::StringArray { "Close", "1st inversion", "2nd inversion", "Open", "With bass" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "chordLength", 1 }, "Chord length",
                                                              juce::StringArray { "1/2 bar", "1 bar", "2 bars", "4 bars" }, 1));
    return layout;
}

//==============================================================================
void ScribaProcessor::prepareToPlay (double sampleRate, int)
{
    hostRate = sampleRate > 0.0 ? sampleRate : 44100.0;

    // live path: gate look-ahead + pitch shifter share one delay line (constant latency, compensated by the DAW)
    const int lookahead = (int) std::lround (0.005 * hostRate);
    liveShifter.prepare (hostRate, 2, lookahead);
    liveGate.prepare (hostRate, getSelectedInstrument() == scriba::Instrument::Kick);
    preview.prepare (hostRate);
    setLatencySamples (lookahead);
    if (state.load() == (int) State::Analysing)
        return;   // non toccare il buffer mentre e' in analisi

    const int newFactor = juce::jmax (1, (int) std::lround (hostRate / kTargetRate));
    const double newRate = hostRate / (double) newFactor;

    // stessa frequenza: tiene l'audio registrato (serve per rianalizzare)
    if (std::abs (newRate - storedRate) < 1.0e-6)
        return;

    // la frequenza e' cambiata: l'audio registrato non vale piu' (i risultati restano)
    const int s = state.load();
    if (s == (int) State::Recording || s == (int) State::Armed)
        state.store ((int) State::Idle);

    decimFactor = newFactor;
    storedRate = newRate;
    capacity.store (0);
    writeIndex.store (0);
    accum = 0.0f;
    accumCount = 0;
}

bool ScribaProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    if (in != out)
        return false;
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void ScribaProcessor::updateTrackProperties (const TrackProperties& properties)
{
    // called on the message thread by the hosts that support it (Reaper, Cubase, Studio One, Live 11+, Logic)
    {
        const juce::ScopedLock sl (resultLock);
        trackName = properties.name.trim();
    }
    scriba::SharedKeyRegistry::get().rename (instanceId, sourceName());
}

void ScribaProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    captureBlock (buffer);   // the recording always takes the original signal
    processLive (buffer);    // then gate / pitch (kick and snare) or the dry signal, delayed by the look-ahead
    preview.render (buffer.getArrayOfWritePointers(), getTotalNumOutputChannels(), buffer.getNumSamples());   // chord buttons
}

void ScribaProcessor::processLive (juce::AudioBuffer<float>& buffer)
{
    const int n = buffer.getNumSamples();
    const int nc = juce::jlimit (1, 2, juce::jmin (buffer.getNumChannels(), getTotalNumInputChannels()));
    const auto inst = getSelectedInstrument();
    const bool drum = scriba::isDrum (inst);
    const bool kickMode = inst == scriba::Instrument::Kick;

    if (drum && liveGate.isKick() != kickMode)
        liveGate.prepare (hostRate, kickMode);   // no allocation: safe on the audio thread
    liveGate.setThresholdDb (gateParam->load());
    const bool listen = drum && gateListenParam->load() > 0.5f;
    liveShifter.setSemitones (inst == scriba::Instrument::Snare ? (double) pitchParam->load() : 0.0);

    float frame[2] {}, out[2] {};
    float detPeak = -120.0f;
    int opened = 0;
    for (int i = 0; i < n; ++i)
    {
        float mono = 0.0f;
        for (int c = 0; c < nc; ++c)
        {
            frame[c] = buffer.getSample (c, i);
            mono += frame[c];
        }
        mono /= (float) nc;

        if (drum && liveGate.process (mono))
        {
            liveShifter.onset();
            ++opened;
        }
        liveShifter.push (frame, nc);
        liveShifter.read (out, nc);
        const float g = listen ? liveGate.getGain() : 1.0f;
        for (int c = 0; c < nc; ++c)
            buffer.setSample (c, i, out[c] * g);
        if (drum)
            detPeak = juce::jmax (detPeak, liveGate.getLevelDb());
    }

    if (detPeak > liveDetectorDb.load())
        liveDetectorDb.store (detPeak);
    liveGateOpen.store (drum && liveGate.isOpen());
    if (opened > 0)
        liveHits.fetch_add (opened);
}

float ScribaProcessor::takeDetectorDb() const noexcept
{
    return liveDetectorDb.exchange (-120.0f);
}

void ScribaProcessor::captureBlock (const juce::AudioBuffer<float>& buffer)
{
    const int numSamples = buffer.getNumSamples();
    const int numIn = getTotalNumInputChannels();

    lastBlockMs.store (juce::Time::getMillisecondCounter());

    const float peak = buffer.getMagnitude (0, numSamples);
    if (peak > level.load())
        level.store (peak);

    // trasporto dell'host
    bool hasTransport = false, playing = false;
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            hasTransport = true;
            playing = pos->getIsPlaying();
            if (auto b = pos->getBpm())
                bpm.store (*b);
            if (auto ts = pos->getTimeSignature())
            {
                tsNum.store (ts->numerator);
                tsDen.store (ts->denominator);
            }
            const auto p = pos->getPpqPosition();
            const auto pb = pos->getPpqPositionOfLastBarStart();
            hasPpq.store (p.hasValue());
            if (p)
                ppq.store (*p);
            ppqBar.store (pb ? *pb : std::numeric_limits<double>::quiet_NaN());
        }
    }
    if (! hasTransport)
        playing = true;   // standalone: nessun trasporto, si registra subito
    transportPlaying.store (playing);

    int s = state.load();

    if (s == (int) State::Armed && playing)
    {
        accum = 0.0f;
        accumCount = 0;
        recBpm.store (bpm.load());
        recTsNum.store (tsNum.load());
        recTsDen.store (tsDen.load());
        tempoVaries.store (false);

        // where the recording starts on the DAW timeline: bar number and offset inside the bar
        int firstBar = 1;
        double phaseQn = 0.0, barStartQn = 0.0;
        bool fromHost = false;
        if (hasPpq.load() && bpmValid (bpm.load()))
        {
            const int num = tsNum.load() > 0 ? tsNum.load() : 4;
            const int den = tsDen.load() > 0 ? tsDen.load() : 4;
            const double qnPerBar = (double) num * 4.0 / (double) den;
            const double now = ppq.load();
            double barStart = ppqBar.load();
            if (! std::isfinite (barStart) || barStart > now + 1.0e-6 || now - barStart >= qnPerBar)
                barStart = std::floor (now / qnPerBar + 1.0e-9) * qnPerBar;
            phaseQn = juce::jlimit (0.0, qnPerBar * 0.999, now - barStart);
            // starting within a 32nd of the downbeat counts as starting on it
            if (phaseQn < 0.125 || qnPerBar - phaseQn < 0.125)
            {
                if (qnPerBar - phaseQn < 0.125)
                    barStart += qnPerBar;
                phaseQn = 0.0;
            }
            firstBar = (int) std::lround (barStart / qnPerBar) + 1;
            barStartQn = barStart;
            fromHost = true;
        }
        recFirstBar.store (firstBar);
        recPhaseQn.store (phaseQn);
        recBarsFromHost.store (fromHost);
        recBarStartQn.store (barStartQn);

        if (state.compare_exchange_strong (s, (int) State::Recording))
            s = (int) State::Recording;
        else
            return;
    }

    if (s != (int) State::Recording)
        return;

    if (hasTransport && ! playing)
    {
        // il trasporto si e' fermato: chiude la registrazione
        int expected = (int) State::Recording;
        state.compare_exchange_strong (expected, (int) State::Analysing);
        return;
    }

    if (recBpm.load() > 0.0 && std::abs (bpm.load() - recBpm.load()) > 0.5)
        tempoVaries.store (true);

    const int numCh = juce::jmax (1, juce::jmin (numIn, buffer.getNumChannels()));
    const float inv = 1.0f / (float) numCh;
    size_t w = writeIndex.load (std::memory_order_relaxed);
    const size_t cap = juce::jmin (capacity.load(), captured.size());

    for (int i = 0; i < numSamples; ++i)
    {
        float m = 0.0f;
        for (int ch = 0; ch < numCh; ++ch)
            m += buffer.getReadPointer (ch)[i];
        accum += m * inv;

        if (++accumCount >= decimFactor)
        {
            if (w < cap)
                captured[w++] = accum / (float) decimFactor;
            accum = 0.0f;
            accumCount = 0;
        }
    }

    writeIndex.store (w, std::memory_order_release);

    if (w >= cap)
    {
        int expected = (int) State::Recording;
        state.compare_exchange_strong (expected, (int) State::Analysing);
    }
}

//==============================================================================
void ScribaProcessor::startCapture()
{
    const int s = state.load();
    if (s == (int) State::Recording || s == (int) State::Analysing)
        return;

    // the recording buffer (~50 MB) is only allocated when it is really needed
    if (storedRate <= 0.0)
    {
        decimFactor = juce::jmax (1, (int) std::lround (hostRate / kTargetRate));
        storedRate = hostRate / (double) decimFactor;
    }
    const size_t needed = (size_t) (kMaxSeconds * storedRate);
    if (captured.size() != needed)
        captured.assign (needed, 0.0f);
    capacity.store (needed);

    rerunRequested.store (false);
    ++captureId;
    writeIndex.store (0);
    recordedSeconds.store (0.0);
    progress.store (0.0f);
    state.store ((int) State::Armed);
}

void ScribaProcessor::stopCapture()
{
    int expected = (int) State::Recording;
    if (state.compare_exchange_strong (expected, (int) State::Analysing))
        return;

    // still waiting for play: go back to what was there before
    expected = (int) State::Armed;
    state.compare_exchange_strong (expected, hasResults() ? (int) State::Done : (int) State::Idle);
}

void ScribaProcessor::reanalyse()
{
    const int s = state.load();
    if (s == (int) State::Analysing)
    {
        rerunRequested.store (true);   // riparte appena finisce quella in corso
        return;
    }
    if (s != (int) State::Done || writeIndex.load() == 0)
        return;

    progress.store (0.0f);
    state.store ((int) State::Analysing);
}

double ScribaProcessor::getRecordedSeconds() const noexcept
{
    const auto st = getState();
    const size_t w = writeIndex.load();
    if (st == State::Recording || st == State::Analysing || st == State::Done)
        return (storedRate > 0.0 && w > 0) ? (double) w / storedRate : recordedSeconds.load();
    return 0.0;
}

void ScribaProcessor::supervise()
{
    // the key chosen by hand changed: tell Scriba Pitch
    if ((int) std::lround (apvts.getRawParameterValue ("songKey")->load()) != linkPublishedSongKey.load())
        publishMeteoraLink();

    // se l'host smette di chiamare processBlock (trasporto fermo) chiude la registrazione
    if (state.load() != (int) State::Recording)
        return;

    const juce::uint32 now = juce::Time::getMillisecondCounter();
    if (writeIndex.load() > 0 && (juce::uint32) (now - lastBlockMs.load()) > 700u)
    {
        int expected = (int) State::Recording;
        state.compare_exchange_strong (expected, (int) State::Analysing);
    }
}

void ScribaProcessor::runAnalysis()
{
    const size_t n = juce::jmin (writeIndex.load (std::memory_order_acquire), captured.size());
    if (n == 0)
    {
        // nothing recorded (stopped before any audio): keep what was there before
        progress.store (1.0f);
        state.store (hasResults() ? (int) State::Done : (int) State::Idle);
        return;
    }
    recordedSeconds.store (storedRate > 0.0 ? (double) n / storedRate : 0.0);

    scriba::KeyResult r;
    std::vector<scriba::Note> found;
    std::vector<float> ov;
    scriba::DrumResult drums;
    std::vector<scriba::Section> secs;
    scriba::LyricsResult lyr;
    int lyrLang = -1;

    const auto inst = (scriba::Instrument) juce::jlimit (0, 5, (int) std::lround (apvts.getRawParameterValue ("instrument")->load()));
    const float sens = juce::jlimit (0.0f, 1.0f, apvts.getRawParameterValue ("sensitivity")->load());

    progress.store (0.0f);

    if (scriba::isDrum (inst))
    {
        // no key detection on a drum track: the key comes from the other Scriba instances.
        // The hits are the openings of the same gate you hear: move the GATE and the MIDI follows.
        drums = scriba::DrumAnalyzer::gateHits (captured.data(), n, storedRate, inst, gateParam->load(),
                                                [this] (float p) { progress.store (0.95f * p); },
                                                [this] { return analysisThread.cancelled(); });
        found = scriba::DrumAnalyzer::toNotes (drums, inst);
    }
    else if (inst == scriba::Instrument::Vocals)
    {
        // melody of the voice -> MIDI
        found = scriba::NoteTranscriber::transcribe (captured.data(), n, storedRate, inst, sens,
                                                     [this] (float p) { progress.store (0.12f * p); },
                                                     [this] { return analysisThread.cancelled(); });
        r = scriba::keyFromNotes (scriba::KeyResult(), found);

        // lyrics: only when the recording or the language changed (Whisper takes a while)
        const auto lang = (scriba::LyricsLanguage) juce::jlimit (0, 2, (int) std::lround (lyricsLangParam->load()));
        bool reuse = false;
        {
            const juce::ScopedLock sl (resultLock);
            reuse = lyrics.ran && lyrics.ok && lyricsCapture == captureId.load() && lyricsLangUsed == (int) lang;
            if (reuse)
                lyr = lyrics;
        }
        if (! reuse)
        {
            const auto model = scriba::VoiceModel::get().modelFile();
            if (model == juce::File())
            {
                lyr.ran = true;
                lyr.error = "voice model missing";
            }
            else
                lyr = scriba::LyricsTranscriber::transcribe (captured.data(), n, storedRate, model.getFullPathName().toStdString(), lang,
                                                             [this] (float p) { progress.store (0.12f + 0.86f * p); },
                                                             [this] { return analysisThread.cancelled(); });
        }
        lyrLang = (int) lang;
    }
    else
    {
        r = scriba::KeyDetector::analyse (captured.data(), n, storedRate);
        progress.store (0.05f);

        found = scriba::NoteTranscriber::transcribe (captured.data(), n, storedRate, inst, sens,
                                                     [this] (float p) { progress.store (0.05f + 0.88f * p); },
                                                     [this] { return analysisThread.cancelled(); });
        r = scriba::keyFromNotes (r, found);

        // song sections, on the bars of the DAW
        scriba::ReportInfo bi;
        bi.bpm = recBpm.load();
        bi.tsNum = recTsNum.load();
        bi.tsDen = recTsDen.load();
        const double phaseSec = bpmValid (bi.bpm) ? recPhaseQn.load() * 60.0 / bi.bpm : 0.0;
        secs = scriba::StructureDetector::analyse (captured.data(), n, storedRate, scriba::report_detail::barSecondsOf (bi), phaseSec);
    }

    constexpr size_t bins = 600;
    ov.assign (bins, 0.0f);
    for (size_t i = 0; i < n; ++i)
    {
        const size_t b = juce::jmin (bins - 1, (size_t) ((double) i / (double) n * (double) bins));
        ov[b] = juce::jmax (ov[b], std::abs (captured[i]));
    }

    if (analysisThread.cancelled())
        return;

    {
        const juce::ScopedLock sl (resultLock);
        result = r;
        notes = std::move (found);
        drumResult = std::move (drums);
        sections = std::move (secs);
        overview = std::move (ov);
        analysedInstrument = inst;
        analysedSensitivity = sens;
        analysisOk = true;
        lyrics = std::move (lyr);
        lyricsLangUsed = lyrLang;
        lyricsCapture = captureId.load();
    }
    ++resultsVersion;
    publishKey();
    progress.store (1.0f);

    if (rerunRequested.exchange (false))
        state.store ((int) State::Analysing);   // un altro cambio e' arrivato durante l'analisi
    else
        state.store ((int) State::Done);
}

void ScribaProcessor::AnalysisThread::run()
{
    while (! threadShouldExit())
    {
        wait (100);
        proc.supervise();
        if (proc.state.load() == (int) State::Analysing)
            proc.runAnalysis();
    }
}

//==============================================================================
juce::String ScribaProcessor::sourceName() const
{
    const juce::ScopedLock sl (resultLock);
    if (trackName.isNotEmpty())
        return "'" + trackName + "'";
    return juce::String (scriba::instrumentName (analysedInstrument)) + " track";
}

void ScribaProcessor::publishKey()
{
    scriba::KeyResult k;
    scriba::Instrument inst;
    {
        const juce::ScopedLock sl (resultLock);
        k = result;
        inst = analysedInstrument;
    }
    auto& reg = scriba::SharedKeyRegistry::get();
    if (scriba::isDrum (inst))
    {
        reg.remove (instanceId);
        return;
    }
    reg.publish (instanceId, sourceName(), k);

    // chords (or bass notes) and sections on the DAW timeline, for a Scriba on the vocals
    std::vector<std::pair<double, std::string>> chordMarks, sectionMarks;
    const auto info = makeReportInfo();
    if (inst != scriba::Instrument::Vocals && info.barsFromHost && bpmValid (info.bpm))
    {
        const int num = info.tsNum > 0 ? info.tsNum : 4;
        const int den = info.tsDen > 0 ? info.tsDen : 4;
        const double qnPerBeat = 4.0 / (double) den;
        const double barSec = scriba::report_detail::barSecondsOf (info);
        const double barStartQn = recBarStartQn.load();
        scriba::ChartGrid g;
        g.beatSec = 60.0 / info.bpm * qnPerBeat;
        g.beatsPerBar = num;
        g.phaseSec = info.phaseSec;
        g.bars = juce::jmax (1, (int) std::ceil ((info.seconds + info.phaseSec) / barSec));
        const auto ns = getNotes();
        const bool real = inst != scriba::Instrument::Bass && ! scriba::ChordChart::isMostlySingleNotes (ns);
        const auto names = real ? scriba::ChordChart::beatChords (ns, g, k.valid ? k.best : -1) : scriba::ChordChart::beatNotes (ns, g);
        std::string last = "\x01";
        for (size_t b = 0; b < names.size(); ++b)
            if (names[b] != last)
            {
                chordMarks.push_back ({ barStartQn + (double) b * qnPerBeat, names[b] });
                last = names[b];
            }
        for (const auto& sec : info.sections)
            sectionMarks.push_back ({ barStartQn + (double) (sec.startBar - 1) * qnPerBeat * num, sec.label });
        {
            // chords for Meteora Link (guitar / piano chords only; bass notes are not chords)
            const juce::ScopedLock ll (linkLock);
            linkChords.clear();
            if (real)
                for (size_t i = 0; i < chordMarks.size(); ++i)
                {
                    const double next = i + 1 < chordMarks.size() ? chordMarks[i + 1].first
                                                                  : barStartQn + (double) names.size() * qnPerBeat;
                    if (chordMarks[i].second.empty() || chordMarks[i].second == "-" || chordMarks[i].second == "N.C.") continue;
                    linkChords.push_back ({ chordMarks[i].first, next - chordMarks[i].first, juce::String (chordMarks[i].second) });
                }
        }
        reg.publishMap (instanceId, sourceName(), std::move (chordMarks), std::move (sectionMarks), real);
    }
    else
    {
        {
            const juce::ScopedLock ll (linkLock);
            linkChords.clear();
        }
        reg.publishMap (instanceId, sourceName(), {}, {}, false);
    }
    publishMeteoraLink();
}

void ScribaProcessor::publishMeteoraLink()
{
    scriba::KeyResult k;
    scriba::Instrument inst;
    bool ok;
    {
        const juce::ScopedLock sl (resultLock);
        k = result;
        inst = analysedInstrument;
        ok = analysisOk;
    }
    const int choice = juce::jlimit (0, 24, (int) std::lround (apvts.getRawParameterValue ("songKey")->load()));
    linkPublishedSongKey = choice;

    // a key chosen by hand counts even before any analysis
    if (! ok) inst = getSelectedInstrument();
    // drums have no key; nothing analysed and no key chosen = nothing to say
    if ((! ok && choice == 0) || scriba::isDrum (inst))
    {
        linkPublisher.withdraw();
        return;
    }

    meteora::link::Info info;
    info.plugin = "Scriba";   // Scriba Pitch reads only this name
    info.instrument = juce::String (scriba::instrumentName (inst)).toUpperCase();
    if (choice > 0)
    {
        // the key chosen by hand in Scriba wins
        info.key = (choice - 1) / 2 + (((choice - 1) % 2) == 1 ? 12 : 0);
        info.confidence = 1.0f;
    }
    else if (ok && k.valid)
    {
        info.key = k.best;
        info.alt = k.second;
        info.confidence = juce::jlimit (0.0f, 1.0f, k.bestShare);
    }
    {
        const juce::ScopedLock ll (linkLock);
        info.chords = linkChords;
    }
    linkPublisher.publish (info);
}

scriba::LyricsResult ScribaProcessor::getLyrics() const
{
    const juce::ScopedLock sl (resultLock);
    return lyrics;
}

scriba::LyricsSheetInfo ScribaProcessor::getLyricsSheetInfo() const
{
    // the chords / sections of the other Scriba, moved onto the time of this recording
    scriba::LyricsSheetInfo li;
    const double b = recBpm.load();
    if (! recBarsFromHost.load() || ! bpmValid (b))
        return li;
    const auto m = scriba::SharedKeyRegistry::get().songMap (instanceId);
    if (! m.valid)
        return li;
    const double startQn = recBarStartQn.load() + recPhaseQn.load();
    auto toSec = [&] (double qn) { return (qn - startQn) * 60.0 / b; };
    li.haveMap = true;
    li.source = ("Scriba on " + m.source).toStdString();
    li.fromBass = m.fromBass;
    for (const auto& c : m.chords)
        li.chords.push_back ({ toSec (c.first), c.second });
    for (const auto& sct : m.sections)
        li.sections.push_back ({ toSec (sct.first), sct.second });
    return li;
}

scriba::KeyResult ScribaProcessor::getResult() const
{
    const juce::ScopedLock sl (resultLock);
    return result;
}

std::vector<scriba::Note> ScribaProcessor::getNotes() const
{
    const juce::ScopedLock sl (resultLock);
    return notes;
}

std::vector<float> ScribaProcessor::getOverview() const
{
    const juce::ScopedLock sl (resultLock);
    return overview;
}

scriba::DrumResult ScribaProcessor::getDrumResult() const
{
    const juce::ScopedLock sl (resultLock);
    return drumResult;
}

bool ScribaProcessor::hasResults() const
{
    const juce::ScopedLock sl (resultLock);
    return analysisOk;
}

scriba::Instrument ScribaProcessor::getAnalysedInstrument() const
{
    const juce::ScopedLock sl (resultLock);
    return analysedInstrument;
}

scriba::Instrument ScribaProcessor::getSelectedInstrument() const
{
    return (scriba::Instrument) juce::jlimit (0, 5, (int) std::lround (apvts.getRawParameterValue ("instrument")->load()));
}

scriba::SongKeyInfo ScribaProcessor::getSongKey() const
{
    scriba::SongKeyInfo k;
    const int choice = juce::jlimit (0, 24, (int) std::lround (apvts.getRawParameterValue ("songKey")->load()));
    if (choice > 0)
    {
        const int pc = (choice - 1) / 2;
        const bool minor = ((choice - 1) % 2) == 1;
        k.valid = true;
        k.key = pc + (minor ? 12 : 0);
        k.manual = true;
        return k;
    }

    const auto c = scriba::SharedKeyRegistry::get().combined (instanceId);
    if (c.valid)
    {
        k.valid = true;
        k.key = c.key.best;
        k.share = c.key.bestShare;
        k.source = (juce::String ("Scriba on ") + c.sources.joinIntoString (" + ")).toStdString();
    }
    return k;
}

scriba::SnareTuning ScribaProcessor::getSnareTuning() const
{
    const auto k = getSongKey();
    if (! k.valid)
        return {};
    return scriba::computeSnareTuning (getDrumResult().pitch, k.key);
}

scriba::ReportInfo ScribaProcessor::makeReportInfo() const
{
    scriba::ReportInfo info;
    {
        const juce::ScopedLock sl (resultLock);
        info.key = result;
        info.instrument = analysedInstrument;
        info.sensitivity = analysedSensitivity;
        info.sections = sections;
    }
    info.bpm = recBpm.load();
    info.tsNum = recTsNum.load();
    info.tsDen = recTsDen.load();
    info.tempoVaries = tempoVaries.load();
    info.seconds = recordedSeconds.load();
    info.firstBar = recFirstBar.load();
    info.barsFromHost = recBarsFromHost.load();
    info.phaseSec = bpmValid (info.bpm) ? recPhaseQn.load() * 60.0 / info.bpm : 0.0;
    if (info.instrument == scriba::Instrument::Snare)
    {
        info.songKey = getSongKey();
        info.pitchSemis = (double) pitchParam->load();
    }
    info.gateDb = getDrumResult().thresholdDb;
    return info;
}

bool ScribaProcessor::hasTextReport() const
{
    return getAnalysedInstrument() != scriba::Instrument::Kick;
}

juce::String ScribaProcessor::buildTextReport() const
{
    std::string text;
    if (getAnalysedInstrument() == scriba::Instrument::Snare)
        text = scriba::buildSnareReport (getDrumResult(), makeReportInfo());
    else if (getAnalysedInstrument() == scriba::Instrument::Vocals)
        text = scriba::buildLyricsReport (getLyrics(), makeReportInfo(), getLyricsSheetInfo(), getNotes().size());
    else
        text = scriba::buildReport (getNotes(), makeReportInfo());
    return juce::String::fromUTF8 (text.c_str(), (int) text.size());
}

std::vector<juce::uint8> ScribaProcessor::buildMidiBytes() const
{
    const auto info = makeReportInfo();

    scriba::MidiExportInfo mi;
    mi.bpm = bpmValid (info.bpm) ? info.bpm : 120.0;
    mi.tsNum = info.tsNum > 0 ? info.tsNum : 4;
    mi.tsDen = info.tsDen > 0 ? info.tsDen : 4;
    if (info.key.valid)
    {
        mi.keyTonic = info.key.best % 12;
        mi.keyMinor = info.key.best >= 12;
        mi.keyText = scriba::keyName (info.key.best);
    }
    mi.trackName = std::string ("Scriba - ") + scriba::instrumentName (info.instrument);
    for (const auto& sec : info.sections)
        mi.markers.push_back ({ sec.start, sec.label });

    return scriba::buildMidiFile (getNotes(), mi);
}

//==============================================================================
// Chord buttons
int ScribaProcessor::getChordKey() const
{
    const auto inst = getSelectedInstrument();
    if (! scriba::isDrum (inst) && inst != scriba::Instrument::Vocals)
    {
        const juce::ScopedLock sl (resultLock);
        if (analysisOk && analysedInstrument == inst && result.valid && result.best >= 0)
            return result.best;
    }
    const auto k = getSongKey();
    return k.valid ? k.key : -1;
}

ScribaProcessor::ChordOptions ScribaProcessor::chordOptions() const
{
    auto get = [this] (const char* id, int hi) { return juce::jlimit (0, hi, (int) std::lround (apvts.getRawParameterValue (id)->load())); };
    static const double bars[4] = { 0.5, 1.0, 2.0, 4.0 };
    return { (scriba::ChordType) get ("chordType", 4), (scriba::ChordStyle) get ("chordStyle", 13),
             (scriba::ChordVoicing) get ("chordVoicing", 4), bars[get ("chordLength", 3)] };
}

double ScribaProcessor::chordBpm() const
{
    const double b = bpm.load(), r = recBpm.load();
    return bpmValid (b) ? b : bpmValid (r) ? r : 120.0;
}

double ScribaProcessor::chordBeatsPerBar() const
{
    const int num = tsNum.load() > 0 ? tsNum.load() : 4;
    const int den = tsDen.load() > 0 ? tsDen.load() : 4;
    return (double) num * 4.0 / (double) den;   // in quarter notes
}

std::vector<scriba::ChordDef> ScribaProcessor::getKeyChords() const
{
    return scriba::diatonicChords (getChordKey(), chordOptions().type);
}

std::vector<scriba::Note> ScribaProcessor::renderChord (const scriba::ChordDef& c) const
{
    const auto o = chordOptions();
    std::vector<scriba::Note> out;
    const double bpb = chordBeatsPerBar();
    // piano parts: the left hand is already there, the right hand takes close / inverted voicings
    auto v = scriba::voice (c, scriba::isPianoStyle (o.style) && (o.voicing == scriba::ChordVoicing::Open || o.voicing == scriba::ChordVoicing::WithBass)
                                   ? scriba::ChordVoicing::Close : o.voicing);
    // dragged alone over several bars: the left hand walks back to the chord at the end of each bar
    scriba::renderAny (out, c, v, o.style, 0.0, o.bars * bpb, chordBpm(), bpb, 0.0, -1);
    std::sort (out.begin(), out.end(), [] (const scriba::Note& a, const scriba::Note& b) { return a.start < b.start; });
    return out;
}

std::vector<juce::uint8> ScribaProcessor::buildChordMidi (const scriba::ChordDef& c) const
{
    scriba::MidiExportInfo mi;
    mi.bpm = chordBpm();
    mi.tsNum = tsNum.load() > 0 ? tsNum.load() : 4;
    mi.tsDen = tsDen.load() > 0 ? tsDen.load() : 4;
    const int key = getChordKey();
    if (key >= 0)
    {
        mi.keyTonic = key % 12;
        mi.keyMinor = key >= 12;
        mi.keyText = scriba::keyName (key);
    }
    mi.trackName = "Scriba - " + c.name;
    return scriba::buildMidiFile (renderChord (c), mi);
}

bool ScribaProcessor::hasSongProgression() const
{
    const auto m = scriba::SharedKeyRegistry::get().songMap (-1);
    for (const auto& ch : m.chords)
        if (! ch.second.empty())
            return true;
    return false;
}

// all the chords found in the song (by this Scriba or the others), on the DAW timeline from bar 1,
// played in the chosen style: drop it at the start of the project and it lines up with the song
std::vector<juce::uint8> ScribaProcessor::buildProgressionMidi() const
{
    auto m = scriba::SharedKeyRegistry::get().songMap (-1);
    const auto o = chordOptions();
    const int key = getChordKey();
    const double bpmNow = chordBpm(), bpb = chordBeatsPerBar(), qSec = 60.0 / bpmNow;

    // a chord that lasts less than a beat is a passing detail of the detection: the previous chord goes on
    {
        std::vector<std::pair<double, std::string>> clean;
        for (size_t i = 0; i < m.chords.size(); ++i)
        {
            const double end = i + 1 < m.chords.size() ? m.chords[i + 1].first : m.chords[i].first + bpb;
            if (end - m.chords[i].first < 0.99 && ! clean.empty())
                continue;
            if (! clean.empty() && clean.back().second == m.chords[i].second)
                continue;
            clean.push_back (m.chords[i]);
        }
        m.chords.swap (clean);
    }

    std::vector<scriba::Note> out;
    std::vector<int> prev;
    for (size_t i = 0; i < m.chords.size(); ++i)
    {
        const auto& name = m.chords[i].second;
        if (name.empty())
            continue;
        const double startQn = m.chords[i].first;
        double endQn = i + 1 < m.chords.size() ? m.chords[i + 1].first : startQn + bpb;
        if (endQn <= startQn)
            continue;
        auto chord = m.fromBass ? scriba::chordOnBass (name, key) : scriba::parseChord (name);
        if (chord.intervals.empty())
            continue;
        chord = scriba::withType (chord, o.type, key);

        // voicing: close and inversions move smoothly from chord to chord; open / with bass as chosen
        // the next chord, for the left hand walking into it
        int nextRoot = -1;
        for (size_t j = i + 1; j < m.chords.size() && nextRoot < 0; ++j)
            if (! m.chords[j].second.empty())
            {
                const auto nc = m.fromBass ? scriba::chordOnBass (m.chords[j].second, key) : scriba::parseChord (m.chords[j].second);
                if (! nc.intervals.empty())
                    nextRoot = nc.root;
            }
        const bool piano = scriba::isPianoStyle (o.style);

        std::vector<int> v;
        if (piano)
            v = scriba::voiceSmooth (chord, prev, false);
        else if (o.voicing == scriba::ChordVoicing::Open)
            v = scriba::voice (chord, o.voicing);
        else
            v = scriba::voiceSmooth (chord, prev, o.voicing == scriba::ChordVoicing::WithBass);
        prev = v;

        // split at the bar lines, so the pattern starts again on every bar
        double a = startQn;
        while (a < endQn - 1.0e-6)
        {
            const double nextBar = (std::floor (a / bpb + 1.0e-6) + 1.0) * bpb;
            const double b = std::min (endQn, nextBar);
            const double phase = a - std::floor (a / bpb + 1.0e-6) * bpb;
            scriba::renderAny (out, chord, v, o.style, a * qSec, b - a, bpmNow, bpb, phase, b >= endQn - 1.0e-6 ? nextRoot : -1);
            a = b;
        }
    }
    std::sort (out.begin(), out.end(), [] (const scriba::Note& x, const scriba::Note& y) { return x.start < y.start; });

    scriba::MidiExportInfo mi;
    mi.bpm = bpmNow;
    mi.tsNum = tsNum.load() > 0 ? tsNum.load() : 4;
    mi.tsDen = tsDen.load() > 0 ? tsDen.load() : 4;
    if (key >= 0)
    {
        mi.keyTonic = key % 12;
        mi.keyMinor = key >= 12;
        mi.keyText = scriba::keyName (key);
    }
    mi.trackName = "Scriba - Chords";
    for (const auto& sec : m.sections)
        mi.markers.push_back ({ sec.first * qSec, sec.second });
    return scriba::buildMidiFile (out, mi);
}

void ScribaProcessor::previewChord (const scriba::ChordDef& c)
{
    // what the MIDI will play, up to 2 bars
    auto notes = renderChord (c);
    const double maxT = 2.0 * chordBeatsPerBar() * 60.0 / chordBpm();
    notes.erase (std::remove_if (notes.begin(), notes.end(), [maxT] (const scriba::Note& n) { return n.start >= maxT; }), notes.end());
    for (auto& n : notes)
        n.end = std::min (n.end, maxT);
    preview.play (notes);
}

juce::AudioProcessorEditor* ScribaProcessor::createEditor()
{
    return new ScribaEditor (*this);
}

//==============================================================================
// The results are saved with the project, so after reopening it the key, the TXT and the MIDI are still there
// (and the key is shared again with the Scriba on the snare).
void ScribaProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto st = apvts.copyState();
    st.removeChild (st.getChildWithName ("RESULTS"), nullptr);

    {
        const juce::ScopedLock sl (resultLock);
        if (analysisOk)
        {
            juce::MemoryOutputStream mo;
            mo.writeInt (4);   // format version
            mo.writeInt ((int) analysedInstrument);
            mo.writeFloat (analysedSensitivity);
            mo.writeBool (result.valid);
            for (float c : result.chroma)
                mo.writeFloat (c);
            mo.writeInt (result.best);
            mo.writeInt (result.second);
            mo.writeFloat (result.bestShare);
            mo.writeDouble (recBpm.load());
            mo.writeInt (recTsNum.load());
            mo.writeInt (recTsDen.load());
            mo.writeInt (recFirstBar.load());
            mo.writeDouble (recPhaseQn.load());
            mo.writeBool (recBarsFromHost.load());
            mo.writeBool (tempoVaries.load());
            mo.writeDouble (recordedSeconds.load());

            mo.writeInt ((int) notes.size());
            for (const auto& nt : notes)
            {
                mo.writeDouble (nt.start);
                mo.writeDouble (nt.end);
                mo.writeByte ((char) nt.midi);
                mo.writeByte ((char) nt.velocity);
            }
            mo.writeInt ((int) sections.size());
            for (const auto& s : sections)
            {
                mo.writeInt (s.startBar);
                mo.writeInt (s.endBar);
                mo.writeDouble (s.start);
                mo.writeDouble (s.end);
                mo.writeString (juce::String (s.label));
            }
            mo.writeInt ((int) overview.size());
            for (float v : overview)
                mo.writeFloat (v);

            const auto& p = drumResult.pitch;
            mo.writeInt ((int) drumResult.hits.size());
            for (const auto& h : drumResult.hits)
            {
                mo.writeDouble (h.time);
                mo.writeByte ((char) h.velocity);
            }
            mo.writeBool (p.valid);
            mo.writeDouble (p.hz);
            mo.writeInt (p.midi);
            mo.writeDouble (p.cents);
            mo.writeDouble (p.spreadCents);
            mo.writeFloat (p.confidence);
            mo.writeInt (p.hitsUsed);
            mo.writeFloat (drumResult.thresholdDb);
            mo.writeInt ((int) drumResult.detectorDb.size());
            for (float v : drumResult.detectorDb)
                mo.writeFloat (v);

            mo.writeDouble (recBarStartQn.load());
            mo.writeBool (lyrics.ran);
            mo.writeBool (lyrics.ok);
            mo.writeString (juce::String::fromUTF8 (lyrics.error.c_str()));
            mo.writeString (juce::String::fromUTF8 (lyrics.language.c_str()));
            mo.writeInt (lyricsLangUsed);
            mo.writeInt ((int) lyrics.lines.size());
            for (const auto& ln : lyrics.lines)
            {
                mo.writeInt ((int) ln.words.size());
                for (const auto& w : ln.words)
                {
                    mo.writeDouble (w.t0);
                    mo.writeDouble (w.t1);
                    mo.writeString (juce::String::fromUTF8 (w.text.c_str()));
                }
            }

            juce::ValueTree res ("RESULTS");
            res.setProperty ("data", mo.getMemoryBlock().toBase64Encoding(), nullptr);
            st.appendChild (res, nullptr);
        }
    }

    if (auto xml = st.createXml())
        copyXmlToBinary (*xml, destData);
}

void ScribaProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    auto tree = juce::ValueTree::fromXml (*xml);
    const auto res = tree.getChildWithName ("RESULTS");
    const juce::String encoded = res.isValid() ? res.getProperty ("data").toString() : juce::String();
    tree.removeChild (res, nullptr);
    apvts.replaceState (tree);

    if (encoded.isEmpty() || state.load() == (int) State::Recording || state.load() == (int) State::Analysing)
        return;

    juce::MemoryBlock mb;
    if (! mb.fromBase64Encoding (encoded))
        return;
    juce::MemoryInputStream in (mb, false);
    if (in.readInt() != 4)
        return;

    const auto inst = (scriba::Instrument) juce::jlimit (0, 5, in.readInt());
    const float sens = in.readFloat();
    const bool keyValid = in.readBool();
    std::array<float, 12> chroma {};
    for (float& c : chroma)
        c = in.readFloat();
    const int savedBest = in.readInt(), savedSecond = in.readInt();
    const float savedShare = in.readFloat();
    recBpm.store (in.readDouble());
    recTsNum.store (in.readInt());
    recTsDen.store (in.readInt());
    recFirstBar.store (in.readInt());
    recPhaseQn.store (in.readDouble());
    recBarsFromHost.store (in.readBool());
    tempoVaries.store (in.readBool());
    recordedSeconds.store (in.readDouble());

    auto count = [&in] (int maxItems) { return juce::jlimit (0, maxItems, in.readInt()); };

    std::vector<scriba::Note> nts ((size_t) count (200000));
    for (auto& nt : nts)
    {
        nt.start = in.readDouble();
        nt.end = in.readDouble();
        nt.midi = (int) (juce::uint8) in.readByte();
        nt.velocity = (int) (juce::uint8) in.readByte();
    }
    std::vector<scriba::Section> secs ((size_t) count (1000));
    for (auto& s : secs)
    {
        s.startBar = in.readInt();
        s.endBar = in.readInt();
        s.start = in.readDouble();
        s.end = in.readDouble();
        s.label = in.readString().toStdString();
    }
    std::vector<float> ov ((size_t) count (10000));
    for (float& v : ov)
        v = in.readFloat();

    scriba::DrumResult dr;
    dr.hits.resize ((size_t) count (100000));
    for (auto& h : dr.hits)
    {
        h.time = in.readDouble();
        h.velocity = (int) (juce::uint8) in.readByte();
    }
    dr.pitch.valid = in.readBool();
    dr.pitch.hz = in.readDouble();
    dr.pitch.midi = in.readInt();
    dr.pitch.cents = in.readDouble();
    dr.pitch.spreadCents = in.readDouble();
    dr.pitch.confidence = in.readFloat();
    dr.pitch.hitsUsed = in.readInt();
    dr.thresholdDb = in.readFloat();
    dr.detectorDb.resize ((size_t) count (10000));
    for (float& v : dr.detectorDb)
        v = in.readFloat();

    recBarStartQn.store (in.readDouble());
    scriba::LyricsResult lr;
    lr.ran = in.readBool();
    lr.ok = in.readBool();
    lr.error = in.readString().toStdString();
    lr.language = in.readString().toStdString();
    const int lrLang = in.readInt();
    lr.lines.resize ((size_t) count (20000));
    for (auto& ln : lr.lines)
    {
        ln.words.resize ((size_t) count (2000));
        for (auto& w : ln.words)
        {
            w.t0 = in.readDouble();
            w.t1 = in.readDouble();
            w.text = in.readString().toStdString();
        }
        if (! ln.words.empty())
        {
            ln.t0 = ln.words.front().t0;
            ln.t1 = ln.words.back().t1;
        }
    }

    if (in.isExhausted() && in.getPosition() > in.getTotalLength())
        return;   // truncated data

    {
        const juce::ScopedLock sl (resultLock);
        result = keyValid ? scriba::KeyDetector::fromChroma (chroma) : scriba::KeyResult();
        if (result.valid && savedBest >= 0 && savedBest < 24 && savedSecond >= 0 && savedSecond < 24)
        {
            result.best = savedBest;
            result.second = savedSecond;
            result.relativeOfBest = scriba::KeyDetector::relativeOf (savedBest);
            result.bestShare = juce::jlimit (0.0f, 1.0f, savedShare);
            result.secondShare = 1.0f - result.bestShare;
        }
        notes = std::move (nts);
        sections = std::move (secs);
        overview = std::move (ov);
        drumResult = std::move (dr);
        analysedInstrument = inst;
        analysedSensitivity = sens;
        analysisOk = true;
        lyrics = std::move (lr);
        lyricsLangUsed = lrLang;
        lyricsCapture = -1;
    }
    ++resultsVersion;
    writeIndex.store (0);   // the audio itself is not saved: Analyze again to change instrument / sensitivity
    state.store ((int) State::Done);
    publishKey();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ScribaProcessor();
}
