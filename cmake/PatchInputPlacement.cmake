# Places each recorded input block at the edit time of the output rendered in the same callback,
# including a take's first block, which plays from the queued start.
# The playback context resyncs its reference stream to [streamTime, streamTime + block] before a
# callback and then advances it by a block, so it renders one block ahead of the device's stream
# time; the input was stamped with the device's stream time, which put every take one block earlier
# than what was heard through a loopback (tests/RecordingLatencyTests.cpp). Applied after the
# recording errors patch. Upstream GPL notices remain in the patched dependency source.
function(halftrack_patch_input_placement source_dir)
    set(source "${source_dir}/modules/tracktion_engine/playback/devices/tracktion_WaveInputDevice.cpp")
    set(original "76ca176d4107398e061a85f3db0043043badd13a9065be37c6625ac3ec7b13f7")
    set(patched "b7ea47bf3f5680c433197eaa3426c459ffbdf58ae6f1a513708af52d1b8feb2d")
    file(SHA256 "${source}" hash)
    if(hash STREQUAL patched)
        return()
    endif()
    if(NOT hash STREQUAL original)
        message(FATAL_ERROR "Halftrack input placement patch requires the recording errors source; unexpected SHA256 ${hash}.")
    endif()
    file(READ "${source}" content)
    set(before [=[            const auto blockStart = context.globalStreamTimeToEditTimeUnlooped (streamTime);]=])
    set(after [=[            // Halftrack: the playback context's reference stream runs a block ahead of the device's
            // stream time, so the block it renders in this callback plays from the edit time of
            // streamTime plus a block, or from a queued start it applies first. Input arriving in
            // the same callback is placed there too; otherwise every take lands a block earlier
            // than what was heard, and a take's first block was dropped.
            const auto pendingStart = context.getPendingPositionChange();
            const auto blockStart = pendingStart ? *pendingStart
                                                 : context.globalStreamTimeToEditTimeUnlooped (streamTime + numSamples / context.getSampleRate());]=])
    string(REPLACE "${before}" "${after}" content "${content}")
    # The wave input start patch dropped a take's first block while its start was queued; it is
    # now placed at that start instead.
    set(before [=[                // Input arrives before the graph applies its queued start position.
                // Do not accept a first block mapped through the old playhead location.
                if (recordingContext->firstRecCallback && context.getPendingPositionChange())
                    continue;

]=])
    string(REPLACE "${before}" "" content "${content}")
    string(SHA256 hash "${content}")
    if(NOT hash STREQUAL patched)
        message(FATAL_ERROR "Halftrack input placement patch produced unexpected SHA256 ${hash}.")
    endif()
    string(REGEX REPLACE "\n$" "" configure_content "${content}")
    file(CONFIGURE OUTPUT "${source}" CONTENT "@configure_content@" @ONLY NEWLINE_STYLE LF)
    file(SHA256 "${source}" hash)
    if(NOT hash STREQUAL patched)
        message(FATAL_ERROR "Halftrack input placement patch wrote unexpected SHA256 ${hash}.")
    endif()
endfunction()
