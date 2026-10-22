# Halftrack loop recording of any length for pinned Tracktion Engine. The host splits a loop
# recording into its passes itself, so the engine's two-second minimum loop is lowered to a guard
# against degenerate loops. Retains all upstream source notices. Validates before writing.
function(halftrack_patch_loop_record_length source_dir)
    set(source "${source_dir}/modules/tracktion_engine/playback/tracktion_TransportControl.cpp")
    set(original "07f46651b67377e32bd962204c27e3f1fe57e819f21cde4e65e1f884d35eb6c0")
    set(patched "5e43a391154ab64092d83eeeb8dddfc57718a11be23e753e4a1fdb0ec83c931d")
    set(downstream "d85899d168abaf3ca1e47eb586a2018ca54b642b20ab92c15de1e970edd9724f" "cfd436495804e855c4c99db7aeb0080ea35b328ac339d926ce30804e40d431be") # The later playback continuation and count-in start patches.
    set(before [=[                    if (loopRange.getLength() < 2s)
                    {
                        engine.getUIBehaviour().showWarningMessage (TRANS("To record in loop mode, the length of loop must be greater than 2 seconds."));
                        return std::nullopt;
                    }]=])
    set(after [=[                    // Halftrack: the host splits loop recordings into passes itself, so loops of any
                    // length record; only a degenerate one is refused.
                    if (loopRange.getLength() < 0.01s)
                    {
                        engine.getUIBehaviour().showWarningMessage (TRANS("To record in loop mode, the loop must be longer than 10 milliseconds."));
                        return std::nullopt;
                    }]=])
    file(SHA256 "${source}" hash)
    if(hash STREQUAL "${patched}" OR hash IN_LIST downstream)
        return()
    endif()
    if(NOT hash STREQUAL "${original}")
        message(FATAL_ERROR "Halftrack loop recording patch requires pinned source ${source}; unexpected SHA256 ${hash}.")
    endif()
    file(READ "${source}" content)
    string(REPLACE "${before}" "${after}" content "${content}")
    string(SHA256 hash "${content}")
    if(NOT hash STREQUAL "${patched}")
        message(FATAL_ERROR "Halftrack loop recording patch produced unexpected SHA256 ${hash}.")
    endif()
    string(REGEX REPLACE "\n$" "" configure_content "${content}")
    file(CONFIGURE OUTPUT "${source}" CONTENT "@configure_content@" @ONLY NEWLINE_STYLE LF)
    file(SHA256 "${source}" hash)
    if(NOT hash STREQUAL "${patched}")
        message(FATAL_ERROR "Halftrack loop recording patch wrote unexpected SHA256 ${hash}.")
    endif()
endfunction()
