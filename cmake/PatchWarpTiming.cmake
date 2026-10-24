# Halftrack warp-marker timing for pinned Tracktion Engine.
# The warp reader set each block's stretch ratio from warpTime(), which gives a time on the first
# marker the ratio 1, so playback from a clip's start primed and ran its first segment at the wrong
# ratio. A ratio set now is also first heard after the stretcher's output latency, so the reader now
# takes the ratio of the segment that later time falls in when priming, seeking and reading.
# Retains all upstream source notices. Validates every file before writing any.
function(halftrack_patch_warp_timing source_dir)
    set(source0 "${source_dir}/modules/tracktion_engine/playback/graph/tracktion_WaveNode.cpp")
    set(original0 "2f2a0038be5991510a1d0222f166d831fa9865dfe6f9d42e4dbbe4e20c6b2cd0")
    set(patched0 "04ccc34aa76ca5f81534007a7d68da4e595b6680b323879d7ffe6b69d947b269")
    # The stretch-priming patch edits this file afterwards.
    set(downstream0 "2e4354f76d96dc25dc489042afa6a3263439dbc3b3095b8639d008cd94b7a7d9")
    set(count0 3)
    set(before0_0 [=[          reader (static_cast<TimeStretchReader*> (source.get())), map (std::move (warpMap))
    {
    }]=])
    set(after0_0 [=[          reader (static_cast<TimeStretchReader*> (source.get())), map (std::move (warpMap))
    {
        // Halftrack: prime the stretcher at the ratio playback starts with.
        reader->setSpeed (ratioAhead (0));
    }]=])
    set(before0_1 [=[    bool readSamples (choc::buffer::ChannelArrayView<float>& destBuffer) override
    {
        const auto unwarpedStartTime = TimePosition::fromSamples (readPosition, getSampleRate());
        const auto ratio = warpTime (map, unwarpedStartTime).stretchRatio;

        reader->setSpeed (ratio);
        readPosition += (SampleCount) destBuffer.getNumFrames();]=])
    set(after0_1 [=[    bool readSamples (choc::buffer::ChannelArrayView<float>& destBuffer) override
    {
        reader->setSpeed (ratioAhead (readPosition));
        readPosition += (SampleCount) destBuffer.getNumFrames();]=])
    set(before0_2 [=[        const auto warpedSamplePos = toSamples (warpedTime, sampleRate);
        source->setPosition (warpedSamplePos);
    }
};]=])
    set(after0_2 [=[        const auto warpedSamplePos = toSamples (warpedTime, sampleRate);
        // Halftrack: re-prime at the ratio playback resumes with.
        reader->setSpeed (ratioAhead (pos));
        source->setPosition (warpedSamplePos);
    }

    // Halftrack: the ratio to set at a warped position. A ratio set now is first heard after the
    // stretcher's output latency, so it is that of the segment the later time falls in.
    double ratioAhead (SampleCount position)
    { return ratioAt (TimePosition::fromSamples (position + std::max (0, reader->outputLatencySamples), getSampleRate())); }
    // The stretch ratio of the segment holding a warped time. Unlike warpTime(), a time on a marker
    // belongs to the segment it starts; beyond the outer markers the ratio is 1.
    double ratioAt (TimePosition time) const
    {
        if (map.size() < 2 || time < map.front().warpTime || time >= map.back().warpTime) return 1.0;
        size_t index = 1;
        while (index + 1 < map.size() && map[index].warpTime <= time) ++index;
        const auto warped = map[index].warpTime - map[index - 1].warpTime;
        return warped > 0s ? (map[index].sourceTime - map[index - 1].sourceTime) / warped : 1.0;
    }
};]=])
    foreach(index RANGE 0 0)
        file(SHA256 "${source${index}}" hash)
        if(hash STREQUAL "${patched${index}}" OR (DEFINED downstream${index} AND hash IN_LIST downstream${index}))
            set(write${index} FALSE)
            continue()
        endif()
        if(NOT hash STREQUAL "${original${index}}")
            message(FATAL_ERROR "Halftrack warp timing patch requires pinned source ${source${index}}; unexpected SHA256 ${hash}.")
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
            message(FATAL_ERROR "Halftrack warp timing patch produced unexpected SHA256 ${hash} for ${source${index}}.")
        endif()
        set(write${index} TRUE)
    endforeach()
    foreach(index RANGE 0 0)
        if(write${index})
            string(REGEX REPLACE "\n$" "" configure_content "${content${index}}")
            file(CONFIGURE OUTPUT "${source${index}}" CONTENT "@configure_content@" @ONLY NEWLINE_STYLE LF)
            file(SHA256 "${source${index}}" hash)
            if(NOT hash STREQUAL "${patched${index}}")
                message(FATAL_ERROR "Halftrack warp timing patch wrote unexpected SHA256 ${hash}.")
            endif()
        endif()
    endforeach()
endfunction()
