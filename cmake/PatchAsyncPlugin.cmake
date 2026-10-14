# Asynchronous plug-in lifecycle fixes for Tracktion 07e2d674884293f1a6c8f35fb8d1b9f469ffa41f.
# Preserve dependency notices; no real-time processing path is changed.
function(halftrack_patch_async_plugin source_dir)
    set(source "${source_dir}/modules/tracktion_engine/plugins/external/tracktion_ExternalPlugin.cpp")
    file(SHA256 "${source}" source_hash)
    if(source_hash STREQUAL "9e489383996f1e5da43529f3bf2ce4f12818bef01e17378a014b07144688b01c")
        return()
    endif()
    if(NOT source_hash STREQUAL "b13b051c3c7b2c098a43aef53d0607be26dbfab0b948884b11d638518ab14476"
       AND NOT source_hash STREQUAL "f01dd4643715e585904b1adc15b38334644ca82bb4567e2d6623e0b3478c92e7")
        message(FATAL_ERROR "Halftrack async plug-in patch requires pinned source: ${source}; unexpected SHA256 ${source_hash}.")
    endif()
    file(READ "${source}" content)
    if(source_hash STREQUAL "b13b051c3c7b2c098a43aef53d0607be26dbfab0b948884b11d638518ab14476")
    set(before [=[                                            if (! ep)
                                                return;

                                            ep->loadError = err;]=])
    set(after [=[                                            if (! ep)
                                                return;

                                            // Halftrack: a removed plug-in can remain in the edit cache.
                                            // Do not initialise detached state through its UndoManager.
                                            if (! ep->state.isAChildOf (ep->edit.state))
                                            {
                                                ep->isAsyncInitialising = false;
                                                ep->fullyInitialised = false;
                                                return;
                                            }

                                            ep->loadError = err;]=])
    string(REPLACE "${before}" "${after}" content "${content}")
    set(before [=[    if (! newInstance)
    {
        TRACKTION_LOG_ERROR (loadError);
        return;
    }
]=])
    set(after [=[    if (! newInstance)
    {
        isAsyncInitialising = false;
        TRACKTION_LOG_ERROR (loadError);
        return;
    }
]=])
    string(REPLACE "${before}" "${after}" content "${content}")
    set(before [=[void ExternalPlugin::trackPropertiesChanged()
{
    if (edit.isLoading())
        return;

    TRACKTION_ASSERT_MESSAGE_THREAD
]=])
    set(after [=[void ExternalPlugin::trackPropertiesChanged()
{
    if (edit.isLoading())
        return;

    TRACKTION_ASSERT_MESSAGE_THREAD
    // Halftrack: undo/redo may reattach a cached plug-in whose pending creation
    // completed while detached. Restart only that deferred initialisation.
    if (! fullyInitialised && state.isAChildOf (edit.state))
        initialiseFully();

]=])
    string(REPLACE "${before}" "${after}" content "${content}")
    endif()
    set(before [=[                                            if (! ep)
                                                return;

                                            // Halftrack: a removed plug-in can remain in the edit cache.
                                            // Do not initialise detached state through its UndoManager.
                                            if (! ep->state.isAChildOf (ep->edit.state))
                                            {
                                                ep->isAsyncInitialising = false;
                                                ep->fullyInitialised = false;
                                                return;
                                            }

                                            ep->loadError = err;
                                            ep->completePluginInstanceCreation (std::move (instance));

                                            // Trigger any status messages to be updated
                                            ep->changed();

                                            // If we call restartPlayback without clearing the sampleRate and blockSize,
                                            // the plugin won't get fully initialised which we need in order for
                                            // prepareToPlay to be called
                                            ep->sampleRate = {};
                                            ep->blockSizeSamples = {};
                                            ep->edit.restartPlayback();
]=])
    set(after [=[                                            // Halftrack: formats may deliver creation on any thread.
                                            // Check the weak target and touch edit state only on the message thread.
                                            auto completeOnMessageThread = [ep, instance = std::move (instance), err = std::move (err)]() mutable
                                            {
                                                if (! ep)
                                                    return;

                                                // Halftrack: a removed plug-in can remain in the edit cache.
                                                // Do not initialise detached state through its UndoManager.
                                                if (! ep->state.isAChildOf (ep->edit.state))
                                                {
                                                    ep->isAsyncInitialising = false;
                                                    ep->fullyInitialised = false;
                                                    return;
                                                }

                                                ep->loadError = err;
                                                ep->completePluginInstanceCreation (std::move (instance));

                                                // Trigger any status messages to be updated
                                                ep->changed();

                                                // If we call restartPlayback without clearing the sampleRate and blockSize,
                                                // the plugin won't get fully initialised which we need in order for
                                                // prepareToPlay to be called
                                                ep->sampleRate = {};
                                                ep->blockSizeSamples = {};
                                                ep->edit.restartPlayback();
                                            };

                                            if (juce::MessageManager::getInstance()->isThisTheMessageThread())
                                                completeOnMessageThread();
                                            else
                                                juce::MessageManager::callAsync (std::move (completeOnMessageThread));
]=])
    string(REPLACE "${before}" "${after}" content "${content}")
    string(SHA256 patched_hash "${content}")
    if(NOT patched_hash STREQUAL "9e489383996f1e5da43529f3bf2ce4f12818bef01e17378a014b07144688b01c")
        message(FATAL_ERROR "Halftrack async plug-in patch produced unexpected SHA256 ${patched_hash}: ${source}.")
    endif()
    # CONFIGURE adds one template line ending; preserve exact source bytes.
    string(REGEX REPLACE "\n$" "" configure_content "${content}")
    file(CONFIGURE OUTPUT "${source}" CONTENT "@configure_content@" @ONLY NEWLINE_STYLE LF)
    file(SHA256 "${source}" written_hash)
    if(NOT written_hash STREQUAL "9e489383996f1e5da43529f3bf2ce4f12818bef01e17378a014b07144688b01c")
        message(FATAL_ERROR "Halftrack async plug-in patch wrote unexpected SHA256 ${written_hash}: ${source}.")
    endif()
endfunction()
