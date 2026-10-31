// Scriba - by Meteora Plugins
// Copyright (c) 2026 Meteora Plugins. All rights reserved.
// Proprietary and confidential: copying, distributing or modifying this file, in whole or in part,
// by any means, without the written permission of the copyright holder is prohibited. See LICENSE.txt.

#include "LyricsTranscriber.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <thread>

#include "whisper.h"

namespace scriba
{
namespace
{
constexpr double kWhisperRate = 16000.0;

// ---------------------------------------------------------------- audio preparation
std::vector<float> to16k (const float* x, size_t n, double sr)
{
    // 2 x 2nd order low-pass at 7 kHz (anti-aliasing), then linear interpolation
    std::vector<float> y (x, x + n);
    if (sr > kWhisperRate * 1.01)
    {
        const double w0 = 2.0 * 3.14159265358979323846 * std::min (7000.0, sr * 0.45) / sr;
        const double alpha = std::sin (w0) / (2.0 * 0.70710678), c = std::cos (w0), a0 = 1.0 + alpha;
        const double b0 = (1.0 - c) * 0.5 / a0, b1 = (1.0 - c) / a0, b2 = b0, a1 = -2.0 * c / a0, a2 = (1.0 - alpha) / a0;
        for (int pass = 0; pass < 2; ++pass)
        {
            double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
            for (auto& v : y)
            {
                const double o = b0 * v + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
                x2 = x1; x1 = v; y2 = y1; y1 = o;
                v = (float) o;
            }
        }
    }
    const double ratio = sr / kWhisperRate;
    const size_t m = (size_t) ((double) n / ratio);
    std::vector<float> out (m);
    for (size_t i = 0; i < m; ++i)
    {
        const double p = (double) i * ratio;
        const size_t k = (size_t) p;
        const float f = (float) (p - (double) k);
        out[i] = k + 1 < n ? y[k] + (y[k + 1] - y[k]) * f : y[std::min (k, n - 1)];
    }
    return out;
}

// voice activity, one value every 20 ms
std::vector<char> voiceMask (const std::vector<float>& a, size_t hop)
{
    const size_t frames = a.size() / hop;
    std::vector<float> db (frames);
    for (size_t f = 0; f < frames; ++f)
    {
        double e = 0.0;
        for (size_t i = 0; i < hop; ++i)
            e += (double) a[f * hop + i] * a[f * hop + i];
        db[f] = (float) (10.0 * std::log10 (e / (double) hop + 1.0e-12));
    }
    std::vector<float> s (db);
    std::sort (s.begin(), s.end());
    const float loud = s.empty() ? -60.0f : s[(size_t) ((double) (s.size() - 1) * 0.95)];
    const float thr = std::max (-62.0f, loud - 32.0f);
    std::vector<char> v (frames, 0);
    for (size_t f = 0; f < frames; ++f)
        v[f] = db[f] > thr ? 1 : 0;
    // close short holes (consonants, breaths) and remove short blips
    const size_t hole = 15, blip = 5;   // 300 ms, 100 ms
    for (size_t f = 0; f < frames;)
    {
        if (v[f]) { ++f; continue; }
        size_t g = f;
        while (g < frames && ! v[g]) ++g;
        if (f > 0 && g < frames && g - f <= hole)
            std::fill (v.begin() + (long) f, v.begin() + (long) g, 1);
        f = g;
    }
    for (size_t f = 0; f < frames;)
    {
        if (! v[f]) { ++f; continue; }
        size_t g = f;
        while (g < frames && v[g]) ++g;
        if (g - f < blip)
            std::fill (v.begin() + (long) f, v.begin() + (long) g, 0);
        f = g;
    }
    return v;
}

std::string lower (std::string s)
{
    for (auto& c : s)
        c = (char) std::tolower ((unsigned char) c);
    return s;
}

std::string trim (const std::string& s)
{
    size_t a = 0, b = s.size();
    while (a < b && std::isspace ((unsigned char) s[a])) ++a;
    while (b > a && std::isspace ((unsigned char) s[b - 1])) --b;
    return s.substr (a, b - a);
}

// typical phrases that Whisper invents on music / silence (it learned them from subtitles)
bool isKnownHallucination (const std::string& text)
{
    static const char* bad[] = { "amara.org", "sottotitoli", "sottotitolato", "thanks for watching", "thank you for watching",
                                 "grazie per la visione", "grazie a tutti per la visione", "iscriviti", "subscribe", "www.", "http",
                                 "copyright", "transcription by", "subtitles by", "a cura di qtss" };
    const std::string t = lower (text);
    for (const char* b : bad)
        if (t.find (b) != std::string::npos)
            return true;
    return false;
}

// removes [Music], (applause), music notes ... from a word
std::string cleanWord (std::string w)
{
    std::string out;
    int depth = 0;
    for (size_t i = 0; i < w.size(); ++i)
    {
        const char c = w[i];
        if (c == '[' || c == '(' || c == '{') { ++depth; continue; }
        if (c == ']' || c == ')' || c == '}') { depth = std::max (0, depth - 1); continue; }
        // UTF-8 music notes: E2 99 AA / E2 99 AB / E2 99 AC
        if ((unsigned char) c == 0xE2 && i + 2 < w.size() && (unsigned char) w[i + 1] == 0x99)
        {
            i += 2;
            continue;
        }
        if (depth == 0)
            out += c;
    }
    return trim (out);
}

void silentLog (ggml_log_level, const char*, void*) {}

struct Callbacks
{
    LyricsTranscriber::ProgressFn progress;
    LyricsTranscriber::CancelFn cancelled;
    float from = 0.0f, to = 1.0f;
};
} // namespace

bool LyricsTranscriber::looksLikeModel (const std::string& path)
{
    FILE* f = std::fopen (path.c_str(), "rb");
    if (f == nullptr)
        return false;
    uint32_t magic = 0;
    const size_t got = std::fread (&magic, 1, 4, f);
    std::fclose (f);
    return got == 4 && magic == 0x67676d6c;   // "ggml"
}

LyricsResult LyricsTranscriber::transcribe (const float* x, size_t n, double sr, const std::string& modelPath,
                                            LyricsLanguage language, const ProgressFn& progress, const CancelFn& cancelled)
{
    LyricsResult res;
    res.ran = true;
    if (x == nullptr || n == 0 || sr <= 0.0)
    {
        res.error = "no audio";
        return res;
    }
    if (! looksLikeModel (modelPath))
    {
        res.error = "voice model missing";
        return res;
    }

    // 1. 16 kHz, steady level
    std::vector<float> a = to16k (x, n, sr);
    const size_t hop = 320;   // 20 ms
    std::vector<char> voiced = voiceMask (a, hop);
    {
        double e = 0.0;
        size_t cnt = 0;
        for (size_t f = 0; f < voiced.size(); ++f)
            if (voiced[f])
                for (size_t i = 0; i < hop; ++i)
                {
                    e += (double) a[f * hop + i] * a[f * hop + i];
                    ++cnt;
                }
        const double rms = cnt > 0 ? std::sqrt (e / (double) cnt) : 0.0;
        if (rms <= 1.0e-5)
        {
            res.ok = true;   // nothing sung: empty result, not an error
            return res;
        }
        const float gain = (float) std::min (30.0, 0.1 / rms);   // about -20 dBFS on the singing
        for (auto& v : a)
            v = std::max (-1.0f, std::min (1.0f, v * gain));
    }
    // 2. silence where there is no voice (with 50 ms fades)
    {
        std::vector<float> g (a.size(), 1.0f);
        for (size_t f = 0; f < voiced.size(); ++f)
            if (! voiced[f])
                std::fill (g.begin() + (long) (f * hop), g.begin() + (long) ((f + 1) * hop), 0.0f);
        // fades of 50 ms that stay inside the silent parts (forward and backward ramps, take the larger)
        const float step = 1.0f / 800.0f;
        std::vector<float> fwd (g.size()), bwd (g.size());
        float cur = 1.0f;
        for (size_t i = 0; i < g.size(); ++i)
        {
            cur = g[i] > 0.5f ? 1.0f : std::max (0.0f, cur - step);
            fwd[i] = cur;
        }
        cur = 1.0f;
        for (size_t i = g.size(); i-- > 0;)
        {
            cur = g[i] > 0.5f ? 1.0f : std::max (0.0f, cur - step);
            bwd[i] = cur;
        }
        for (size_t i = 0; i < a.size() && i < g.size(); ++i)
            a[i] *= std::max (fwd[i], bwd[i]);
    }
    auto voicedFraction = [&] (double t0, double t1)
    {
        const long f0 = std::max (0L, (long) (t0 * kWhisperRate / (double) hop));
        const long f1 = std::min ((long) voiced.size(), (long) std::ceil (t1 * kWhisperRate / (double) hop));
        if (f1 <= f0)
            return 0.0;
        long on = 0;
        for (long f = f0; f < f1; ++f)
            on += voiced[(size_t) f];
        return (double) on / (double) (f1 - f0);
    };

    if (cancelled && cancelled())
        return res;
    if (progress)
        progress (0.03f);

    // 3. model
    whisper_log_set (silentLog, nullptr);
    whisper_context_params cparams = whisper_context_default_params();
    cparams.use_gpu = true;
    whisper_context* ctx = whisper_init_from_file_with_params (modelPath.c_str(), cparams);
    if (ctx == nullptr)
    {
        res.error = "the voice model could not be loaded (file damaged? download it again)";
        return res;
    }
    if (progress)
        progress (0.08f);

    const int threads = (int) std::max (1u, std::min (8u, std::thread::hardware_concurrency() > 1 ? std::thread::hardware_concurrency() - 1 : 1u));

    // language: the more likely of Italian / English, measured on the 30 s with the most singing
    std::string lang = language == LyricsLanguage::English ? "en" : "it";
    if (language == LyricsLanguage::Auto)
    {
        const size_t win = (size_t) (30.0 * kWhisperRate);
        size_t bestStart = 0, bestVoiced = 0;
        for (size_t s = 0; s + hop < a.size(); s += (size_t) (5.0 * kWhisperRate))
        {
            size_t on = 0;
            for (size_t f = s / hop; f < std::min (voiced.size(), (s + win) / hop); ++f)
                on += (size_t) voiced[f];
            if (on > bestVoiced)
            {
                bestVoiced = on;
                bestStart = s;
            }
        }
        const size_t len = std::min (win, a.size() - bestStart);
        if (whisper_pcm_to_mel (ctx, a.data() + bestStart, (int) len, threads) == 0)
        {
            std::vector<float> probs ((size_t) whisper_lang_max_id() + 1, 0.0f);
            if (whisper_lang_auto_detect (ctx, 0, threads, probs.data()) >= 0)
            {
                const float pIt = probs[(size_t) whisper_lang_id ("it")], pEn = probs[(size_t) whisper_lang_id ("en")];
                lang = pEn > pIt ? "en" : "it";
            }
        }
    }
    res.language = lang == "en" ? "English" : "Italian";

    // 4. transcription
    whisper_full_params p = whisper_full_default_params (WHISPER_SAMPLING_BEAM_SEARCH);
    p.beam_search.beam_size = 5;
    p.n_threads = threads;
    p.language = lang == "en" ? "en" : "it";
    p.detect_language = false;
    p.translate = false;
    p.no_context = true;            // no conditioning on the previous text: avoids endless repeated lines
    p.token_timestamps = true;
    p.suppress_nst = true;
    p.print_progress = false;
    p.print_realtime = false;
    p.print_timestamps = false;
    p.print_special = false;
    p.no_speech_thold = 0.6f;

    Callbacks cb { progress, cancelled, 0.10f, 1.0f };
    p.progress_callback = [] (whisper_context*, whisper_state*, int pct, void* user)
    {
        auto* c = static_cast<Callbacks*> (user);
        if (c->progress)
            c->progress (c->from + (c->to - c->from) * (float) pct / 100.0f);
    };
    p.progress_callback_user_data = &cb;
    p.abort_callback = [] (void* user) { auto* c = static_cast<Callbacks*> (user); return c->cancelled && c->cancelled(); };
    p.abort_callback_user_data = &cb;

    if (whisper_full (ctx, p, a.data(), (int) a.size()) != 0)
    {
        whisper_free (ctx);
        if (! (cancelled && cancelled()))
            res.error = "the transcription failed";
        return res;
    }

    // 5. words with their times
    struct Seg { double t0, t1; std::vector<LyricWord> words; std::string text; };
    std::vector<Seg> segs;
    const whisper_token eot = whisper_token_eot (ctx);
    const int ns = whisper_full_n_segments (ctx);
    for (int i = 0; i < ns; ++i)
    {
        Seg s;
        s.t0 = (double) whisper_full_get_segment_t0 (ctx, i) * 0.01;
        s.t1 = (double) whisper_full_get_segment_t1 (ctx, i) * 0.01;
        s.text = trim (whisper_full_get_segment_text (ctx, i));
        const float noSpeech = whisper_full_get_segment_no_speech_prob (ctx, i);
        const double vf = voicedFraction (s.t0, s.t1);
        if (s.text.empty() || isKnownHallucination (s.text) || vf < 0.2 || (noSpeech > 0.6f && vf < 0.5))
            continue;

        LyricWord cur;
        bool open = false;
        const int nt = whisper_full_n_tokens (ctx, i);
        for (int k = 0; k < nt; ++k)
        {
            const whisper_token_data d = whisper_full_get_token_data (ctx, i, k);
            if (d.id >= eot)
                continue;   // special and timestamp tokens
            const std::string tt = whisper_full_get_token_text (ctx, i, k);
            if (tt.empty())
                continue;
            const bool startsWord = std::isspace ((unsigned char) tt[0]) != 0;
            if (startsWord || ! open)
            {
                if (open)
                {
                    cur.text = cleanWord (cur.text);
                    if (! cur.text.empty())
                        s.words.push_back (cur);
                }
                cur = LyricWord();
                cur.t0 = (double) d.t0 * 0.01;
                open = true;
            }
            cur.text += tt;
            cur.t1 = (double) d.t1 * 0.01;
        }
        if (open)
        {
            cur.text = cleanWord (cur.text);
            if (! cur.text.empty())
                s.words.push_back (cur);
        }
        // keep the word times inside the segment and in order
        double last = s.t0;
        for (auto& w : s.words)
        {
            w.t0 = std::max (last, std::min (w.t0, s.t1));
            w.t1 = std::max (w.t0, std::min (std::max (w.t1, w.t0), s.t1));
            last = w.t0;
        }
        if (! s.words.empty())
            segs.push_back (std::move (s));
    }
    whisper_free (ctx);

    // endless loops: the same sentence more than 3 times in a row is an invention
    {
        std::vector<Seg> kept;
        int run = 0;
        for (auto& s : segs)
        {
            run = (! kept.empty() && lower (kept.back().text) == lower (s.text)) ? run + 1 : 0;
            if (run < 3)
                kept.push_back (std::move (s));
        }
        segs.swap (kept);
    }

    // 6. lyric lines: a new line on a pause of the singer, or when the line gets long
    for (const auto& s : segs)
    {
        LyricLine line;
        size_t chars = 0;
        auto flush = [&]
        {
            if (line.words.empty())
                return;
            line.t0 = line.words.front().t0;
            line.t1 = line.words.back().t1;
            // lyrics style: no full stop / comma at the end of the line, capital first letter
            auto& lastW = line.words.back().text;
            while (! lastW.empty() && (lastW.back() == '.' || lastW.back() == ','))
                lastW.pop_back();
            auto& firstW = line.words.front().text;
            if (! firstW.empty() && std::islower ((unsigned char) firstW[0]))
                firstW[0] = (char) std::toupper ((unsigned char) firstW[0]);
            if (! line.words.back().text.empty() || line.words.size() > 1)
                res.lines.push_back (line);
            line = LyricLine();
            chars = 0;
        };
        for (size_t k = 0; k < s.words.size(); ++k)
        {
            const auto& w = s.words[k];
            if (! line.words.empty())
            {
                const double gap = w.t0 - line.words.back().t1;
                const char endCh = line.words.back().text.empty() ? ' ' : line.words.back().text.back();
                const bool sentenceEnd = endCh == '.' || endCh == '?' || endCh == '!';
                if (gap > 0.55 || (chars > 30 && (sentenceEnd || endCh == ',')) || chars > 46)
                    flush();
            }
            line.words.push_back (w);
            chars += w.text.size() + 1;
        }
        flush();
    }

    if (progress)
        progress (1.0f);
    res.ok = true;
    return res;
}
} // namespace scriba
