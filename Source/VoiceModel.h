// Scriba - by Meteora Plugins
// Copyright (c) 2026 Meteora Plugins. All rights reserved.
// Proprietary and confidential: copying, distributing or modifying this file, in whole or in part,
// by any means, without the written permission of the copyright holder is prohibited. See LICENSE.txt.

#pragma once

// The Whisper model used by VOCALS mode: where it lives, and its download.
//
// One copy for all the Scriba instances and all the projects:
//   macOS    ~/Library/Application Support/Meteora Plugins/Scriba/Models
//   Windows  %APPDATA%\Meteora Plugins\Scriba\Models
//   Linux    ~/.config/Meteora Plugins/Scriba/Models
// Any whisper.cpp "ggml-*.bin" model put in that folder by hand works too (the large-v3-turbo one is preferred).

#include <atomic>
#include <juce_core/juce_core.h>
#include "LyricsTranscriber.h"

namespace scriba
{
class VoiceModel : private juce::URL::DownloadTaskListener
{
public:
    enum class Status { Missing = 0, Downloading, Ready, Failed };

    static constexpr const char* kFileName = "ggml-large-v3-turbo-q5_0.bin";
    static constexpr const char* kUrl = "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-large-v3-turbo-q5_0.bin";
    static constexpr juce::int64 kApproxBytes = 574'000'000;

    static VoiceModel& get()
    {
        static VoiceModel instance;
        return instance;
    }

    static juce::File folder()
    {
       #if JUCE_MAC
        auto base = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Library/Application Support");
       #else
        auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
       #endif
        return base.getChildFile ("Meteora Plugins").getChildFile ("Scriba").getChildFile ("Models");
    }

    // the model to use: the preferred one, or any other valid whisper model in the folder
    juce::File modelFile() const
    {
        const auto preferred = folder().getChildFile (kFileName);
        if (LyricsTranscriber::looksLikeModel (preferred.getFullPathName().toStdString()))
            return preferred;
        for (const auto& f : folder().findChildFiles (juce::File::findFiles, false, "ggml-*.bin"))
            if (LyricsTranscriber::looksLikeModel (f.getFullPathName().toStdString()))
                return f;
        return {};
    }

    bool isInstalled() const { return modelFile().existsAsFile(); }

    Status getStatus() const
    {
        if (downloading.load())
            return Status::Downloading;
        if (isInstalled())
            return Status::Ready;
        return failed.load() ? Status::Failed : Status::Missing;
    }

    float getProgress() const { return progressValue.load(); }
    juce::String getError() const { const juce::ScopedLock sl (lock); return error; }

    void startDownload()
    {
        const juce::ScopedLock sl (lock);
        if (downloading.load() || isInstalled())
            return;
        folder().createDirectory();
        partFile = folder().getChildFile (juce::String (kFileName) + ".part");
        partFile.deleteFile();
        failed.store (false);
        error.clear();
        progressValue.store (0.0f);
        downloading.store (true);
        task = juce::URL (kUrl).downloadToFile (partFile, juce::URL::DownloadTaskOptions().withListener (this));
        if (task == nullptr)
        {
            downloading.store (false);
            failed.store (true);
            error = "the download could not start (no internet?)";
        }
    }

    void cancelDownload()
    {
        std::unique_ptr<juce::URL::DownloadTask> t;
        {
            const juce::ScopedLock sl (lock);
            t = std::move (task);   // destroyed outside the lock: its thread may be waiting for it
        }
        t.reset();
        downloading.store (false);
        const juce::ScopedLock sl (lock);
        partFile.deleteFile();
    }

private:
    VoiceModel() = default;
    ~VoiceModel() override
    {
        task.reset();
    }

    void progress (juce::URL::DownloadTask*, juce::int64 done, juce::int64 total) override
    {
        const double t = total > 0 ? (double) total : (double) kApproxBytes;
        progressValue.store ((float) juce::jlimit (0.0, 1.0, (double) done / t));
    }

    void finished (juce::URL::DownloadTask* t, bool success) override
    {
        const juce::ScopedLock sl (lock);
        const bool httpOk = t == nullptr || t->statusCode() == 0 || (t->statusCode() >= 200 && t->statusCode() < 300);
        const auto target = folder().getChildFile (kFileName);
        if (success && httpOk && partFile.getSize() > 100'000'000
            && LyricsTranscriber::looksLikeModel (partFile.getFullPathName().toStdString()) && partFile.moveFileTo (target))
        {
            failed.store (false);
            progressValue.store (1.0f);
        }
        else
        {
            partFile.deleteFile();
            failed.store (true);
            error = "download failed" + (t != nullptr && t->statusCode() > 0 ? " (HTTP " + juce::String (t->statusCode()) + ")" : juce::String())
                    + ": check the internet connection and try again";
        }
        downloading.store (false);
    }

    mutable juce::CriticalSection lock;
    std::unique_ptr<juce::URL::DownloadTask> task;
    juce::File partFile;
    juce::String error;
    std::atomic<bool> downloading { false }, failed { false };
    std::atomic<float> progressValue { 0.0f };
};
} // namespace scriba
