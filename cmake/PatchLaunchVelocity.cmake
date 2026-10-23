# Halftrack launch velocity for pinned Tracktion Engine, after every earlier patch. As the reference's
# launch velocity does (static reading of its code), a clip launch carries a gain and a MIDI velocity
# scale on its launch handle; the clip's slot takes them up as the launch starts or restarts a
# playing clip, keeps them across graph rebuilds and loops until the next launch, and applies them
# to the clip's sound and its note-on velocities (never below velocity 1). Retains all upstream
# source notices. Validates every file before writing any.
function(halftrack_patch_launch_velocity source_dir)
    set(source0 "${source_dir}/modules/tracktion_engine/model/clips/tracktion_LaunchHandle.h")
    set(original0 "da905fda9f13b54127f59441550d2ac0c2c2268336f05ec28d13bfb9093542fd")
    set(patched0 "c76b3c71ed1e6c17c28962a4f6f5598b4a37b4af9141add8fd9bbe94d94480a6")
    set(count0 1)
    set(before0_0 [=[    std::optional<BeatRange> getLastPlayedRange() const;

private:]=])
    set(after0_0 [=[    std::optional<BeatRange> getLastPlayedRange() const;

    //==============================================================================
    /** Halftrack: the gain and the MIDI velocity scale the next launch plays with, as the
        reference's launch velocity sets them. Set them before play(); the clip's slot takes
        them up when that launch starts, and keeps them until the next one.
    */
    void setLaunchVelocity (float audioGain, float midiVelocityScale)
    {
        launchGain.store (audioGain, std::memory_order_relaxed);
        launchVelocityScale.store (midiVelocityScale, std::memory_order_relaxed);
    }

    float getLaunchGain() const                 { return launchGain.load (std::memory_order_relaxed); }
    float getLaunchVelocityScale() const        { return launchVelocityScale.load (std::memory_order_relaxed); }

private:
    //==============================================================================
    // message-write, audio-read (published to the audio thread by the play() that follows)
    std::atomic<float> launchGain { 1.0f }, launchVelocityScale { 1.0f };
]=])
    set(source1 "${source_dir}/modules/tracktion_engine/playback/graph/tracktion_SlotControlNode.h")
    set(original1 "9d5d84386b23635aa010b0bcfffd93cc22a0539a916eb1b71f7498429bc72c8f")
    set(patched1 "288596a5359fad12c2cac73d2b37044a1e3b79b0f0142a678388a88125efcbfb")
    set(count1 1)
    set(before1_0 [=[        bool wasPlaying = false;
        BeatDuration lastOffset;
        BeatPosition nextClipStart;     // The clip position the next section continues from]=])
    set(after1_0 [=[        bool wasPlaying = false;
        BeatDuration lastOffset;
        BeatPosition nextClipStart;     // The clip position the next section continues from
        float launchGain = 1.0f, launchVelocityScale = 1.0f; // Halftrack: the launch's velocity, taken up as it starts]=])
    set(source2 "${source_dir}/modules/tracktion_engine/playback/graph/tracktion_SlotControlNode.cpp")
    set(original2 "decfd887689b87caf5b5140ad5494309253573a66ed3640aba8562ae94d12c5e")
    set(patched2 "dd5682389efa47a9245ae561605b209dcb3b2ef1e1743261337a979a8dd7a783")
    set(count2 3)
    set(before2_0 [=[    else if (! playbackState->wasPlaying)
    {
        // Force the playheadJumped state to true in order to resync MIDI streams etc.
        localPlayheadState.playheadJumped = true;]=])
    set(after2_0 [=[    else if (! playbackState->wasPlaying)
    {
        // Halftrack: a launch starting takes up the velocity it was launched with
        playbackState->launchGain = launchHandle->getLaunchGain();
        playbackState->launchVelocityScale = launchHandle->getLaunchVelocityScale();

        // Force the playheadJumped state to true in order to resync MIDI streams etc.
        localPlayheadState.playheadJumped = true;]=])
    set(before2_1 [=[            retriggered = playbackState->wasPlaying && clipPositionJumped;]=])
    set(after2_1 [=[            retriggered = playbackState->wasPlaying && clipPositionJumped;

            // Halftrack: so does a launch restarting a playing clip
            if (retriggered)
            {
                playbackState->launchGain = launchHandle->getLaunchGain();
                playbackState->launchVelocityScale = launchHandle->getLaunchVelocityScale();
            }]=])
    set(before2_2 [=[    copyIfNotAliased (pc.buffers.audio, sourceBuffers.audio);

    // The MIDI buffer is the whole block's, which may have the MIDI of an earlier section in it]=])
    set(after2_2 [=[    copyIfNotAliased (pc.buffers.audio, sourceBuffers.audio);

    // Halftrack: the launch's velocity scales the clip's sound and its note-on velocities,
    // never below velocity 1, as the reference's launch velocity does
    if (playbackState->launchGain != 1.0f)
        choc::buffer::applyGain (pc.buffers.audio, playbackState->launchGain);

    if (playbackState->launchVelocityScale != 1.0f)
        for (auto& m : sourceBuffers.midi)
            if (m.isNoteOn())
                m.setVelocity (juce::jlimit (1.0f, 127.0f, m.getVelocity() * playbackState->launchVelocityScale) / 127.0f);

    // The MIDI buffer is the whole block's, which may have the MIDI of an earlier section in it]=])
    foreach(index RANGE 0 2)
        file(SHA256 "${source${index}}" hash)
        if(hash STREQUAL "${patched${index}}" OR (DEFINED downstream${index} AND hash IN_LIST downstream${index}))
            set(write${index} FALSE)
            continue()
        endif()
        if(NOT hash STREQUAL "${original${index}}")
            message(FATAL_ERROR "Halftrack launch velocity patch requires pinned source ${source${index}}; unexpected SHA256 ${hash}.")
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
            message(FATAL_ERROR "Halftrack launch velocity patch produced unexpected SHA256 ${hash} for ${source${index}}.")
        endif()
        set(write${index} TRUE)
    endforeach()
    foreach(index RANGE 0 2)
        if(write${index})
            string(REGEX REPLACE "\n$" "" configure_content "${content${index}}")
            file(CONFIGURE OUTPUT "${source${index}}" CONTENT "@configure_content@" @ONLY NEWLINE_STYLE LF)
            file(SHA256 "${source${index}}" hash)
            if(NOT hash STREQUAL "${patched${index}}")
                message(FATAL_ERROR "Halftrack launch velocity patch wrote unexpected SHA256 ${hash}.")
            endif()
        endif()
    endforeach()
endfunction()
