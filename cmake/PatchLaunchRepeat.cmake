# Halftrack launch repeat timing for pinned Tracktion Engine.
# A launch handle repeat loop point that falls on a block start restarts that whole
# block instead of producing an empty first section, which skipped the block.
# Retains all upstream source notices. Validates every file before writing any.
function(halftrack_patch_launch_repeat source_dir)
    set(source0 "${source_dir}/modules/tracktion_engine/model/clips/tracktion_LaunchHandle.cpp")
    set(original0 "060884330b8e511f0d207d7b0dc2e8f506ef846b1eff19ca795422f0cf805a24")
    set(patched0 "1cd20c6ace77ed96f2cc99ef41754bfc066d2745c9e91e4f36d14c2ca7009693")
    set(count0 1)
    set(before0_0 [=[            const auto elapsedBeats = blockEditBeatRange.getEnd() - *splitStatus.playStartTime1;

            if (elapsedBeats <= *ld)
                return;

            if (elapsedBeats > (*ld + duration))
                return;

            const auto secondSplitLength    = BeatDuration::fromBeats (std::fmod (elapsedBeats.inBeats(), ld->inBeats()));
            const auto firstSplitLength     = duration - secondSplitLength;
]=])
    set(after0_0 [=[            // Halftrack: locate the loop point itself. One that lands on a block start, within
            // rounding, restarts the whole block instead of leaving an empty first section
            // (which skips the block) or being missed when rounding places it past the block.
            const auto loopPoint = *splitStatus.playStartTime1 + *ld;
            const auto tolerance = BeatDuration::fromBeats (1.0e-9);

            if (loopPoint >= blockEditBeatRange.getEnd() - tolerance
                || loopPoint < blockEditBeatRange.getStart() - tolerance)
                return;

            if (loopPoint <= blockEditBeatRange.getStart() + tolerance)
            {
                splitStatus.playStartTime1 = blockEditBeatRange.getStart();
                currentState.store (CurrentState
                                    {
                                        blockEditBeatRange.getStart(),
                                        MonotonicBeat { blockMonotonicBeatRange.v.getStart() },
                                        duration,
                                        blockEditBeatRange.getEnd()
                                    });
                return;
            }

            const auto firstSplitLength     = loopPoint - blockEditBeatRange.getStart();
            const auto secondSplitLength    = duration - firstSplitLength;
]=])
    foreach(index RANGE 0 0)
        file(SHA256 "${source${index}}" hash)
        if(hash STREQUAL "${patched${index}}")
            set(write${index} FALSE)
            continue()
        endif()
        if(NOT hash STREQUAL "${original${index}}")
            message(FATAL_ERROR "Halftrack launch repeat patch requires pinned source ${source${index}}; unexpected SHA256 ${hash}.")
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
            message(FATAL_ERROR "Halftrack launch repeat patch produced unexpected SHA256 ${hash} for ${source${index}}.")
        endif()
        set(write${index} TRUE)
    endforeach()
    foreach(index RANGE 0 0)
        if(write${index})
            string(REGEX REPLACE "\n$" "" configure_content "${content${index}}")
            file(CONFIGURE OUTPUT "${source${index}}" CONTENT "@configure_content@" @ONLY NEWLINE_STYLE LF)
            file(SHA256 "${source${index}}" hash)
            if(NOT hash STREQUAL "${patched${index}}")
                message(FATAL_ERROR "Halftrack launch repeat patch wrote unexpected SHA256 ${hash}.")
            endif()
        endif()
    endforeach()
endfunction()
