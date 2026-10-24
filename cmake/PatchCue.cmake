# Halftrack cue output for pinned Tracktion Engine, after every earlier patch. The reference cues a
# track from its Post FX tap (pre-fader listen, unity and unpanned) to its Cue Out, where its click
# and previews also play, at the cue level. The engine behaviour gains three hooks the host answers:
# whether a track is cued, the cue's output device, and the cue level (on the audio thread). The
# node builder keeps a cued track's taps, sums the cued tracks' Post FX taps on the cue's device and
# plays the click there at the cue level, after the master's devices and meter.
# Retains all upstream source notices. Validates every file before writing any.
function(halftrack_patch_cue source_dir)
    set(source0 "${source_dir}/modules/tracktion_engine/utilities/tracktion_EngineBehaviour.h")
    set(original0 "d7306d85635ac5cc07de62145c249a8a5ebbea87ef43002918493ba223d25e95")
    set(patched0 "296e079a8c70e5d13fd8aec6c640daa56c325f1cf33e836e69d2b3037c14ecc6")
    set(count0 1)
    set(before0_0 [=[    virtual TimeDuration getAudioClipPreRoll (AudioClipBase&)                       { return {}; }
]=])
    set(after0_0 [=[    virtual TimeDuration getAudioClipPreRoll (AudioClipBase&)                       { return {}; }

    /// Halftrack: the cue output. Whether a track is cued, heard on the cue output from its Post FX
    /// tap, and the output device the cue plays on (empty for the default one), both asked when
    /// the graph is built; and the cue level, asked on the audio thread, so it must be lock-free.
    /// The click plays at the cue level.
    virtual bool isTrackCued (AudioTrack&)                                          { return false; }
    virtual juce::String getCueOutputDeviceID (Edit&)                               { return {}; }
    virtual float getCueGain (Edit&)                                                { return 1.0f; }
]=])
    set(source1 "${source_dir}/modules/tracktion_engine/playback/graph/tracktion_EditNodeBuilder.cpp")
    set(original1 "9e1afcb494391df977c7f7950ce8030410576a2fab891e757d30a0249f87d60c")
    set(patched1 "bbc12a579f50896a7be4a5c13127b63c47863b22986b776290a7d523f80866cf")
    set(downstream1 "05e73d01d1cc0a824f96a7dad772a850e5906e3a645bf56affd072f91d986bcc") # The later group outputs patch.
    set(count1 4)
    set(before1_0 [=[    // Halftrack: a plug-in reads its sidechain source unless its sidechain is switched off.]=])
    set(after1_0 [=[    // Halftrack: the cue output at the cue level, which the host gives on the audio thread; a change
    // ramps across the block. The output has the device's channels: a mono tap plays on each, and a mono
    // device hears both sides of a stereo one.
    class CueGainNode final : public tracktion::graph::Node
    {
    public:
        CueGainNode (std::unique_ptr<Node> inputNode, Edit& e, int channels)
            : input (std::move (inputNode)), edit (e), numChannels (channels) {}

        tracktion::graph::NodeProperties getNodeProperties() override
        {
            auto props = input->getNodeProperties();
            props.numberOfChannels = numChannels;
            hash_combine (props.nodeID, static_cast<size_t> (0x63756567u + static_cast<size_t> (numChannels)));
            return props;
        }

        std::vector<Node*> getDirectInputNodes() override     { return { input.get() }; }
        bool isReadyToProcess() override                        { return input->hasProcessed(); }

        void prepareToPlay (const tracktion::graph::PlaybackInitialisationInfo&) override
        {
            gain = edit.engine.getEngineBehaviour().getCueGain (edit);
        }

        void process (ProcessContext& pc) override
        {
            auto source = input->getProcessedOutput();
            pc.buffers.midi.copyFrom (source.midi);
            const auto target = edit.engine.getEngineBehaviour().getCueGain (edit);
            const auto frames = pc.buffers.audio.size.numFrames;
            const auto outputs = pc.buffers.audio.size.numChannels, inputs = source.audio.size.numChannels;

            for (choc::buffer::ChannelCount channel = 0; channel < outputs; ++channel)
            {
                const auto dest = pc.buffers.audio.getIterator (channel).sample;

                for (choc::buffer::FrameCount i = 0; i < frames; ++i)
                {
                    float in = 0.0f;

                    if (inputs == 1)
                        in = source.audio.getSample (0, i);
                    else if (outputs == 1 && inputs > 1)
                        in = 0.5f * (source.audio.getSample (0, i) + source.audio.getSample (1, i));
                    else if (channel < inputs)
                        in = source.audio.getSample (channel, i);

                    dest[i] = in * (gain + (target - gain) * static_cast<float> (i + 1) / static_cast<float> (frames));
                }
            }

            gain = target;
        }

    private:
        std::unique_ptr<Node> input;
        Edit& edit;
        const int numChannels;
        float gain = 1.0f;
    };

    // Halftrack: a plug-in reads its sidechain source unless its sidechain is switched off.]=])
    set(before1_1 [=[    if (tapTrack != nullptr && (&tapTrack->pluginList != &list || ! (tapTrack->getWaveInputDevice().isEnabled() || isSidechainSource (*tapTrack))))
        tapTrack = nullptr;]=])
    set(after1_1 [=[    // Halftrack: and a cued track, whose Post FX tap the cue output hears.
    if (tapTrack != nullptr && (&tapTrack->pluginList != &list || ! (tapTrack->getWaveInputDevice().isEnabled() || isSidechainSource (*tapTrack)
                                                                       || list.getEdit().engine.getEngineBehaviour().isTrackCued (*tapTrack))))
        tapTrack = nullptr;]=])
    set(before1_2 [=[    // Add deviceNodes for any devices only being used by the click track]=])
    set(after1_2 [=[    // Halftrack: the device the cue output plays on, and the cued tracks it hears.
    OutputDevice* cueDevice = nullptr;
    std::vector<AudioTrack*> cuedTracks;

    {
        auto& behaviour = edit.engine.getEngineBehaviour();
        auto& dm = edit.engine.getDeviceManager();
        const auto cueID = behaviour.getCueOutputDeviceID (edit);
        cueDevice = cueID.isEmpty() ? static_cast<OutputDevice*> (dm.getDefaultWaveOutDevice()) : dm.findOutputDeviceForID (cueID);

        for (auto t : getAudioTracks (edit))
            if ((params.allowedTracks == nullptr || params.allowedTracks->contains (t)) && behaviour.isTrackCued (*t))
                cuedTracks.push_back (t);

        if (cueDevice != nullptr && cueDevice->isEnabled() && ! cueDevice->isMidi() && ! cuedTracks.empty())
            deviceNodes[cueDevice];
        else
            cueDevice = nullptr;
    }

    // Add deviceNodes for any devices only being used by the click track]=])
    set(before1_3 [=[        if (edit.isClickTrackDevice (*device))
        {
            auto clickAndTracksNode = makeSummingNode ({ node.release(),
                                                         makeNode<ClickNode> (edit, getNumChannelsFromDevice (*device),
                                                                              device->isMidi(), playHeadState.playHead).release() });
            node = std::move (clickAndTracksNode);
        }]=])
    set(after1_3 [=[        // Halftrack: the cue output, at the cue level: the cued tracks' Post FX taps on the cue's
        // device, each on all its channels, and the click on its own. A MIDI click is left as it was.
        std::vector<std::unique_ptr<Node>> cue;
        const auto cueChannels = getNumChannelsFromDevice (*device);

        if (device == cueDevice)
            for (auto t : cuedTracks)
                cue.push_back (makeNode<CueGainNode> (makeNode<ReturnNode> (getWaveInputDeviceTapBusID (t->itemID, 1)), edit, cueChannels));

        if (edit.isClickTrackDevice (*device))
        {
            auto click = makeNode<ClickNode> (edit, cueChannels, device->isMidi(), playHeadState.playHead);

            if (device->isMidi())
                node = makeSummingNode ({ node.release(), click.release() });
            else
                cue.push_back (makeNode<CueGainNode> (std::move (click), edit, cueChannels));
        }

        if (! cue.empty())
        {
            auto sum = std::make_unique<SummingNode> (std::move (cue));
            sum->addInput (std::move (node));
            node = std::move (sum);
        }]=])
    foreach(index RANGE 0 1)
        file(SHA256 "${source${index}}" hash)
        if(hash STREQUAL "${patched${index}}" OR (DEFINED downstream${index} AND hash IN_LIST downstream${index}))
            set(write${index} FALSE)
            continue()
        endif()
        if(NOT hash STREQUAL "${original${index}}")
            message(FATAL_ERROR "Halftrack cue patch requires pinned source ${source${index}}; unexpected SHA256 ${hash}.")
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
            message(FATAL_ERROR "Halftrack cue patch produced unexpected SHA256 ${hash} for ${source${index}}.")
        endif()
        set(write${index} TRUE)
    endforeach()
    foreach(index RANGE 0 1)
        if(write${index})
            string(REGEX REPLACE "\n$" "" configure_content "${content${index}}")
            file(CONFIGURE OUTPUT "${source${index}}" CONTENT "@configure_content@" @ONLY NEWLINE_STYLE LF)
            file(SHA256 "${source${index}}" hash)
            if(NOT hash STREQUAL "${patched${index}}")
                message(FATAL_ERROR "Halftrack cue patch wrote unexpected SHA256 ${hash}.")
            endif()
        endif()
    endforeach()
endfunction()
