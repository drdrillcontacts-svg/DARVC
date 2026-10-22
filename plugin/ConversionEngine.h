#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <atomic>
#include <functional>

/** Everything the worker needs for one conversion (mirrors job.schema). */
struct DarvcJob
{
    juce::File inputFile;
    juce::File outputFile;
    juce::File modelFile;
    juce::File indexFile;    // may be invalid -> worker auto-detects a sibling
    juce::File modelsDir;
    int pitch = 0;
    juce::String f0Method { "rmvpe" };
    double indexRate = 0.75;
    double protect = 0.33;
    juce::String device { "auto" };       // auto | cpu | cuda | mps
    juce::String modelVersion { "v2" };   // v1 | v2

    [[nodiscard]] juce::String toJSON(const juce::String& jobId) const;
};

/** Runs the Python worker (conversion, environment check, one-click engine
    install) on a background thread. Inference must never happen on the
    audio or message thread, and a conversion must survive the editor being
    closed, which is why this is owned by the processor.

    State is readable from any thread through the atomic/locked accessors;
    `onChange` is invoked on the message thread whenever something changes.
*/
class ConversionEngine final : private juce::Thread
{
public:
    ConversionEngine();
    ~ConversionEngine() override;

    // ------------------------------------------------------------------
    // Paths (all created lazily by ensureDirectories()/ensureWorkerScript())
    static juce::File appDataDir();
    static juce::File defaultModelsDir();
    static juce::File defaultLibraryDir();
    static juce::File managedVenvDir();
    static juce::File managedPython();
    static juce::File bundledWorkerScript();

    static void ensureDirectories();
    /** Writes the embedded worker.py to disk if its contents changed. */
    static void ensureWorkerScript();

    // ------------------------------------------------------------------
    /** Optional interpreter chosen in Settings; empty means "auto-detect". */
    void setPythonOverride(juce::File newPython);
    [[nodiscard]] juce::File getPythonOverride() const { return pythonOverride; }

    /** Best interpreter to run the worker with right now, or an invalid
        File if no suitable Python could be found. */
    [[nodiscard]] juce::File findPython() const;
    /** A system Python suitable for creating the managed venv. */
    [[nodiscard]] juce::File findSystemPython() const;

    // ------------------------------------------------------------------
    bool startConversion(const DarvcJob& job);
    bool startCheck();
    /** Creates/updates the managed venv and installs torch + rvc-python. */
    bool startInstall(bool useCuda);
    void cancel();

    [[nodiscard]] bool isBusy() const { return busy.load(); }
    [[nodiscard]] int getProgressPercent() const { return progressPercent.load(); }
    [[nodiscard]] juce::String getStatusMessage() const;
    [[nodiscard]] juce::String getLastMessage() const;
    [[nodiscard]] juce::File getLastOutput() const;
    [[nodiscard]] juce::var getLastCheckInfo() const;
    [[nodiscard]] juce::StringArray getRecentLog() const;

    /** Called on the message thread after any state change. Assign a
        lambda that captures a Component::SafePointer to stay crash-safe. */
    std::function<void()> onChange;

private:
    enum class Task { convert, check, install };

    void run() override;
    void runConversion();
    void runCheck(const juce::File& python);
    void runInstall(bool useCuda);

    /** Launches a process, streaming each output line to handleLine.
        Returns the exit code, or -1 if it could not be started/killed. */
    int runStreaming(const juce::StringArray& args,
                     const std::function<void(const juce::String&)>& handleLine);

    void setStatus(const juce::String& message, int percent);
    void appendLog(const juce::String& line);
    void finish(bool success, const juce::String& message, juce::File output = {});
    void notify();

    juce::File pythonOverride;

    std::atomic<bool> busy { false };
    std::atomic<bool> cancelRequested { false };
    std::atomic<int> progressPercent { -1 };

    juce::CriticalSection stateLock;
    Task currentTask = Task::convert;
    DarvcJob pendingJob;
    bool pendingUseCuda = false;
    juce::String statusMessage;
    juce::String lastMessage;
    juce::File lastOutput;
    juce::var lastCheckInfo;
    juce::StringArray recentLog;

    juce::CriticalSection childLock;
    juce::ChildProcess child;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ConversionEngine)
};
