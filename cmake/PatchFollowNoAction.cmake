# Halftrack follow-action "no action" for pinned Tracktion Engine.
# When a weighted follow action chooses no action, the clip carries on playing in phase
# rather than staying stopped at the follow-action time.
# Retains all upstream source notices. Validates every file before writing any.
function(halftrack_patch_follow_no_action source_dir)
    set(source0 "${source_dir}/modules/tracktion_engine/model/clips/tracktion_FollowActions.cpp")
    set(original0 "02b464d6810afd73e644fddce42c3b5a9ee63850bb7b2c56427b50337ea8c846")
    set(patched0 "e09f9b565661defe66fea39d770f53b9efc8f74f6a78c8d5a91babc51d08d286")
    set(count0 3)
    set(before0_0 [=[        juce::Range<double> probabilityRange;
        std::function<void (MonotonicBeat)> action;
    };]=])
    set(after0_0 [=[        juce::Range<double> probabilityRange;
        std::function<void (MonotonicBeat)> action;
        bool continuePlaying = false; // Halftrack: the choice was "no action".
    };]=])
    set(before0_1 [=[        container.action = createFollowAction (ctx, action->action);]=])
    set(after0_1 [=[        container.action = createFollowAction (ctx, action->action);
        container.continuePlaying = action->action.get() == FollowAction::none;]=])
    set(before0_2 [=[                      if (actionContainer.action)
                          return actionContainer.action (beat);

                      return;]=])
    set(after0_2 [=[                      if (actionContainer.action)
                          return actionContainer.action (beat);

                      // Halftrack: a chosen "no action" keeps the clip playing in phase instead of
                      // leaving it stopped at the follow-action time.
                      if (actionContainer.continuePlaying && ctx->launchHandle)
                          ctx->launchHandle->playSynced (*ctx->launchHandle, beat);

                      return;]=])
    foreach(index RANGE 0 0)
        file(SHA256 "${source${index}}" hash)
        if(hash STREQUAL "${patched${index}}")
            set(write${index} FALSE)
            continue()
        endif()
        if(NOT hash STREQUAL "${original${index}}")
            message(FATAL_ERROR "Halftrack follow no-action patch requires pinned source ${source${index}}; unexpected SHA256 ${hash}.")
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
            message(FATAL_ERROR "Halftrack follow no-action patch produced unexpected SHA256 ${hash} for ${source${index}}.")
        endif()
        set(write${index} TRUE)
    endforeach()
    foreach(index RANGE 0 0)
        if(write${index})
            string(REGEX REPLACE "\n$" "" configure_content "${content${index}}")
            file(CONFIGURE OUTPUT "${source${index}}" CONTENT "@configure_content@" @ONLY NEWLINE_STYLE LF)
            file(SHA256 "${source${index}}" hash)
            if(NOT hash STREQUAL "${patched${index}}")
                message(FATAL_ERROR "Halftrack follow no-action patch wrote unexpected SHA256 ${hash}.")
            endif()
        endif()
    endforeach()
endfunction()
