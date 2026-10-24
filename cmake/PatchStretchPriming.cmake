# Halftrack stretch priming for pinned Tracktion Engine, after every earlier patch. A stretched
# clip's readers prime their stretcher with the clip's first audio as the playback graph builds them,
# on the message thread, but they read the file cache without waiting, as real-time reads must. A file
# region not yet cached primed them with silence, so a clip's first play after a graph build lost its
# first stretcher latency (about 150 ms in Complex, half a second in Beats) until it was repositioned.
# The cache reader now waits for that audio while the graph builds the readers, as an offline render
# does, and reads without waiting once they are built.
# Retains all upstream source notices. Validates every file before writing any.
function(halftrack_patch_stretch_priming source_dir)
    set(source0 "${source_dir}/modules/tracktion_engine/playback/graph/tracktion_WaveNode.cpp")
    set(original0 "04ccc34aa76ca5f81534007a7d68da4e595b6680b323879d7ffe6b69d947b269")
    set(patched0 "2e4354f76d96dc25dc489042afa6a3263439dbc3b3095b8639d008cd94b7a7d9")
    set(count0 3)
    set(before0_0 [=[    void setLoopRange (TimeRange loopRange)
    {
        reader->setLoopRange (toSamples (loopRange, getSampleRate()));
    }
]=])
    set(after0_0 [=[    void setLoopRange (TimeRange loopRange)
    {
        reader->setLoopRange (toSamples (loopRange, getSampleRate()));
    }

    /** Halftrack: how long a read may wait for the file cache. */
    void setTimeout (TimeDuration timeout)
    {
        timeoutMs = (int) std::lround (timeout.inSeconds() * 1000.0);
    }
]=])
    set(before0_1 [=[    auto audioFileCacheReader = std::make_unique<AudioFileCacheReader> (std::move (fileCacheReader), isOfflineRender ? 5s : 0ms,
                                                                        destChannels, channelsToUse);
]=])
    set(after0_1 [=[    // Halftrack: the stretch readers below prime with the clip's first audio as they are made, here
    // on the message thread. Wait for it as an offline render does, so a file not yet cached does not
    // prime them with silence; once they are built, read without waiting.
    auto audioFileCacheReader = std::make_unique<AudioFileCacheReader> (std::move (fileCacheReader), isOfflineRender ? 5s : 2s,
                                                                        destChannels, channelsToUse);
    auto* const cacheReader = audioFileCacheReader.get();
]=])
    set(before0_2 [=[    editReader = std::make_shared<SpeedFadeEditReader> (std::move (basicEditReader), speedFadeDescription, editTempoSequence);
]=])
    set(after0_2 [=[    editReader = std::make_shared<SpeedFadeEditReader> (std::move (basicEditReader), speedFadeDescription, editTempoSequence);

    if (! isOfflineRender)
        cacheReader->setTimeout (0ms);
]=])
    foreach(index RANGE 0 0)
        file(SHA256 "${source${index}}" hash)
        if(hash STREQUAL "${patched${index}}" OR (DEFINED downstream${index} AND hash IN_LIST downstream${index}))
            set(write${index} FALSE)
            continue()
        endif()
        if(NOT hash STREQUAL "${original${index}}")
            message(FATAL_ERROR "Halftrack stretch priming patch requires pinned source ${source${index}}; unexpected SHA256 ${hash}.")
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
            message(FATAL_ERROR "Halftrack stretch priming patch produced unexpected SHA256 ${hash} for ${source${index}}.")
        endif()
        set(write${index} TRUE)
    endforeach()
    foreach(index RANGE 0 0)
        if(write${index})
            string(REGEX REPLACE "\n$" "" configure_content "${content${index}}")
            file(CONFIGURE OUTPUT "${source${index}}" CONTENT "@configure_content@" @ONLY NEWLINE_STYLE LF)
            file(SHA256 "${source${index}}" hash)
            if(NOT hash STREQUAL "${patched${index}}")
                message(FATAL_ERROR "Halftrack stretch priming patch wrote unexpected SHA256 ${hash}.")
            endif()
        endif()
    endforeach()
endfunction()
