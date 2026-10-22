# Halftrack count-in start for pinned Tracktion Engine, after every earlier patch. As the reference's
# count-in does (static reading of its code), a count-in starts on the bar line nearest to its length
# before the recording's start, rounding a half away from zero, so a count-in from mid-bar is not
# whole bars long, and its first click is at its start: the engine's half-beat lead before the first
# click goes. Retains all upstream source notices. Validates every file before writing any.
function(halftrack_patch_count_in_start source_dir)
    set(source0 "${source_dir}/modules/tracktion_engine/playback/tracktion_TransportControl.cpp")
    set(original0 "d85899d168abaf3ca1e47eb586a2018ca54b642b20ab92c15de1e970edd9724f")
    set(patched0 "cfd436495804e855c4c99db7aeb0080ea35b328ac339d926ce30804e40d431be")
    set(count0 2)
    set(before0_0 [=[                if (numCountInBeats > 0)
                {
                    auto currentBeat = ts.toBeats (transportState->startTime);
                    prerollStart = ts.toTime (currentBeat - BeatDuration::fromBeats (numCountInBeats + 0.5));
                    // N.B. this +0.5 beats here specifies the behaviour further down when setting the click range.
                    // If this changes, that will also need to change.
                }]=])
    set(after0_0 [=[                if (numCountInBeats > 0)
                {
                    // Halftrack: as the reference's count-in does, start on the bar line nearest to
                    // the count-in's length before the start, rounding a half away from zero, so a
                    // count-in from mid-bar is not whole bars long; its first click is at its start.
                    const auto& sig = *ts.getTimeSig (0);
                    const double bar = sig.numerator * 4.0 / sig.denominator;
                    const double from = std::round ((ts.toBeats (transportState->startTime).inBeats() - numCountInBeats) * 3843840.0) / 3843840.0;
                    prerollStart = ts.toTime (BeatPosition::fromBeats (std::round (from / bar) * bar));
                }]=])
    set(before0_1 [=[                    if (edit.getNumCountInBeats() > 0)
                    {
                        // As the pre-roll will be "num count in beats - 0.5" we have to add that back on before our calculation
                        // We also roll back 0.5 beats the end time to avoid hearing a block that starts directly or just before a beat
                        const auto clickStartBeat = ts.toBeats (prerollStart);
                        const auto clickEndBeat = ts.toBeats (transportState->startTime.get());

                        edit.setClickTrackRange (ts.toTime ({ BeatPosition::fromBeats (std::ceil (clickStartBeat.inBeats() + 0.5)),
                                                              BeatPosition::fromBeats (std::ceil (clickEndBeat.inBeats())) - 0.5_bd }));
                    }]=])
    set(after0_1 [=[                    if (edit.getNumCountInBeats() > 0)
                    {
                        // Halftrack: the count-in clicks from its start up to the recording's.
                        edit.setClickTrackRange ({ prerollStart, transportState->startTime.get() });
                    }]=])
    set(downstream0 "9da5f4cbc09c1dc1c7b09eaae63655a4d78534fe5b7d01f4486063e13a39aea4") # The later loop playback patch.
    foreach(index RANGE 0 0)
        file(SHA256 "${source${index}}" hash)
        if(hash STREQUAL "${patched${index}}" OR (DEFINED downstream${index} AND hash IN_LIST downstream${index}))
            set(write${index} FALSE)
            continue()
        endif()
        if(NOT hash STREQUAL "${original${index}}")
            message(FATAL_ERROR "Halftrack count-in start patch requires pinned source ${source${index}}; unexpected SHA256 ${hash}.")
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
            message(FATAL_ERROR "Halftrack count-in start patch produced unexpected SHA256 ${hash} for ${source${index}}.")
        endif()
        set(write${index} TRUE)
    endforeach()
    foreach(index RANGE 0 0)
        if(write${index})
            string(REGEX REPLACE "\n$" "" configure_content "${content${index}}")
            file(CONFIGURE OUTPUT "${source${index}}" CONTENT "@configure_content@" @ONLY NEWLINE_STYLE LF)
            file(SHA256 "${source${index}}" hash)
            if(NOT hash STREQUAL "${patched${index}}")
                message(FATAL_ERROR "Halftrack count-in start patch wrote unexpected SHA256 ${hash}.")
            endif()
        endif()
    endforeach()
endfunction()
