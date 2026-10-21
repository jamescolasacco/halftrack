# Halftrack Re-Pitch warping for pinned Tracktion Engine, after the reverse-channels patch.
# A clip following the tempo with its time stretcher disabled was given the default stretcher,
# which keeps its pitch. A clip marked for Re-Pitch now keeps the stretcher disabled, so the
# engine resamples it alone and its pitch follows the tempo.
# Retains all upstream source notices. Validates every file before writing any.
function(halftrack_patch_repitch source_dir)
    set(source0 "${source_dir}/modules/tracktion_engine/model/clips/tracktion_AudioClipBase.cpp")
    set(original0 "1902c1ad6fd72e5a62c9f83778bf190d239e7e8ae8f77229d66ff8ba6de2df57")
    set(patched0 "63336ba34baf1de28fce2e8bc0aff7b06d015ba45f9776cc62b0e7716449c455")
    set(count0 1)
    set(before0_0 [=[    TimeStretcher::Mode ts = timeStretchMode;

    if (ts == TimeStretcher::disabled && (getAutoPitch() || getAutoTempo() || getPitchChange() != 0.0f))
        ts = TimeStretcher::defaultMode;]=])
    set(after0_0 [=[    TimeStretcher::Mode ts = timeStretchMode;

    // Halftrack: a Re-Pitch clip follows the tempo by resampling alone, so its pitch moves with it.
    if (ts == TimeStretcher::disabled && getAutoTempo() && getPitchChange() == 0.0f
        && static_cast<bool> (state.getProperty ("halftrackRepitch", false)))
        return ts;

    if (ts == TimeStretcher::disabled && (getAutoPitch() || getAutoTempo() || getPitchChange() != 0.0f))
        ts = TimeStretcher::defaultMode;]=])
    foreach(index RANGE 0 0)
        file(SHA256 "${source${index}}" hash)
        if(hash STREQUAL "${patched${index}}")
            set(write${index} FALSE)
            continue()
        endif()
        if(NOT hash STREQUAL "${original${index}}")
            message(FATAL_ERROR "Halftrack Re-Pitch patch requires pinned source ${source${index}}; unexpected SHA256 ${hash}.")
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
            message(FATAL_ERROR "Halftrack Re-Pitch patch produced unexpected SHA256 ${hash} for ${source${index}}.")
        endif()
        set(write${index} TRUE)
    endforeach()
    foreach(index RANGE 0 0)
        if(write${index})
            string(REGEX REPLACE "\n$" "" configure_content "${content${index}}")
            file(CONFIGURE OUTPUT "${source${index}}" CONTENT "@configure_content@" @ONLY NEWLINE_STYLE LF)
            file(SHA256 "${source${index}}" hash)
            if(NOT hash STREQUAL "${patched${index}}")
                message(FATAL_ERROR "Halftrack Re-Pitch patch wrote unexpected SHA256 ${hash}.")
            endif()
        endif()
    endforeach()
endfunction()
