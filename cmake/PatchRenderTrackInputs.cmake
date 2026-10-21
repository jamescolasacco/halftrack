# Halftrack render routing for pinned Tracktion Engine, after the render sends-only patch.
# Playback carries a track's output to the tracks that take it as their input (Audio From a
# track), but renders built neither the send nor the input, so the routed audio was missing
# from exports. A render has no playback context, so it reads the routes from the edit's input
# state, and includes them while they are monitored. Hardware inputs are still left out.
# Retains all upstream source notices. Validates every file before writing any.
function(halftrack_patch_render_track_inputs source_dir)
    set(source0 "${source_dir}/modules/tracktion_engine/playback/graph/tracktion_EditNodeBuilder.cpp")
    set(original0 "51ef9b7dfa77221251da5f43f77f91110f8b375de8974e6f45cac2c08c98ad48")
    set(patched0 "e871f1b3ca972a8b8a56cccc53b2958c5fcbaf9767a4f5ec1e1767b4bc34d20a")
    set(count0 2)
    set(before0_0 [=[                    if (auto node = createLiveInputNodeForDevice (*in, playHeadState, params, track.itemID))
                        nodes.push_back (std::move (node));

    if (nodes.empty())
        return {};]=])
    set(after0_0 [=[                    if (auto node = createLiveInputNodeForDevice (*in, playHeadState, params, track.itemID))
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
                            nodes.push_back (makeNode<ReturnNode> (getWaveInputDeviceBusID (source->itemID)));
                    }

    if (nodes.empty())
        return {};]=])
    set(before0_1 [=[    if (! params.forRendering)
    {
        if (at.getWaveInputDevice().isEnabled())
            node = makeNode<SendNode> (std::move (node), getWaveInputDeviceBusID (at.itemID));

        if (at.getMidiInputDevice().isEnabled())
            node = makeNode<SendNode> (std::move (node), getMidiInputDeviceBusID (at.itemID));
    }]=])
    set(after0_1 [=[    // Halftrack: renders send a track's output to the tracks taking it as their input too.
    if (at.getWaveInputDevice().isEnabled())
        node = makeNode<SendNode> (std::move (node), getWaveInputDeviceBusID (at.itemID));

    if (! params.forRendering)
    {
        if (at.getMidiInputDevice().isEnabled())
            node = makeNode<SendNode> (std::move (node), getMidiInputDeviceBusID (at.itemID));
    }]=])
    foreach(index RANGE 0 0)
        file(SHA256 "${source${index}}" hash)
        if(hash STREQUAL "${patched${index}}" OR (DEFINED downstream${index} AND hash STREQUAL "${downstream${index}}"))
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
