# Extend the recording-safety MIDI discovery deferral to takes that inputs record while the
# transport is not recording, such as Session slot takes. Builds on PatchTracktion.cmake and
# PatchMidiDevices.cmake. Upstream GPL notices remain in the patched dependency source.
function(halftrack_patch_input_take_rescan source_dir)
    set(source "${source_dir}/modules/tracktion_engine/playback/tracktion_DeviceManager.cpp")
    set(original "49374cb08b8752baa385b18806c2824005cd1e0bb67481a373638977b3dbf498")
    set(patched "896d6fc2b35ecaaffb1a8bca4b453880e01328a29eae4ee5e222c02da570ed36")
    set(downstream "e653f519cdc6ae62cd56be45919cdfd5030e041a35e84ee77c18fadc4b008c91" "12383b707e12ce57113f93d9d447a06a4b14a4fc90a76483f0912e4c4ddf8e63") # The later MIDI ports and Resampling patches.
    file(SHA256 "${source}" hash)
    if(hash STREQUAL patched OR hash IN_LIST downstream)
        return()
    endif()
    if(NOT hash STREQUAL original)
        message(FATAL_ERROR "Halftrack input take rescan patch requires the recording-safety and MIDI device patches on the pinned DeviceManager.cpp; unexpected SHA256 ${hash}.")
    endif()
    file(READ "${source}" content)
    set(before [=[    // Defer both automatic and requested MIDI discovery until recording stops.
    {
        const std::shared_lock sl (contextLock);

        for (auto* context : activeContexts)
            if (context->transport.isRecording())
            {
                startTimer (100);
                return;
            }
    }]=])
    set(after [=[    // Defer both automatic and requested MIDI discovery until recording stops,
    // including takes that inputs record while the transport is not recording.
    {
        const std::shared_lock sl (contextLock);

        for (auto* context : activeContexts)
        {
            bool recording = context->transport.isRecording();

            for (auto* input : context->getAllInputs())
                recording = recording || input->isRecording();

            if (recording)
            {
                startTimer (100);
                return;
            }
        }
    }]=])
    string(REPLACE "${before}" "${after}" content "${content}")
    string(SHA256 hash "${content}")
    if(NOT hash STREQUAL patched)
        message(FATAL_ERROR "Halftrack input take rescan patch produced unexpected SHA256 ${hash}.")
    endif()
    string(REGEX REPLACE "\n$" "" configure_content "${content}")
    file(CONFIGURE OUTPUT "${source}" CONTENT "@configure_content@" @ONLY NEWLINE_STYLE LF)
    file(SHA256 "${source}" hash)
    if(NOT hash STREQUAL patched)
        message(FATAL_ERROR "Halftrack input take rescan patch wrote unexpected SHA256 ${hash}.")
    endif()
    message(STATUS "Applied Tracktion input take rescan patch: defer MIDI discovery during Session takes")
endfunction()
