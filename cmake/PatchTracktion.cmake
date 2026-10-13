# Narrow downstream fix for Tracktion Engine 07e2d674884293f1a6c8f35fb8d1b9f469ffa41f.
# Preserve the dependency's GPL/commercial notices in the modified source.
# Applies after population so FetchContent archives and local overrides agree.
function(halftrack_patch_tracktion source_dir)
    set(source "${source_dir}/modules/tracktion_engine/playback/tracktion_DeviceManager.cpp")
    file(SHA256 "${source}" source_hash)
    if(source_hash STREQUAL "04869fc7afd45b70fdadebd891742c57c77101ac58e9227298d3531aafef3d32")
        return()
    endif()
    if(NOT source_hash STREQUAL "28f900c36a68f768706bad72ef7d65cac7ff2adc313f7aa0c2b047cab398696e")
        message(FATAL_ERROR "Halftrack's recording-safety patch requires the pinned Tracktion DeviceManager.cpp. Unexpected SHA256: ${source_hash}. Restore the pinned source before configuring.")
    endif()
    file(READ "${source}" content)
    set(before [=[    restartMidiCheckTimer();

    if (lastMIDIDeviceList == nullptr)]=])
    set(after [=[    restartMidiCheckTimer();

    // Halftrack: refreshing devices clears playback contexts and discards takes.
    // Defer both automatic and requested MIDI discovery until recording stops.
    {
        const std::shared_lock sl (contextLock);

        for (auto* context : activeContexts)
            if (context->transport.isRecording())
            {
                startTimer (100);
                return;
            }
    }

    if (lastMIDIDeviceList == nullptr)]=])
    string(REPLACE "${before}" "${after}" content "${content}")
    # WRITE uses text mode and changes LF to CRLF on Windows. Explicit LF
    # CONFIGURE output uses binary mode, preserving the same source-byte pin.
    file(CONFIGURE OUTPUT "${source}" CONTENT "${content}" @ONLY NEWLINE_STYLE LF)
    file(SHA256 "${source}" patched_hash)
    if(NOT patched_hash STREQUAL "04869fc7afd45b70fdadebd891742c57c77101ac58e9227298d3531aafef3d32")
        message(FATAL_ERROR "Halftrack's Tracktion recording-safety patch did not produce the expected source. Actual SHA256: ${patched_hash}.")
    endif()
    message(STATUS "Applied Tracktion recording-safety patch: defer MIDI discovery during takes")
endfunction()
