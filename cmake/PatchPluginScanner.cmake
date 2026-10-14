# Narrow scanner fix for Tracktion Engine 07e2d674884293f1a6c8f35fb8d1b9f469ffa41f.
# Keep dependency notices; preflight every source and result before changing any file.
function(halftrack_patch_plugin_scanner source_dir)
    set(relative_0 "modules/tracktion_engine/plugins/tracktion_PluginScanHelpers.h")
    set(original_0 "94eb54614af016e38e67af2e27f870e0ad14aa6bf83b42638d7b36c8291f8d8c")
    set(previous_0 "521a1479001bb633c73f5a4780fe85418128728e1dab2f3c0abe43d84c4b0895")
    set(patched_0 "a4498068deaeec17081d992825d1df2faf49a1e6c289b91d0fc9a8a54aa7071c")
    set(relative_1 "modules/tracktion_engine/utilities/tracktion_EngineBehaviour.h")
    set(original_1 "71025051424caea898d1f07a8a6a67b4d7500b4d1c3991a734bcad0188c6ee18")
    set(patched_1 "09b64a7d5a5e58b45370424262f61a562f620affdb9e2b92829666d0a024347d")
    foreach(index RANGE 0 1)
        set(source "${source_dir}/${relative_${index}}")
        file(SHA256 "${source}" source_hash)
        set(needs_patch_${index} FALSE)
        if(source_hash STREQUAL "${patched_${index}}")
            continue()
        endif()
        if(NOT source_hash STREQUAL "${original_${index}}"
           AND NOT (index EQUAL 0 AND source_hash STREQUAL "${previous_0}"))
            message(FATAL_ERROR "Halftrack's scanner patch requires pinned ${relative_${index}}. Unexpected SHA256: ${source_hash}.")
        endif()
        file(READ "${source}" content)
        if(index EQUAL 0)
            if(source_hash STREQUAL "${original_0}")
                string(REPLACE [=[        launched = launchWorkerProcess (juce::File::getSpecialLocation (juce::File::currentExecutableFile),
                                        commandLineUID, 0, 0);]=] [=[        const auto executable = engine.getEngineBehaviour().getPluginScanExecutable();
        launched = executable.existsAsFile() && launchWorkerProcess (executable, commandLineUID, 0, 0);]=] content "${content}")
                string(REPLACE [=[    PluginScanMasterProcess (Engine& e) : engine (e) {}]=] [=[    PluginScanMasterProcess (Engine& e) : engine (e) {}
    ~PluginScanMasterProcess()
    {
        // Halftrack: stop IPC callbacks before replies and replyLock are destroyed.
        killWorkerProcess();
    }]=] content "${content}")
                string(REPLACE [=[    volatile bool launched = false, crashed = false;]=] [=[    std::atomic<bool> launched { false }, crashed { false };]=] content "${content}")
                string(REPLACE [=[    std::unique_ptr<juce::XmlElement> findReply (int requestID)
    {]=] [=[    std::unique_ptr<juce::XmlElement> findReply (int requestID)
    {
        const juce::ScopedLock sl (replyLock);]=] content "${content}")
                string(REPLACE [=[            // panic! Can't run the child for some reason, so just do it here..
            TRACKTION_LOG_ERROR ("Falling back to scanning in main process..");
            masterProcess.reset();]=] [=[            // Halftrack: a failed child launch must not load an untrusted plug-in
            // into the host. The launch warning explains how to retry safely.
            TRACKTION_LOG_ERROR ("Plug-in scan failed: could not start the separate scanner process");
            masterProcess.reset();
            return false;]=] content "${content}")
                string(REPLACE [=[                .getUIBehaviour().showWarningAlert ("Plugin Scanning...",
                                                    TRANS("There are some problems in launching a child-process to scan for plugins.")
                                                       + "\n\n"
                                                       + TRANS("If you have a virus-checker or firewall running, you may need to temporarily disable it for the scan to work correctly."));]=] [=[                .getUIBehaviour().showWarningAlert ("Plug-in scanning unavailable",
                                                    TRANS("Could not start the separate plug-in scanner. The plug-in was not loaded in this process.")
                                                       + "\n\n"
                                                       + TRANS("Restart the application and retry the scan. If it still fails, check that the application can launch its scanner process."));]=] content "${content}")
            endif()
            string(REPLACE [=[            // Halftrack: a failed child launch must not load an untrusted plug-in
            // into the host. The launch warning explains how to retry safely.
            TRACKTION_LOG_ERROR ("Plug-in scan failed: could not start the separate scanner process");
            masterProcess.reset();
            return false;]=] [=[            // Halftrack: a failed child launch must not load an untrusted plug-in
            // into the host. The launch warning explains how to retry safely.
            TRACKTION_LOG_ERROR ("Plug-in scan failed: could not start the separate scanner process");
            masterProcess.reset();
            // JUCE treats false as a plug-in crash and blacklists it. Startup
            // failure returns no descriptions, but must allow the next scan.
            return true;]=] content "${content}")
        else()
            string(REPLACE [=[    virtual bool canScanPluginsOutOfProcess()                                       { return false; }]=] [=[    virtual bool canScanPluginsOutOfProcess()                                       { return false; }

    /** Executable used for the separate plug-in scanner. Defaults to this application. */
    virtual juce::File getPluginScanExecutable()
    {
        return juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    }]=] content "${content}")
        endif()
        string(SHA256 result_hash "${content}")
        if(NOT result_hash STREQUAL "${patched_${index}}")
            message(FATAL_ERROR "Halftrack's scanner patch produced an unexpected ${relative_${index}}. SHA256: ${result_hash}.")
        endif()
        set(needs_patch_${index} TRUE)
        set(result_${index} "${content}")
    endforeach()
    foreach(index RANGE 0 1)
        if(needs_patch_${index})
            set(source "${source_dir}/${relative_${index}}")
            # Explicit LF preserves the exact result pin on every platform.
            file(CONFIGURE OUTPUT "${source}" CONTENT "${result_${index}}" @ONLY NEWLINE_STYLE LF)
            file(SHA256 "${source}" written_hash)
            if(NOT written_hash STREQUAL "${patched_${index}}")
                message(FATAL_ERROR "Halftrack's scanner patch could not write the verified ${relative_${index}}.")
            endif()
            message(STATUS "Applied Tracktion scanner safety patch to ${relative_${index}}")
        endif()
    endforeach()
endfunction()
