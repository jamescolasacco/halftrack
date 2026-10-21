# Halftrack warp modes for pinned Tracktion Engine, after the stretch alignment patch.
# Adds Beats, Tones, Texture and Complex Pro modes, whose stretchers Halftrack supplies through a
# factory the time stretcher calls for them, and four mode settings carried in the stretch options
# (saved as a version 2 options string only when set). The stretcher interface becomes public for
# those stretchers to implement.
# Retains all upstream source notices. Validates every file before writing any.
function(halftrack_patch_warp_modes source_dir)
    set(source0 "${source_dir}/modules/tracktion_engine/timestretch/tracktion_TimeStretch.h")
    set(original0 "823e52adaf06223d36c886c754c0b464f9d0f93ba28d9bbb9d379ac67b345a30")
    set(patched0 "27e50e271e9b07c9f30c50951d387b462dbdb0684bfb50c46d81e454ff4dc3f2")
    set(count0 3)
    set(before0_0 [=[        signalsmithCheaper = 16,        /**< Signalsmith Stretch cheaper/faster preset. */
]=])
    set(after0_0 [=[        signalsmithCheaper = 16,        /**< Signalsmith Stretch cheaper/faster preset. */
        // Halftrack: warp modes whose stretchers Halftrack supplies through createHalftrackStretcher.
        halftrackBeats = 40,            /**< Segments at transients, each at its own speed. */
        halftrackTones = 41,            /**< Signalsmith Stretch at a chosen grain size. */
        halftrackTexture = 42,          /**< Granular stretching. */
        halftrackComplexPro = 43,       /**< Signalsmith Stretch keeping formants. */
]=])
    set(before0_1 [=[        int envelopeOrder = 64;             /**< Sets a spectral envelope shift factor. */
]=])
    set(after0_1 [=[        int envelopeOrder = 64;             /**< Sets a spectral envelope shift factor. */
        std::array<float, 4> halftrack {};  /**< Halftrack: a Halftrack warp mode's settings. */
]=])
    set(before0_2 [=[    /** @internal */
    struct Stretcher;
]=])
    set(after0_2 [=[    /** Halftrack: the interface stretchers implement, public so Halftrack can supply its own. */
    struct Stretcher
    {
        virtual ~Stretcher() {}

        virtual bool isOk() const = 0;
        virtual void reset() = 0;
        virtual bool setSpeedAndPitch (float speedRatio, float semitonesUp) = 0;
        virtual int getFramesNeeded() const = 0;
        virtual int getMaxFramesNeeded() const = 0;
        virtual int processData (const float* const* inChannels, int numSamples, float* const* outChannels) = 0;
        virtual int flush (float* const* outChannels) = 0;
        virtual int getLatencySamples() const { return 0; }
        virtual int getOutputLatencySamples() const { return 0; }
    };

    /** Halftrack: creates the stretcher for one of the Halftrack modes, or returns nullptr. */
    static inline std::function<std::unique_ptr<Stretcher> (double sourceSampleRate, int samplesPerBlock, int numChannels,
                                                            Mode, const ElastiqueProOptions&, bool realtime)> createHalftrackStretcher;
]=])
    set(source1 "${source_dir}/modules/tracktion_engine/timestretch/tracktion_TimeStretch.cpp")
    set(original1 "51775bb94b0c46021476489a9d4bbbe4a004c029f1652dec53407d09e0f1c530")
    set(patched1 "9058c56542eacba92ee5890eb63ffdb059faac60baf7e6b7998101ff30e8b9f4")
    set(count1 5)
    set(before1_0 [=[struct TimeStretcher::Stretcher
{
    virtual ~Stretcher() {}

    virtual bool isOk() const = 0;
    virtual void reset() = 0;
    virtual bool setSpeedAndPitch (float speedRatio, float semitonesUp) = 0;
    virtual int getFramesNeeded() const = 0;
    virtual int getMaxFramesNeeded() const = 0;
    virtual int processData (const float* const* inChannels, int numSamples, float* const* outChannels) = 0;
    virtual int flush (float* const* outChannels) = 0;
    virtual int getLatencySamples() const { return 0; }
    virtual int getOutputLatencySamples() const { return 0; }
};
]=])
    set(after1_0 [=[// Halftrack: TimeStretcher::Stretcher is declared in the header.
]=])
    set(before1_1 [=[        if (tokens.size() == 5)
        {
            midSideStereo        = tokens[1].getIntValue() != 0;
            syncTimeStrPitchShft = tokens[2].getIntValue() != 0;
            preserveFormants     = tokens[3].getIntValue() != 0;
            envelopeOrder        = tokens[4].getIntValue();
            return;]=])
    set(after1_1 [=[        if (tokens.size() == 5)
        {
            midSideStereo        = tokens[1].getIntValue() != 0;
            syncTimeStrPitchShft = tokens[2].getIntValue() != 0;
            preserveFormants     = tokens[3].getIntValue() != 0;
            envelopeOrder        = tokens[4].getIntValue();
            return;
        }
    }

    // Halftrack: version 2 adds a Halftrack warp mode's four settings.
    if (string.startsWith ("2/"))
    {
        juce::StringArray tokens;
        tokens.addTokens (string, "/", {});

        if (tokens.size() == 9)
        {
            midSideStereo        = tokens[1].getIntValue() != 0;
            syncTimeStrPitchShft = tokens[2].getIntValue() != 0;
            preserveFormants     = tokens[3].getIntValue() != 0;
            envelopeOrder        = tokens[4].getIntValue();

            for (size_t i = 0; i < halftrack.size(); ++i)
                halftrack[i] = tokens[(int) i + 5].getFloatValue();

            return;]=])
    set(before1_2 [=[juce::String TimeStretcher::ElastiqueProOptions::toString() const
{
    // version / midside / sync / preserve / order
    return]=])
    set(after1_2 [=[juce::String TimeStretcher::ElastiqueProOptions::toString() const
{
    // Halftrack: version 2 when a Halftrack warp mode has settings, so other strings are unchanged.
    if (halftrack != std::array<float, 4> {})
        return juce::String::formatted ("2/%d/%d/%d/%d/", midSideStereo ? 1 : 0, syncTimeStrPitchShft ? 1 : 0, preserveFormants ? 1 : 0, envelopeOrder)
                + juce::String (halftrack[0]) + "/" + juce::String (halftrack[1]) + "/" + juce::String (halftrack[2]) + "/" + juce::String (halftrack[3]);

    // version / midside / sync / preserve / order
    return]=])
    set(before1_3 [=[        && preserveFormants     == other.preserveFormants
        && envelopeOrder        == other.envelopeOrder;]=])
    set(after1_3 [=[        && preserveFormants     == other.preserveFormants
        && envelopeOrder        == other.envelopeOrder
        && halftrack            == other.halftrack;]=])
    set(before1_4 [=[    CRASH_TRACER
    jassert (stretcher == nullptr);

   #if TRACKTION_ENABLE_TIMESTRETCH_ELASTIQUE || TRACKTION_ENABLE_TIMESTRETCH_RUBBERBAND || TRACKTION_ENABLE_TIMESTRETCH_SOUNDTOUCH || TRACKTION_ENABLE_TIMESTRETCH_SIGNALSMITH
    switch (mode)]=])
    set(after1_4 [=[    CRASH_TRACER
    jassert (stretcher == nullptr);

    // Halftrack: its own warp modes come from Halftrack's stretchers.
    if (mode >= halftrackBeats && mode <= halftrackComplexPro)
    {
        if (createHalftrackStretcher)
            stretcher = createHalftrackStretcher (sourceSampleRate, samplesPerBlock, numChannels, mode, options, realtime);

        if (stretcher != nullptr && ! stretcher->isOk())
            stretcher.reset();

        return;
    }

   #if TRACKTION_ENABLE_TIMESTRETCH_ELASTIQUE || TRACKTION_ENABLE_TIMESTRETCH_RUBBERBAND || TRACKTION_ENABLE_TIMESTRETCH_SOUNDTOUCH || TRACKTION_ENABLE_TIMESTRETCH_SIGNALSMITH
    switch (mode)]=])
    foreach(index RANGE 0 1)
        file(SHA256 "${source${index}}" hash)
        if(hash STREQUAL "${patched${index}}")
            set(write${index} FALSE)
            continue()
        endif()
        if(NOT hash STREQUAL "${original${index}}")
            message(FATAL_ERROR "Halftrack warp modes patch requires pinned source ${source${index}}; unexpected SHA256 ${hash}.")
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
            message(FATAL_ERROR "Halftrack warp modes patch produced unexpected SHA256 ${hash} for ${source${index}}.")
        endif()
        set(write${index} TRUE)
    endforeach()
    foreach(index RANGE 0 1)
        if(write${index})
            string(REGEX REPLACE "\n$" "" configure_content "${content${index}}")
            file(CONFIGURE OUTPUT "${source${index}}" CONTENT "@configure_content@" @ONLY NEWLINE_STYLE LF)
            file(SHA256 "${source${index}}" hash)
            if(NOT hash STREQUAL "${patched${index}}")
                message(FATAL_ERROR "Halftrack warp modes patch wrote unexpected SHA256 ${hash}.")
            endif()
        endif()
    endforeach()
endfunction()
