# Halftrack render job lifetimes for pinned Tracktion Engine. A RenderManager job cancelled while it
# waited in the background job pool, which does not own it (for example a reversed clip's render when
# an undo deletes the clip a moment later), handled its completion message as finished and released
# itself while still queued, and a job cancelled before its start message still joined the pool. The
# pool's next pick then read the deleted job: a heap use after free in
# juce::ThreadPool::pickNextJobToRun, found with AddressSanitizer, and an intermittent crash. A job
# cancelled before it starts no longer joins the pool, and a finished or cancelled job leaves the
# pool's queue before it is released; one a pool thread has taken waits as a running job does.
# Retains all upstream source notices. Validates every file before writing any.
function(halftrack_patch_render_jobs source_dir)
    set(source0 "${source_dir}/modules/tracktion_engine/model/export/tracktion_RenderManager.cpp")
    set(original0 "0d8bac1242b5ebc27e835959b79dd0a60ee1ce4fefc9ddfc5a47e3784432de24")
    set(patched0 "108919a7f97f21e77dbb916b4e4648a952885430526d418f0454bc6c9d65a9d6")
    set(count0 2)
    set(before0_0 [=[        case UpdateMessage::started:
            // starts the job running
            engine.getRenderManager().addJobToPool (this);]=])
    set(after0_0 [=[        case UpdateMessage::started:
            // starts the job running
            // Halftrack: unless it was cancelled before it started, as when its clip goes at once.
            if (! hasFinished)
                engine.getRenderManager().addJobToPool (this);]=])
    set(before0_1 [=[        case UpdateMessage::succeded:
        case UpdateMessage::cancelled:
            if (isRunning())
            {]=])
    set(after0_1 [=[        case UpdateMessage::succeded:
        case UpdateMessage::cancelled:
            // Halftrack: a job still waiting in the pool, which does not own it, leaves the pool before it
            // is released, so the pool never holds a deleted job; one a pool thread has taken waits as a
            // running job does.
            if (isRunning() || ! engine.getBackgroundJobs().getPool().removeJob (this, false, 0))
            {]=])
    foreach(index RANGE 0 0)
        file(SHA256 "${source${index}}" hash)
        if(hash STREQUAL "${patched${index}}" OR (DEFINED downstream${index} AND hash IN_LIST downstream${index}))
            set(write${index} FALSE)
            continue()
        endif()
        if(NOT hash STREQUAL "${original${index}}")
            message(FATAL_ERROR "Halftrack render job lifetimes patch requires pinned source ${source${index}}; unexpected SHA256 ${hash}.")
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
            message(FATAL_ERROR "Halftrack render job lifetimes patch produced unexpected SHA256 ${hash} for ${source${index}}.")
        endif()
        set(write${index} TRUE)
    endforeach()
    foreach(index RANGE 0 0)
        if(write${index})
            string(REGEX REPLACE "\n$" "" configure_content "${content${index}}")
            file(CONFIGURE OUTPUT "${source${index}}" CONTENT "@configure_content@" @ONLY NEWLINE_STYLE LF)
            file(SHA256 "${source${index}}" hash)
            if(NOT hash STREQUAL "${patched${index}}")
                message(FATAL_ERROR "Halftrack render job lifetimes patch wrote unexpected SHA256 ${hash}.")
            endif()
        endif()
    endforeach()
endfunction()
