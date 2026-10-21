# Retain MIDI release velocity in pinned Tracktion native recording/import.
# Upstream GPL notices remain in the patched dependency source.
function(halftrack_patch_midi_release_velocity source_dir)
    set(source "${source_dir}/modules/tracktion_engine/midi/tracktion_MidiList.cpp")
    set(original "161898bf64101cb385b7ef5a99d18bedd8ad5909fa0d422a4ae734ec70fe1a79")
    set(patched "93b05169b4008e66831d4ee3a9d42d065b32b011cd5f27c1dd04fd0a0854dcbd")
    # The MPE timing patch edits this file afterwards.
    set(downstream "ed53b9158c0162bef0c3c041c473bd4e6e740bad5eee65c61e4f3e5775436949")
    file(SHA256 "${source}" hash)
    if(hash STREQUAL patched OR hash STREQUAL downstream)
        return()
    endif()
    if(NOT hash STREQUAL original)
        message(FATAL_ERROR "Halftrack MIDI release patch requires pinned source; unexpected SHA256 ${hash}.")
    endif()
    file(READ "${source}" content)
    set(before [=[                addNote (m.getNoteNumber(),
                         beatTime,
                         ts != nullptr ? ((ts->toBeats (keyUpTime) - firstBeatNum) - toDuration (beatTime))
                                       : (BeatDuration::fromBeats (keyUpTime.inSeconds()) - toDuration (beatTime)),
                         m.getVelocity(), edit != nullptr ? edit->engine.getEngineBehaviour().getDefaultNoteColour() : 0, um);]=])
    set(after [=[                auto* note = addNote (m.getNoteNumber(),
                         beatTime,
                         ts != nullptr ? ((ts->toBeats (keyUpTime) - firstBeatNum) - toDuration (beatTime))
                                       : (BeatDuration::fromBeats (keyUpTime.inSeconds()) - toDuration (beatTime)),
                         m.getVelocity(), edit != nullptr ? edit->engine.getEngineBehaviour().getDefaultNoteColour() : 0, um);
                if (auto* noteOff = sequence.getEventPointer (i)->noteOffObject)
                    note->setNoteOffVelocity (noteOff->message.getVelocity(), um);]=])
    string(REPLACE "${before}" "${after}" content "${content}")
    string(SHA256 hash "${content}")
    if(NOT hash STREQUAL patched)
        message(FATAL_ERROR "Halftrack MIDI release patch produced unexpected SHA256 ${hash}.")
    endif()
    string(REGEX REPLACE "\n$" "" configure_content "${content}")
    file(CONFIGURE OUTPUT "${source}" CONTENT "@configure_content@" @ONLY NEWLINE_STYLE LF)
    file(SHA256 "${source}" hash)
    if(NOT hash STREQUAL patched)
        message(FATAL_ERROR "Halftrack MIDI release patch wrote unexpected SHA256 ${hash}.")
    endif()
endfunction()
