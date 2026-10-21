# Halftrack clip envelopes for pinned Tracktion Engine, two fixes to automatable parameters.
# A curve of one point holds its value, but the engine streams only curves of two or more
# points, so once a clip envelope modulated the parameter the audio thread took the base from
# the manual value instead; the curve source now keeps a one-point curve's value for it.
# A clip curve's source on a parameter held a plain reference to the curve, so a plug-in kept
# for undo outlived a deleted clip's curve and read it later; the source now detaches when
# the curve is deleted.
# Retains all upstream source notices. Validates every file before writing any.
function(halftrack_patch_clip_envelopes source_dir)
    set(source0 "${source_dir}/modules/tracktion_engine/model/automation/tracktion_AutomatableParameter.cpp")
    set(original0 "bd3609d9b42fa1312e775c48488307639894a79528c1d84b1909c050d1ae9aad")
    set(patched0 "0f92ff4402fcbe24dae9477ce7910c7bcc8f2b6cb5229b47a2eab92226dbe2a7")
    set(count0 11)
    set(before0_0 [=[    AutomatableParameter& parameter;
    AutomationCurve curve;
]=])
    set(after0_0 [=[    AutomatableParameter& parameter;
    AutomationCurve curve;
    // Halftrack: a curve of one point has no stream but holds its value, which clip
    // envelopes then modulate on the audio thread.
    std::atomic<bool> holdsValue { false };
    std::atomic<float> heldValue { 0.0f };
]=])
    set(before0_1 [=[        {
            const juce::ScopedLock sl (parameterStreamLock);
            automationActive.store (newStream != nullptr, std::memory_order_relaxed);
            parameterStream = std::move (newStream);
]=])
    set(after0_1 [=[        // Halftrack: see holdsValue.
        heldValue.store (curve.getNumPoints() == 1 ? curve.getPointValue (0) : 0.0f, std::memory_order_relaxed);
        holdsValue.store (newStream == nullptr && curve.getNumPoints() == 1, std::memory_order_relaxed);

        {
            const juce::ScopedLock sl (parameterStreamLock);
            automationActive.store (newStream != nullptr, std::memory_order_relaxed);
            parameterStream = std::move (newStream);
]=])
    set(before0_2 [=[                             return currentParameterValue.load();
                         }();

    getAutomationSourceList()
        .visitSources ([&newBaseValue, &newModifierValue, time] (AutomationSource& m) mutable]=])
    set(after0_2 [=[                             if (curveSource->holdsValue.load (std::memory_order_relaxed)
                                 && curveSource->isEnabledAt (time) && ! isCurrentlyRecording())
                                 return curveSource->heldValue.load (std::memory_order_relaxed);

                             return currentParameterValue.load();
                         }();

    getAutomationSourceList()
        .visitSources ([&newBaseValue, &newModifierValue, time] (AutomationSource& m) mutable]=])
    set(before0_3 [=[    void triggerAsyncIteratorUpdate()
    {
        if (! curveModifier.edit.isLoading())
            deferredUpdateTimer.startTimer (10);
    }]=])
    set(after0_3 [=[    void triggerAsyncIteratorUpdate()
    {
        if (! modifierDeleted.load (std::memory_order_acquire) && ! curveModifier.edit.isLoading())
            deferredUpdateTimer.startTimer (10);
    }]=])
    set(before0_4 [=[    bool isEnabledAt (TimePosition t) override
    {
        auto& ts = getTempoSequence (curveModifier);]=])
    set(after0_4 [=[    bool isEnabledAt (TimePosition t) override
    {
        if (modifierDeleted.load (std::memory_order_acquire))
            return false;

        auto& ts = getTempoSequence (curveModifier);]=])
    set(before0_5 [=[    void setPosition (TimePosition editTime) override
    {
        if (lastTime.exchange (editTime) == editTime)
            return;

        bool anyEnabled = false;]=])
    set(after0_5 [=[    void setPosition (TimePosition editTime) override
    {
        if (modifierDeleted.load (std::memory_order_acquire))
            return;

        if (lastTime.exchange (editTime) == editTime)
            return;

        bool anyEnabled = false;]=])
    set(before0_6 [=[    void processValueAt (TimePosition t, float& baseValue, float& modValue) override
    {
        auto values = getValuesAtEditPosition (curveModifier, parameter, t);]=])
    set(after0_6 [=[    void processValueAt (TimePosition t, float& baseValue, float& modValue) override
    {
        if (modifierDeleted.load (std::memory_order_acquire))
            return;

        auto values = getValuesAtEditPosition (curveModifier, parameter, t);]=])
    set(before0_7 [=[    LambdaTimer deferredUpdateTimer;
    std::atomic<bool> enabledAtCurrentStreamTime { false };]=])
    set(after0_7 [=[    LambdaTimer deferredUpdateTimer;
    std::atomic<bool> enabledAtCurrentStreamTime { false };
    std::atomic<bool> modifierDeleted { false };]=])
    set(before0_8 [=[        void processValue (float& baseValue, float& modValue)
        {
            if (isActive())]=])
    set(after0_8 [=[        void detach()
        {
            const juce::ScopedLock sl (parameterStreamLock);
            automationActive.store (false, std::memory_order_relaxed);
            parameterStream.reset();
        }

        void processValue (float& baseValue, float& modValue)
        {
            if (isActive())]=])
    set(before0_9 [=[    void updateIterator()
    {
        jassert (! curveModifier.edit.isLoading());]=])
    set(after0_9 [=[    void updateIterator()
    {
        if (modifierDeleted.load (std::memory_order_acquire))
            return;

        jassert (! curveModifier.edit.isLoading());]=])
    set(before0_10 [=[    void selectableObjectAboutToBeDeleted (Selectable*) override
    {}

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutomationCurveModifierSource)]=])
    set(after0_10 [=[    // Halftrack: a cached plug-in's parameter can outlive a clip's curve modifier, as when undo
    // removes a duplicated track, so detach rather than read the deleted modifier later.
    void selectableObjectAboutToBeDeleted (Selectable*) override
    {
        modifierDeleted.store (true, std::memory_order_release);
        deferredUpdateTimer.stopTimer();
        enabledAtCurrentStreamTime.store (false, std::memory_order_release);

        for (auto& curve : curves)
            curve.detach();
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutomationCurveModifierSource)]=])
    foreach(index RANGE 0 0)
        file(SHA256 "${source${index}}" hash)
        if(hash STREQUAL "${patched${index}}")
            set(write${index} FALSE)
            continue()
        endif()
        if(NOT hash STREQUAL "${original${index}}")
            message(FATAL_ERROR "Halftrack clip envelope patch requires pinned source ${source${index}}; unexpected SHA256 ${hash}.")
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
            message(FATAL_ERROR "Halftrack clip envelope patch produced unexpected SHA256 ${hash} for ${source${index}}.")
        endif()
        set(write${index} TRUE)
    endforeach()
    foreach(index RANGE 0 0)
        if(write${index})
            string(REGEX REPLACE "\n$" "" configure_content "${content${index}}")
            file(CONFIGURE OUTPUT "${source${index}}" CONTENT "@configure_content@" @ONLY NEWLINE_STYLE LF)
            file(SHA256 "${source${index}}" hash)
            if(NOT hash STREQUAL "${patched${index}}")
                message(FATAL_ERROR "Halftrack clip envelope patch wrote unexpected SHA256 ${hash}.")
            endif()
        endif()
    endforeach()
endfunction()
