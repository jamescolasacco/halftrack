# Halftrack Arrangement loop playback for pinned Tracktion Engine, after every earlier patch. As the
# reference's transport does (static reading of its code), playback and recording with the loop on
# start where the transport is, not at the loop's start: from before the loop the play head rolls
# into it, and from at or past its end it plays on and never loops. A jump outside the loop does the
# same rather than being clamped into it. Retains all upstream source notices. Validates every file
# before writing any.
function(halftrack_patch_loop_playback source_dir)
    set(source0 "${source_dir}/modules/tracktion_graph/tracktion_graph/tracktion_PlayHead.h")
    set(original0 "e592ff87c34b02bc3934fb39d470a9e12bf168601c4d7e3d9bf950b2050e04d5")
    set(patched0 "29b0133c12b035f159b8464765e6751f57e0a7ac75c8b75a5bfce4403cbe06ee")
    set(count0 3)
    set(before0_0 [=[inline void PlayHead::overridePosition (int64_t newPosition)
{
    if (looping && rollInToLoop)
        newPosition = std::min (newPosition, timelinePlayRange.load().getEnd());
    else if (looping)
        newPosition = timelinePlayRange.load().clipValue (newPosition);
]=])
    set(after0_0 [=[inline void PlayHead::overridePosition (int64_t newPosition)
{
    // Halftrack: as the reference's transport does, a position outside the loop plays on from
    // there, rolling into the loop from before it and never looping from past its end.
    if (looping)
    {
        const auto range = timelinePlayRange.load();
        rollInToLoop = newPosition < range.getStart() || newPosition >= range.getEnd();
    }
]=])
    set(before0_1 [=[    newSyncPositions.playoutSyncPosition = std::min (position, timelinePlayRange.load().getEnd());]=])
    set(after0_1 [=[    newSyncPositions.playoutSyncPosition = position; // Halftrack: past the loop's end too, which plays on.]=])
    set(before0_2 [=[    if (rollInToLoop && getPosition() >= timelinePlayRange.load().getStart())
        rollInToLoop = false;]=])
    set(after0_2 [=[    // Halftrack: the loop takes over once the play head is inside it; past its end it plays on.
    if (rollInToLoop)
    {
        const auto position = getPosition();
        const auto range = timelinePlayRange.load();

        if (position >= range.getStart() && position < range.getEnd())
            rollInToLoop = false;
    }]=])
    set(source1 "${source_dir}/modules/tracktion_engine/playback/tracktion_TransportControl.cpp")
    set(original1 "cfd436495804e855c4c99db7aeb0080ea35b328ac339d926ce30804e40d431be")
    set(patched1 "9da5f4cbc09c1dc1c7b09eaae63655a4d78534fe5b7d01f4486063e13a39aea4")
    set(count1 3)
    set(before1_0 [=[        if (looping)
        {
            const auto cursorPos = position.get();
            const auto loopRange = getLoopRange();

            if (cursorPos < loopRange.getStart()
                || cursorPos > loopRange.getEnd() - 0.1s)
            {
                position = loopRange.getStart();
            }
]=])
    set(after1_0 [=[        if (looping)
        {
            // Halftrack: playback starts where the transport is, as the reference's does: from
            // before the loop the play head rolls into it, and from past its end it plays on.
            const auto loopRange = getLoopRange();
]=])
    set(before1_1 [=[                    if (edit.recordingPunchInOut)
                    {
                        engine.getUIBehaviour().showWarningMessage (TRANS("Recording can be done in either loop mode or punch in/out mode, but not both at the same time!"));
                        return std::nullopt;
                    }

                    transportState->startTime = loopRange.getStart();
                }]=])
    set(after1_1 [=[                    if (edit.recordingPunchInOut)
                    {
                        engine.getUIBehaviour().showWarningMessage (TRANS("Recording can be done in either loop mode or punch in/out mode, but not both at the same time!"));
                        return std::nullopt;
                    }

                    // Halftrack: recording starts where the transport is, as playback does.
                }]=])
    set(before1_2 [=[    if (isPlaying() && looping)
    {
        auto range = getLoopRange();
        newPos = juce::jlimit (range.getStart(), range.getEnd(), newPos);
    }
    else
    {
        const auto minStartTime = edit.tempoSequence.toTime (-BeatPosition::fromBeats (edit.getNumCountInBeats())) - 0.5s;
        newPos = juce::jlimit (minStartTime, Edit::getMaximumEditEnd(), newPos);
    }]=])
    set(after1_2 [=[    // Halftrack: a jump while looping goes where it is asked, as the reference's does; the play head
    // rolls into the loop from before it and plays on past its end.
    {
        const auto minStartTime = edit.tempoSequence.toTime (-BeatPosition::fromBeats (edit.getNumCountInBeats())) - 0.5s;
        newPos = juce::jlimit (minStartTime, Edit::getMaximumEditEnd(), newPos);
    }]=])
    set(source2 "${source_dir}/modules/tracktion_engine/model/edit/tracktion_EditUtilities.cpp")
    set(original2 "cbc08effa9de1b1143f3e39931ae5c7fe6a2e07a27b23037d9cfc2378dfc3918")
    set(patched2 "8214ed9e16d54647e3d6a6e5085234915286e7ee3b1b806a509e2fc864885aad")
    set(count2 1)
    set(before2_0 [=[    else if (context.isLooping())
    {
        params.punchRange = params.punchRange.withStart (context.getLoopTimes().getStart());
    }
]=])
    set(after2_0 [=[    // Halftrack: a loop recording starts where the transport is, as the reference's does, rather
    // than at the loop's start: from before the loop its first pass includes the roll-in.
]=])
    foreach(index RANGE 0 2)
        file(SHA256 "${source${index}}" hash)
        if(hash STREQUAL "${patched${index}}")
            set(write${index} FALSE)
            continue()
        endif()
        if(NOT hash STREQUAL "${original${index}}")
            message(FATAL_ERROR "Halftrack loop playback patch requires pinned source ${source${index}}; unexpected SHA256 ${hash}.")
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
            message(FATAL_ERROR "Halftrack loop playback patch produced unexpected SHA256 ${hash} for ${source${index}}.")
        endif()
        set(write${index} TRUE)
    endforeach()
    foreach(index RANGE 0 2)
        if(write${index})
            string(REGEX REPLACE "\n$" "" configure_content "${content${index}}")
            file(CONFIGURE OUTPUT "${source${index}}" CONTENT "@configure_content@" @ONLY NEWLINE_STYLE LF)
            file(SHA256 "${source${index}}" hash)
            if(NOT hash STREQUAL "${patched${index}}")
                message(FATAL_ERROR "Halftrack loop playback patch wrote unexpected SHA256 ${hash}.")
            endif()
        endif()
    endforeach()
endfunction()
