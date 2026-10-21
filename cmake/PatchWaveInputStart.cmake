# Skip stale first input until the queued recording start has reached the playback graph.
# Upstream GPL notices remain in the patched dependency source.
function(halftrack_patch_wave_input_start source_dir)
    set(source "${source_dir}/modules/tracktion_engine/playback/devices/tracktion_WaveInputDevice.cpp")
    set(original "d08f647d63c6d478dff1bbe93e26477a5e807e47ccc4590a9433dc08f61d10c2")
    set(patched "3547f2134733cde3032237fbac4852e217067b4dbb9642235128843738f34ac1")
    # Already carrying the later recording errors patch.
    set(downstream "76ca176d4107398e061a85f3db0043043badd13a9065be37c6625ac3ec7b13f7")
    file(SHA256 "${source}" hash)
    if(hash STREQUAL patched OR hash STREQUAL downstream)
        return()
    endif()
    if(NOT hash STREQUAL original)
        message(FATAL_ERROR "Halftrack wave input start patch requires pinned source; unexpected SHA256 ${hash}.")
    endif()
    file(READ "${source}" content)
    set(before [=[            for (auto& recordingContext : recordingContexts)
            {
                const TimeRange blockRange (blockStart, TimeDuration::fromSamples (numSamples, recordingContext->sampleRate));]=])
    set(after [=[            for (auto& recordingContext : recordingContexts)
            {
                // Input arrives before the graph applies its queued start position.
                // Do not accept a first block mapped through the old playhead location.
                if (recordingContext->firstRecCallback && context.getPendingPositionChange())
                    continue;

                const TimeRange blockRange (blockStart, TimeDuration::fromSamples (numSamples, recordingContext->sampleRate));]=])
    string(REPLACE "${before}" "${after}" content "${content}")
    string(SHA256 hash "${content}")
    if(NOT hash STREQUAL patched)
        message(FATAL_ERROR "Halftrack wave input start patch produced unexpected SHA256 ${hash}.")
    endif()
    string(REGEX REPLACE "\n$" "" configure_content "${content}")
    file(CONFIGURE OUTPUT "${source}" CONTENT "@configure_content@" @ONLY NEWLINE_STYLE LF)
    file(SHA256 "${source}" hash)
    if(NOT hash STREQUAL patched)
        message(FATAL_ERROR "Halftrack wave input start patch wrote unexpected SHA256 ${hash}.")
    endif()
endfunction()
