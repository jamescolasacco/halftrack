# Narrow offline-render fix for Tracktion Engine 07e2d674884293f1a6c8f35fb8d1b9f469ffa41f.
# Preserve the dependency's notices and verify exact source before changing it.
function(halftrack_patch_render_latency source_dir)
    set(source "${source_dir}/modules/tracktion_engine/playback/graph/tracktion_NodeRenderContext.cpp")
    file(SHA256 "${source}" source_hash)
    if(source_hash STREQUAL "cde4f3dbc50f0f1378e6f7b5f638422ba985333bb8abb3e8a6a4d060320a10e5")
        return()
    endif()
    if(NOT source_hash STREQUAL "40f8e18c146c05e93dc40a54602a6af674ca81a6253120e4782e9a59ae5af6a2")
        message(FATAL_ERROR "Halftrack's export-latency patch requires the pinned NodeRenderContext.cpp. Unexpected SHA256: ${source_hash}.")
    endif()
    file(READ "${source}" content)
    string(REPLACE [=[            blockOffset = destView.getNumFrames() - blockSize;]=] [=[            // Halftrack: the final render block may be shorter than destView.
            blockOffset = numToDrop;]=] content "${content}")
    string(REPLACE [=[    juce::AudioBuffer<float> buffer (block.data.channels, numOutputChans, blockSizeSamples);]=] [=[    // Halftrack: latency compensation passes a sliced view with an offset.
    juce::AudioBuffer<float> buffer (block.data.channels, numOutputChans, (int) block.data.offset, blockSizeSamples);]=] content "${content}")
    # Explicit LF keeps the patched source pin identical on every platform.
    file(CONFIGURE OUTPUT "${source}" CONTENT "${content}" @ONLY NEWLINE_STYLE LF)
    file(SHA256 "${source}" patched_hash)
    if(NOT patched_hash STREQUAL "cde4f3dbc50f0f1378e6f7b5f638422ba985333bb8abb3e8a6a4d060320a10e5")
        message(FATAL_ERROR "Halftrack's export-latency patch did not produce the expected source. Actual SHA256: ${patched_hash}.")
    endif()
    message(STATUS "Applied Tracktion export-latency patch: preserve sliced and short audio blocks")
endfunction()
