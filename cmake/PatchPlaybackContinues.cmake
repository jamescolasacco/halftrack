# Halftrack playback through device rebuilds for pinned Tracktion Engine. Rebuilding a playback
# context's devices, as the engine does when a MIDI device appears or goes, clears its nodes and
# stops its play head; the new nodes left the play head stopped, so the transport stopped itself a
# moment later. Now a rebuild while the transport plays resumes the play head where it stopped, as soon as the
# device list is rebuilt, before the transport's timer can stop it.
# Retains all upstream source notices. Validates every file before writing any.
function(halftrack_patch_playback_continues source_dir)
    set(source0 "${source_dir}/modules/tracktion_engine/playback/tracktion_EditPlaybackContext.h")
    set(original0 "d172d8de5ee35392557d3e0e363893283de2d9d0392d7e58114b3c8c3ee09b4b")
    set(patched0 "3d4804a3b406a4eafe30c7db36b9f8b18d511e9b2586fd6705144025ac3b9d93")
    set(count0 1)
    set(before0_0 [=[    bool isAllocated = false;]=])
    set(after0_0 [=[    bool isAllocated = false;
    bool playHeadWasRunning = false; // Halftrack: the play head ran when the nodes were last cleared.]=])
    set(source1 "${source_dir}/modules/tracktion_engine/playback/tracktion_EditPlaybackContext.cpp")
    set(original1 "1742b29b2e581de7583892454d08e988dab60598254d5669164acbd9918bb019")
    set(patched1 "dc3f41c9031fcc94ff32196c1a99be684a09c53d4d50da09247f856b0ec406d2")
    set(count1 3)
    set(before1_0 [=[    const juce::ScopedValueSetter<bool> alocateSetter (isAllocated, isAllocated);
    clearNodes();]=])
    set(after1_0 [=[    const juce::ScopedValueSetter<bool> alocateSetter (isAllocated, isAllocated);
    // Halftrack: remembered, so that rebuilding the device list while the transport plays resumes
    // the play head. Only here: the transport clears nodes itself and restarts its play head then.
    playHeadWasRunning = playHeadWasRunning || (nodePlaybackContext != nullptr && nodePlaybackContext->playHead.isPlaying());
    clearNodes();]=])
    set(before1_1 [=[void EditPlaybackContext::createPlayAudioNodes (TimePosition startTime)
{
    createNode();
    startPlaying (startTime);
}]=])
    set(after1_1 [=[void EditPlaybackContext::createPlayAudioNodes (TimePosition startTime)
{
    createNode();
    startPlaying (startTime);

    // Halftrack: a rebuild of the devices or graph while the transport plays, such as a MIDI
    // device appearing, carries on from where the play head stopped, rather than leaving it
    // stopped for the transport to stop too.
    if (std::exchange (playHeadWasRunning, false) && transport.isPlaying()
         && nodePlaybackContext != nullptr && ! nodePlaybackContext->playHead.isPlaying())
        nodePlaybackContext->playHead.play();
}]=])
    set(before1_2 [=[    midiDispatcher.setMidiDeviceList (midiOutputs);

    if (isAllocated)
        reallocate();
}]=])
    set(after1_2 [=[    midiDispatcher.setMidiDeviceList (midiOutputs);

    if (isAllocated)
        reallocate();

    // Halftrack: resume now rather than when the graph is rebuilt, which may come later, so the
    // transport's timer never finds its play head standing still and stops it meanwhile.
    if (std::exchange (playHeadWasRunning, false) && transport.isPlaying()
         && nodePlaybackContext != nullptr && ! nodePlaybackContext->playHead.isPlaying())
        nodePlaybackContext->playHead.play();
}]=])
    foreach(index RANGE 0 1)
        file(SHA256 "${source${index}}" hash)
        if(hash STREQUAL "${patched${index}}")
            set(write${index} FALSE)
            continue()
        endif()
        if(NOT hash STREQUAL "${original${index}}")
            message(FATAL_ERROR "Halftrack playback continuation patch requires pinned source ${source${index}}; unexpected SHA256 ${hash}.")
        endif()
        file(READ "${source${index}}" content${index})
        math(EXPR last "${count${index}} - 1")
        foreach(pair RANGE 0 ${last})
            string(REPLACE "${before${index}_${pair}}" "${after${index}_${pair}}" content${index} "${content${index}}")
        endforeach()
        # The LF configure-write always ends with one newline; hash what will be written.
        string(REGEX MATCH "\n$" ends_with_newline "${content${index}}")
        if(NOT ends_with_newline)
            string(APPEND content${index} "\n")
        endif()
        string(SHA256 hash "${content${index}}")
        if(NOT hash STREQUAL "${patched${index}}")
            message(FATAL_ERROR "Halftrack playback continuation patch produced unexpected SHA256 ${hash} for ${source${index}}.")
        endif()
        set(write${index} TRUE)
    endforeach()
    foreach(index RANGE 0 1)
        if(write${index})
            string(REGEX REPLACE "\n$" "" configure_content "${content${index}}")
            file(CONFIGURE OUTPUT "${source${index}}" CONTENT "@configure_content@" @ONLY NEWLINE_STYLE LF)
            file(SHA256 "${source${index}}" hash)
            if(NOT hash STREQUAL "${patched${index}}")
                message(FATAL_ERROR "Halftrack playback continuation patch wrote unexpected SHA256 ${hash}.")
            endif()
        endif()
    endforeach()
endfunction()
