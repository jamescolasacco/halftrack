# Halftrack track-input routing for pinned Tracktion Engine, after the render sends-only patch.
# A track can take another track's audio as its input (Audio From a track). The engine offered
# only the source's output after its mixer, and renders built neither the send nor the input, so
# the routed audio was missing from exports. Sources now also send Pre FX and Post FX taps, the
# destination reads the tap its input destination names, and a render, which has no playback
# context, reads the routes from the edit's input state and includes them while they are
# monitored. Hardware inputs are still left out of renders.
# Retains all upstream source notices. Validates every file before writing any.
function(halftrack_patch_render_track_inputs source_dir)
    set(source0 "${source_dir}/modules/tracktion_engine/playback/graph/tracktion_EditNodeBuilder.cpp")
    set(original0 "51ef9b7dfa77221251da5f43f77f91110f8b375de8974e6f45cac2c08c98ad48")
    set(patched0 "06c1d2be2f00c494f5db33c2b5ced64f9395e9fb7e16e4e8002ef7beacfa9ee8")
    set(count0 5)
    set(downstream0 "bfc26bb83dcc389b9b3cd64db016f5e493e7f1f982b7c8910be6524c30703766" "d5ebc0acfbfc0dae66b15881ad2a8f9ece557e737c0c8ad7d4f7b8038a30d647" "1a07134c8c4c2a229b4da262dc6b547d3ab1302496fd5e5fbf1d375e9614517a") # The later MIDI variations, sidechain taps and MIDI ports patches.
    set(before0_0 [=[    int getMidiInputDeviceBusID (EditItemID trackItemID)
    {
        constexpr size_t midiMagicNum = 0x9a2762;
        return static_cast<int> (hash (midiMagicNum, trackItemID.getRawID()));
    }]=])
    set(after0_0 [=[    int getMidiInputDeviceBusID (EditItemID trackItemID)
    {
        constexpr size_t midiMagicNum = 0x9a2762;
        return static_cast<int> (hash (midiMagicNum, trackItemID.getRawID()));
    }

    // Halftrack: a track taking another track's audio as its input reads one of its taps, named by
    // its input destination: 0 Pre FX, 1 Post FX or 2 Post Mixer (the device's own bus, the default).
    int getTrackInputTap (const juce::ValueTree& destination)
    {
        return juce::jlimit (0, 2, static_cast<int> (destination.getProperty ("halftrackInputTap", 2)));
    }

    int getWaveInputDeviceTapBusID (EditItemID trackItemID, int tap)
    {
        if (tap == 2)
            return getWaveInputDeviceBusID (trackItemID);

        constexpr size_t tapMagicNum = 0xc1abdf;
        return static_cast<int> (hash (tapMagicNum + static_cast<size_t> (tap), trackItemID.getRawID()));
    }]=])
    set(before0_1 [=[        if (waveDevice->isTrackDevice())
            if (auto sourceTrack = getTrackContainingTrackDevice (inputDeviceInstance.edit, *waveDevice))
            {
                waveDevice->setChannelConfiguration (sourceTrack->getChannelConfiguration());
                return makeNode<TrackWaveInputDeviceNode> (params.processState,
                                                           *waveDevice,
                                                           makeNode<ReturnNode> (getWaveInputDeviceBusID (sourceTrack->itemID)),
                                                           shouldMonitorTrackDevice (inputDeviceInstance));
            }]=])
    set(after0_1 [=[        if (waveDevice->isTrackDevice())
            if (auto sourceTrack = getTrackContainingTrackDevice (inputDeviceInstance.edit, *waveDevice))
            {
                int tap = 2;

                for (auto destination : inputDeviceInstance.destinations.objects)
                    if (destination->targetID == trackID)
                        tap = getTrackInputTap (destination->state);

                waveDevice->setChannelConfiguration (sourceTrack->getChannelConfiguration());
                return makeNode<TrackWaveInputDeviceNode> (params.processState,
                                                           *waveDevice,
                                                           makeNode<ReturnNode> (getWaveInputDeviceTapBusID (sourceTrack->itemID, tap)),
                                                           shouldMonitorTrackDevice (inputDeviceInstance));
            }]=])
    set(before0_2 [=[                    if (auto node = createLiveInputNodeForDevice (*in, playHeadState, params, track.itemID))
                        nodes.push_back (std::move (node));

    if (nodes.empty())
        return {};]=])
    set(after0_2 [=[                    if (auto node = createLiveInputNodeForDevice (*in, playHeadState, params, track.itemID))
                        nodes.push_back (std::move (node));

    // Halftrack: a render has no playback context, so it reads the tracks this one takes as its
    // input from the edit's input state, and carries them while it monitors them, as playback does.
    // Hardware inputs are still left out.
    if (params.forRendering)
        for (auto device : track.edit.state.getChildWithName (IDs::INPUTDEVICES))
            if (auto source = findAudioTrackForID (track.edit, EditItemID::fromProperty (device, IDs::sourceTrack));
                source != nullptr && source != &track && device[IDs::type].toString() == "audio" && source->getWaveInputDevice().isEnabled())
                for (auto destination : device)
                    if (destination.hasType (IDs::INPUTDEVICEDESTINATION) && EditItemID::fromProperty (destination, IDs::targetID) == track.itemID)
                    {
                        const auto mode = source->getWaveInputDevice().getMonitorMode();

                        if (mode == InputDevice::MonitorMode::on
                            || (mode == InputDevice::MonitorMode::automatic && static_cast<bool> (destination[IDs::armed])))
                            nodes.push_back (makeNode<ReturnNode> (getWaveInputDeviceTapBusID (source->itemID, getTrackInputTap (destination))));
                    }

    if (nodes.empty())
        return {};]=])
    set(before0_3 [=[std::unique_ptr<tracktion::graph::Node> createPluginNodeForList (PluginList& list, const TrackMuteState* trackMuteState, std::unique_ptr<Node> node,
                                                                 tracktion::graph::PlayHeadState& playHeadState, const CreateNodeParams& params)
{
    for (auto p : list)
    {
        if (! params.forRendering && p->isFrozen())
            continue;
]=])
    set(after0_3 [=[std::unique_ptr<tracktion::graph::Node> createPluginNodeForList (PluginList& list, const TrackMuteState* trackMuteState, std::unique_ptr<Node> node,
                                                                 tracktion::graph::PlayHeadState& playHeadState, const CreateNodeParams& params)
{
    // Halftrack: a track whose audio another track takes as its input sends its Pre FX tap after
    // its instrument (before its first plug-in on an audio track) and its Post FX tap before its
    // own volume plug-in. Both precede the track's muting, as its mixer does.
    auto tapTrack = dynamic_cast<AudioTrack*> (list.getOwnerTrack());

    if (tapTrack != nullptr && (&tapTrack->pluginList != &list || ! tapTrack->getWaveInputDevice().isEnabled()))
        tapTrack = nullptr;

    const auto plugins = list.getPlugins();
    int preTapIndex = 0;
    auto volume = tapTrack != nullptr ? tapTrack->getVolumePlugin() : nullptr;

    if (tapTrack != nullptr)
        for (int i = 0; i < plugins.size(); ++i)
            if (plugins[i]->isSynth())
                preTapIndex = i + 1;

    bool postTapped = false;

    for (int index = 0; index <= plugins.size(); ++index)
    {
        if (tapTrack != nullptr && node != nullptr)
        {
            if (index == preTapIndex)
                node = makeNode<SendNode> (std::move (node), getWaveInputDeviceTapBusID (tapTrack->itemID, 0));

            if (! postTapped && (index == plugins.size() || plugins[index] == volume))
            {
                node = makeNode<SendNode> (std::move (node), getWaveInputDeviceTapBusID (tapTrack->itemID, 1));
                postTapped = true;
            }
        }

        if (index == plugins.size())
            break;

        Plugin* p = plugins.getUnchecked (index).get();

        if (! params.forRendering && p->isFrozen())
            continue;
]=])
    set(before0_4 [=[    if (! params.forRendering)
    {
        if (at.getWaveInputDevice().isEnabled())
            node = makeNode<SendNode> (std::move (node), getWaveInputDeviceBusID (at.itemID));

        if (at.getMidiInputDevice().isEnabled())
            node = makeNode<SendNode> (std::move (node), getMidiInputDeviceBusID (at.itemID));
    }]=])
    set(after0_4 [=[    // Halftrack: renders send a track's output to the tracks taking it as their input too.
    if (at.getWaveInputDevice().isEnabled())
        node = makeNode<SendNode> (std::move (node), getWaveInputDeviceBusID (at.itemID));

    if (! params.forRendering)
    {
        if (at.getMidiInputDevice().isEnabled())
            node = makeNode<SendNode> (std::move (node), getMidiInputDeviceBusID (at.itemID));
    }]=])
    foreach(index RANGE 0 0)
        file(SHA256 "${source${index}}" hash)
        if(hash STREQUAL "${patched${index}}" OR (DEFINED downstream${index} AND hash IN_LIST downstream${index}))
            set(write${index} FALSE)
            continue()
        endif()
        if(NOT hash STREQUAL "${original${index}}")
            message(FATAL_ERROR "Halftrack render track-inputs patch requires pinned source ${source${index}}; unexpected SHA256 ${hash}.")
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
            message(FATAL_ERROR "Halftrack render track-inputs patch produced unexpected SHA256 ${hash} for ${source${index}}.")
        endif()
        set(write${index} TRUE)
    endforeach()
    foreach(index RANGE 0 0)
        if(write${index})
            string(REGEX REPLACE "\n$" "" configure_content "${content${index}}")
            file(CONFIGURE OUTPUT "${source${index}}" CONTENT "@configure_content@" @ONLY NEWLINE_STYLE LF)
            file(SHA256 "${source${index}}" hash)
            if(NOT hash STREQUAL "${patched${index}}")
                message(FATAL_ERROR "Halftrack render track-inputs patch wrote unexpected SHA256 ${hash}.")
            endif()
        endif()
    endforeach()
endfunction()
