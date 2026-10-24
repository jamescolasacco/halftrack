# Native launcher fixes for Tracktion 07e2d674884293f1a6c8f35fb8d1b9f469ffa41f.
# Preserve dependency notices. Accept only exact upstream, prior Halftrack, or
# current Halftrack bytes, and validate every file before modifying any file.
function(halftrack_patch_clip_launcher source_dir)
    set(source "${source_dir}/modules/tracktion_engine/model/tracks/tracktion_AudioTrack.h")
    file(SHA256 "${source}" source_hash)
    if(NOT source_hash STREQUAL "8f7ae3dd2f45939efb36fd15f5fd1c64f82d0ae6d543dab7e003a9419b54b8c5"
       AND NOT source_hash STREQUAL "933f35baa4ded602872777489b1661ca15a668de49f901fb0dc1701e94528ad7"
       AND NOT source_hash STREQUAL "f02cd152ab14ced230c3102a5a8aff6c98615a0ecdf139c33db85dd1982398e2")
        message(FATAL_ERROR "Halftrack launcher patch requires pinned source: ${source}; unexpected SHA256 ${source_hash}.")
    endif()
    set(source "${source_dir}/modules/tracktion_engine/model/tracks/tracktion_AudioTrack.cpp")
    file(SHA256 "${source}" source_hash)
    if(NOT source_hash STREQUAL "b5565cf52845479493ece086382d21c678b2f17ae93526f6ebf756169e03cdcc"
       AND NOT source_hash STREQUAL "b70f5b63b7c58ca0da78e6de1b49b540bbb9172ffc0b8af9f2c21e11313b2bf8"
       AND NOT source_hash STREQUAL "de63d10123c0f286e8811a8f8a8d166b373a1be741cde8a0a34599e707d9c861"
       AND NOT source_hash STREQUAL "cf1765d05ea1a9484450cce0045b77f1be3f26ecc816619ab675fc2e273022ea") # The later freeze tails patch.
        message(FATAL_ERROR "Halftrack launcher patch requires pinned source: ${source}; unexpected SHA256 ${source_hash}.")
    endif()
    set(source "${source_dir}/modules/tracktion_engine/playback/graph/tracktion_ArrangerLauncherSwitchingNode.cpp")
    file(SHA256 "${source}" source_hash)
    if(NOT source_hash STREQUAL "3afeace9d9eff030b8cb83b70850c6cc9c8c575f2c07c05115807f47407231fa"
       AND NOT source_hash STREQUAL "565b43eb8aef36972755c7b509bc9e6419d04e19ba7071fbd0c62581879c55f1"
       AND NOT source_hash STREQUAL "85f01686fff5abfb6a7e54fe0961c50d8ae362617007eda4a9dba7f7c93ac2eb")
        message(FATAL_ERROR "Halftrack launcher patch requires pinned source: ${source}; unexpected SHA256 ${source_hash}.")
    endif()
    set(source "${source_dir}/modules/tracktion_engine/playback/graph/tracktion_EditNodeBuilder.cpp")
    file(SHA256 "${source}" source_hash)
    if(NOT source_hash STREQUAL "31fb6f4d5b21355a67867c71461c26b17464e450c3c0e945173dd99f8550b074"
       AND NOT source_hash STREQUAL "6bc90a7dae60e9a0ab3d01fd77b0f90377b8933a9fe11bccbb38b31f471f060d"
       AND NOT source_hash STREQUAL "51ef9b7dfa77221251da5f43f77f91110f8b375de8974e6f45cac2c08c98ad48" # The later render sends-only patch.
       AND NOT source_hash STREQUAL "06c1d2be2f00c494f5db33c2b5ced64f9395e9fb7e16e4e8002ef7beacfa9ee8" # The later render track-inputs patch,
       AND NOT source_hash STREQUAL "bfc26bb83dcc389b9b3cd64db016f5e493e7f1f982b7c8910be6524c30703766" # the MIDI variations patch after it,
       AND NOT source_hash STREQUAL "d5ebc0acfbfc0dae66b15881ad2a8f9ece557e737c0c8ad7d4f7b8038a30d647" # the sidechain taps patch,
       AND NOT source_hash STREQUAL "1a07134c8c4c2a229b4da262dc6b547d3ab1302496fd5e5fbf1d375e9614517a" # the MIDI ports patch,
       AND NOT source_hash STREQUAL "9e1afcb494391df977c7f7950ce8030410576a2fab891e757d30a0249f87d60c" # the clip fades patch,
       AND NOT source_hash STREQUAL "bbc12a579f50896a7be4a5c13127b63c47863b22986b776290a7d523f80866cf") # and the cue patch.
        message(FATAL_ERROR "Halftrack launcher patch requires pinned source: ${source}; unexpected SHA256 ${source_hash}.")
    endif()
    set(source "${source_dir}/modules/tracktion_engine/model/tracks/tracktion_AudioTrack.h")
    file(SHA256 "${source}" source_hash)
    if(NOT source_hash STREQUAL "933f35baa4ded602872777489b1661ca15a668de49f901fb0dc1701e94528ad7")
        file(READ "${source}" content)
        if(source_hash STREQUAL "f02cd152ab14ced230c3102a5a8aff6c98615a0ecdf139c33db85dd1982398e2")
            set(previous [=[    juce::CachedValue<AtomicWrapper<bool>> playSlotClips;

    // Halftrack: audio ownership must not wait for the message-thread timer.
    // Bit 0 owns the track; the remaining bits invalidate stale audio claims
    // when a message-thread Return command races a processing block.
    static_assert (std::atomic<uint64_t>::is_always_lock_free);
    std::atomic<uint64_t> launcherOwnership { 0 };
    void releaseLauncherOwnership()
    {
        auto previous = launcherOwnership.load (std::memory_order_acquire);
        while (! launcherOwnership.compare_exchange_weak (previous, (previous + 2) & ~uint64_t (1),
                                                            std::memory_order_acq_rel)) {}
    }]=])
            set(upstream [=[    juce::CachedValue<AtomicWrapper<bool>> playSlotClips;]=])
            string(REPLACE "${previous}" "${upstream}" content "${content}")
            string(SHA256 upstream_hash "${content}")
            if(NOT upstream_hash STREQUAL "8f7ae3dd2f45939efb36fd15f5fd1c64f82d0ae6d543dab7e003a9419b54b8c5")
                message(FATAL_ERROR "Halftrack could not normalize the pinned prior launcher patch: ${source}.")
            endif()
        endif()
        set(before [=[    juce::CachedValue<AtomicWrapper<bool>> playSlotClips;]=])
        set(after [=[    juce::CachedValue<AtomicWrapper<bool>> playSlotClips;

    // Halftrack: audio ownership must not wait for the message-thread timer.
    // Bit 0 owns the track; the remaining bits invalidate stale audio claims
    // when a message-thread Return command races a processing block.
    static_assert (std::atomic<uint64_t>::is_always_lock_free);
    std::atomic<uint64_t> launcherOwnership { 0 };
    // Transient scheduler state for empty-slot takeover; never a hidden clip.
    LaunchHandle launcherSilence;
    void releaseLauncherOwnership()
    {
        auto previous = launcherOwnership.load (std::memory_order_acquire);
        while (! launcherOwnership.compare_exchange_weak (previous, (previous + 2) & ~uint64_t (1),
                                                            std::memory_order_acq_rel)) {}
    }]=])
        string(REPLACE "${before}" "${after}" content "${content}")
        string(SHA256 patched_hash "${content}")
        if(NOT patched_hash STREQUAL "933f35baa4ded602872777489b1661ca15a668de49f901fb0dc1701e94528ad7")
            message(FATAL_ERROR "Halftrack launcher patch produced unexpected SHA256 ${patched_hash}: ${source}.")
        endif()
        # CONFIGURE terminates its template line with LF. Remove one source LF
        # before the single-token template so expansion preserves exact bytes.
        string(REGEX REPLACE "\n$" "" configure_content "${content}")
        file(CONFIGURE OUTPUT "${source}" CONTENT "@configure_content@" @ONLY NEWLINE_STYLE LF)
        file(SHA256 "${source}" written_hash)
        if(NOT written_hash STREQUAL "933f35baa4ded602872777489b1661ca15a668de49f901fb0dc1701e94528ad7")
            message(FATAL_ERROR "Halftrack launcher patch wrote unexpected SHA256 ${written_hash}: ${source}.")
        endif()
    endif()
    set(source "${source_dir}/modules/tracktion_engine/model/tracks/tracktion_AudioTrack.cpp")
    file(SHA256 "${source}" source_hash)
    if(NOT source_hash STREQUAL "b70f5b63b7c58ca0da78e6de1b49b540bbb9172ffc0b8af9f2c21e11313b2bf8"
       AND NOT source_hash STREQUAL "cf1765d05ea1a9484450cce0045b77f1be3f26ecc816619ab675fc2e273022ea") # Already carrying the later freeze tails patch.
        file(READ "${source}" content)
        if(source_hash STREQUAL "b5565cf52845479493ece086382d21c678b2f17ae93526f6ebf756169e03cdcc")
            set(previous [=[            if (! playSlotClips.get())
            {
                for (auto cs : getClipSlotList().getClipSlots())
                    if (auto c = cs->getClip())
                        if (auto lh = c->getLaunchHandle())
                            lh->stop ({});
                releaseLauncherOwnership();
            }]=])
            set(upstream [=[            if (! playSlotClips.get())
                for (auto cs : getClipSlotList().getClipSlots())
                    if (auto c = cs->getClip())
                        if (auto lh = c->getLaunchHandle())
                            lh->stop ({});]=])
            string(REPLACE "${previous}" "${upstream}" content "${content}")
            string(SHA256 upstream_hash "${content}")
            if(NOT upstream_hash STREQUAL "de63d10123c0f286e8811a8f8a8d166b373a1be741cde8a0a34599e707d9c861")
                message(FATAL_ERROR "Halftrack could not normalize the pinned prior launcher patch: ${source}.")
            endif()
        endif()
        set(before [=[            if (! playSlotClips.get())
                for (auto cs : getClipSlotList().getClipSlots())
                    if (auto c = cs->getClip())
                        if (auto lh = c->getLaunchHandle())
                            lh->stop ({});]=])
        set(after [=[            if (! playSlotClips.get())
            {
                for (auto cs : getClipSlotList().getClipSlots())
                    if (auto c = cs->getClip())
                        if (auto lh = c->getLaunchHandle())
                            lh->stop ({});
                launcherSilence.stop ({});
                releaseLauncherOwnership();
            }]=])
        string(REPLACE "${before}" "${after}" content "${content}")
        string(SHA256 patched_hash "${content}")
        if(NOT patched_hash STREQUAL "b70f5b63b7c58ca0da78e6de1b49b540bbb9172ffc0b8af9f2c21e11313b2bf8")
            message(FATAL_ERROR "Halftrack launcher patch produced unexpected SHA256 ${patched_hash}: ${source}.")
        endif()
        # CONFIGURE terminates its template line with LF. Remove one source LF
        # before the single-token template so expansion preserves exact bytes.
        string(REGEX REPLACE "\n$" "" configure_content "${content}")
        file(CONFIGURE OUTPUT "${source}" CONTENT "@configure_content@" @ONLY NEWLINE_STYLE LF)
        file(SHA256 "${source}" written_hash)
        if(NOT written_hash STREQUAL "b70f5b63b7c58ca0da78e6de1b49b540bbb9172ffc0b8af9f2c21e11313b2bf8")
            message(FATAL_ERROR "Halftrack launcher patch wrote unexpected SHA256 ${written_hash}: ${source}.")
        endif()
    endif()
    set(source "${source_dir}/modules/tracktion_engine/playback/graph/tracktion_ArrangerLauncherSwitchingNode.cpp")
    file(SHA256 "${source}" source_hash)
    if(NOT source_hash STREQUAL "85f01686fff5abfb6a7e54fe0961c50d8ae362617007eda4a9dba7f7c93ac2eb")
        file(READ "${source}" content)
        if(source_hash STREQUAL "565b43eb8aef36972755c7b509bc9e6419d04e19ba7071fbd0c62581879c55f1")
            set(previous [=[    if (playArranger)
        processArranger (pc, slotStatus);

    // Merged sources can append an old arrangement note-off after a new slot
    // note-on at the same sample. Sort without allocation and release old notes
    // first; keep equal-time controller ordering stable within its class.
    choc::sorting::stable_sort (pc.buffers.midi.begin(), pc.buffers.midi.end(),
        [] (const juce::MidiMessage& a, const juce::MidiMessage& b)
        {
            if (a.getTimeStamp() < b.getTimeStamp()) return true;
            if (b.getTimeStamp() < a.getTimeStamp()) return false;
            return a.isNoteOff() && ! b.isNoteOff();
        });
}]=])
            set(upstream [=[    if (playArranger)
        processArranger (pc, slotStatus);
}]=])
            string(REPLACE "${previous}" "${upstream}" content "${content}")
            set(previous [=[    if (slotStatus.beatsUntilQueuedStartTrimmedToBlock)
    {
        // Arranger about to stop so only use some of the buffer and trigger fade out
        const auto endFrame = beatToSamplePosition (slotStatus.beatsUntilQueuedStartTrimmedToBlock,
                                                    editBeatRange.getLength(), numFrames);
        if (numSourceChannels > 0)
        {
            auto destSubView = destAudioView.getFirstChannels (numSourceChannels).getStart (endFrame);
            auto sourceSubView = sourceBuffers.audio.getStart (endFrame);
            arrangerSampleFader->trigger (10);

            if (sourceSubView.getNumFrames() > 0)
            {
                arrangerSampleFader->push (sourceSubView);

                choc::buffer::add (destSubView, sourceSubView);
                launcherSampleFader->applyAt (destAudioView,  endFrame, SampleFader::FadeType::fadeOut);
            }
        }

        const auto endTime = TimePosition::fromSamples (endFrame, getSampleRate());

        if (sourceBuffers.midi.isNotEmpty())
        {
            pc.buffers.midi.isAllNotesOff = sourceBuffers.midi.isAllNotesOff;

            for (auto& m : sourceBuffers.midi)
            {
                if (m.getTimeStamp() > endTime.inSeconds())
                    continue;

                pc.buffers.midi.add (m);

                if (m.isNoteOn())
                    arrangerActiveNoteList->startNote (m.getChannel(), m.getNoteNumber());
                else if (m.isNoteOff())
                    arrangerActiveNoteList->clearNote (m.getChannel(), m.getNoteNumber());
            }
        }

        MidiNodeHelpers::createNoteOffs (*arrangerActiveNoteList,
                                         pc.buffers.midi,
                                         midiSourceID,
                                         endTime.inSeconds(),
                                         getPlayHead().isPlaying());
    }
]=])
            set(upstream [=[    if (slotStatus.beatsUntilQueuedStartTrimmedToBlock)
    {
        // Arranger about to stop so only use some of the buffer and trigger fade out
        if (numSourceChannels > 0)
        {
            const auto endFrame = beatToSamplePosition (slotStatus.beatsUntilQueuedStartTrimmedToBlock,
                                                        editBeatRange.getLength(), numFrames);

            auto destSubView = destAudioView.getFirstChannels (numSourceChannels).getStart (endFrame);
            auto sourceSubView = sourceBuffers.audio.getStart (endFrame);
            arrangerSampleFader->trigger (10);

            if (sourceSubView.getNumFrames() > 0)
            {
                arrangerSampleFader->push (sourceSubView);

                choc::buffer::add (destSubView, sourceSubView);
                launcherSampleFader->applyAt (destAudioView,  endFrame, SampleFader::FadeType::fadeOut);
            }

            const auto endTime = TimePosition::fromSamples (endFrame, getSampleRate());

            if (sourceBuffers.midi.isNotEmpty())
            {
                pc.buffers.midi.isAllNotesOff = sourceBuffers.midi.isAllNotesOff;

                for (auto& m : sourceBuffers.midi)
                {
                    if (m.getTimeStamp() > endTime.inSeconds())
                        continue;

                    pc.buffers.midi.add (m);

                    if (m.isNoteOn())
                        arrangerActiveNoteList->startNote (m.getChannel(), m.getNoteNumber());
                    else if (m.isNoteOff())
                        arrangerActiveNoteList->clearNote (m.getChannel(), m.getNoteNumber());
                }
            }

            MidiNodeHelpers::createNoteOffs (*arrangerActiveNoteList,
                                             pc.buffers.midi,
                                             midiSourceID,
                                             endTime.inSeconds(),
                                             getPlayHead().isPlaying());
        }
    }
]=])
            string(REPLACE "${previous}" "${upstream}" content "${content}")
            set(previous [=[            if (queuedPos && queuedPos->v < blockRange.getEnd())
                status.beatsUntilQueuedStartTrimmedToBlock = std::max (0_bd, queuedPos->v - blockRange.getStart());]=])
            set(upstream [=[            if (queuedPos && blockRange.contains (queuedPos->v))
                status.beatsUntilQueuedStartTrimmedToBlock = queuedPos->v - blockRange.getStart();]=])
            string(REPLACE "${previous}" "${upstream}" content "${content}")
            set(previous [=[        track->playSlotClips = false;
        for (auto slot : track->getClipSlotList().getClipSlots())
            if (auto clip = slot->getClip())
                if (auto handle = clip->getLaunchHandle())
                    handle->stop ({});
        track->releaseLauncherOwnership();
        return;]=])
            set(upstream [=[        track->playSlotClips = false;
        return;]=])
            string(REPLACE "${previous}" "${upstream}" content "${content}")
            set(previous [=[    // Publish even if a short clip has already stopped. Audio ownership is
    // retained until an explicit Return, independently of message-loop timing.
    if ((track->launcherOwnership.load (std::memory_order_acquire) & 1) != 0)
        track->playSlotClips = true;]=])
            set(upstream [=[    for (auto& n : launcherNodesCopy)
    {
        if (auto lh = n->getLaunchHandleIfNotUnique())
        {
            if (lh->getPlayingStatus() == LaunchHandle::PlayState::playing)
            {
                track->playSlotClips = true;
                return;
            }
        }
    }]=])
            string(REPLACE "${previous}" "${upstream}" content "${content}")
            set(previous [=[    processLauncher (pc, slotStatus);

    // Claim only after the native scheduler actually starts a clip. The old
    // ownership value preserves the arrangement before a boundary in this block.
    // A racing Return changes the generation, so this claim cannot undo it.
    if (slotStatus.beatsUntilQueuedStartTrimmedToBlock)
        for (auto& node : launcherNodes)
            if (node->getLaunchHandle().getPlayingStatus() == LaunchHandle::PlayState::playing)
            {
                auto expected = ownership;
                track->launcherOwnership.compare_exchange_strong (expected, ownership | 1,
                                                                    std::memory_order_acq_rel);
                break;
            }

    if (playArranger)]=])
            set(upstream [=[    processLauncher (pc, slotStatus);

     if (playArranger)]=])
            string(REPLACE "${previous}" "${upstream}" content "${content}")
            set(previous [=[    const auto ownership = track->launcherOwnership.load (std::memory_order_acquire);
    const auto playArranger = ! track->playSlotClips.get() && (ownership & 1) == 0;]=])
            set(upstream [=[    const auto playArranger = ! track->playSlotClips.get();]=])
            string(REPLACE "${previous}" "${upstream}" content "${content}")
            string(SHA256 upstream_hash "${content}")
            if(NOT upstream_hash STREQUAL "3afeace9d9eff030b8cb83b70850c6cc9c8c575f2c07c05115807f47407231fa")
                message(FATAL_ERROR "Halftrack could not normalize the pinned prior launcher patch: ${source}.")
            endif()
        endif()
        set(before [=[    const auto playArranger = ! track->playSlotClips.get();]=])
        set(after [=[    const auto ownership = track->launcherOwnership.load (std::memory_order_acquire);
    const auto playArranger = ! track->playSlotClips.get() && (ownership & 1) == 0;]=])
        string(REPLACE "${before}" "${after}" content "${content}")
        set(before [=[    processLauncher (pc, slotStatus);

     if (playArranger)]=])
        set(after [=[    processLauncher (pc, slotStatus);

    // Claim only after the native scheduler actually starts a clip. The old
    // ownership value preserves the arrangement before a boundary in this block.
    // A racing Return changes the generation, so this claim cannot undo it.
    if (slotStatus.beatsUntilQueuedStartTrimmedToBlock)
        for (auto& node : launcherNodes)
            if (node->getLaunchHandle().getPlayingStatus() == LaunchHandle::PlayState::playing)
            {
                auto expected = ownership;
                track->launcherOwnership.compare_exchange_strong (expected, ownership | 1,
                                                                    std::memory_order_acq_rel);
                break;
            }

    if (playArranger)]=])
        string(REPLACE "${before}" "${after}" content "${content}")
        set(before [=[    for (auto& n : launcherNodesCopy)
    {
        if (auto lh = n->getLaunchHandleIfNotUnique())
        {
            if (lh->getPlayingStatus() == LaunchHandle::PlayState::playing)
            {
                track->playSlotClips = true;
                return;
            }
        }
    }]=])
        set(after [=[    // Publish even if a short clip has already stopped. Audio ownership is
    // retained until an explicit Return, independently of message-loop timing.
    if ((track->launcherOwnership.load (std::memory_order_acquire) & 1) != 0)
        track->playSlotClips = true;]=])
        string(REPLACE "${before}" "${after}" content "${content}")
        set(before [=[        track->playSlotClips = false;
        return;]=])
        set(after [=[        track->playSlotClips = false;
        for (auto slot : track->getClipSlotList().getClipSlots())
            if (auto clip = slot->getClip())
                if (auto handle = clip->getLaunchHandle())
                    handle->stop ({});
        track->launcherSilence.stop ({});
        track->releaseLauncherOwnership();
        return;]=])
        string(REPLACE "${before}" "${after}" content "${content}")
        set(before [=[            if (queuedPos && blockRange.contains (queuedPos->v))
                status.beatsUntilQueuedStartTrimmedToBlock = queuedPos->v - blockRange.getStart();]=])
        set(after [=[            if (queuedPos && queuedPos->v < blockRange.getEnd())
                status.beatsUntilQueuedStartTrimmedToBlock = std::max (0_bd, queuedPos->v - blockRange.getStart());]=])
        string(REPLACE "${before}" "${after}" content "${content}")
        set(before [=[    if (slotStatus.beatsUntilQueuedStartTrimmedToBlock)
    {
        // Arranger about to stop so only use some of the buffer and trigger fade out
        if (numSourceChannels > 0)
        {
            const auto endFrame = beatToSamplePosition (slotStatus.beatsUntilQueuedStartTrimmedToBlock,
                                                        editBeatRange.getLength(), numFrames);

            auto destSubView = destAudioView.getFirstChannels (numSourceChannels).getStart (endFrame);
            auto sourceSubView = sourceBuffers.audio.getStart (endFrame);
            arrangerSampleFader->trigger (10);

            if (sourceSubView.getNumFrames() > 0)
            {
                arrangerSampleFader->push (sourceSubView);

                choc::buffer::add (destSubView, sourceSubView);
                launcherSampleFader->applyAt (destAudioView,  endFrame, SampleFader::FadeType::fadeOut);
            }

            const auto endTime = TimePosition::fromSamples (endFrame, getSampleRate());

            if (sourceBuffers.midi.isNotEmpty())
            {
                pc.buffers.midi.isAllNotesOff = sourceBuffers.midi.isAllNotesOff;

                for (auto& m : sourceBuffers.midi)
                {
                    if (m.getTimeStamp() > endTime.inSeconds())
                        continue;

                    pc.buffers.midi.add (m);

                    if (m.isNoteOn())
                        arrangerActiveNoteList->startNote (m.getChannel(), m.getNoteNumber());
                    else if (m.isNoteOff())
                        arrangerActiveNoteList->clearNote (m.getChannel(), m.getNoteNumber());
                }
            }

            MidiNodeHelpers::createNoteOffs (*arrangerActiveNoteList,
                                             pc.buffers.midi,
                                             midiSourceID,
                                             endTime.inSeconds(),
                                             getPlayHead().isPlaying());
        }
    }
]=])
        set(after [=[    if (slotStatus.beatsUntilQueuedStartTrimmedToBlock)
    {
        // Arranger about to stop so only use some of the buffer and trigger fade out
        const auto endFrame = beatToSamplePosition (slotStatus.beatsUntilQueuedStartTrimmedToBlock,
                                                    editBeatRange.getLength(), numFrames);
        if (numSourceChannels > 0)
        {
            auto destSubView = destAudioView.getFirstChannels (numSourceChannels).getStart (endFrame);
            auto sourceSubView = sourceBuffers.audio.getStart (endFrame);
            arrangerSampleFader->trigger (10);

            if (sourceSubView.getNumFrames() > 0)
            {
                arrangerSampleFader->push (sourceSubView);

                choc::buffer::add (destSubView, sourceSubView);
                launcherSampleFader->applyAt (destAudioView,  endFrame, SampleFader::FadeType::fadeOut);
            }
        }

        const auto endTime = TimePosition::fromSamples (endFrame, getSampleRate());

        if (sourceBuffers.midi.isNotEmpty())
        {
            pc.buffers.midi.isAllNotesOff = sourceBuffers.midi.isAllNotesOff;

            for (auto& m : sourceBuffers.midi)
            {
                if (m.getTimeStamp() > endTime.inSeconds())
                    continue;

                pc.buffers.midi.add (m);

                if (m.isNoteOn())
                    arrangerActiveNoteList->startNote (m.getChannel(), m.getNoteNumber());
                else if (m.isNoteOff())
                    arrangerActiveNoteList->clearNote (m.getChannel(), m.getNoteNumber());
            }
        }

        MidiNodeHelpers::createNoteOffs (*arrangerActiveNoteList,
                                         pc.buffers.midi,
                                         midiSourceID,
                                         endTime.inSeconds(),
                                         getPlayHead().isPlaying());
    }
]=])
        string(REPLACE "${before}" "${after}" content "${content}")
        set(before [=[    if (playArranger)
        processArranger (pc, slotStatus);
}]=])
        set(after [=[    if (playArranger)
        processArranger (pc, slotStatus);

    // Merged sources can append an old arrangement note-off after a new slot
    // note-on at the same sample. Sort without allocation and release old notes
    // first; keep equal-time controller ordering stable within its class.
    choc::sorting::stable_sort (pc.buffers.midi.begin(), pc.buffers.midi.end(),
        [] (const juce::MidiMessage& a, const juce::MidiMessage& b)
        {
            if (a.getTimeStamp() < b.getTimeStamp()) return true;
            if (b.getTimeStamp() < a.getTimeStamp()) return false;
            return a.isNoteOff() && ! b.isNoteOff();
        });
}]=])
        string(REPLACE "${before}" "${after}" content "${content}")
        set(before [=[    assert (arrangerNode || ! launcherNodes.empty());]=])
        set(after [=[    // A source-free track still advances scheduled empty-slot takeover.
    // It has zero audio channels here and does not create a dummy reader.]=])
        string(REPLACE "${before}" "${after}" content "${content}")
        set(before [=[    const auto slotStatus = getSlotsStatus (launcherNodes,
                                            editBeatRange,
                                            getProcessState().getSyncRange().start.monotonicBeat);]=])
        set(after [=[    auto slotStatus = getSlotsStatus (launcherNodes,
                                      editBeatRange,
                                      getProcessState().getSyncRange().start.monotonicBeat);

    // Empty targets use the same native monotonic scheduler as audible clips.
    // Detect an actual new start after advance, rather than peeking at its queue:
    // a delayed native try-lock must neither drop the takeover nor reclaim a
    // returned generation merely because an old handle still reports playing.
    const auto previousSilence = track->launcherSilence.getPlayedMonotonicRange();
    const auto silenceSplit = track->launcherSilence.advance (getProcessState().getSyncRange());
    const auto currentSilence = track->launcherSilence.getPlayedMonotonicRange();
    const bool silenceStarted = currentSilence && (! previousSilence
        || currentSilence->v.getStart() != previousSilence->v.getStart());

    if (silenceStarted)
    {
        const auto untilSilence = silenceSplit.isSplit ? silenceSplit.range1.getLength() : 0_bd;
        if (! slotStatus.beatsUntilQueuedStartTrimmedToBlock
            || untilSilence < *slotStatus.beatsUntilQueuedStartTrimmedToBlock)
            slotStatus.beatsUntilQueuedStartTrimmedToBlock = untilSilence;
        auto expected = ownership;
        track->launcherOwnership.compare_exchange_strong (expected, ownership | 1,
                                                            std::memory_order_acq_rel);
    }]=])
        string(REPLACE "${before}" "${after}" content "${content}")
        string(SHA256 patched_hash "${content}")
        if(NOT patched_hash STREQUAL "85f01686fff5abfb6a7e54fe0961c50d8ae362617007eda4a9dba7f7c93ac2eb")
            message(FATAL_ERROR "Halftrack launcher patch produced unexpected SHA256 ${patched_hash}: ${source}.")
        endif()
        # CONFIGURE terminates its template line with LF. Remove one source LF
        # before the single-token template so expansion preserves exact bytes.
        string(REGEX REPLACE "\n$" "" configure_content "${content}")
        file(CONFIGURE OUTPUT "${source}" CONTENT "@configure_content@" @ONLY NEWLINE_STYLE LF)
        file(SHA256 "${source}" written_hash)
        if(NOT written_hash STREQUAL "85f01686fff5abfb6a7e54fe0961c50d8ae362617007eda4a9dba7f7c93ac2eb")
            message(FATAL_ERROR "Halftrack launcher patch wrote unexpected SHA256 ${written_hash}: ${source}.")
        endif()
    endif()
    set(source "${source_dir}/modules/tracktion_engine/playback/graph/tracktion_EditNodeBuilder.cpp")
    file(SHA256 "${source}" source_hash)
    if(NOT source_hash STREQUAL "31fb6f4d5b21355a67867c71461c26b17464e450c3c0e945173dd99f8550b074"
       AND NOT source_hash STREQUAL "51ef9b7dfa77221251da5f43f77f91110f8b375de8974e6f45cac2c08c98ad48" # Already carrying the later render sends-only patch,
       AND NOT source_hash STREQUAL "06c1d2be2f00c494f5db33c2b5ced64f9395e9fb7e16e4e8002ef7beacfa9ee8" # or the render track-inputs patch after it,
       AND NOT source_hash STREQUAL "bfc26bb83dcc389b9b3cd64db016f5e493e7f1f982b7c8910be6524c30703766" # or the MIDI variations patch after that,
       AND NOT source_hash STREQUAL "d5ebc0acfbfc0dae66b15881ad2a8f9ece557e737c0c8ad7d4f7b8038a30d647" # or the sidechain taps patch after that,
       AND NOT source_hash STREQUAL "1a07134c8c4c2a229b4da262dc6b547d3ab1302496fd5e5fbf1d375e9614517a" # or the MIDI ports patch after that,
       AND NOT source_hash STREQUAL "9e1afcb494391df977c7f7950ce8030410576a2fab891e757d30a0249f87d60c" # or the clip fades patch after that,
       AND NOT source_hash STREQUAL "bbc12a579f50896a7be4a5c13127b63c47863b22986b776290a7d523f80866cf") # or the cue patch after that.
        file(READ "${source}" content)
        set(before [=[    if (arrangerNodes.empty() && launcherNodes.empty())
        return {};]=])
        set(after [=[    if (arrangerNodes.empty() && launcherNodes.empty()
        && at.getClipSlotList().getClipSlots().isEmpty()
        && ! at.launcherSilence.getQueuedStatus()
        && at.launcherSilence.getPlayingStatus() == LaunchHandle::PlayState::stopped
        && (at.launcherOwnership.load (std::memory_order_acquire) & 1) == 0)
        return {};]=])
        string(REPLACE "${before}" "${after}" content "${content}")
        string(SHA256 patched_hash "${content}")
        if(NOT patched_hash STREQUAL "31fb6f4d5b21355a67867c71461c26b17464e450c3c0e945173dd99f8550b074")
            message(FATAL_ERROR "Halftrack launcher patch produced unexpected SHA256 ${patched_hash}: ${source}.")
        endif()
        # CONFIGURE terminates its template line with LF. Remove one source LF
        # before the single-token template so expansion preserves exact bytes.
        string(REGEX REPLACE "\n$" "" configure_content "${content}")
        file(CONFIGURE OUTPUT "${source}" CONTENT "@configure_content@" @ONLY NEWLINE_STYLE LF)
        file(SHA256 "${source}" written_hash)
        if(NOT written_hash STREQUAL "31fb6f4d5b21355a67867c71461c26b17464e450c3c0e945173dd99f8550b074")
            message(FATAL_ERROR "Halftrack launcher patch wrote unexpected SHA256 ${written_hash}: ${source}.")
        endif()
    endif()
endfunction()
