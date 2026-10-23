# Halftrack remote MIDI for pinned Tracktion Engine, after every earlier patch. A MIDI input device can
# have a remote control that sees each message before the device's instances and keeps the ones it
# takes from them, as the reference's mapped notes and controllers stop acting as track input; a
# note-off goes where its note-on went, so no note sticks when a mapping changes while it is held.
# Retains all upstream source notices. Validates every file before writing any.
function(halftrack_patch_remote_midi source_dir)
    set(source0 "${source_dir}/modules/tracktion_engine/playback/devices/tracktion_MidiInputDevice.h")
    set(original0 "e99fb2a7f68fb187d50e7d7f72fab502b4b03ca838eb82d560e6e101905c7101")
    set(patched0 "4caa2c5b2dfa88a8ff9f0d2ca15830e83c432ac27e3122de275c75875d1fe54e")
    set(count0 2)
    set(before0_0 [=[    MidiChannel getMidiChannelFor (int rawChannelNumber) const;
    MPESourceID getMPESourceID() const                              { return midiSourceID; }
]=])
    set(after0_0 [=[    MidiChannel getMidiChannelFor (int rawChannelNumber) const;
    MPESourceID getMPESourceID() const                              { return midiSourceID; }

    //==============================================================================
    /** Halftrack: a remote control sees the device's messages before its instances do and can
        keep them from them, as mapped notes and controllers stop acting as track input. It is
        called on the thread that delivers the device's messages. A note-off goes where its
        note-on went: to the remote, which must take it, or to the instances. A physical device
        does not pass what its remote takes on to the virtual devices that gather it.
    */
    struct RemoteControl
    {
        virtual ~RemoteControl() = default;
        /** Returns true to keep the message from the device's instances. */
        virtual bool takeMidiMessage (MidiInputDevice&, const juce::MidiMessage&) = 0;
    };

    /** Halftrack: sets or clears the device's remote control; it must outlive its use here. */
    void setRemoteControl (RemoteControl*);
]=])
    set(before0_1 [=[    void sendMessageToInstances (const juce::MidiMessage&, MPESourceID);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiInputDevice)]=])
    set(after0_1 [=[    /** Halftrack: returns true if the remote control took the message instead. */
    bool sendMessageToInstances (const juce::MidiMessage&, MPESourceID);

    juce::SpinLock remoteLock;
    RemoteControl* remoteControl = nullptr;
    std::array<bool, 16 * 128> remoteNotes {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiInputDevice)]=])
    set(source1 "${source_dir}/modules/tracktion_engine/playback/devices/tracktion_MidiInputDevice.cpp")
    set(original1 "c1c06cdd51d2e5bc7f7fae0696f9d0752757740cb33ca41b7d562acab68ce6c9")
    set(patched1 "5f31534136e50526dd3c30aa82a679e8f6a87a90ec1b5aaca4fbf4ac07bb8fba")
    set(count1 2)
    set(before1_0 [=[void MidiInputDevice::sendMessageToInstances (const juce::MidiMessage& message, MPESourceID sourceID)
{
    bool messageUnused = true;
]=])
    set(after1_0 [=[void MidiInputDevice::setRemoteControl (RemoteControl* newRemote)
{
    const juce::SpinLock::ScopedLockType sl (remoteLock);

    if (remoteControl != newRemote)
        remoteNotes.fill (false);

    remoteControl = newRemote;
}

bool MidiInputDevice::sendMessageToInstances (const juce::MidiMessage& message, MPESourceID sourceID)
{
    {
        // Halftrack: the remote control may take the message; a note-off follows its note-on.
        const juce::SpinLock::ScopedLockType sl (remoteLock);
        const auto note = message.isNoteOnOrOff() ? (size_t) ((message.getChannel() - 1) * 128 + message.getNoteNumber()) : remoteNotes.size();

        if (note < remoteNotes.size() && message.isNoteOff())
        {
            if (remoteNotes[note])
            {
                remoteNotes[note] = false;

                if (remoteControl != nullptr)
                    remoteControl->takeMidiMessage (*this, message);

                return true;
            }
        }
        else if (remoteControl != nullptr)
        {
            const bool taken = remoteControl->takeMidiMessage (*this, message);

            if (note < remoteNotes.size())
                remoteNotes[note] = taken;

            if (taken)
                return true;
        }
    }

    bool messageUnused = true;
]=])
    set(before1_1 [=[    if (messageUnused && message.isNoteOn())
        if (auto& warnOfWasted = engine.getDeviceManager().warnOfWastedMidiMessagesFunction)
            warnOfWasted (this);
}]=])
    set(after1_1 [=[    if (messageUnused && message.isNoteOn())
        if (auto& warnOfWasted = engine.getDeviceManager().warnOfWastedMidiMessagesFunction)
            warnOfWasted (this);

    return false;
}]=])
    set(source2 "${source_dir}/modules/tracktion_engine/playback/devices/tracktion_PhysicalMidiInputDevice.cpp")
    set(original2 "c4221217f5d2c1dfa57a02268d6a762f6c733ea95a17b9c53371a3c035a5a617")
    set(patched2 "baa5513d1ecb6d3aad29b74c85ef3393b118892654502a49903330b48877a2f6")
    set(count2 1)
    set(before2_0 [=[        auto message = m;

        if (handleIncomingMessage (message))
        {
            if (! tryToSendTimecode (message))
            {
                if (isTakingControllerMessages)
                    controllerParser->processMessage (message);

                sendMessageToInstances (message, sourceID);
            }
        }

        engine.getDeviceManager().broadcastMessageToAllVirtualDevices (*this, m);]=])
    set(after2_0 [=[        auto message = m;
        bool takenByRemote = false;

        if (handleIncomingMessage (message))
        {
            if (! tryToSendTimecode (message))
            {
                if (isTakingControllerMessages)
                    controllerParser->processMessage (message);

                takenByRemote = sendMessageToInstances (message, sourceID);
            }
        }

        // Halftrack: what the remote control took is not track input anywhere.
        if (! takenByRemote)
            engine.getDeviceManager().broadcastMessageToAllVirtualDevices (*this, m);]=])
    foreach(index RANGE 0 2)
        file(SHA256 "${source${index}}" hash)
        if(hash STREQUAL "${patched${index}}" OR (DEFINED downstream${index} AND hash IN_LIST downstream${index}))
            set(write${index} FALSE)
            continue()
        endif()
        if(NOT hash STREQUAL "${original${index}}")
            message(FATAL_ERROR "Halftrack remote MIDI patch requires pinned source ${source${index}}; unexpected SHA256 ${hash}.")
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
            message(FATAL_ERROR "Halftrack remote MIDI patch produced unexpected SHA256 ${hash} for ${source${index}}.")
        endif()
        set(write${index} TRUE)
    endforeach()
    foreach(index RANGE 0 2)
        if(write${index})
            string(REGEX REPLACE "\n$" "" configure_content "${content${index}}")
            file(CONFIGURE OUTPUT "${source${index}}" CONTENT "@configure_content@" @ONLY NEWLINE_STYLE LF)
            file(SHA256 "${source${index}}" hash)
            if(NOT hash STREQUAL "${patched${index}}")
                message(FATAL_ERROR "Halftrack remote MIDI patch wrote unexpected SHA256 ${hash}.")
            endif()
        endif()
    endforeach()
endfunction()
