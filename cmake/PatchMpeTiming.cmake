# Halftrack MPE timing for pinned Tracktion Engine, after the MIDI release velocity patch.
# MIDI clips build their playback in raw beats, but expressive (MPE) notes and their expression
# were placed in seconds there, which then played as beats: at 120 BPM everything came at half
# its time. Raw-beat sequences now place them in beats, as other notes are.
# Retains all upstream source notices. Validates every file before writing any.
function(halftrack_patch_mpe_timing source_dir)
    set(source0 "${source_dir}/modules/tracktion_engine/midi/tracktion_MidiList.cpp")
    set(original0 "93b05169b4008e66831d4ee3a9d42d065b32b011cd5f27c1dd04fd0a0854dcbd")
    set(patched0 "ed53b9158c0162bef0c3c041c473bd4e6e740bad5eee65c61e4f3e5775436949")
    set(count0 3)
    set(before0_0 [=[    const auto time = tb == MidiList::TimeBase::beats ? beat.inBeats()
                                                      : ts.toTime (beat).inSeconds();]=])
    set(after0_0 [=[    // Halftrack: raw-beat sequences are in beats too.
    const auto time = tb != MidiList::TimeBase::seconds ? beat.inBeats()
                                                        : ts.toTime (beat).inSeconds();]=])
    set(before0_1 [=[    const auto downTime = tb == MidiList::TimeBase::beats ? note.getPlaybackBeats (MidiNote::startEdge, clip, grooveTemplate).inBeats()
                                                          : note.getPlaybackTime (MidiNote::startEdge, clip, grooveTemplate).inSeconds();
    const auto upTime   = tb == MidiList::TimeBase::beats ? note.getPlaybackBeats (MidiNote::endEdge,   clip, grooveTemplate).inBeats()
                                                          : note.getPlaybackTime (MidiNote::endEdge,   clip, grooveTemplate).inSeconds();]=])
    set(after0_1 [=[    // Halftrack: raw-beat sequences place expressive notes at their own beats, as other notes are;
    // they were placed in seconds, which then played as beats.
    const auto edgeTime = [&] (MidiNote::NoteEdge edge)
    {
        if (tb == MidiList::TimeBase::beatsRaw)
            return edge == MidiNote::startEdge ? note.getStartBeat().inBeats() : note.getEndBeat().inBeats();

        return tb == MidiList::TimeBase::beats ? note.getPlaybackBeats (edge, clip, grooveTemplate).inBeats()
                                               : note.getPlaybackTime (edge, clip, grooveTemplate).inSeconds();
    };
    const auto downTime = edgeTime (MidiNote::startEdge);
    const auto upTime   = edgeTime (MidiNote::endEdge);]=])
    set(before0_2 [=[    const auto noteStartBeat = tb == MidiList::TimeBase::beats ? BeatPosition::fromBeats (downTime)
                                                               : clip.edit.tempoSequence.toBeats (TimePosition::fromSeconds (downTime));
    const auto noteEndBeat = tb == MidiList::TimeBase::beats ? BeatPosition::fromBeats (upTime)
                                                             : clip.edit.tempoSequence.toBeats (TimePosition::fromSeconds (upTime));]=])
    set(after0_2 [=[    const auto noteStartBeat = tb != MidiList::TimeBase::seconds ? BeatPosition::fromBeats (downTime)
                                                                 : clip.edit.tempoSequence.toBeats (TimePosition::fromSeconds (downTime));
    const auto noteEndBeat = tb != MidiList::TimeBase::seconds ? BeatPosition::fromBeats (upTime)
                                                               : clip.edit.tempoSequence.toBeats (TimePosition::fromSeconds (upTime));]=])
    foreach(index RANGE 0 0)
        file(SHA256 "${source${index}}" hash)
        if(hash STREQUAL "${patched${index}}")
            set(write${index} FALSE)
            continue()
        endif()
        if(NOT hash STREQUAL "${original${index}}")
            message(FATAL_ERROR "Halftrack MPE timing patch requires pinned source ${source${index}}; unexpected SHA256 ${hash}.")
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
            message(FATAL_ERROR "Halftrack MPE timing patch produced unexpected SHA256 ${hash} for ${source${index}}.")
        endif()
        set(write${index} TRUE)
    endforeach()
    foreach(index RANGE 0 0)
        if(write${index})
            string(REGEX REPLACE "\n$" "" configure_content "${content${index}}")
            file(CONFIGURE OUTPUT "${source${index}}" CONTENT "@configure_content@" @ONLY NEWLINE_STYLE LF)
            file(SHA256 "${source${index}}" hash)
            if(NOT hash STREQUAL "${patched${index}}")
                message(FATAL_ERROR "Halftrack MPE timing patch wrote unexpected SHA256 ${hash}.")
            endif()
        endif()
    endforeach()
endfunction()
