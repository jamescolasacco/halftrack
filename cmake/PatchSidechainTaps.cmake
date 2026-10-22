# Halftrack sidechain taps for pinned Tracktion Engine, after the MIDI variations patch.
# A plug-in with a sidechain input reads another track's audio, as in the reference, at the
# source's Pre FX, Post FX (the default) or Post Mixer tap, the three taps track inputs read.
# The engine sent a sidechain source's audio after its devices and mixer, before its muting, to
# a bus of its own. Sources now send the track-input taps instead (Post Mixer after the muting),
# plug-ins read the tap their state names, and a plug-in whose sidechain is switched off reads
# none, so its source sends nothing for it.
# Retains all upstream source notices. Validates every file before writing any.
function(halftrack_patch_sidechain_taps source_dir)
    set(source0 "${source_dir}/modules/tracktion_engine/playback/graph/tracktion_EditNodeBuilder.cpp")
    set(original0 "bfc26bb83dcc389b9b3cd64db016f5e493e7f1f982b7c8910be6524c30703766")
    set(patched0 "d5ebc0acfbfc0dae66b15881ad2a8f9ece557e737c0c8ad7d4f7b8038a30d647")
    set(count0 8)
    set(before0_0 [=[    int getSidechainBusID (EditItemID sidechainSourceID)
    {]=])
    set(after0_0 [=[    // Halftrack: plug-ins read their sidechain at one of the source's taps instead.
    [[maybe_unused]] int getSidechainBusID (EditItemID sidechainSourceID)
    {]=])
    set(before0_1 [=[    bool isSidechainSource (Track& t)
    {
        const auto itemID = t.itemID;

        for (auto p : t.edit.getPluginCache().getPlugins())
            if (p->getSidechainSourceID() == itemID)
                return true;

        return false;
    }]=])
    set(after0_1 [=[    // Halftrack: a plug-in reads its sidechain source unless its sidechain is switched off.
    bool readsSidechain (Plugin& p)
    {
        return ! p.isMissing() && p.getSidechainSourceID().isValid() && ! static_cast<bool> (p.state.getProperty ("halftrackSidechainOff", false));
    }

    // Halftrack: the source's tap a plug-in's sidechain reads: 0 Pre FX, 1 Post FX (the default) or 2 Post Mixer.
    int getSidechainTap (Plugin& p)
    {
        return juce::jlimit (0, 2, static_cast<int> (p.state.getProperty ("halftrackSidechainTap", 1)));
    }

    bool isSidechainSource (Track& t)
    {
        const auto itemID = t.itemID;

        for (auto p : t.edit.getPluginCache().getPlugins())
            if (p->getSidechainSourceID() == itemID && readsSidechain (*p))
                return true;

        return false;
    }]=])
    set(before0_2 [=[    if (isSidechainSource (track))
        node = makeNode<SendNode> (std::move (node), getSidechainBusID (track.itemID));

    node = makeNode<TrackMutingNode> (std::move (trackMuteState), std::move (node), false);

    return node;]=])
    set(after0_2 [=[    node = makeNode<TrackMutingNode> (std::move (trackMuteState), std::move (node), false);

    // Halftrack: a frozen sidechain source sends its Post Mixer tap after its muting; its plug-in
    // list sent the other two.
    if (isSidechainSource (track))
        node = makeNode<SendNode> (std::move (node), getWaveInputDeviceTapBusID (track.itemID, 2));

    return node;]=])
    set(before0_3 [=[    const auto sidechainSourceID = plugin.getSidechainSourceID();
    const bool usesSidechain = ! plugin.isMissing() && sidechainSourceID.isValid();]=])
    set(after0_3 [=[    const auto sidechainSourceID = plugin.getSidechainSourceID();
    const bool usesSidechain = readsSidechain (plugin);]=])
    set(before0_4 [=[    auto sidechainInput = makeNode<ReturnNode> (getSidechainBusID (sidechainSourceID),]=])
    set(after0_4 [=[    auto sidechainInput = makeNode<ReturnNode> (getWaveInputDeviceTapBusID (sidechainSourceID, getSidechainTap (plugin)),]=])
    set(before0_5 [=[    const bool usesSidechain = plugin.getSidechainSourceID().isValid();]=])
    set(after0_5 [=[    const bool usesSidechain = readsSidechain (plugin);]=])
    set(before0_6 [=[    if (tapTrack != nullptr && (&tapTrack->pluginList != &list || ! tapTrack->getWaveInputDevice().isEnabled()))
        tapTrack = nullptr;]=])
    set(after0_6 [=[    // Halftrack: as does a track a plug-in takes its sidechain from.
    if (tapTrack != nullptr && (&tapTrack->pluginList != &list || ! (tapTrack->getWaveInputDevice().isEnabled() || isSidechainSource (*tapTrack))))
        tapTrack = nullptr;]=])
    set(before0_7 [=[    if (isSidechainSource (at))
        node = makeNode<SendNode> (std::move (node), getSidechainBusID (at.itemID));

    node = makeNode<TrackMutingNode> (std::move (trackMuteState), std::move (node), false);

    // Halftrack: renders send a track's output to the tracks taking it as their input too.
    if (at.getWaveInputDevice().isEnabled())]=])
    set(after0_7 [=[    node = makeNode<TrackMutingNode> (std::move (trackMuteState), std::move (node), false);

    // Halftrack: renders send a track's output to the tracks taking it as their input too, and a
    // sidechain source sends it to the plug-ins reading its Post Mixer tap.
    if (at.getWaveInputDevice().isEnabled() || isSidechainSource (at))]=])
    foreach(index RANGE 0 0)
        file(SHA256 "${source${index}}" hash)
        if(hash STREQUAL "${patched${index}}")
            set(write${index} FALSE)
            continue()
        endif()
        if(NOT hash STREQUAL "${original${index}}")
            message(FATAL_ERROR "Halftrack sidechain taps patch requires pinned source ${source${index}}; unexpected SHA256 ${hash}.")
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
            message(FATAL_ERROR "Halftrack sidechain taps patch produced unexpected SHA256 ${hash} for ${source${index}}.")
        endif()
        set(write${index} TRUE)
    endforeach()
    foreach(index RANGE 0 0)
        if(write${index})
            string(REGEX REPLACE "\n$" "" configure_content "${content${index}}")
            file(CONFIGURE OUTPUT "${source${index}}" CONTENT "@configure_content@" @ONLY NEWLINE_STYLE LF)
            file(SHA256 "${source${index}}" hash)
            if(NOT hash STREQUAL "${patched${index}}")
                message(FATAL_ERROR "Halftrack sidechain taps patch wrote unexpected SHA256 ${hash}.")
            endif()
        endif()
    endforeach()
endfunction()
