# Halftrack MIDI ports for pinned Tracktion Engine, after the MIDI variations, input take rescan and
# sidechain taps patches. The engine behaviour can leave physical MIDI ports out, so a test host sees
# only the ports it makes, and the device scan honours it. A track playing to a MIDI port can name
# the channel (1-16) its MIDI goes out on in its halftrackMidiOutChannel property, as the
# reference's MIDI To channel chooser does; a node at the end of the track rewrites each channel
# message's channel, allocating nothing. Newly found MIDI outputs open only as the host chooses.
# Retains all upstream source notices. Validates every file before writing any.
function(halftrack_patch_midi_ports source_dir)
    set(source0 "${source_dir}/modules/tracktion_engine/utilities/tracktion_EngineBehaviour.h")
    set(original0 "5721d06c407ce781dee9c3e804981f3a283fc98d26d412f2ce27246381ba9eaa")
    set(patched0 "546f1bc2652ba56474e93efabe4f054f8c699cebb2fb446b341a6e176af339e3")
    set(count0 1)
    set(before0_0 [=[    virtual bool usePhysicalMidiDevices()                                           { return true; }
]=])
    set(after0_0 [=[    virtual bool usePhysicalMidiDevices()                                           { return true; }

    /// Halftrack: whether one physical MIDI port takes part when physical devices are used. A test
    /// host uses it to see only the ports it makes.
    virtual bool shouldUsePhysicalMidiDevice (const juce::MidiDeviceInfo&, bool /*isInput*/)    { return true; }

    /// Halftrack: whether a newly found MIDI output opens, until its own setting says otherwise.
    virtual bool shouldOpenMidiOutputByDefault()                                    { return true; }
]=])
    set(source1 "${source_dir}/modules/tracktion_engine/playback/devices/tracktion_MidiOutputDevice.cpp")
    set(original1 "330630e1e82ae881bf7c65cce26bebe7961a26531174d1ca53b45021b7658b82")
    set(patched1 "03dbf07bcf6a2483a744e2f1751f452dead34c6b7d89ba27d15db093790a2a64")
    set(count1 1)
    set(before1_0 [=[    enabled = true;

    timecodeGenerator = std::make_unique<MidiTimecodeGenerator>();]=])
    set(after1_0 [=[    // Halftrack: the host chooses whether a newly found output opens.
    enabled = e.getEngineBehaviour().shouldOpenMidiOutputByDefault();

    timecodeGenerator = std::make_unique<MidiTimecodeGenerator>();]=])
    set(source2 "${source_dir}/modules/tracktion_engine/playback/tracktion_DeviceManager.cpp")
    set(original2 "896d6fc2b35ecaaffb1a8bca4b453880e01328a29eae4ee5e222c02da570ed36")
    set(patched2 "e653f519cdc6ae62cd56be45919cdfd5030e041a35e84ee77c18fadc4b008c91")
    set(count2 1)
    set(before2_0 [=[        auto comparator = [] (const juce::MidiDeviceInfo& a, const juce::MidiDeviceInfo& b)
        {]=])
    set(after2_0 [=[        // Halftrack: the host can leave physical ports out.
        auto& behaviour = e.getEngineBehaviour();
        midiIns.removeIf ([&behaviour] (const juce::MidiDeviceInfo& info) { return ! behaviour.shouldUsePhysicalMidiDevice (info, true); });
        midiOuts.removeIf ([&behaviour] (const juce::MidiDeviceInfo& info) { return ! behaviour.shouldUsePhysicalMidiDevice (info, false); });

        auto comparator = [] (const juce::MidiDeviceInfo& a, const juce::MidiDeviceInfo& b)
        {]=])
    set(source3 "${source_dir}/modules/tracktion_engine/playback/graph/tracktion_EditNodeBuilder.cpp")
    set(original3 "d5ebc0acfbfc0dae66b15881ad2a8f9ece557e737c0c8ad7d4f7b8038a30d647")
    set(patched3 "1a07134c8c4c2a229b4da262dc6b547d3ab1302496fd5e5fbf1d375e9614517a")
    set(count3 2)
    set(before3_0 [=[    // Halftrack: a plug-in reads its sidechain source unless its sidechain is switched off.]=])
    set(after3_0 [=[    // Halftrack: a track playing to a MIDI port sends its MIDI on the channel it names (1-16), as
    // the reference's MIDI To channel chooser does. Only channel messages change.
    class MidiChannelNode final : public tracktion::graph::Node
    {
    public:
        MidiChannelNode (std::unique_ptr<Node> inputNode, int channelToUse)
            : input (std::move (inputNode)), channel (channelToUse) {}

        tracktion::graph::NodeProperties getNodeProperties() override
        {
            auto props = input->getNodeProperties();
            hash_combine (props.nodeID, static_cast<size_t> (0x6d69646963686eu + static_cast<size_t> (channel)));
            return props;
        }

        std::vector<Node*> getDirectInputNodes() override     { return { input.get() }; }
        bool isReadyToProcess() override                        { return input->hasProcessed(); }

        void process (ProcessContext& pc) override
        {
            auto source = input->getProcessedOutput();
            copyIfNotAliased (pc.buffers.audio, source.audio);
            pc.buffers.midi.copyFrom (source.midi);

            for (auto& message : pc.buffers.midi)
                if (message.getChannel() > 0)
                    message.setChannel (channel);
        }

    private:
        std::unique_ptr<Node> input;
        const int channel;
    };

    // Halftrack: a plug-in reads its sidechain source unless its sidechain is switched off.]=])
    set(before3_1 [=[    if (! params.forRendering)
    {
        if (at.getMidiInputDevice().isEnabled())
            node = makeNode<SendNode> (std::move (node), getMidiInputDeviceBusID (at.itemID));
    }

    return node;
}

//==============================================================================
std::unique_ptr<tracktion::graph::Node> createNodeForSubmixTrack]=])
    set(after3_1 [=[    if (! params.forRendering)
    {
        if (at.getMidiInputDevice().isEnabled())
            node = makeNode<SendNode> (std::move (node), getMidiInputDeviceBusID (at.itemID));
    }

    // Halftrack: a track playing to a MIDI port can name the channel its MIDI goes out on.
    if (auto device = at.getOutput().getOutputDevice (false); device != nullptr && device->isMidi())
        if (const int channel = static_cast<int> (at.state.getProperty ("halftrackMidiOutChannel", 0)); channel >= 1 && channel <= 16)
            node = makeNode<MidiChannelNode> (std::move (node), channel);

    return node;
}

//==============================================================================
std::unique_ptr<tracktion::graph::Node> createNodeForSubmixTrack]=])
    foreach(index RANGE 0 3)
        file(SHA256 "${source${index}}" hash)
        if(hash STREQUAL "${patched${index}}" OR (DEFINED downstream${index} AND hash IN_LIST downstream${index}))
            set(write${index} FALSE)
            continue()
        endif()
        if(NOT hash STREQUAL "${original${index}}")
            message(FATAL_ERROR "Halftrack MIDI ports patch requires pinned source ${source${index}}; unexpected SHA256 ${hash}.")
        endif()
        file(READ "${source${index}}" content${index})
        math(EXPR last "${count${index}} - 1")
        foreach(pair RANGE 0 ${last})
            string(REPLACE "${before${index}_${pair}}" "${after${index}_${pair}}" content${index} "${content${index}}")
        endforeach()
        # The LF configure-write always ends with one newline; hash what will be written.
        string(REGEX MATCH "
$" ends_with_newline "${content${index}}")
        if(NOT ends_with_newline)
            string(APPEND content${index} "
")
        endif()
        string(SHA256 hash "${content${index}}")
        if(NOT hash STREQUAL "${patched${index}}")
            message(FATAL_ERROR "Halftrack MIDI ports patch produced unexpected SHA256 ${hash} for ${source${index}}.")
        endif()
        set(write${index} TRUE)
    endforeach()
    foreach(index RANGE 0 3)
        if(write${index})
            string(REGEX REPLACE "\n$" "" configure_content "${content${index}}")
            file(CONFIGURE OUTPUT "${source${index}}" CONTENT "@configure_content@" @ONLY NEWLINE_STYLE LF)
            file(SHA256 "${source${index}}" hash)
            if(NOT hash STREQUAL "${patched${index}}")
                message(FATAL_ERROR "Halftrack MIDI ports patch wrote unexpected SHA256 ${hash}.")
            endif()
        endif()
    endforeach()
endfunction()
