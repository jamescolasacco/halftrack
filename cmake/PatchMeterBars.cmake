# Halftrack bars for pinned Tracktion Engine, after every earlier patch. With crotchet beats (a beat is
# a quarter note at every meter) the tempo sequence counted a bar as numerator beats, so a bar of 6/8
# was six quarter notes and plug-ins were told half the quarter notes played. A bar is now
# numerator x 4 / denominator beats, which may be fractional (7/8 is three and a half), and a beat
# is one quarter note. As in the reference, a time signature starts a bar where it is, leaving the
# bar before it short (a fragmentary bar) when it falls inside one; tempo changes keep the bars
# going. Launch grids finer than a bar are note values, so 1/4 is a quarter note in any meter, and
# end at the next bar. The metronome counts its ticks from each bar's start. Retains all upstream
# source notices. Validates every file before writing any.
function(halftrack_patch_meter_bars source_dir)
    set(source0 "${source_dir}/modules/tracktion_core/utilities/tracktion_Tempo.h")
    set(original0 "a78f2961a868d3595c4a80599610c1fd1af1f29327722614e25ef53847b66664")
    set(patched0 "93e8dbbd94f466189bda4aec01b5147424caa650c8680a7af09a5ba438ffcbb1")
    set(count0 13)
    set(before0_0 [=[        int numerator = 0;          /**< The number of beats in the current bar. */]=])
    set(after0_0 [=[        double numerator = 0;       /**< The number of beats in the current bar. Halftrack: a double, as
                                         with crotchet beats a bar of 7/8 is three and a half. */]=])
    set(before0_1 [=[    int barNumberOfFirstBar, numerator, prevNumerator, denominator;
    bool triplets;]=])
    set(after0_1 [=[    int barNumberOfFirstBar, numerator, prevNumerator, denominator;
    // Halftrack: a bar's length in beats (numerator x 4 / denominator with crotchet beats), the
    // previous bar's, and the quarter notes in a beat.
    double barBeats, prevBarBeats, ppqPerBeat;
    bool triplets;]=])
    set(before0_2 [=[            if (it.barNumberOfFirstBar == barsBeats.bars + 1
                  && barsBeats.beats.inBeats() >= it.prevNumerator - it.beatsUntilFirstBar.inBeats())
                return it.timeOfFirstBar - it.secondsPerBeat * (BeatDuration::fromBeats (it.prevNumerator) - barsBeats.beats);

            if (it.barNumberOfFirstBar <= barsBeats.bars || i == 0)
                return it.timeOfFirstBar + it.secondsPerBeat * (BeatDuration::fromBeats (((barsBeats.bars - it.barNumberOfFirstBar) * it.numerator)) + barsBeats.beats);]=])
    set(after0_2 [=[            if (it.barNumberOfFirstBar == barsBeats.bars + 1
                  && barsBeats.beats.inBeats() >= it.prevBarBeats - it.beatsUntilFirstBar.inBeats())
                return it.timeOfFirstBar - it.secondsPerBeat * (BeatDuration::fromBeats (it.prevBarBeats) - barsBeats.beats);

            if (it.barNumberOfFirstBar <= barsBeats.bars || i == 0)
                return it.timeOfFirstBar + it.secondsPerBeat * (BeatDuration::fromBeats (((barsBeats.bars - it.barNumberOfFirstBar) * it.barBeats)) + barsBeats.beats);]=])
    set(before0_3 [=[                if (beatsSinceFirstBar < 0)
                    return { it.barNumberOfFirstBar + (int) std::floor (beatsSinceFirstBar / it.numerator),
                             BeatDuration::fromBeats (std::fmod (std::fmod (beatsSinceFirstBar, it.numerator) + it.numerator, it.numerator)),
                             it.numerator };

                return { it.barNumberOfFirstBar + (int) std::floor (beatsSinceFirstBar / it.numerator),
                         BeatDuration::fromBeats (std::fmod (beatsSinceFirstBar, it.numerator)),
                         it.numerator };]=])
    set(after0_3 [=[                if (beatsSinceFirstBar < 0)
                    return { it.barNumberOfFirstBar + (int) std::floor (beatsSinceFirstBar / it.barBeats),
                             BeatDuration::fromBeats (std::fmod (std::fmod (beatsSinceFirstBar, it.barBeats) + it.barBeats, it.barBeats)),
                             it.barBeats };

                return { it.barNumberOfFirstBar + (int) std::floor (beatsSinceFirstBar / it.barBeats),
                         BeatDuration::fromBeats (std::fmod (beatsSinceFirstBar, it.barBeats)),
                         it.barBeats };]=])
    set(before0_4 [=[        if (timeSigIdx < timeSigs.size() && timeSigs[timeSigIdx].startBeat == currentBeat)
            currTimeSig = timeSigs[timeSigIdx++];]=])
    set(after0_4 [=[        // Halftrack: whether a time signature starts here, starting a bar.
        const bool timeSigStartsHere = timeSigIdx < timeSigs.size() && timeSigs[timeSigIdx].startBeat == currentBeat;

        if (timeSigStartsHere)
            currTimeSig = timeSigs[timeSigIdx++];]=])
    set(before0_5 [=[            it.beatsPerSecond   = 1.0 / it.secondsPerBeat;

            it.ppqAtStart = ppq;
            ppq += 4 * numBeats.inBeats() / it.denominator;]=])
    set(after0_5 [=[            it.beatsPerSecond   = 1.0 / it.secondsPerBeat;

            // Halftrack: with crotchet beats a bar is numerator x 4 / denominator beats, and a beat
            // is one quarter note.
            it.barBeats         = useDenominator ? (double) it.numerator : it.numerator * 4.0 / it.denominator;
            it.prevBarBeats     = it.barBeats;
            it.ppqPerBeat       = useDenominator ? 4.0 / it.denominator : 1.0;

            it.ppqAtStart = ppq;
            ppq += numBeats.inBeats() * it.ppqPerBeat;]=])
    set(before0_6 [=[                const auto barsSincePrevBar = (int) std::ceil (beatsSincePreviousBarUntilStart.inBeats() / prevSection.numerator - 1.0e-5);

                it.barNumberOfFirstBar = prevSection.barNumberOfFirstBar + barsSincePrevBar;

                const auto beatNumInEditOfNextBar = BeatPosition::fromBeats ((int) std::lround ((prevSection.startBeat + prevSection.beatsUntilFirstBar).inBeats())
                                                                             + (barsSincePrevBar * prevSection.numerator));

                it.beatsUntilFirstBar = beatNumInEditOfNextBar - it.startBeat;
                it.timeOfFirstBar = time + it.beatsUntilFirstBar * it.secondsPerBeat;

                for (int j = (int) sections.size(); --j >= 0;)
                {
                    auto& tempo = sections[(size_t) j];

                    if (tempo.barNumberOfFirstBar < it.barNumberOfFirstBar)
                    {
                        it.prevNumerator = tempo.numerator;
                        break;
                    }
                }]=])
    set(after0_6 [=[                const auto barsSincePrevBar = (int) std::ceil (beatsSincePreviousBarUntilStart.inBeats() / prevSection.barBeats - 1.0e-5);

                it.barNumberOfFirstBar = prevSection.barNumberOfFirstBar + barsSincePrevBar;

                // Halftrack: as in the reference, a time signature starts a bar where it is, leaving
                // the bar before it short when it falls inside one. Tempo changes keep the bars going,
                // and a bar may hold a fractional number of beats, so its start is not rounded.
                if (k == 0 && timeSigStartsHere)
                {
                    it.beatsUntilFirstBar = {};
                    it.timeOfFirstBar = time;
                }
                else
                {
                    const auto beatNumInEditOfNextBar = prevSection.startBeat + prevSection.beatsUntilFirstBar
                                                          + BeatDuration::fromBeats (barsSincePrevBar * prevSection.barBeats);

                    it.beatsUntilFirstBar = beatNumInEditOfNextBar - it.startBeat;
                    it.timeOfFirstBar = time + it.beatsUntilFirstBar * it.secondsPerBeat;
                }

                for (int j = (int) sections.size(); --j >= 0;)
                {
                    auto& tempo = sections[(size_t) j];

                    if (tempo.barNumberOfFirstBar < it.barNumberOfFirstBar)
                    {
                        it.prevNumerator = tempo.numerator;
                        it.prevBarBeats = tempo.barBeats;
                        break;
                    }
                }]=])
    set(before0_7 [=[    if (beatsSinceFirstBar < 0)
        return { it.barNumberOfFirstBar + (int) std::floor (beatsSinceFirstBar / it.numerator),
                 BeatDuration::fromBeats (std::fmod (std::fmod (beatsSinceFirstBar, it.numerator) + it.numerator, it.numerator)),
                 it.numerator };

    return { it.barNumberOfFirstBar + (int) std::floor (beatsSinceFirstBar / it.numerator),
             BeatDuration::fromBeats (std::fmod (beatsSinceFirstBar, it.numerator)),
             it.numerator };]=])
    set(after0_7 [=[    if (beatsSinceFirstBar < 0)
        return { it.barNumberOfFirstBar + (int) std::floor (beatsSinceFirstBar / it.barBeats),
                 BeatDuration::fromBeats (std::fmod (std::fmod (beatsSinceFirstBar, it.barBeats) + it.barBeats, it.barBeats)),
                 it.barBeats };

    return { it.barNumberOfFirstBar + (int) std::floor (beatsSinceFirstBar / it.barBeats),
             BeatDuration::fromBeats (std::fmod (beatsSinceFirstBar, it.barBeats)),
             it.barBeats };]=])
    set(before0_8 [=[            add (BeatDuration::fromBeats (sequence.sections[index].numerator));]=])
    set(after0_8 [=[            add (BeatDuration::fromBeats (sequence.sections[index].barBeats));]=])
    set(before0_9 [=[            add (BeatDuration::fromBeats (-sequence.sections[index].numerator));]=])
    set(after0_9 [=[            add (BeatDuration::fromBeats (-sequence.sections[index].barBeats));]=])
    set(before0_10 [=[    const auto beatsSinceStart = BeatPosition::fromBeats (((ppq - it.ppqAtStart) * it.denominator) / 4.0);]=])
    set(after0_10 [=[    const auto beatsSinceStart = BeatPosition::fromBeats ((ppq - it.ppqAtStart) / it.ppqPerBeat);]=])
    set(before0_11 [=[    return it.ppqAtStart + 4.0 * beatsSinceStart.inBeats() / it.denominator;]=])
    set(after0_11 [=[    return it.ppqAtStart + beatsSinceStart.inBeats() * it.ppqPerBeat;]=])
    set(before0_12 [=[            const double beatNumberOfLastBarSinceFirstBar = it.numerator * std::floor (beatsSinceFirstBar / it.numerator);

            return it.ppqAtStart + 4.0 * (it.beatsUntilFirstBar.inBeats() + beatNumberOfLastBarSinceFirstBar) / it.denominator;]=])
    set(after0_12 [=[            const double beatNumberOfLastBarSinceFirstBar = it.barBeats * std::floor (beatsSinceFirstBar / it.barBeats);

            return it.ppqAtStart + (it.beatsUntilFirstBar.inBeats() + beatNumberOfLastBarSinceFirstBar) * it.ppqPerBeat;]=])
    set(source1 "${source_dir}/modules/tracktion_engine/model/edit/tracktion_LaunchQuantisation.cpp")
    set(original1 "3becd7c4526ace7b2596389bdbb7e04361552ff5038779ddf2948c47731af177")
    set(patched1 "9bc45be9a73cf706f6ca11f6c173c0143fb4dd4069e3cbd0b85b4e850cbd2b5b")
    set(count1 1)
    set(before1_0 [=[    else
    {
        // Fractional bars
        const auto beatFraction = barsBeats.numerator * qFraction;
        barsBeats.beats = BeatDuration::fromBeats (beatFraction * std::floor ((barsBeats.beats.inBeats() / beatFraction) + adjustment));
    }]=])
    set(after1_0 [=[    else
    {
        // Halftrack: a fraction of a bar is that note value (a quarter of a whole note is a quarter
        // note), as the reference's 1/4 is a quarter note in any meter; the bar holds numerator
        // beats, or numerator x 4 / denominator crotchets. The next one is at most the next bar.
        const auto sig = ts.getTimeSignatureAt (ts.toTime (pos));
        const auto beatFraction = qFraction * barsBeats.numerator * sig.denominator / std::max (1, sig.numerator);
        const auto next = beatFraction * std::floor ((barsBeats.beats.inBeats() / beatFraction) + adjustment);

        if (next >= barsBeats.numerator - 1.0e-9)
        {
            barsBeats.bars += 1;
            barsBeats.beats = {};
        }
        else
        {
            barsBeats.beats = BeatDuration::fromBeats (next);
        }
    }]=])
    set(source2 "${source_dir}/modules/tracktion_engine/playback/graph/tracktion_ClickNode.cpp")
    set(original2 "89c978d830b759cf006bd56c2a125d144479bb77b390f0eb64d2be2d74373941")
    set(patched2 "8321bcd0ec19e6e80ed52ef5af28c6d168c13cacbc984784fc77eb7e0be45ad3")
    set(count2 1)
    set(before2_0 [=[    // Halftrack: ticks fall on a grid of the tick length from the start, and the first of each bar
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
    set(after2_0 [=[    // Halftrack: ticks fall on a grid of the tick length from the start of each bar, as the
    // sequence's bars run (a time signature starts a bar where it is), and each bar's first is
    // accented.
    inline BeatInfo getBeatInfo (const tempo::Sequence& sequence, tempo::Sequence::Position& tempoPosition, double setting, double& tick)
    {
        const auto sig = tempoPosition.getTimeSignature();
        tick = resolveClickTick (setting, sig.numerator, sig.denominator);
        const auto intoBar = tempoPosition.getBarsBeats().beats.inBeats();
        const auto barStart = tempoPosition.getBeats().inBeats() - intoBar;
        const auto index = (int64_t) std::floor (intoBar / tick + 1.0e-9);
        const auto beatTime = sequence.toTime (BeatPosition::fromBeats (barStart + (double) index * tick));

        return { beatTime, index == 0 };
    }]=])
    foreach(index RANGE 0 2)
        file(SHA256 "${source${index}}" hash)
        if(hash STREQUAL "${patched${index}}" OR (DEFINED downstream${index} AND hash IN_LIST downstream${index}))
            set(write${index} FALSE)
            continue()
        endif()
        if(NOT hash STREQUAL "${original${index}}")
            message(FATAL_ERROR "Halftrack bars patch requires pinned source ${source${index}}; unexpected SHA256 ${hash}.")
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
            message(FATAL_ERROR "Halftrack bars patch produced unexpected SHA256 ${hash} for ${source${index}}.")
        endif()
        set(write${index} TRUE)
    endforeach()
    foreach(index RANGE 0 2)
        if(write${index})
            string(REGEX REPLACE "\n$" "" configure_content "${content${index}}")
            file(CONFIGURE OUTPUT "${source${index}}" CONTENT "@configure_content@" @ONLY NEWLINE_STYLE LF)
            file(SHA256 "${source${index}}" hash)
            if(NOT hash STREQUAL "${patched${index}}")
                message(FATAL_ERROR "Halftrack bars patch wrote unexpected SHA256 ${hash}.")
            endif()
        endif()
    endforeach()
endfunction()
