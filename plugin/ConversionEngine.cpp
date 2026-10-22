#include "ConversionEngine.h"

#include <BinaryData.h>

#include <utility>

namespace
{
    [[nodiscard]] bool isExecutableFile(const juce::File& f)
    {
        return f.existsAsFile();
    }

    [[nodiscard]] juce::File searchOnPath(const juce::StringArray& names)
    {
        const auto pathVar = juce::SystemStats::getEnvironmentVariable("PATH", {});
       #if JUCE_WINDOWS
        const auto separator = ";";
       #else
        const auto separator = ":";
       #endif
        const auto dirs = juce::StringArray::fromTokens(pathVar, separator, {});
        for (const auto& dir : dirs)
            for (const auto& name : names)
                if (auto candidate = juce::File(dir).getChildFile(name); isExecutableFile(candidate))
                    return candidate;
        return {};
    }
} // namespace

//==============================================================================
juce::String DarvcJob::toJSON(const juce::String& jobId) const
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty("version", 1);
    obj->setProperty("job_id", jobId);
    obj->setProperty("input", inputFile.getFullPathName());
    obj->setProperty("output", outputFile.getFullPathName());
    obj->setProperty("model", modelFile.getFullPathName());
    obj->setProperty("index", indexFile.existsAsFile() ? indexFile.getFullPathName() : juce::String{});
    obj->setProperty("models_dir", modelsDir.getFullPathName());
    obj->setProperty("pitch", pitch);
    obj->setProperty("f0_method", f0Method);
    obj->setProperty("index_rate", indexRate);
    obj->setProperty("protect", protect);
    obj->setProperty("device", device);
    obj->setProperty("model_version", modelVersion);
    return juce::JSON::toString(juce::var(obj));
}

//==============================================================================
ConversionEngine::ConversionEngine() : juce::Thread("DARVC conversion")
{
    ensureDirectories();
    ensureWorkerScript();
}

ConversionEngine::~ConversionEngine()
{
    onChange = nullptr;
    cancel();
    stopThread(4000);
}

//==============================================================================
juce::File ConversionEngine::appDataDir()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("DARVC");
}

juce::File ConversionEngine::defaultModelsDir()  { return appDataDir().getChildFile("Models"); }
juce::File ConversionEngine::defaultLibraryDir() { return appDataDir().getChildFile("Library"); }
juce::File ConversionEngine::managedVenvDir()    { return appDataDir().getChildFile("engine"); }

juce::File ConversionEngine::managedPython()
{
   #if JUCE_WINDOWS
    return managedVenvDir().getChildFile("Scripts").getChildFile("python.exe");
   #else
    return managedVenvDir().getChildFile("bin").getChildFile("python3");
   #endif
}

juce::File ConversionEngine::bundledWorkerScript()
{
    return appDataDir().getChildFile("worker").getChildFile("worker.py");
}

void ConversionEngine::ensureDirectories()
{
    for (auto dir : { appDataDir(), defaultModelsDir(), defaultLibraryDir(),
                      appDataDir().getChildFile("jobs"),
                      appDataDir().getChildFile("worker") })
        dir.createDirectory();
}

void ConversionEngine::ensureWorkerScript()
{
    const auto target = bundledWorkerScript();
    target.getParentDirectory().createDirectory();

    juce::MemoryBlock existing;
    if (target.existsAsFile() && target.loadFileAsData(existing)
        && existing.getSize() == static_cast<size_t>(BinaryData::worker_pySize)
        && std::memcmp(existing.getData(), BinaryData::worker_py, existing.getSize()) == 0)
        return; // already up to date

    target.replaceWithData(BinaryData::worker_py, static_cast<size_t>(BinaryData::worker_pySize));
}

//==============================================================================
void ConversionEngine::setPythonOverride(juce::File newPython)
{
    pythonOverride = std::move(newPython);
}

juce::File ConversionEngine::findPython() const
{
    if (isExecutableFile(pythonOverride))
        return pythonOverride;
    if (auto managed = managedPython(); isExecutableFile(managed))
        return managed;
    return findSystemPython();
}

juce::File ConversionEngine::findSystemPython() const
{
   #if JUCE_WINDOWS
    if (auto found = searchOnPath({ "python.exe", "python3.exe", "py.exe" }); found.existsAsFile())
        return found;
    // Common per-user install location that may not be on the DAW's PATH.
    const auto localAppData = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                                  .getParentDirectory().getChildFile("Local");
    for (const auto* version : { "Python312", "Python311", "Python310" })
    {
        const auto candidate = localAppData.getChildFile("Programs")
                                   .getChildFile("Python").getChildFile(version)
                                   .getChildFile("python.exe");
        if (isExecutableFile(candidate))
            return candidate;
    }
   #else
    for (const auto* fixed : { "/opt/homebrew/bin/python3", "/usr/local/bin/python3",
                               "/Library/Frameworks/Python.framework/Versions/3.11/bin/python3",
                               "/Library/Frameworks/Python.framework/Versions/3.10/bin/python3",
                               "/usr/bin/python3" })
        if (juce::File candidate(fixed); isExecutableFile(candidate))
            return candidate;
    if (auto found = searchOnPath({ "python3", "python" }); found.existsAsFile())
        return found;
   #endif
    return {};
}

//==============================================================================
bool ConversionEngine::startConversion(const DarvcJob& job)
{
    if (busy.load())
        return false;
    {
        const juce::ScopedLock sl(stateLock);
        currentTask = Task::convert;
        pendingJob = job;
        lastOutput = juce::File{};
    }
    cancelRequested = false;
    progressPercent = 0;
    busy = true;
    waitForThreadToExit(3000); // make sure a just-finished task fully exited
    startThread();
    notify();
    return true;
}

bool ConversionEngine::startCheck()
{
    if (busy.load())
        return false;
    {
        const juce::ScopedLock sl(stateLock);
        currentTask = Task::check;
    }
    cancelRequested = false;
    busy = true;
    waitForThreadToExit(3000); // make sure a just-finished task fully exited
    startThread();
    notify();
    return true;
}

bool ConversionEngine::startInstall(bool useCuda)
{
    if (busy.load())
        return false;
    {
        const juce::ScopedLock sl(stateLock);
        currentTask = Task::install;
        pendingUseCuda = useCuda;
        recentLog.clear();
    }
    cancelRequested = false;
    progressPercent = 0;
    busy = true;
    waitForThreadToExit(3000); // make sure a just-finished task fully exited
    startThread();
    notify();
    return true;
}

void ConversionEngine::cancel()
{
    cancelRequested = true;
    const juce::ScopedLock sl(childLock);
    if (child.isRunning())
        child.kill();
}

//==============================================================================
void ConversionEngine::run()
{
    Task task;
    {
        const juce::ScopedLock sl(stateLock);
        task = currentTask;
    }

    switch (task)
    {
        case Task::convert: runConversion(); break;
        case Task::check:
            if (auto python = findPython(); python.existsAsFile())
                runCheck(python);
            else
                finish(false, "Python was not found. Install Python 3.10-3.12 from python.org, "
                              "then run 'Install / Repair Engine'.");
            break;
        case Task::install: runInstall(pendingUseCuda); break;
    }
}

int ConversionEngine::runStreaming(const juce::StringArray& args,
                                   const std::function<void(const juce::String&)>& handleLine)
{
    {
        const juce::ScopedLock sl(childLock);
        if (!child.start(args, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
            return -1;
    }

    juce::String pending;
    char buffer[1024];
    for (;;)
    {
        if (cancelRequested.load() || threadShouldExit())
        {
            const juce::ScopedLock sl(childLock);
            child.kill();
            break;
        }

        // Not guarded by childLock: this blocks until output arrives, and
        // cancel() must be able to kill() the process meanwhile (which makes
        // this call return). JUCE buffers child output internally, so a
        // verbose process cannot deadlock on a full pipe either.
        const auto numRead = child.readProcessOutput(buffer, static_cast<int>(sizeof(buffer)));
        if (numRead <= 0)
            break;

        pending += juce::String::fromUTF8(buffer, numRead);
        int newline = -1;
        while ((newline = pending.indexOf("\n")) >= 0)
        {
            auto line = pending.substring(0, newline).trim();
            pending = pending.substring(newline + 1);
            if (line.isNotEmpty())
                handleLine(line);
        }
    }
    if (pending.trim().isNotEmpty())
        handleLine(pending.trim());

    const juce::ScopedLock sl(childLock);
    child.waitForProcessToFinish(2000);
    return static_cast<int>(child.getExitCode());
}

//==============================================================================
void ConversionEngine::runConversion()
{
    DarvcJob job;
    {
        const juce::ScopedLock sl(stateLock);
        job = pendingJob;
    }

    const auto python = findPython();
    if (!python.existsAsFile())
    {
        finish(false, "Python was not found. Open Settings and use 'Install / Repair Engine' "
                      "(requires Python 3.10-3.12 from python.org).");
        return;
    }

    ensureWorkerScript();
    const auto jobId = juce::Uuid().toDashedString();
    const auto jobDir = appDataDir().getChildFile("jobs").getChildFile(jobId);
    jobDir.createDirectory();
    const auto jobFile = jobDir.getChildFile("job.json");
    jobFile.replaceWithText(job.toJSON(jobId));

    setStatus("Starting RVC worker...", 2);

    juce::String errorMessage;
    juce::String errorCode;
    bool sawDone = false;

    const auto exitCode = runStreaming(
        { python.getFullPathName(), "-u", bundledWorkerScript().getFullPathName(),
          jobFile.getFullPathName() },
        [&](const juce::String& line) {
            appendLog(line);
            const auto parsed = juce::JSON::parse(line);
            if (!parsed.isObject())
            {
                setStatus(line, progressPercent.load());
                return;
            }
            const auto state = parsed.getProperty("state", {}).toString();
            const auto message = parsed.getProperty("message", {}).toString();
            if (state == "progress")
                setStatus(message, static_cast<int>(parsed.getProperty("percent", -1)));
            else if (state == "done")
            {
                sawDone = true;
                setStatus(message.isNotEmpty() ? message : juce::String("Conversion complete."), 100);
            }
            else if (state == "error")
            {
                errorMessage = message;
                errorCode = parsed.getProperty("code", {}).toString();
            }
        });

    if (cancelRequested.load())
    {
        job.outputFile.deleteFile();
        finish(false, "Conversion cancelled.");
    }
    else if (sawDone && job.outputFile.existsAsFile() && job.outputFile.getSize() > 0)
    {
        const juce::ScopedLock sl(stateLock);
        lastOutput = job.outputFile;
        finish(true, "Conversion complete: " + job.outputFile.getFileName(), job.outputFile);
    }
    else if (errorCode == "engine_not_installed")
    {
        finish(false, "RVC engine is not installed. Open Settings and press "
                      "'Install / Repair Engine'.");
    }
    else if (errorMessage.isNotEmpty())
    {
        finish(false, errorMessage);
    }
    else
    {
        finish(false, "Conversion failed (worker exit code "
                          + juce::String(exitCode) + "). See the log in Settings.");
    }
}

void ConversionEngine::runCheck(const juce::File& python)
{
    setStatus("Checking RVC engine...", -1);
    juce::var result;

    runStreaming({ python.getFullPathName(), "-u", bundledWorkerScript().getFullPathName(), "--check" },
                 [&](const juce::String& line) {
                     appendLog(line);
                     const auto parsed = juce::JSON::parse(line);
                     if (parsed.isObject() && parsed.getProperty("state", {}).toString() == "check")
                         result = parsed;
                 });

    if (cancelRequested.load())
    {
        finish(false, "Engine check cancelled.");
        return;
    }

    {
        const juce::ScopedLock sl(stateLock);
        lastCheckInfo = result;
    }

    if (result.isVoid())
    {
        finish(false, "Engine check failed: the worker did not respond. "
                      "Verify the Python path in Settings.");
        return;
    }

    const bool ready = static_cast<bool>(result.getProperty("engine_ready", false));
    const auto deviceInfo = static_cast<bool>(result.getProperty("cuda", false))
                                ? juce::String(", CUDA available")
                                : (static_cast<bool>(result.getProperty("mps", false))
                                       ? juce::String(", MPS available")
                                       : juce::String(", CPU only"));
    finish(ready,
           ready ? "RVC engine ready (Python " + result.getProperty("python_version", {}).toString()
                       + ", torch " + result.getProperty("torch_version", {}).toString() + deviceInfo + ")."
                 : "RVC engine is not installed yet. Use 'Install / Repair Engine' in Settings.");
}

void ConversionEngine::runInstall(bool useCuda)
{
    const auto basePython = findSystemPython();
    if (!basePython.existsAsFile())
    {
        finish(false, "Python was not found. Install Python 3.10-3.12 from python.org "
                      "(tick 'Add python.exe to PATH' on Windows), then try again.");
        return;
    }

    // Pick a torch build compatible with this interpreter. torch >= 2.6
    // changed torch.load defaults in a way that breaks rvc-python 0.1.x.
    juce::String versionOutput;
    runStreaming({ basePython.getFullPathName(), "-c",
                   "import sys; print('%d.%d' % sys.version_info[:2])" },
                 [&](const juce::String& line) { versionOutput = line.trim(); });

    juce::String torchVersion;
    if (versionOutput == "3.10" || versionOutput == "3.11")
        torchVersion = "2.1.1";
    else if (versionOutput == "3.12")
        torchVersion = "2.4.1";
    else
    {
        finish(false, "Python " + versionOutput + " is not supported by the RVC engine. "
                      "Please install Python 3.10, 3.11 or 3.12 and select it in Settings.");
        return;
    }

    const auto venvPython = managedPython();
    auto streamLog = [this](const juce::String& line) { appendLog(line); };

    if (!venvPython.existsAsFile())
    {
        setStatus("Creating engine environment...", 5);
        if (runStreaming({ basePython.getFullPathName(), "-m", "venv",
                           managedVenvDir().getFullPathName() }, streamLog) != 0
            || !venvPython.existsAsFile())
        {
            finish(false, "Failed to create the engine environment. See the log above.");
            return;
        }
    }
    if (cancelRequested.load()) { finish(false, "Install cancelled."); return; }

    setStatus("Updating pip...", 15);
    runStreaming({ venvPython.getFullPathName(), "-m", "pip", "install", "--upgrade", "pip" }, streamLog);
    if (cancelRequested.load()) { finish(false, "Install cancelled."); return; }

    // PyTorch (platform-specific wheel index).
    juce::StringArray torchArgs { venvPython.getFullPathName(), "-m", "pip", "install" };
   #if JUCE_MAC
    torchArgs.addArray({ "torch==" + torchVersion, "torchaudio==" + torchVersion });
   #else
    if (useCuda)
        torchArgs.addArray({ "torch==" + torchVersion + "+cu118",
                             "torchaudio==" + torchVersion + "+cu118",
                             "--index-url", "https://download.pytorch.org/whl/cu118" });
    else
        torchArgs.addArray({ "torch==" + torchVersion, "torchaudio==" + torchVersion,
                             "--index-url", "https://download.pytorch.org/whl/cpu" });
   #endif
    setStatus("Installing PyTorch (large download, please wait)...", 30);
    if (runStreaming(torchArgs, streamLog) != 0)
    {
        finish(false, "PyTorch installation failed. Check your internet connection "
                      "and see the log above.");
        return;
    }
    if (cancelRequested.load()) { finish(false, "Install cancelled."); return; }

    setStatus("Installing rvc-python...", 70);
    if (runStreaming({ venvPython.getFullPathName(), "-m", "pip", "install", "rvc-python==0.1.5" },
                     streamLog) != 0)
    {
        finish(false, "rvc-python installation failed. See the log above.");
        return;
    }

    setStatus("Verifying engine...", 90);
    runCheck(venvPython);
}

//==============================================================================
void ConversionEngine::setStatus(const juce::String& message, int percent)
{
    {
        const juce::ScopedLock sl(stateLock);
        statusMessage = message;
    }
    if (percent >= 0)
        progressPercent = percent;
    notify();
}

void ConversionEngine::appendLog(const juce::String& line)
{
    const juce::ScopedLock sl(stateLock);
    recentLog.add(line);
    while (recentLog.size() > 300)
        recentLog.remove(0);
}

void ConversionEngine::finish(bool success, const juce::String& message, juce::File output)
{
    {
        const juce::ScopedLock sl(stateLock);
        statusMessage = message;
        lastMessage = message;
        if (output.existsAsFile())
            lastOutput = output;
    }
    progressPercent = success ? 100 : -1;
    appendLog(message);
    busy = false;
    notify();
}

void ConversionEngine::notify()
{
    if (auto callback = onChange)
        juce::MessageManager::callAsync([callback] { callback(); });
}

//==============================================================================
juce::String ConversionEngine::getStatusMessage() const
{
    const juce::ScopedLock sl(stateLock);
    return statusMessage;
}

juce::String ConversionEngine::getLastMessage() const
{
    const juce::ScopedLock sl(stateLock);
    return lastMessage;
}

juce::File ConversionEngine::getLastOutput() const
{
    const juce::ScopedLock sl(stateLock);
    return lastOutput;
}

juce::var ConversionEngine::getLastCheckInfo() const
{
    const juce::ScopedLock sl(stateLock);
    return lastCheckInfo;
}

juce::StringArray ConversionEngine::getRecentLog() const
{
    const juce::ScopedLock sl(stateLock);
    return recentLog;
}
