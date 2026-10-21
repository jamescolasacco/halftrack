# Halftrack render routing for pinned Tracktion Engine, after the clip-launcher patch.
# Live playback processes a track whose output goes to no device or track as a silent bus, so
# only its sends are heard, but the render graph summed it into the mix. Renders now process
# such a track the same way.
# Retains all upstream source notices. Validates every file before writing any.
function(halftrack_patch_render_sends_only source_dir)
    set(source0 "${source_dir}/modules/tracktion_engine/playback/graph/tracktion_EditNodeBuilder.cpp")
    set(original0 "31fb6f4d5b21355a67867c71461c26b17464e450c3c0e945173dd99f8550b074")
    set(patched0 "51ef9b7dfa77221251da5f43f77f91110f8b375de8974e6f45cac2c08c98ad48")
    set(count0 1)
    set(downstream0 "06c1d2be2f00c494f5db33c2b5ced64f9395e9fb7e16e4e8002ef7beacfa9ee8" "bfc26bb83dcc389b9b3cd64db016f5e493e7f1f982b7c8910be6524c30703766") # The later track-inputs and MIDI variations patches.
    set(before0_0 [=[        if (auto node = createNodeForTrack (*t, params))
            trackNodes.push_back (std::move (node));
    }

    auto sumNode = std::make_unique<SummingNode> (std::move (trackNodes));]=])
    set(after0_0 [=[        if (auto node = createNodeForTrack (*t, params))
        {
            // Halftrack: a track playing to no device or track is processed for its sends, as live
            // playback processes it, but its own output is not part of the render.
            if (auto output = getTrackOutput (*t); output != nullptr && output->outputsToNone())
                trackNodes.push_back (makeNode<SinkNode> (std::move (node)));
            else
                trackNodes.push_back (std::move (node));
        }
    }

    auto sumNode = std::make_unique<SummingNode> (std::move (trackNodes));]=])
    foreach(index RANGE 0 0)
        file(SHA256 "${source${index}}" hash)
        if(hash STREQUAL "${patched${index}}" OR (DEFINED downstream${index} AND hash IN_LIST downstream${index}))
            set(write${index} FALSE)
            continue()
        endif()
        if(NOT hash STREQUAL "${original${index}}")
            message(FATAL_ERROR "Halftrack render sends-only patch requires pinned source ${source${index}}; unexpected SHA256 ${hash}.")
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
            message(FATAL_ERROR "Halftrack render sends-only patch produced unexpected SHA256 ${hash} for ${source${index}}.")
        endif()
        set(write${index} TRUE)
    endforeach()
    foreach(index RANGE 0 0)
        if(write${index})
            string(REGEX REPLACE "\n$" "" configure_content "${content${index}}")
            file(CONFIGURE OUTPUT "${source${index}}" CONTENT "@configure_content@" @ONLY NEWLINE_STYLE LF)
            file(SHA256 "${source${index}}" hash)
            if(NOT hash STREQUAL "${patched${index}}")
                message(FATAL_ERROR "Halftrack render sends-only patch wrote unexpected SHA256 ${hash}.")
            endif()
        endif()
    endforeach()
endfunction()
