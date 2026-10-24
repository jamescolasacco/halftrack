# Preserve audio/MIDI activation transitions. Validate all inputs before writing. Retains GPL notices.
function(halftrack_patch_clip_activation source_dir)
    set(source0 "${source_dir}/modules/tracktion_engine/playback/graph/tracktion_WaveNode.cpp")
    set(original0 "b7c3e5191bb380770db9b6e5aa8da5a3fd2131aee102d28485be669c807c9d76")
    set(patched0 "a723324379bc424d352772f91f538f6a457a9559caa34fd80b9699484e3f7fc2")
    set(before0 [=[    hash_combine (stateHash, pitchChangeSemitones);
}]=])
    set(after0 [=[    hash_combine (stateHash, pitchChangeSemitones);
    // A reader can already contain stretched samples at the previous mute level.
    hash_combine (stateHash, clipLevel.isMute());
}]=])
    set(source1 "${source_dir}/modules/tracktion_engine/playback/graph/tracktion_LoopingMidiNode.cpp")
    set(original1 "5bfeb512499f922cbe1acb464366c37a55faec511b4b5ce959b6fd4e8ce17a95")
    set(patched1 "e7ceddf6d9d00adf8035070f911d0e4c69f5b8f9daf62ee70e125d4825ad8820")
    set(before1 [=[        if (mute != wasMute)
        {
            wasMute = mute;]=])
    set(after1 [=[        // Rebuilt nodes may inherit held notes while already starting muted.
        if (mute != wasMute || generatorAndNoteList->getActiveNoteList()->areAnyNotesActive())
        {
            wasMute = mute;]=])
    set(source2 "${source_dir}/modules/tracktion_engine/playback/graph/tracktion_MidiNode.h")
    set(original2 "1be178ac571b28ba4c85f15db946c37cfb16209a05c0efcc1061e790fc94aa09")
    set(patched2 "0e3af9e8d1c1ba7aca706181876b8f6ccce5d31910f8d9048f63aaaae7a417e4")
    set(before2 [=[bool wasMute = false,]=])
    set(after2 [=[std::atomic<bool> wasMute { false };
    bool]=])
    set(source3 "${source_dir}/modules/tracktion_engine/playback/graph/tracktion_MidiNode.cpp")
    set(original3 "161f6183ef28aa1c64f6862d7be299b8ce17b413a7b67c759f9112abeb16b5e8")
    set(patched3 "208a3f2a6fd88470544538a01fc03a0b447296ce55022be1b0ece89ca4244e98")
    set(before3 [=[    shouldCreateMessagesForTime = findNodeWithID<MidiNode> (*info.nodeGraphToReplace, getNodeProperties().nodeID) == nullptr;
}

bool MidiNode::isReadyToProcess()
{
    return true;
}

void MidiNode::process (ProcessContext& pc)
{
    SCOPED_REALTIME_CHECK
    const auto timelineRange = getTimelineSampleRange();

    if (timelineRange.isEmpty())
        return;

    if (shouldBeMutedDelegate && shouldBeMutedDelegate())
        return;

    if (ms.size() > 0 && timelineRange.getStart() < lastStart)
        if (++currentSequence >= ms.size())
            currentSequence = 0;

    lastStart = timelineRange.getStart();

    if (timeBase == MidiList::TimeBase::beats)
    {
        const auto sectionEditTime = getEditBeatRange();

        if (sectionEditTime.isEmpty()
            || sectionEditTime.getEnd().inBeats() <= editRange.getStart()
            || sectionEditTime.getStart().inBeats() >= editRange.getEnd())
           return;

        const auto secondsPerBeat = getEditTimeRange().getLength().inSeconds() / sectionEditTime.getLength().inBeats();

        processSection (pc,
                        { sectionEditTime.getStart().inBeats(), sectionEditTime.getEnd().inBeats() },
                        secondsPerBeat, ms[currentSequence]);
    }
    else
    {
        const auto sectionEditTime = getEditTimeRange();

        if (sectionEditTime.isEmpty()
            || sectionEditTime.getEnd().inSeconds() <= editRange.getStart()
            || sectionEditTime.getStart().inSeconds() >= editRange.getEnd())
           return;

        processSection (pc,
                        { sectionEditTime.getStart().inSeconds(), sectionEditTime.getEnd().inSeconds() },
                        1.0, ms[currentSequence]);
    }
}

void MidiNode::processSection (Node::ProcessContext& pc,
                               juce::Range<double> sectionEditRange,
                               double secondsPerTimeBase,
                               juce::MidiMessageSequence& sequence)
{
    if (sectionEditRange.isEmpty()
        || sectionEditRange.getEnd() <= editRange.getStart()
        || sectionEditRange.getStart() >= editRange.getEnd())
       return;

    const auto localTime = sectionEditRange - editRange.getStart();
    const bool mute = clipLevel.isMute();

    if (mute)
    {
        if (mute != wasMute)
        {
            wasMute = mute;
            MidiNodeHelpers::createNoteOffs (pc.buffers.midi, sequence, midiSourceID, localTime.getStart(), {}, getPlayHead().isPlaying());
        }

        return;
    }]=])
    set(after3 [=[{
        if (auto oldNode = findNodeWithID<MidiNode> (*info.nodeGraphToReplace, getNodeProperties().nodeID))
        {
            shouldCreateMessagesForTime = false;
            midiSourceID = oldNode->midiSourceID;
            wasMute.store (oldNode->wasMute.load (std::memory_order_relaxed), std::memory_order_relaxed);
        }
        else
        {
            shouldCreateMessagesForTime = true;
        }
    }
}

bool MidiNode::isReadyToProcess()
{
    return true;
}

void MidiNode::process (ProcessContext& pc)
{
    SCOPED_REALTIME_CHECK
    const auto timelineRange = getTimelineSampleRange();

    if (timelineRange.isEmpty())
        return;

    if (shouldBeMutedDelegate && shouldBeMutedDelegate())
        return;

    if (ms.size() > 0 && timelineRange.getStart() < lastStart)
        if (++currentSequence >= ms.size())
            currentSequence = 0;

    lastStart = timelineRange.getStart();

    if (timeBase == MidiList::TimeBase::beats)
    {
        const auto sectionEditTime = getEditBeatRange();

        if (sectionEditTime.isEmpty()
            || sectionEditTime.getEnd().inBeats() <= editRange.getStart()
            || sectionEditTime.getStart().inBeats() >= editRange.getEnd())
           return;

        const auto secondsPerBeat = getEditTimeRange().getLength().inSeconds() / sectionEditTime.getLength().inBeats();

        processSection (pc,
                        { sectionEditTime.getStart().inBeats(), sectionEditTime.getEnd().inBeats() },
                        secondsPerBeat, ms[currentSequence]);
    }
    else
    {
        const auto sectionEditTime = getEditTimeRange();

        if (sectionEditTime.isEmpty()
            || sectionEditTime.getEnd().inSeconds() <= editRange.getStart()
            || sectionEditTime.getStart().inSeconds() >= editRange.getEnd())
           return;

        processSection (pc,
                        { sectionEditTime.getStart().inSeconds(), sectionEditTime.getEnd().inSeconds() },
                        1.0, ms[currentSequence]);
    }
}

void MidiNode::processSection (Node::ProcessContext& pc,
                               juce::Range<double> sectionEditRange,
                               double secondsPerTimeBase,
                               juce::MidiMessageSequence& sequence)
{
    if (sectionEditRange.isEmpty()
        || sectionEditRange.getEnd() <= editRange.getStart()
        || sectionEditRange.getStart() >= editRange.getEnd())
       return;

    const auto localTime = sectionEditRange - editRange.getStart();
    const bool mute = clipLevel.isMute();

    if (mute)
    {
        if (mute != wasMute)
        {
            wasMute = mute;
            MidiNodeHelpers::createNoteOffs (pc.buffers.midi, sequence, midiSourceID, localTime.getStart(), {}, getPlayHead().isPlaying());
        }

        return;
    }

    if (wasMute.exchange (false, std::memory_order_relaxed))
        shouldCreateMessagesForTime = true;]=])
    # The later stretch-alignment and warp-timing patches build on this WaveNode result.
    set(downstream0 "2f2a0038be5991510a1d0222f166d831fa9865dfe6f9d42e4dbbe4e20c6b2cd0" "04ccc34aa76ca5f81534007a7d68da4e595b6680b323879d7ffe6b69d947b269" "2e4354f76d96dc25dc489042afa6a3263439dbc3b3095b8639d008cd94b7a7d9")
    set(downstream1 "012a4ac8ea6c65ffb647e8d0244ecf5c0ce62d6f1841e72009c14309cb3307e1") # The later MIDI variations patch.
    foreach(index RANGE 0 3)
        file(SHA256 "${source${index}}" hash)
        if(hash STREQUAL "${patched${index}}" OR (DEFINED downstream${index} AND hash IN_LIST downstream${index}))
            set(write${index} FALSE)
            continue()
        endif()
        if(NOT hash STREQUAL "${original${index}}")
            message(FATAL_ERROR "Halftrack clip activation patch requires pinned source ${source${index}}; unexpected SHA256 ${hash}.")
        endif()
        file(READ "${source${index}}" content${index})
        string(REPLACE "${before${index}}" "${after${index}}" content${index} "${content${index}}")
        string(SHA256 hash "${content${index}}")
        if(NOT hash STREQUAL "${patched${index}}")
            message(FATAL_ERROR "Halftrack clip activation patch produced unexpected SHA256 ${hash}.")
        endif()
        set(write${index} TRUE)
    endforeach()
    foreach(index RANGE 0 3)
        if(write${index})
            string(REGEX REPLACE "\n$" "" configure_content "${content${index}}")
            file(CONFIGURE OUTPUT "${source${index}}" CONTENT "@configure_content@" @ONLY NEWLINE_STYLE LF)
            file(SHA256 "${source${index}}" hash)
            if(NOT hash STREQUAL "${patched${index}}")
                message(FATAL_ERROR "Halftrack clip activation patch wrote unexpected SHA256 ${hash}.")
            endif()
        endif()
    endforeach()
endfunction()
