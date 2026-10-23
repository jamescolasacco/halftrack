# Halftrack MIDI input channels for pinned Tracktion Engine, after every earlier patch. As the
# reference's MIDI From channel chooser does, a track can take one MIDI channel from its inputs: the
# track's halftrackMidiInputChannel (1 to 16, or 0 for every channel), read for a track or a clip
# slot's track when graphs and recordings are prepared, filters what its input node passes on and
# what its recordings keep. Retains all upstream source notices. Validates every file before writing any.
function(halftrack_patch_midi_input_channel source_dir)
    set(source0 "${source_dir}/modules/tracktion_engine/playback/devices/tracktion_MidiInputDevice.h")
    set(original0 "4caa2c5b2dfa88a8ff9f0d2ca15830e83c432ac27e3122de275c75875d1fe54e")
    set(patched0 "b35855d978d2444c773834360c345c8df19d70451adc4f5c28a9ffd50c926dbe")
    set(count0 1)
    set(before0_0 [=[    /** Halftrack: sets or clears the device's remote control; it must outlive its use here. */
    void setRemoteControl (RemoteControl*);
]=])
    set(after0_0 [=[    /** Halftrack: sets or clears the device's remote control; it must outlive its use here. */
    void setRemoteControl (RemoteControl*);

    /** Halftrack: the MIDI channel, 1 to 16, that the track a target names (or a clip slot's track)
        takes from its MIDI inputs, as the reference's MIDI From channel; 0 for every channel.
        Read on the message thread when graphs and recordings are prepared.
    */
    static int getInputChannelFor (const Edit&, EditItemID target);
]=])
    set(source1 "${source_dir}/modules/tracktion_engine/playback/devices/tracktion_MidiInputDevice.cpp")
    set(original1 "5f31534136e50526dd3c30aa82a679e8f6a87a90ec1b5aaca4fbf4ac07bb8fba")
    set(patched1 "c5181f604c9fd59d45712fd68c454645d36e3aae26f29d870038222fbb84eec4")
    set(count1 4)
    set(before1_0 [=[MidiInputDevice::~MidiInputDevice()]=])
    set(after1_0 [=[int MidiInputDevice::getInputChannelFor (const Edit& edit, EditItemID target)
{
    const Track* track = findTrackForID (edit, target);

    if (track == nullptr)
        if (auto slot = findClipSlotForID (edit, target))
            track = &slot->track;

    if (track == nullptr)
        return 0;

    const int channel = track->state.getProperty ("halftrackMidiInputChannel", 0);
    return channel >= 1 && channel <= 16 ? channel : 0;
}

MidiInputDevice::~MidiInputDevice()]=])
    set(before1_1 [=[        detail::ScopedActiveRecordingDevice scopedActiveRecordingDevice;
        const TimeRange punchRange;
        juce::MidiMessageSequence recorded;]=])
    set(after1_1 [=[        detail::ScopedActiveRecordingDevice scopedActiveRecordingDevice;
        const TimeRange punchRange;
        int channel = 0; // Halftrack: the only channel recorded, or 0 for every one
        juce::MidiMessageSequence recorded;]=])
    set(before1_2 [=[            auto recContext = std::make_unique<MidiRecordingContext> (context,
                                                                      targetID,
                                                                      params.punchRange);
            results.emplace_back (std::move (recContext));]=])
    set(after1_2 [=[            auto recContext = std::make_unique<MidiRecordingContext> (context,
                                                                      targetID,
                                                                      params.punchRange);
            recContext->channel = MidiInputDevice::getInputChannelFor (edit, targetID);
            results.emplace_back (std::move (recContext));]=])
    set(before1_3 [=[            for (auto& recContext : recordingContexts)
                recContext->liveNotes->push (m1);

            const std::shared_lock sl (contextLock);

            for (auto& recContext : recordingContexts)
                recContext->recorded.addEvent (m2);]=])
    set(after1_3 [=[            // Halftrack: a target recording one channel takes only that channel's messages
            const auto wanted = [&message] (const MidiRecordingContext& c) { return c.channel == 0 || message.getChannel() == 0 || message.getChannel() == c.channel; };

            for (auto& recContext : recordingContexts)
                if (wanted (*recContext))
                    recContext->liveNotes->push (m1);

            const std::shared_lock sl (contextLock);

            for (auto& recContext : recordingContexts)
                if (wanted (*recContext))
                    recContext->recorded.addEvent (m2);]=])
    set(source2 "${source_dir}/modules/tracktion_engine/playback/graph/tracktion_MidiInputDeviceNode.h")
    set(original2 "6103673d444ed07506ab92f96fbfb835639ececeb834c5b936d6bd0838d146ef")
    set(patched2 "90dec853c902a4349a64b450007f314c7380b5f414494d482385c5f6b33cacde")
    set(count2 1)
    set(before2_0 [=[    const EditItemID targetID;
    const bool useAccurateReceiptClock;]=])
    set(after2_0 [=[    const EditItemID targetID;
    const int inputChannel; // Halftrack: the track's MIDI From channel, or 0 for every channel
    const bool useAccurateReceiptClock;]=])
    set(source3 "${source_dir}/modules/tracktion_engine/playback/graph/tracktion_MidiInputDeviceNode.cpp")
    set(original3 "b0e8e73a5ab9f5dd1d13e96c2adc84313ed3a579ddb8ca7d80b6564a2c497000")
    set(patched3 "7f8dd4ed48893d3ba46e4b09a44404dd9bcf334f772aa09be98a06573892cd74")
    set(count3 2)
    set(before3_0 [=[      targetID (targetID_),
      useAccurateReceiptClock (! owner.isTrackDevice() && ! HostedAudioDeviceInterface::isHostedMidiInputDevice (owner))
{
}]=])
    set(after3_0 [=[      targetID (targetID_),
      inputChannel (MidiInputDevice::getInputChannelFor (idi.edit, targetID_)),
      useAccurateReceiptClock (! owner.isTrackDevice() && ! HostedAudioDeviceInterface::isHostedMidiInputDevice (owner))
{
}]=])
    set(before3_1 [=[    if (active != this)
        return;

    auto channelToUse = midiInputDevice.getChannelToUse().getChannelNumber();]=])
    set(after3_1 [=[    if (active != this)
        return;

    // Halftrack: a track taking one channel lets only that channel's messages in
    if (inputChannel > 0 && message.getChannel() > 0 && message.getChannel() != inputChannel)
        return;

    auto channelToUse = midiInputDevice.getChannelToUse().getChannelNumber();]=])
    foreach(index RANGE 0 3)
        file(SHA256 "${source${index}}" hash)
        if(hash STREQUAL "${patched${index}}" OR (DEFINED downstream${index} AND hash IN_LIST downstream${index}))
            set(write${index} FALSE)
            continue()
        endif()
        if(NOT hash STREQUAL "${original${index}}")
            message(FATAL_ERROR "Halftrack MIDI input channel patch requires pinned source ${source${index}}; unexpected SHA256 ${hash}.")
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
            message(FATAL_ERROR "Halftrack MIDI input channel patch produced unexpected SHA256 ${hash} for ${source${index}}.")
        endif()
        set(write${index} TRUE)
    endforeach()
    foreach(index RANGE 0 3)
        if(write${index})
            string(REGEX REPLACE "\n$" "" configure_content "${content${index}}")
            file(CONFIGURE OUTPUT "${source${index}}" CONTENT "@configure_content@" @ONLY NEWLINE_STYLE LF)
            file(SHA256 "${source${index}}" hash)
            if(NOT hash STREQUAL "${patched${index}}")
                message(FATAL_ERROR "Halftrack MIDI input channel patch wrote unexpected SHA256 ${hash}.")
            endif()
        endif()
    endforeach()
endfunction()
