# Narrow offline-render corrections for the pinned Tracktion source.
# Preserve dependency notices; preflight and verify before replacing the source.
function(halftrack_patch_render_latency source_dir)
    set(source "${source_dir}/modules/tracktion_engine/playback/graph/tracktion_NodeRenderContext.cpp")
    set(original "40f8e18c146c05e93dc40a54602a6af674ca81a6253120e4782e9a59ae5af6a2")
    set(previous "cde4f3dbc50f0f1378e6f7b5f638422ba985333bb8abb3e8a6a4d060320a10e5")
    set(patched "57e58a225e0f433fb96920f03731ac37abe805205420e49e5c4ef7f0276186a4")
    set(downstream "9d09d2ac4b345d6d59d9c9c9b1bf809badf5ce2c08e2bb6961794309350a8bae") # The later freeze tails patch.
    file(SHA256 "${source}" source_hash)
    if(source_hash STREQUAL patched OR source_hash STREQUAL downstream)
        return()
    endif()
    if(NOT source_hash STREQUAL original AND NOT source_hash STREQUAL previous)
        message(FATAL_ERROR "Halftrack render patch requires pinned source; unexpected SHA256 ${source_hash}.")
    endif()
    file(READ "${source}" content)
    if(source_hash STREQUAL original)
        string(REPLACE [=[            blockOffset = destView.getNumFrames() - blockSize;]=] [=[            // Halftrack: the final render block may be shorter than destView.
            blockOffset = numToDrop;]=] content "${content}")
        string(REPLACE [=[    juce::AudioBuffer<float> buffer (block.data.channels, numOutputChans, blockSizeSamples);]=] [=[    // Halftrack: latency compensation passes a sliced view with an offset.
    juce::AudioBuffer<float> buffer (block.data.channels, numOutputChans, (int) block.data.offset, blockSizeSamples);]=] content "${content}")
    endif()
    string(REPLACE [=[        for (int destCh = 0; destCh < numOutputChans; ++destCh)
        {
            auto srcCh = outputChannelConfig[(size_t) destCh].indexInDevice;

            if (srcCh >= 0 && srcCh < graphChans)
                outputChannelMap.entries.push_back ({ srcCh, destCh, 1.0f });
        }]=] [=[        if (r.mustRenderInMono && graphChans > 1)
        {
            // A forced mono render combines the full mix, not only channel zero.
            const float gain = 1.0f / static_cast<float> (graphChans);
            for (int srcCh = 0; srcCh < graphChans; ++srcCh)
                outputChannelMap.entries.push_back ({ srcCh, 0, gain });
        }
        else
        {
            for (int destCh = 0; destCh < numOutputChans; ++destCh)
            {
                auto srcCh = outputChannelConfig[(size_t) destCh].indexInDevice;

                if (srcCh >= 0 && srcCh < graphChans)
                    outputChannelMap.entries.push_back ({ srcCh, destCh, 1.0f });
            }
        }]=] content "${content}")
    string(SHA256 transformed "${content}")
    if(NOT transformed STREQUAL patched)
        message(FATAL_ERROR "Halftrack render patch produced unexpected SHA256 ${transformed}.")
    endif()
    string(REGEX REPLACE "\n$" "" configure_content "${content}")
    file(CONFIGURE OUTPUT "${source}" CONTENT "@configure_content@" @ONLY NEWLINE_STYLE LF)
    file(SHA256 "${source}" written)
    if(NOT written STREQUAL patched)
        message(FATAL_ERROR "Halftrack render patch wrote unexpected SHA256 ${written}.")
    endif()
endfunction()
