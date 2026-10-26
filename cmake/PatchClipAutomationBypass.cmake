# Halftrack clip automation overrides for pinned Tracktion Engine, after every earlier patch. A clip's
# curve on a parameter kept playing while its curve was bypassed unless all three of its curves
# (absolute, relative and scale) were, so an overridden clip automation envelope still set the
# control while the clip's other curves were in use, and the message thread's value queries ignored
# bypassing altogether. A bypassed curve now leaves the parameter alone in both, as a bypassed track
# curve does. Retains all upstream source notices. Validates every file before writing any.
function(halftrack_patch_clip_automation_bypass source_dir)
    set(source0 "${source_dir}/modules/tracktion_engine/model/automation/tracktion_AutomatableParameter.cpp")
    set(original0 "fd6b4d431d6d47a1478b972e419d8a0f10c06be9ca4e7695385dde9420767de1")
    set(patched0 "d2a1876cc068b09b26def43b0dce3cb3585afdafd2aa313920d74b2c20cf3164")
    set(count0 1)
    set(before0_0 [=[        void processValue (float& baseValue, float& modValue)
        {
            if (isActive())
                detail::processValue (parameter, curveInfo.type, getCurrentValue(), baseValue, modValue);
        }]=])
    set(after0_0 [=[        void processValue (float& baseValue, float& modValue)
        {
            // Halftrack: a bypassed curve (an overridden clip automation envelope) leaves the parameter alone.
            if (isActive() && ! curveInfo.curve.bypass.get())
                detail::processValue (parameter, curveInfo.type, getCurrentValue(), baseValue, modValue);
        }]=])
    set(source1 "${source_dir}/modules/tracktion_engine/model/automation/tracktion_AutomationCurveList.cpp")
    set(original1 "1c2b5cd4338376b342ef856e811762b396bf6fa40dd2cee57a8723a5a62f3987")
    set(patched1 "ac33fb50883eb5a6c9115b001f7069272f8133abd1ac03abcbb1212376992434")
    set(count1 1)
    set(before1_0 [=[        if (curveInfo.curve.getNumPoints() == 0)
            continue;

        EditPosition curvePos = editPos;]=])
    set(after1_0 [=[        // Halftrack: a bypassed curve leaves the parameter alone, as it does on the audio thread.
        if (curveInfo.curve.getNumPoints() == 0 || curveInfo.curve.bypass.get())
            continue;

        EditPosition curvePos = editPos;]=])
    foreach(index RANGE 0 1)
        file(SHA256 "${source${index}}" hash)
        if(hash STREQUAL "${patched${index}}" OR (DEFINED downstream${index} AND hash IN_LIST downstream${index}))
            set(write${index} FALSE)
            continue()
        endif()
        if(NOT hash STREQUAL "${original${index}}")
            message(FATAL_ERROR "Halftrack clip automation bypass patch requires pinned source ${source${index}}; unexpected SHA256 ${hash}.")
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
            message(FATAL_ERROR "Halftrack clip automation bypass patch produced unexpected SHA256 ${hash} for ${source${index}}.")
        endif()
        set(write${index} TRUE)
    endforeach()
    foreach(index RANGE 0 1)
        if(write${index})
            string(REGEX REPLACE "\n$" "" configure_content "${content${index}}")
            file(CONFIGURE OUTPUT "${source${index}}" CONTENT "@configure_content@" @ONLY NEWLINE_STYLE LF)
            file(SHA256 "${source${index}}" hash)
            if(NOT hash STREQUAL "${patched${index}}")
                message(FATAL_ERROR "Halftrack clip automation bypass patch wrote unexpected SHA256 ${hash}.")
            endif()
        endif()
    endforeach()
endfunction()
