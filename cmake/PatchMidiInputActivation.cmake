# Preserve fresh MIDI input through initial activation and interrupted callbacks.
# Upstream GPL notices remain in the patched dependency source.
function(halftrack_patch_midi_input_activation source_dir)
    set(base "${source_dir}/modules/tracktion_engine/playback/graph/tracktion_MidiInputDeviceNode")
    set(original_cpp "f893b43a88aa1897164b1f6fe45516b4e5e7e5db435afd09292da34870f91b16")
    set(patched_cpp "a3e623634363acecd4096d29498b8237af3a468015aeccdf48716d4dab115866")
    set(state_preserving_cpp "335da793205ef43c19384f666e0c13bb3d7117595d12b60676f66e5af2027dec")
    set(receipt_age_cpp "a1660920bf4c22133e1a5c33264c478ee466cedfccca26edf4be79248aa81c2f")
    set(original_h "976acfb6df698e01935bdb3cfad7761da884e07b41875edb325b2ee7a10879a9")
    set(patched_h "82f9fae59e49a0293f5aa0e0ba8926faa359df5d33fafc92226480083fb049e5")
    set(previous_cpp "b417dbcd6f6dafba3078eaa408b0074d0cd6ba50e7e43f123d5a4f283b4af4a2")
    set(accurate_receipt_cpp "b0e8e73a5ab9f5dd1d13e96c2adc84313ed3a579ddb8ca7d80b6564a2c497000")
    set(accurate_receipt_h "6103673d444ed07506ab92f96fbfb835639ececeb834c5b936d6bd0838d146ef")
    # The later MIDI input channel patch builds on this patch's results.
    file(SHA256 "${base}.cpp" current_cpp)
    file(SHA256 "${base}.h" current_h)
    if(current_cpp STREQUAL "7f8dd4ed48893d3ba46e4b09a44404dd9bcf334f772aa09be98a06573892cd74"
       AND current_h STREQUAL "90dec853c902a4349a64b450007f314c7380b5f414494d482385c5f6b33cacde")
        return()
    endif()
    # Preflight every source before transforming or writing either file.
    foreach(ext IN ITEMS cpp h)
        file(SHA256 "${base}.${ext}" hash_${ext})
        if(NOT hash_${ext} STREQUAL original_${ext} AND NOT hash_${ext} STREQUAL patched_${ext} AND NOT hash_${ext} STREQUAL accurate_receipt_${ext})
            if(NOT ext STREQUAL "cpp" OR (NOT hash_${ext} STREQUAL previous_cpp AND NOT hash_${ext} STREQUAL receipt_age_cpp AND NOT hash_${ext} STREQUAL state_preserving_cpp))
                message(FATAL_ERROR "Halftrack MIDI input patch requires pinned source: ${ext} SHA256 ${hash_${ext}}.")
            endif()
        endif()
        file(READ "${base}.${ext}" content_${ext})
    endforeach()
    if(NOT hash_cpp STREQUAL patched_cpp AND NOT hash_cpp STREQUAL receipt_age_cpp AND NOT hash_cpp STREQUAL state_preserving_cpp AND NOT hash_cpp STREQUAL accurate_receipt_cpp)
        # Normalize the earlier published patch before applying this complete patch.
        string(REPLACE "if (state->lastReadTime != 0 && timeNow > state->lastReadTime + maxExpectedMsPerBuffer)" "if (timeNow > state->lastReadTime + maxExpectedMsPerBuffer)" content_cpp "${content_cpp}")
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
        string(REPLACE "${before}" "${after}" content_cpp "${content_cpp}")
        set(before [=[            ++(state->numIncomingMessages);]=])
        set(after [=[            state->incomingMessageTimes[state->numIncomingMessages] = juce::Time::getApproximateMillisecondCounter();
            ++(state->numIncomingMessages);]=])
        string(REPLACE "${before}" "${after}" content_cpp "${content_cpp}")
        set(before [=[        // if it's been a long time since the last block, clear the buffer because
        // it means we were muted or glitching
        if (timeNow > state->lastReadTime + maxExpectedMsPerBuffer)
        {
            //jassertfalse;
            state->numIncomingMessages = 0;
        }]=])
        set(after [=[        // After an interruption, discard old queued events, not newly received keys.
        // The signed difference also tolerates a receipt newer than this block's
        // cached clock and the ordinary millisecond-counter wrap.
        const bool resumedAfterGap = state->lastReadTime != 0
            && static_cast<juce::int32> (timeNow - state->lastReadTime) > static_cast<juce::int32> (maxExpectedMsPerBuffer);]=])
        string(REPLACE "${before}" "${after}" content_cpp "${content_cpp}")
        set(before [=[            const auto timeAdjust = state->incomingMessages.front()->getTimeStamp();]=])
        set(after [=[            double timeAdjust = 0;
            bool hasTimeAdjust = false;]=])
        string(REPLACE "${before}" "${after}" content_cpp "${content_cpp}")
        set(before [=[                auto& m = *state->incomingMessages[i];

                destMidi.addMidiMessage]=])
        set(after [=[                if (resumedAfterGap
                    && static_cast<juce::int32> (timeNow - state->incomingMessageTimes[i]) > static_cast<juce::int32> (maxExpectedMsPerBuffer))
                    continue;

                auto& m = *state->incomingMessages[i];
                if (! hasTimeAdjust)
                {
                    timeAdjust = m.getTimeStamp();
                    hasTimeAdjust = true;
                }

                destMidi.addMidiMessage]=])
        string(REPLACE "${before}" "${after}" content_cpp "${content_cpp}")
    endif()
    if(NOT hash_cpp STREQUAL patched_cpp AND NOT hash_cpp STREQUAL state_preserving_cpp AND NOT hash_cpp STREQUAL accurate_receipt_cpp)
        set(before [=[    instance.removeConsumer (this);
}]=])
        set(after [=[    instance.removeConsumer (this);
    if (state)
    {
        // A replacement may not have processed yet when its previous consumer
        // is retired. Leave a vacancy that a surviving consumer can claim.
        auto* expected = this;
        state->activeNode.compare_exchange_strong (expected, nullptr, std::memory_order_acq_rel);
    }
}]=])
        string(REPLACE "${before}" "${after}" content_cpp "${content_cpp}")
        set(before [=[    if (state->activeNode.load (std::memory_order_acquire) != this)
        return;]=])
        set(after [=[    auto* active = state->activeNode.load (std::memory_order_acquire);
    if (active == nullptr)
    {
        // Input can precede the replacement graph's first audio callback.
        // Exactly one surviving consumer wins ownership of the shared queue.
        state->activeNode.compare_exchange_strong (active, this, std::memory_order_acq_rel);
        active = state->activeNode.load (std::memory_order_acquire);
    }
    if (active != this)
        return;]=])
        string(REPLACE "${before}" "${after}" content_cpp "${content_cpp}")
    endif()
    # An old attack should not replay after an interruption, but discarding an
    # old release can leave an already sounding note stuck indefinitely. Keep
    # non-attack channel state in FIFO order without moving the fresh-event time origin.
    set(before [=[                if (resumedAfterGap
                    && static_cast<juce::int32> (timeNow - state->incomingMessageTimes[i]) > static_cast<juce::int32> (maxExpectedMsPerBuffer))
                    continue;

                auto& m = *state->incomingMessages[i];
                if (! hasTimeAdjust)
                {
                    timeAdjust = m.getTimeStamp();
                    hasTimeAdjust = true;
                }

                destMidi.addMidiMessage (m,
                                         juce::jlimit (0.0, juce::jmax (0.0, editTime.getLength()), m.getTimeStamp() - timeAdjust),
                                         m.mpeSourceID);]=])
    set(after [=[                const bool isStale = resumedAfterGap
                    && static_cast<juce::int32> (timeNow - state->incomingMessageTimes[i]) > static_cast<juce::int32> (maxExpectedMsPerBuffer);
                auto& m = *state->incomingMessages[i];

                // Suppress delayed attacks, but preserve releases and controller
                // state so an interruption cannot leave a sounding voice stuck.
                // Stale system/transport messages retain their previous suppression.
                if (isStale && (m.isNoteOn() || m.getChannel() == 0))
                    continue;

                // Old retained state belongs at the resumed block's start. It
                // must not establish the time origin for freshly received keys.
                if (! hasTimeAdjust && ! isStale)
                {
                    timeAdjust = m.getTimeStamp();
                    hasTimeAdjust = true;
                }

                destMidi.addMidiMessage (m,
                                         isStale ? 0.0 : juce::jlimit (0.0, juce::jmax (0.0, editTime.getLength()), m.getTimeStamp() - timeAdjust),
                                         m.mpeSourceID);]=])
    string(REPLACE "${before}" "${after}" content_cpp "${content_cpp}")
    set(patched_cpp "${state_preserving_cpp}")
    if(NOT hash_h STREQUAL patched_h AND NOT hash_h STREQUAL accurate_receipt_h)
        set(before [=[        std::vector<std::unique_ptr<MidiMessageWithSource>> incomingMessages;]=])
        set(after [=[        std::vector<std::unique_ptr<MidiMessageWithSource>> incomingMessages;
        std::array<juce::uint32, 256> incomingMessageTimes {};]=])
        string(REPLACE "${before}" "${after}" content_h "${content_h}")
    endif()
    if(NOT hash_cpp STREQUAL accurate_receipt_cpp)
        set(before [=[      targetID (targetID_)
]=])
        set(after [=[      targetID (targetID_),
      useAccurateReceiptClock (! owner.isTrackDevice() && ! HostedAudioDeviceInterface::isHostedMidiInputDevice (owner))
]=])
        string(REPLACE "${before}" "${after}" content_cpp "${content_cpp}")
        set(before [=[            state->incomingMessageTimes[state->numIncomingMessages] = juce::Time::getApproximateMillisecondCounter();]=])
        set(after [=[            // External MIDI receipt needs the actual monotonic clock: the
            // approximate cache can stop updating while desktop dispatch waits.
            // Audio-derived track/hosted inputs retain the cached-clock path.
            state->incomingMessageTimes[state->numIncomingMessages] = useAccurateReceiptClock
                ? juce::Time::getMillisecondCounter() : juce::Time::getApproximateMillisecondCounter();]=])
        string(REPLACE "${before}" "${after}" content_cpp "${content_cpp}")
    endif()
    if(NOT hash_h STREQUAL accurate_receipt_h)
        set(before [=[    const EditItemID targetID;
]=])
        set(after [=[    const EditItemID targetID;
    const bool useAccurateReceiptClock;
]=])
        string(REPLACE "${before}" "${after}" content_h "${content_h}")
    endif()
    set(patched_cpp "${accurate_receipt_cpp}")
    set(patched_h "${accurate_receipt_h}")
    foreach(ext IN ITEMS cpp h)
        string(SHA256 transformed "${content_${ext}}")
        if(NOT transformed STREQUAL patched_${ext})
            message(FATAL_ERROR "Halftrack MIDI input patch produced unexpected ${ext} SHA256 ${transformed}.")
        endif()
    endforeach()
    foreach(ext IN ITEMS cpp h)
        if(NOT hash_${ext} STREQUAL patched_${ext})
            string(REGEX REPLACE "\n$" "" configure_content "${content_${ext}}")
            file(CONFIGURE OUTPUT "${base}.${ext}" CONTENT "@configure_content@" @ONLY NEWLINE_STYLE LF)
            file(SHA256 "${base}.${ext}" written)
            if(NOT written STREQUAL patched_${ext})
                message(FATAL_ERROR "Halftrack MIDI input patch wrote unexpected ${ext} SHA256 ${written}.")
            endif()
        endif()
    endforeach()
endfunction()
