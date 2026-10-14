# Confirmed-exit scanner fast path for pinned JUCE 37c894f83d379179b2070d437ccd0f1cd9af9576.
# Preserve notices; preflight every original and computed result before writing.
# The pinned JUCE originals use CRLF; verified patched output is normalised to LF.
function(halftrack_patch_child_process source_dir)
    set(relative_0 "modules/juce_events/interprocess/juce_ConnectedChildProcess.h")
    set(original_0 "697e20d898c9bee6d8f2e87bd2763fe086118c8dd207b36e981d342289911933")
    set(patched_0 "870a7ac9264e8d53dba2a08ff56e20a8afd760e9764c0311588842408f1fde32")
    set(relative_1 "modules/juce_events/interprocess/juce_ConnectedChildProcess.cpp")
    set(original_1 "a8e7e6de59390d48c4b44a8240804e82d4276f57d8aefe51199464c0ea4e53c1")
    set(patched_1 "9563a5c0cb0bfbf90c307742e1a2afbff820d5daca0cbfaff5955e0c699cebef")
    foreach(index RANGE 0 1)
        set(source "${source_dir}/${relative_${index}}")
        file(SHA256 "${source}" source_hash)
        set(needs_patch_${index} FALSE)
        if(source_hash STREQUAL "${patched_${index}}")
            continue()
        endif()
        if(NOT source_hash STREQUAL "${original_${index}}")
            message(FATAL_ERROR "Halftrack child-process patch requires pinned ${relative_${index}}. Unexpected SHA256: ${source_hash}.")
        endif()
        file(READ "${source}" content)
        if(index EQUAL 0)
            string(REPLACE [=[private:
    std::shared_ptr<ChildProcess> childProcess;]=] [=[   #if JUCE_WINDOWS && defined (HALFTRACK_TESTS_ENABLED) && HALFTRACK_TESTS_ENABLED
    // Halftrack: static-only test observations; no instance layout change.
    struct ExitTestCounts
    {
        int classifications, teardownSkips, deadPeerWrites;
    };
    static ExitTestCounts getExitTestCounts();
    static bool setExitTestSuppressConnectionLoss (bool suppress);
   #endif

   #if JUCE_WINDOWS
protected:
    /** Halftrack: true only when the retained worker process handle confirms exit.
        Call on the process-management thread, serialised with launch/kill.
        A missing worker or a failed process query is not a confirmed exit.
    */
    bool hasWorkerProcessExited() const;

    #if defined (HALFTRACK_TESTS_ENABLED) && HALFTRACK_TESTS_ENABLED
    static void recordExitTestClassification();
    static bool isExitTestConnectionLossSuppressed();
    #endif
   #endif

private:
    std::shared_ptr<ChildProcess> childProcess;]=] content "${content}")
        else()
            string(REPLACE [=[bool ChildProcessCoordinator::sendMessageToWorker (const MemoryBlock& mb)
{]=] [=[#if JUCE_WINDOWS
bool ChildProcessCoordinator::hasWorkerProcessExited() const
{
    return childProcess != nullptr && ! childProcess->isRunning();
}
#endif

#if JUCE_WINDOWS && defined (HALFTRACK_TESTS_ENABLED) && HALFTRACK_TESTS_ENABLED
namespace
{
std::atomic<int> halftrackExitClassifications { 0 }, halftrackExitedTeardownSkips { 0 }, halftrackDeadPeerWrites { 0 };
std::atomic<bool> halftrackSuppressConnectionLoss { false };
}

ChildProcessCoordinator::ExitTestCounts ChildProcessCoordinator::getExitTestCounts()
{
    return { halftrackExitClassifications.load(), halftrackExitedTeardownSkips.load(), halftrackDeadPeerWrites.load() };
}

bool ChildProcessCoordinator::setExitTestSuppressConnectionLoss (bool suppress)
{
    return halftrackSuppressConnectionLoss.exchange (suppress);
}

bool ChildProcessCoordinator::isExitTestConnectionLossSuppressed()
{
    return halftrackSuppressConnectionLoss.load();
}

void ChildProcessCoordinator::recordExitTestClassification()
{
    ++halftrackExitClassifications;
}
#endif

bool ChildProcessCoordinator::sendMessageToWorker (const MemoryBlock& mb)
{
   #if JUCE_WINDOWS && defined (HALFTRACK_TESTS_ENABLED) && HALFTRACK_TESTS_ENABLED
    // Only kill messages query child ownership here; ping threads never do.
    if (isMessageType (mb, killMessage) && hasWorkerProcessExited())
        ++halftrackDeadPeerWrites;
   #endif]=] content "${content}")
            string(REPLACE [=[        sendMessageToWorker ({ killMessage, specialMessageSize });
        connection->disconnect();]=] [=[       #if JUCE_WINDOWS
        // An exited child cannot receive shutdown. Disconnect still cancels
        // outstanding pipe I/O; healthy peers retain the original timeout.
        if (! hasWorkerProcessExited())
            sendMessageToWorker ({ killMessage, specialMessageSize });
        #if defined (HALFTRACK_TESTS_ENABLED) && HALFTRACK_TESTS_ENABLED
        else
            ++halftrackExitedTeardownSkips;
        #endif
       #else
        sendMessageToWorker ({ killMessage, specialMessageSize });
       #endif
        connection->disconnect();]=] content "${content}")
        endif()
        string(SHA256 result_hash "${content}")
        if(NOT result_hash STREQUAL "${patched_${index}}")
            message(FATAL_ERROR "Halftrack child-process patch produced unexpected ${relative_${index}}. SHA256: ${result_hash}.")
        endif()
        set(needs_patch_${index} TRUE)
        set(result_${index} "${content}")
    endforeach()
    foreach(index RANGE 0 1)
        if(needs_patch_${index})
            set(source "${source_dir}/${relative_${index}}")
            file(CONFIGURE OUTPUT "${source}" CONTENT "${result_${index}}" @ONLY NEWLINE_STYLE LF)
            file(SHA256 "${source}" written_hash)
            if(NOT written_hash STREQUAL "${patched_${index}}")
                message(FATAL_ERROR "Halftrack child-process patch could not write verified ${relative_${index}}.")
            endif()
            message(STATUS "Applied JUCE confirmed-exit patch to ${relative_${index}}")
        endif()
    endforeach()
endfunction()
