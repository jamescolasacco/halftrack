# Runs at every build: writes the version and the source commit the build came from to OUTPUT,
# touching the file only when they change. A build from a tree with uncommitted changes to
# tracked files says so; one without Git history reports "unknown" unless HALFTRACK_COMMIT is set.
if(NOT HALFTRACK_COMMIT)
    set(HALFTRACK_COMMIT "unknown")
    if(GIT_EXECUTABLE AND EXISTS "${SOURCE_DIR}/.git")
        execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --short=7 HEAD WORKING_DIRECTORY "${SOURCE_DIR}"
                        OUTPUT_VARIABLE head OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE failed ERROR_QUIET)
        if(NOT failed AND head)
            set(HALFTRACK_COMMIT "${head}")
            execute_process(COMMAND "${GIT_EXECUTABLE}" status --porcelain --untracked-files=no WORKING_DIRECTORY "${SOURCE_DIR}"
                            OUTPUT_VARIABLE changes OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
            if(changes)
                string(APPEND HALFTRACK_COMMIT "-modified")
            endif()
        endif()
    endif()
endif()
set(existing "")
set(content "#pragma once\n#define HALFTRACK_VERSION \"${VERSION}\"\n#define HALFTRACK_COMMIT \"${HALFTRACK_COMMIT}\"\n")
if(EXISTS "${OUTPUT}")
    file(READ "${OUTPUT}" existing)
endif()
if(NOT existing STREQUAL content)
    file(WRITE "${OUTPUT}" "${content}")
endif()
