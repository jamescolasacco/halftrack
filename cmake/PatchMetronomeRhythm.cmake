# Halftrack metronome rhythm for pinned Tracktion Engine. The reference's metronome ticks at a
# chosen division (a bar, a half note to a sixteenth triplet), or by default at the time signature's
# denominator, accenting the first tick of each bar; a division that does not divide the bar a whole
# number of times plays as the default. The click generator reads the Edit's rhythm setting when it
# is prepared, resolves it for the time signature, and places its ticks on that grid, with bars of
# numerator x 4 / denominator crotchets. Retains all upstream source notices. Validates every file
# before writing any.
function(halftrack_patch_metronome_rhythm source_dir)
    set(source0 "${source_dir}/modules/tracktion_engine/playback/graph/tracktion_ClickNode.h")
    set(original0 "cfca43d540e922c63be15bddb94fc1f9db1cc5c9f199bd3bc96dde86ae030613")
    set(patched0 "dd21794c667566ec8d0c8f04c16858770f882e9156f410d91889b3361240814a")
    set(count0 1)
    set(before0_0 [=[    double sampleRate = 44100.0;
    juce::AudioBuffer<float> bigClick, littleClick;]=])
    set(after0_0 [=[    double sampleRate = 44100.0;
    // Halftrack: the Edit's metronome rhythm, read when the click is prepared: 0 follows the time
    // signature's denominator, -1 ticks once a bar, otherwise a division in crotchets.
    double tickSetting = 0;
    juce::AudioBuffer<float> bigClick, littleClick;]=])
    set(source1 "${source_dir}/modules/tracktion_engine/playback/graph/tracktion_ClickNode.cpp")
    set(original1 "487e4ef7c67ab8ee3dd5d6dcd27266087306bd4c5adf735b74cceab3e08a7921")
    set(patched1 "89c978d830b759cf006bd56c2a125d144479bb77b390f0eb64d2be2d74373941")
    set(count1 5)
    set(before1_0 [=[    inline BeatInfo getBeatInfo (const tempo::Sequence& sequence, tempo::Sequence::Position& tempoPosition)
    {
        const auto beats = tempoPosition.getBeats().inBeats();
        int beat = static_cast<int> (std::floor (beats));
        const auto beatTime = sequence.toTime (BeatPosition::fromBeats (beat));
        const bool isFirstBeatOfBar = tempoPosition.getBarsBeats().getWholeBeats() == 0;

        return { beatTime, isFirstBeatOfBar };
    }]=])
    set(after1_0 [=[    // Halftrack: the tick length in crotchets for a rhythm setting and time signature, as the
    // reference resolves its metronome's: a bar, the denominator's note (Auto), or a division that
    // divides the bar a whole number of times, else Auto.
    inline double resolveClickTick (double setting, int numerator, int denominator)
    {
        const double bar = numerator * 4.0 / denominator;

        if (std::ceil (setting) == -1.0)
            return bar;

        if (setting > 0)
        {
            const double times = bar / setting;

            if (std::round (times) >= 1.0 && std::abs (times - std::round (times)) < 5.0e-5)
                return setting;
        }

        return 4.0 / denominator;
    }

    // Halftrack: ticks fall on a grid of the tick length from the start, and the first of each bar
    // (of numerator x 4 / denominator crotchets) is accented.
    inline BeatInfo getBeatInfo (const tempo::Sequence& sequence, tempo::Sequence::Position& tempoPosition, double setting, double& tick)
    {
        const auto sig = tempoPosition.getTimeSignature();
        tick = resolveClickTick (setting, sig.numerator, sig.denominator);
        const double bar = sig.numerator * 4.0 / sig.denominator;
        const auto ticksPerBar = std::max ((int64_t) 1, (int64_t) std::ceil (bar / tick - 1.0e-6));
        const auto index = (int64_t) std::floor (tempoPosition.getBeats().inBeats() / tick + 1.0e-9);
        const auto beatTime = sequence.toTime (BeatPosition::fromBeats ((double) index * tick));
        const bool isFirstBeatOfBar = ((index % ticksPerBar) + ticksPerBar) % ticksPerBar == 0;

        return { beatTime, isFirstBeatOfBar };
    }]=])
    set(before1_1 [=[    tempoPosition.set (editTime.getStart());
    auto beatInfo = getBeatInfo (sequence, tempoPosition);]=])
    set(after1_1 [=[    tempoPosition.set (editTime.getStart());
    double tick = 1.0;
    auto beatInfo = getBeatInfo (sequence, tempoPosition, tickSetting, tick);]=])
    set(before1_2 [=[                bufferForMidiMessages->addMidiMessage (juce::MidiMessage::noteOn (10, note, gain),
                                                       (t - editTime.getStart()).inSeconds(),
                                                       {});

            tempoPosition.add (1_bd);
            beatInfo = getBeatInfo (sequence, tempoPosition);
            t = beatInfo.time;]=])
    set(after1_2 [=[                bufferForMidiMessages->addMidiMessage (juce::MidiMessage::noteOn (10, note, gain),
                                                       (t - editTime.getStart()).inSeconds(),
                                                       {});

            tempoPosition.add (BeatDuration::fromBeats (tick));
            beatInfo = getBeatInfo (sequence, tempoPosition, tickSetting, tick);
            t = beatInfo.time;]=])
    set(before1_3 [=[                render (dstView);
            }

            tempoPosition.add (1_bd);
            beatInfo = getBeatInfo (sequence, tempoPosition);
            t = beatInfo.time;]=])
    set(after1_3 [=[                render (dstView);
            }

            tempoPosition.add (BeatDuration::fromBeats (tick));
            beatInfo = getBeatInfo (sequence, tempoPosition, tickSetting, tick);
            t = beatInfo.time;]=])
    set(before1_4 [=[    tempoPosition.set (startTime);]=])
    set(after1_4 [=[    tempoPosition.set (startTime);
    tickSetting = static_cast<double> (edit.state.getProperty ("halftrackMetronomeTick", 0.0)); // Halftrack.]=])
    foreach(index RANGE 0 1)
        file(SHA256 "${source${index}}" hash)
        if(hash STREQUAL "${patched${index}}")
            set(write${index} FALSE)
            continue()
        endif()
        if(NOT hash STREQUAL "${original${index}}")
            message(FATAL_ERROR "Halftrack metronome rhythm patch requires pinned source ${source${index}}; unexpected SHA256 ${hash}.")
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
            message(FATAL_ERROR "Halftrack metronome rhythm patch produced unexpected SHA256 ${hash} for ${source${index}}.")
        endif()
        set(write${index} TRUE)
    endforeach()
    foreach(index RANGE 0 1)
        if(write${index})
            string(REGEX REPLACE "\n$" "" configure_content "${content${index}}")
            file(CONFIGURE OUTPUT "${source${index}}" CONTENT "@configure_content@" @ONLY NEWLINE_STYLE LF)
            file(SHA256 "${source${index}}" hash)
            if(NOT hash STREQUAL "${patched${index}}")
                message(FATAL_ERROR "Halftrack metronome rhythm patch wrote unexpected SHA256 ${hash}.")
            endif()
        endif()
    endforeach()
endfunction()
