# Register a fresh MIDI input consumer before its first audio callback.
# Upstream GPL notices remain in the patched dependency source.
function(halftrack_patch_midi_input_activation source_dir)
    set(source "${source_dir}/modules/tracktion_engine/playback/graph/tracktion_MidiInputDeviceNode.cpp")
    set(original "f893b43a88aa1897164b1f6fe45516b4e5e7e5db435afd09292da34870f91b16")
    set(patched "b417dbcd6f6dafba3078eaa408b0074d0cd6ba50e7e43f123d5a4f283b4af4a2")
    file(SHA256 "${source}" hash)
    if(hash STREQUAL patched)
        return()
    endif()
    if(NOT hash STREQUAL original)
        message(FATAL_ERROR "Halftrack MIDI input activation patch requires pinned source; unexpected SHA256 ${hash}.")
    endif()
    file(READ "${source}" content)
    set(before [=[    if (! state)
        state = std::make_shared<NodeState>();]=])
    set(after [=[    if (! state)
    {
        state = std::make_shared<NodeState>();
        // Accept the first key before the new graph's first audio callback.
        // Replacement graphs keep the active consumer in their shared state.
        state->lastReadTime = 0;
        state->activeNode.store (this, std::memory_order_release);
    }]=])
    string(REPLACE "${before}" "${after}" content "${content}")
    set(before [=[        if (timeNow > state->lastReadTime + maxExpectedMsPerBuffer)]=])
    set(after [=[        if (state->lastReadTime != 0 && timeNow > state->lastReadTime + maxExpectedMsPerBuffer)]=])
    string(REPLACE "${before}" "${after}" content "${content}")
    string(SHA256 hash "${content}")
    if(NOT hash STREQUAL patched)
        message(FATAL_ERROR "Halftrack MIDI input activation patch produced unexpected SHA256 ${hash}.")
    endif()
    string(REGEX REPLACE "\n$" "" configure_content "${content}")
    file(CONFIGURE OUTPUT "${source}" CONTENT "@configure_content@" @ONLY NEWLINE_STYLE LF)
    file(SHA256 "${source}" hash)
    if(NOT hash STREQUAL patched)
        message(FATAL_ERROR "Halftrack MIDI input activation patch wrote unexpected SHA256 ${hash}.")
    endif()
endfunction()
