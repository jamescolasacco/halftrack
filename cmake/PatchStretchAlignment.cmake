# Halftrack time-stretch alignment for pinned Tracktion Engine.
# Priming discards ratio-scaled input latency plus output latency, re-primes once the
# first real playback ratio is known, and carries fractional Signalsmith input frames.
# Retains all upstream source notices. Validates every file before writing any.
function(halftrack_patch_stretch_alignment source_dir)
    set(source0 "${source_dir}/modules/tracktion_engine/timestretch/tracktion_TimeStretch.h")
    set(original0 "8714b1c76395b671f77cf8d91e3f8686bdc8ed3d1b1f38faf20e9f7c992328ee")
    set(patched0 "823e52adaf06223d36c886c754c0b464f9d0f93ba28d9bbb9d379ac67b345a30")
    set(count0 1)
    set(before0_0 [=[        This can be used to report latency to the audio graph for compensation.
    */
    int getLatencySamples() const;
]=])
    set(after0_0 [=[        This can be used to report latency to the audio graph for compensation.
    */
    int getLatencySamples() const;

    /** Halftrack: the part of getLatencySamples() measured in output frames. The
        remainder is measured in input frames and scales with the stretch ratio. */
    int getOutputLatencySamples() const;
]=])
    set(source1 "${source_dir}/modules/tracktion_engine/timestretch/tracktion_TimeStretch.cpp")
    set(original1 "b2a3ca555d7817dc6e3faf0c5e4daf28adf472288e22010ebf2ecace01e06aa9")
    set(patched1 "51775bb94b0c46021476489a9d4bbbe4a004c029f1652dec53407d09e0f1c530")
    set(count1 7)
    set(before1_0 [=[    virtual int getLatencySamples() const { return 0; }
};]=])
    set(after1_0 [=[    virtual int getLatencySamples() const { return 0; }
    virtual int getOutputLatencySamples() const { return 0; }
};]=])
    set(before1_1 [=[        stretch.reset();
        hasDoneFinalBlock = false;]=])
    set(after1_1 [=[        stretch.reset();
        hasDoneFinalBlock = false;
        inputCarry = 0;]=])
    set(before1_2 [=[    int getFramesNeeded() const override
    {
        return juce::roundToInt (samplesPerOutputBuffer / speedRatio);
    }

    int getMaxFramesNeeded() const override
    {
        return stretch.inputLatency() + stretch.outputLatency();
    }]=])
    set(after1_2 [=[    int getFramesNeeded() const override
    {
        // Halftrack: carry the fractional input frame so the long-run ratio is exact.
        return static_cast<int> (std::floor (inputCarry + samplesPerOutputBuffer / static_cast<double> (speedRatio)));
    }

    int getMaxFramesNeeded() const override
    {
        return stretch.inputLatency() + stretch.outputLatency();
    }]=])
    set(before1_3 [=[        stretch.process (inChannels, numSamples, outChannels, samplesPerOutputBuffer);
        return samplesPerOutputBuffer;]=])
    set(after1_3 [=[        stretch.process (inChannels, numSamples, outChannels, samplesPerOutputBuffer);
        inputCarry += samplesPerOutputBuffer / static_cast<double> (speedRatio) - numSamples;
        return samplesPerOutputBuffer;]=])
    set(before1_4 [=[    int getLatencySamples() const override
    {
        return stretch.inputLatency() + stretch.outputLatency();
    }
]=])
    set(after1_4 [=[    int getLatencySamples() const override
    {
        return stretch.inputLatency() + stretch.outputLatency();
    }

    int getOutputLatencySamples() const override
    {
        return stretch.outputLatency();
    }
]=])
    set(before1_5 [=[    float speedRatio = 1.0f;
    bool hasDoneFinalBlock = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SignalsmithStretcher)]=])
    set(after1_5 [=[    float speedRatio = 1.0f;
    bool hasDoneFinalBlock = false;
    double inputCarry = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SignalsmithStretcher)]=])
    set(before1_6 [=[int TimeStretcher::getLatencySamples() const
{
    return stretcher ? stretcher->getLatencySamples() : 0;
}]=])
    set(after1_6 [=[int TimeStretcher::getLatencySamples() const
{
    return stretcher ? stretcher->getLatencySamples() : 0;
}

int TimeStretcher::getOutputLatencySamples() const
{
    return stretcher ? stretcher->getOutputLatencySamples() : 0;
}]=])
    set(source2 "${source_dir}/modules/tracktion_engine/timestretch/tracktion_ReadAheadTimeStretcher.h")
    set(original2 "4c3774f61fa82ffd7b716790802959b0ff6177e564034714db86d759a8fcd43b")
    set(patched2 "9bba658a860b056e7f5a5d5d94d230662884711f7504b7f42dfaeee121714ffb")
    set(count2 1)
    set(before2_0 [=[    /** Returns the latency in samples introduced by the stretching algorithm. */
    int getLatencySamples() const;]=])
    set(after2_0 [=[    /** Returns the latency in samples introduced by the stretching algorithm. */
    int getLatencySamples() const;

    /** Halftrack: the output-frame part of getLatencySamples(). */
    int getOutputLatencySamples() const;]=])
    set(source3 "${source_dir}/modules/tracktion_engine/timestretch/tracktion_ReadAheadTimeStretcher.cpp")
    set(original3 "bd6e380779ea12bae22351081adbbd9ad6bbc134b3088c5e001e1de8038eddb2")
    set(patched3 "32525b4261b040e35ce4ed8644af8b710ca00c3509d91b5a4944b5f3824f09b9")
    set(count3 1)
    set(before3_0 [=[int ReadAheadTimeStretcher::getLatencySamples() const
{
    return stretcher.getLatencySamples();
}]=])
    set(after3_0 [=[int ReadAheadTimeStretcher::getLatencySamples() const
{
    return stretcher.getLatencySamples();
}

int ReadAheadTimeStretcher::getOutputLatencySamples() const
{
    return stretcher.getOutputLatencySamples();
}]=])
    set(source4 "${source_dir}/modules/tracktion_engine/playback/graph/tracktion_WaveNode.cpp")
    set(original4 "a723324379bc424d352772f91f538f6a457a9559caa34fd80b9699484e3f7fc2")
    set(patched4 "2f2a0038be5991510a1d0222f166d831fa9865dfe6f9d42e4dbbe4e20c6b2cd0")
    # The later warp-timing patch builds on this WaveNode result.
    set(downstream4 "04ccc34aa76ca5f81534007a7d68da4e595b6680b323879d7ffe6b69d947b269")
    set(count4 7)
    set(before4_0 [=[        latencySamples = timeStretcher.getLatencySamples();]=])
    set(after4_0 [=[        latencySamples = timeStretcher.getLatencySamples();
        outputLatencySamples = timeStretcher.getOutputLatencySamples();]=])
    set(before4_1 [=[        const auto numToDiscard = static_cast<int> (position - backupPos) + latencySamples;]=])
    set(after4_1 [=[        // Halftrack: the backed-up frames and input-side latency are input frames, so they
        // scale with the playback ratio; the output-side latency is already in output frames.
        const auto outputLatency = std::clamp (outputLatencySamples, 0, latencySamples);
        const auto numToDiscard = static_cast<int> (std::lround (static_cast<double> (position - backupPos + latencySamples - outputLatency) / playbackSpeedRatio)) + outputLatency;
        awaitingFirstRead = true;]=])
    set(before4_2 [=[        playbackSpeedRatio = speedRatio;
        setSpeedAndPitch (playbackSpeedRatio, semitonesShift);
    }]=])
    set(after4_2 [=[        playbackSpeedRatio = speedRatio;
        setSpeedAndPitch (playbackSpeedRatio, semitonesShift);

        // Halftrack: construction primes at the default ratio. Re-prime once the real ratio
        // arrives, before any output is read, so pre-synthesised output keeps its timing.
        if (awaitingFirstRead)
        {
            source->setPosition (getReadPosition());
            timeStretcher.reset();
            setSpeedAndPitch (playbackSpeedRatio, semitonesShift);
            clearStretchBuffers();
            primeStretcher();
        }
    }]=])
    set(before4_3 [=[        const auto numFramesToDo = destBuffer.getNumFrames();

        for (;;)]=])
    set(after4_3 [=[        const auto numFramesToDo = destBuffer.getNumFrames();
        awaitingFirstRead = false;

        for (;;)]=])
    set(before4_4 [=[        const auto numFramesToDo = static_cast<int> (destBuffer.getNumFrames());
        int startFrame = 0;]=])
    set(after4_4 [=[        const auto numFramesToDo = static_cast<int> (destBuffer.getNumFrames());
        int startFrame = 0;
        awaitingFirstRead = false;]=])
    set(before4_5 [=[    static constexpr int chunkSize = 256;
    const int numChannels;
    int latencySamples = 0;]=])
    set(after4_5 [=[    void clearStretchBuffers() { inputFifo.reset(); outputFifo.reset(); }
    static constexpr int chunkSize = 256;
    const int numChannels;
    int latencySamples = 0, outputLatencySamples = 0;
    bool awaitingFirstRead = true;]=])
    set(before4_6 [=[    const int numChannels, chunkSize = 1024;
    int latencySamples = 0;]=])
    set(after4_6 [=[    void clearStretchBuffers() {}
    const int numChannels, chunkSize = 1024;
    int latencySamples = 0, outputLatencySamples = 0;
    bool awaitingFirstRead = true;]=])
    foreach(index RANGE 0 4)
        file(SHA256 "${source${index}}" hash)
        if(hash STREQUAL "${patched${index}}" OR (DEFINED downstream${index} AND hash STREQUAL "${downstream${index}}"))
            set(write${index} FALSE)
            continue()
        endif()
        if(NOT hash STREQUAL "${original${index}}")
            message(FATAL_ERROR "Halftrack stretch alignment patch requires pinned source ${source${index}}; unexpected SHA256 ${hash}.")
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
            message(FATAL_ERROR "Halftrack stretch alignment patch produced unexpected SHA256 ${hash} for ${source${index}}.")
        endif()
        set(write${index} TRUE)
    endforeach()
    foreach(index RANGE 0 4)
        if(write${index})
            string(REGEX REPLACE "\n$" "" configure_content "${content${index}}")
            file(CONFIGURE OUTPUT "${source${index}}" CONTENT "@configure_content@" @ONLY NEWLINE_STYLE LF)
            file(SHA256 "${source${index}}" hash)
            if(NOT hash STREQUAL "${patched${index}}")
                message(FATAL_ERROR "Halftrack stretch alignment patch wrote unexpected SHA256 ${hash}.")
            endif()
        endif()
    endforeach()
endfunction()
