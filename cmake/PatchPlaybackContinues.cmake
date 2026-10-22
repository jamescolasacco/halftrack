# Halftrack playback continuity for pinned Tracktion Engine.
# - Rebuilding a playback context's devices, as the engine does when a MIDI device appears or goes,
#   clears its nodes and stops its play head; the new nodes left the play head stopped, so the
#   transport stopped itself a moment later. Now a device-list rebuild while the transport plays
#   resumes the play head where it stopped, before the transport's timer can stop it.
# - A stop withdraws a play posted for the next block, so a recording started and stopped before
#   that block no longer starts playback a moment later.
# Retains all upstream source notices. Validates every file before writing any.
function(halftrack_patch_playback_continues source_dir)
    set(source0 "${source_dir}/modules/tracktion_engine/playback/tracktion_EditPlaybackContext.h")
    set(original0 "d172d8de5ee35392557d3e0e363893283de2d9d0392d7e58114b3c8c3ee09b4b")
    set(patched0 "5b4993f48aa8700aaa3f7c4e27790c41042536951e20d4d650951b73095ae327")
    set(count0 2)
    set(before0_0 [=[    bool isAllocated = false;]=])
    set(after0_0 [=[    bool isAllocated = false;
    bool playHeadWasRunning = false; // Halftrack: the play head ran when the nodes were last cleared.]=])
    set(before0_1 [=[    /** Returns true if a play message has been posted but not dispatched. */
    bool isPlayPending() const;]=])
    set(after0_1 [=[    /** Returns true if a play message has been posted but not dispatched. */
    bool isPlayPending() const;
    /** Halftrack: withdraws a posted play, for a stop that comes before the next block. */
    void cancelPendingPlay();]=])
    set(source1 "${source_dir}/modules/tracktion_engine/playback/tracktion_EditPlaybackContext.cpp")
    set(original1 "1742b29b2e581de7583892454d08e988dab60598254d5669164acbd9918bb019")
    set(patched1 "7d28b4424f5c1bb4140631529a219e9c4f60e58ee1c293a2faf9eb10a890ad70")
    set(count1 5)
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
    set(before1_3 [=[    void postPlay()
    {
        playPending.store (true, std::memory_order_release);
    }]=])
    set(after1_3 [=[    void postPlay()
    {
        playPending.store (true, std::memory_order_release);
    }

    // Halftrack: a stop before the next block withdraws the play posted for it.
    void cancelPendingPlay()
    {
        playPending.store (false, std::memory_order_release);
    }]=])
    set(before1_4 [=[void EditPlaybackContext::postPlay()
{
    if (nodePlaybackContext)
        nodePlaybackContext->postPlay();
}]=])
    set(after1_4 [=[void EditPlaybackContext::postPlay()
{
    if (nodePlaybackContext)
        nodePlaybackContext->postPlay();
}

void EditPlaybackContext::cancelPendingPlay()
{
    if (nodePlaybackContext)
        nodePlaybackContext->cancelPendingPlay();
}]=])
    set(source2 "${source_dir}/modules/tracktion_engine/playback/tracktion_TransportControl.cpp")
    set(original2 "5e43a391154ab64092d83eeeb8dddfc57718a11be23e753e4a1fdb0ec83c931d")
    set(patched2 "d85899d168abaf3ca1e47eb586a2018ca54b642b20ab92c15de1e970edd9724f")
    set(count2 1)
    set(before2_0 [=[    void stop()
    {
        if (auto ph = getNodePlayHead())
            ph->stop();
    }]=])
    set(after2_0 [=[    void stop()
    {
        // Halftrack: a play posted for the next block is withdrawn first. Otherwise a stop that
        // comes before that block, as when recording is started and stopped at once, leaves the
        // play head to start then, and the transport's timer starts playback to follow it.
        if (transport.playbackContext != nullptr)
            transport.playbackContext->cancelPendingPlay();

        if (auto ph = getNodePlayHead())
            ph->stop();
    }]=])
    foreach(index RANGE 0 2)
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
    foreach(index RANGE 0 2)
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
