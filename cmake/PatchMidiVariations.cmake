# Halftrack per-pass MIDI note variation for pinned Tracktion Engine, after the MIDI-device,
# track-inputs and clip-activation patches. Step clips with probabilities cycle through 64
# generated sequences, one per pass of their loop. MIDI clips now can too: the engine behaviour
# says how many variations a clip has and creates each one, so notes can have a chance to play and
# a velocity range. The looping generator also chose the next sequence on every cache, and caches
# twice a block once a pass's events are done, so a pass now plays the sequence for its own index;
# and it stopped for good at an empty pass, which nothing advanced past, so it now skips them.
# Retains all upstream source notices. Validates every file before writing any.
function(halftrack_patch_midi_variations source_dir)
    set(source0 "${source_dir}/modules/tracktion_engine/utilities/tracktion_EngineBehaviour.h")
    set(original0 "fe49e3934dc91d709c99293a2a8c5dbc7f582c10bcd389a2d9aeb15fec8305cf")
    set(patched0 "5721d06c407ce781dee9c3e804981f3a283fc98d26d412f2ce27246381ba9eaa")
    set(count0 1)
    set(downstream0 "546f1bc2652ba56474e93efabe4f054f8c699cebb2fb446b341a6e176af339e3") # The later MIDI ports patch.
    set(before0_0 [=[    virtual juce::MidiMessageSequence createPlaybackMidiSequence (const MidiList& list, MidiClip& clip, MidiList::TimeBase tb, bool generateMPE)
    {
        return MidiList::createDefaultPlaybackMidiSequence (list, clip, tb, generateMPE);
    }
]=])
    set(after0_0 [=[    virtual juce::MidiMessageSequence createPlaybackMidiSequence (const MidiList& list, MidiClip& clip, MidiList::TimeBase tb, bool generateMPE)
    {
        return MidiList::createDefaultPlaybackMidiSequence (list, clip, tb, generateMPE);
    }

    /// Halftrack: the number of playback sequences a MIDI clip cycles through, one per pass of its
    /// loop, so that its notes can vary between passes, as step clips' probabilities do.
    virtual int getNumPlaybackMidiSequenceVariations (MidiClip&)    { return 1; }

    /// Halftrack: one of those sequences, numbered from zero.
    virtual juce::MidiMessageSequence createPlaybackMidiSequenceVariation (const MidiList& list, MidiClip& clip, MidiList::TimeBase tb,
                                                                           bool generateMPE, int /*variation*/)
    {
        return createPlaybackMidiSequence (list, clip, tb, generateMPE);
    }
]=])
    set(source1 "${source_dir}/modules/tracktion_engine/playback/graph/tracktion_EditNodeBuilder.cpp")
    set(original1 "06c1d2be2f00c494f5db33c2b5ced64f9395e9fb7e16e4e8002ef7beacfa9ee8")
    set(patched1 "bfc26bb83dcc389b9b3cd64db016f5e493e7f1f982b7c8910be6524c30703766")
    set(count1 1)
    set(downstream1 "d5ebc0acfbfc0dae66b15881ad2a8f9ece557e737c0c8ad7d4f7b8038a30d647" "1a07134c8c4c2a229b4da262dc6b547d3ab1302496fd5e5fbf1d375e9614517a") # The later sidechain taps and MIDI ports patches.
    set(before1_0 [=[        std::vector<juce::MidiMessageSequence> sequences;
        sequences.emplace_back (clip.getSequence().exportToPlaybackMidiSequence (clip, timeBase, generateMPE));]=])
    set(after1_0 [=[        std::vector<juce::MidiMessageSequence> sequences;
        auto& behaviour = clip.edit.engine.getEngineBehaviour();
        const auto variations = std::max (1, behaviour.getNumPlaybackMidiSequenceVariations (clip));

        // Halftrack: a clip whose notes vary between passes cycles through several sequences.
        if (variations == 1)
            sequences.emplace_back (clip.getSequence().exportToPlaybackMidiSequence (clip, timeBase, generateMPE));
        else
            for (int variation = 0; variation < variations; ++variation)
                sequences.emplace_back (behaviour.createPlaybackMidiSequenceVariation (clip.getSequence(), clip, timeBase, generateMPE, variation));]=])
    set(source2 "${source_dir}/modules/tracktion_engine/playback/graph/tracktion_LoopingMidiNode.cpp")
    set(original2 "e7ceddf6d9d00adf8035070f911d0e4c69f5b8f9daf62ee70e125d4825ad8820")
    set(patched2 "012a4ac8ea6c65ffb647e8d0244ecf5c0ce62d6f1841e72009c14309cb3307e1")
    set(count2 8)
    set(before2_0 [=[    virtual void cacheSequence (double /*offset*/, std::optional<juce::Range<double>> /*clipRange*/) {}]=])
    set(after2_0 [=[    virtual void cacheSequence (double /*offset*/, std::optional<juce::Range<double>> /*clipRange*/, std::optional<int> /*loopIndex*/ = std::nullopt) {}]=])
    set(before2_1 [=[        // Cache the sequence at 0.0 time to reserve the required storage
        cacheSequence (0.0, {});]=])
    set(after2_1 [=[        // Cache the sequence at 0.0 time to reserve the required storage
        cacheSequence (0.0, {}, 0);]=])
    set(before2_2 [=[    void cacheSequence (double offsetBeats, std::optional<juce::Range<double>> clipRange) override
    {]=])
    set(after2_2 [=[    void cacheSequence (double offsetBeats, std::optional<juce::Range<double>> clipRange, std::optional<int> loopIndex = std::nullopt) override
    {]=])
    set(before2_3 [=[        if (sequences.size() > 0)
            if (++currentSequenceIndex >= sequences.size())
                currentSequenceIndex = 0;]=])
    set(after2_3 [=[        // Halftrack: a looping clip plays the sequence for its pass of the loop, however often the
        // loop position is cached, so per-pass variations do not skip; otherwise each cache moves on.
        if (sequences.size() > 0)
        {
            const auto numSequences = static_cast<int> (sequences.size());

            if (loopIndex)
                currentSequenceIndex = static_cast<size_t> (((*loopIndex % numSequences) + numSequences) % numSequences);
            else if (++currentSequenceIndex >= sequences.size())
                currentSequenceIndex = 0;
        }]=])
    set(before2_4 [=[        generator->cacheSequence (sequenceOffset, loopTimes + sequenceOffset);]=])
    set(after2_4 [=[        generator->cacheSequence (sequenceOffset, loopTimes + sequenceOffset, loopIndex);]=])
    set(before2_5 [=[            // Past the loop's last event, so carry on from the next loop, as advance() does.
            // Otherwise this is exhausted and nothing advances it to the next loop
            if (generator->exhausted())
            {
                setLoopIndex (loopIndex + 1);
                generator->setTime (0.0);
            }]=])
    set(after2_5 [=[            // Past the loop's last event, so carry on from the next loop, as advance() does.
            // Otherwise this is exhausted and nothing advances it to the next loop
            // Halftrack: and past any loops after it whose sequences are empty, as per-pass variations can be.
            for (int skipped = 0; generator->exhausted() && skipped < maxEmptyLoops; ++skipped)
            {
                setLoopIndex (loopIndex + 1);
                generator->setTime (0.0);
            }]=])
    set(before2_6 [=[        generator->advance();

        if (exhausted() && ! loopTimes.isEmpty())
        {
            setLoopIndex (loopIndex + 1);
            generator->setTime (0.0);
        }

        return ! exhausted();]=])
    set(after2_6 [=[        generator->advance();

        // Halftrack: an empty loop is exhausted at once and nothing would advance past it, so
        // carry on past any empty loops too, up to the number of variations a clip can have.
        for (int skipped = 0; exhausted() && ! loopTimes.isEmpty() && skipped < maxEmptyLoops; ++skipped)
        {
            setLoopIndex (loopIndex + 1);
            generator->setTime (0.0);
        }

        return ! exhausted();]=])
    set(before2_7 [=[    const ClipBeatRange loopTimes;
    int loopIndex = 0;]=])
    set(after2_7 [=[    const ClipBeatRange loopTimes;
    int loopIndex = 0;
    static constexpr int maxEmptyLoops = 64;]=])
    foreach(index RANGE 0 2)
        file(SHA256 "${source${index}}" hash)
        if(hash STREQUAL "${patched${index}}" OR (DEFINED downstream${index} AND hash IN_LIST downstream${index}))
            set(write${index} FALSE)
            continue()
        endif()
        if(NOT hash STREQUAL "${original${index}}")
            message(FATAL_ERROR "Halftrack MIDI variations patch requires pinned source ${source${index}}; unexpected SHA256 ${hash}.")
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
            message(FATAL_ERROR "Halftrack MIDI variations patch produced unexpected SHA256 ${hash} for ${source${index}}.")
        endif()
        set(write${index} TRUE)
    endforeach()
    foreach(index RANGE 0 2)
        if(write${index})
            string(REGEX REPLACE "\n$" "" configure_content "${content${index}}")
            file(CONFIGURE OUTPUT "${source${index}}" CONTENT "@configure_content@" @ONLY NEWLINE_STYLE LF)
            file(SHA256 "${source${index}}" hash)
            if(NOT hash STREQUAL "${patched${index}}")
                message(FATAL_ERROR "Halftrack MIDI variations patch wrote unexpected SHA256 ${hash}.")
            endif()
        endif()
    endforeach()
endfunction()
