# Halftrack freeze tails for pinned Tracktion Engine, after every earlier patch. In a render's end
# allowance, the first silent block stopped the render, so a tail with a gap in it, such as a
# delay's echoes after the dry sound ends, was cut at the gap and frozen tracks lost their echoes.
# A render may now ask for a stretch of silence before it stops: the silent blocks are held back,
# written if the tail sounds again, and dropped when the silence lasts. Freezes ask for three
# seconds, as the reference renders a freeze until it is silent; other renders stop as before.
# Retains all upstream source notices. Validates every file before writing any.
function(halftrack_patch_freeze_tails source_dir)
    set(source0 "${source_dir}/modules/tracktion_engine/model/export/tracktion_Renderer.h")
    set(original0 "305c2c08e96648615f06700339ecbecba95da529fcd0a61812158d3941aff696")
    set(patched0 "0511a660f3411abc72afe1bcc6c8bf12e5e152f4d79174db70d021456197b985")
    set(count0 1)
    set(before0_0 [=[                                                                     Mostly useful for freeze files and track renders, not usually in exporting. */
]=])
    set(after0_0 [=[                                                                     Mostly useful for freeze files and track renders, not usually in exporting. */
        TimeDuration silenceBeforeStopping;                     /**< Halftrack: how long the level must stay silent in the end allowance before
                                                                     the render stops, so a tail with gaps (a delay's echoes) is kept. The silence
                                                                     that ends the render is not written. Zero stops at the first silent block. */
]=])
    set(source1 "${source_dir}/modules/tracktion_engine/playback/graph/tracktion_NodeRenderContext.h")
    set(original1 "265b6bee4a3fe7d043f7c591e056bed95b1d3f10d3a91356a70ba3c3713b294b")
    set(patched1 "c6802d754da6f0c6abde0fb290bb69a6136c3528472fad0992b3e0307d1b2a9a")
    set(count1 1)
    set(before1_0 [=[    int64_t samplesPastRangeStart = 0, rangeLengthInSamples = 0;
]=])
    set(after1_0 [=[    int64_t samplesPastRangeStart = 0, rangeLengthInSamples = 0;
    // Halftrack: silent blocks held back in the end allowance, written if the tail sounds again.
    juce::AudioBuffer<float> heldSilence;
    int heldSilenceSamples = 0;
]=])
    set(source2 "${source_dir}/modules/tracktion_engine/playback/graph/tracktion_NodeRenderContext.cpp")
    set(original2 "57e58a225e0f433fb96920f03731ac37abe805205420e49e5c4ef7f0276186a4")
    set(patched2 "9d09d2ac4b345d6d59d9c9c9b1bf809badf5ce2c08e2bb6961794309350a8bae")
    set(count2 3)
    set(before2_0 [=[    rangeLengthInSamples = tracktion::toSamples (r.time.getLength(), r.sampleRateForAudio);
    samplesPastRangeStart = 0;
]=])
    set(after2_0 [=[    rangeLengthInSamples = tracktion::toSamples (r.time.getLength(), r.sampleRateForAudio);
    samplesPastRangeStart = 0;
    heldSilenceSamples = 0;
]=])
    set(before2_1 [=[            const auto numInRange = (uint32_t) juce::jlimit ((int64_t) 0, (int64_t) blockSize,
                                                             rangeLengthInSamples - samplesPastRangeStart);

            if (numInRange < blockSize
                 && renderingBuffer.getMagnitude ((int) (blockOffset + numInRange),
                                                  (int) (blockSize - numInRange)) <= thresholdForStopping)
            {
                if (numInRange > 0)
                    if (writeAudioBlock (destView.getFrameRange ({ blockOffset, blockOffset + numInRange })) == WriteResult::failed)
                        return true;

                return true;
            }

            samplesPastRangeStart += blockSize;
        }
]=])
    set(after2_1 [=[            const auto numInRange = (uint32_t) juce::jlimit ((int64_t) 0, (int64_t) blockSize,
                                                             rangeLengthInSamples - samplesPastRangeStart);
            const bool silent = numInRange < blockSize
                                 && renderingBuffer.getMagnitude ((int) (blockOffset + numInRange),
                                                                  (int) (blockSize - numInRange)) <= thresholdForStopping;

            if (silent && r.silenceBeforeStopping > 0_td)
            {
                // Halftrack: the silence is held back, and stops the render only once it has
                // lasted; a tail that sounds again (a delay's next echo) writes it first.
                if (numInRange > 0)
                    if (writeAudioBlock (destView.getFrameRange ({ blockOffset, blockOffset + numInRange })) == WriteResult::failed)
                        return true;

                const auto quiet = (int) (blockSize - numInRange);
                heldSilence.setSize (numOutputChans, juce::jmax (heldSilence.getNumSamples(), heldSilenceSamples + quiet), true, false, true);

                for (int ch = 0; ch < numOutputChans; ++ch)
                    heldSilence.copyFrom (ch, heldSilenceSamples, renderingBuffer, ch, (int) (blockOffset + numInRange), quiet);

                heldSilenceSamples += quiet;
                samplesPastRangeStart += blockSize;

                if (heldSilenceSamples >= tracktion::toSamples (r.silenceBeforeStopping, r.sampleRateForAudio))
                    return true;

                blockSize = 0;
            }
            else if (silent)
            {
                if (numInRange > 0)
                    if (writeAudioBlock (destView.getFrameRange ({ blockOffset, blockOffset + numInRange })) == WriteResult::failed)
                        return true;

                return true;
            }
            else
            {
                if (heldSilenceSamples > 0)
                {
                    auto held = choc::buffer::createChannelArrayView (heldSilence.getArrayOfWritePointers(),
                                                                      (choc::buffer::ChannelCount) numOutputChans,
                                                                      (choc::buffer::FrameCount) heldSilenceSamples);
                    heldSilenceSamples = 0;

                    if (writeAudioBlock (held) == WriteResult::failed)
                        return true;
                }

                samplesPastRangeStart += blockSize;
            }
        }
]=])
    set(before2_2 [=[    else if (streamTime > r.time.getEnd()
             && renderingBuffer.getMagnitude (0, r.blockSizeForAudio) <= thresholdForStopping)]=])
    set(after2_2 [=[    else if (r.silenceBeforeStopping <= 0_td // Halftrack: otherwise the held silence decides.
             && streamTime > r.time.getEnd()
             && renderingBuffer.getMagnitude (0, r.blockSizeForAudio) <= thresholdForStopping)]=])
    set(source3 "${source_dir}/modules/tracktion_engine/model/tracks/tracktion_AudioTrack.cpp")
    set(original3 "b70f5b63b7c58ca0da78e6de1b49b540bbb9172ffc0b8af9f2c21e11313b2bf8")
    set(patched3 "cf1765d05ea1a9484450cce0045b77f1be3f26ecc816619ab675fc2e273022ea")
    set(count3 1)
    set(before3_0 [=[    r.endAllowance = RenderOptions::findEndAllowance (edit, &trackIDs, nullptr);
]=])
    set(after3_0 [=[    r.endAllowance = RenderOptions::findEndAllowance (edit, &trackIDs, nullptr);
    // Halftrack: a freeze keeps a tail through silences of up to three seconds (a delay's
    // echoes), as the reference renders until the sound has died away.
    r.silenceBeforeStopping = TimeDuration::fromSeconds (3.0);
]=])
    foreach(index RANGE 0 3)
        file(SHA256 "${source${index}}" hash)
        if(hash STREQUAL "${patched${index}}" OR (DEFINED downstream${index} AND hash IN_LIST downstream${index}))
            set(write${index} FALSE)
            continue()
        endif()
        if(NOT hash STREQUAL "${original${index}}")
            message(FATAL_ERROR "Halftrack freeze tails patch requires pinned source ${source${index}}; unexpected SHA256 ${hash}.")
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
            message(FATAL_ERROR "Halftrack freeze tails patch produced unexpected SHA256 ${hash} for ${source${index}}.")
        endif()
        set(write${index} TRUE)
    endforeach()
    foreach(index RANGE 0 3)
        if(write${index})
            string(REGEX REPLACE "\n$" "" configure_content "${content${index}}")
            file(CONFIGURE OUTPUT "${source${index}}" CONTENT "@configure_content@" @ONLY NEWLINE_STYLE LF)
            file(SHA256 "${source${index}}" hash)
            if(NOT hash STREQUAL "${patched${index}}")
                message(FATAL_ERROR "Halftrack freeze tails patch wrote unexpected SHA256 ${hash}.")
            endif()
        endif()
    endforeach()
endfunction()
