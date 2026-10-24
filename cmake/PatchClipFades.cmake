# Halftrack default clip fades and crossfades for pinned Tracktion Engine, after every earlier
# patch. The reference's audio clips fade in and out over 4 ms at their edges by default, with the
# smoothstep 3x^2 - 2x^3, and two touching clips with default fades crossfade: the later one starts
# early, playing the material before its start, and fades in over the end of the earlier one. The
# fade curves gain that smoothstep; the engine behaviour gains two hooks the host answers when a
# clip's node is built, its derived fades and how long before its start it plays; and the node
# builder uses them, reading each kind of clip's source from that much earlier and running the
# clip's node from then. Retains all
# upstream source notices. Validates every file before writing any.
function(halftrack_patch_clip_fades source_dir)
    set(source0 "${source_dir}/modules/tracktion_engine/utilities/tracktion_AudioFadeCurve.h")
    set(original0 "a0c004dd6f3196d3df4808f891878b8816df98269d065bbecd8e6e16eba6a522")
    set(patched0 "a66bb52e2ca925acac9168aaed6f14eb52d7ff3e0058b9e660fa79cb20e09dbd")
    set(count0 4)
    set(before0_0 [=[    /** An S-shaped curve. */
    struct SCurve
    {
        inline static double preadjust (double alpha) noexcept  { return alpha; }
        inline static float get (float alpha) noexcept          { return (1.0f - alpha) * (1.0f - std::cos (alpha * (0.5f * juce::MathConstants<float>::pi)))
                                                                            + alpha * std::sin (alpha * (0.5f * juce::MathConstants<float>::pi)); }
    };
]=])
    set(after0_0 [=[    /** An S-shaped curve. */
    struct SCurve
    {
        inline static double preadjust (double alpha) noexcept  { return alpha; }
        inline static float get (float alpha) noexcept          { return (1.0f - alpha) * (1.0f - std::cos (alpha * (0.5f * juce::MathConstants<float>::pi)))
                                                                            + alpha * std::sin (alpha * (0.5f * juce::MathConstants<float>::pi)); }
    };

    /** Halftrack: the smoothstep 3x^2 - 2x^3, the shape of the reference's default fades. */
    struct Smoothstep
    {
        inline static double preadjust (double alpha) noexcept  { return alpha; }
        inline static float get (float alpha) noexcept          { return alpha * alpha * (3.0f - 2.0f * alpha); }
    };
]=])
    set(before0_1 [=[        sCurve  = 4
    };]=])
    set(after0_1 [=[        sCurve  = 4,
        smoothstep = 5 // Halftrack
    };]=])
    set(before0_2 [=[        if (type == sCurve)    return alphaToGain<SCurve>  (alpha);
]=])
    set(after0_2 [=[        if (type == sCurve)    return alphaToGain<SCurve>  (alpha);
        if (type == smoothstep) return alphaToGain<Smoothstep> (alpha);
]=])
    set(before0_3 [=[            case sCurve:    renderBlock<SCurve>  (dest, numSamples, startAlpha, endAlpha); break;
]=])
    set(after0_3 [=[            case sCurve:    renderBlock<SCurve>  (dest, numSamples, startAlpha, endAlpha); break;
            case smoothstep: renderBlock<Smoothstep> (dest, numSamples, startAlpha, endAlpha); break;
]=])
    set(source1 "${source_dir}/modules/tracktion_engine/utilities/tracktion_EngineBehaviour.h")
    set(original1 "546f1bc2652ba56474e93efabe4f054f8c699cebb2fb446b341a6e176af339e3")
    set(patched1 "d7306d85635ac5cc07de62145c249a8a5ebbea87ef43002918493ba223d25e95")
    set(count1 1)
    set(before1_0 [=[    virtual bool autoAddClipEdgeFades()                                             { return false; }
]=])
    set(after1_0 [=[    virtual bool autoAddClipEdgeFades()                                             { return false; }

    /// Halftrack: lets the host give an arranger audio clip the fades it derives, such as default
    /// edge fades and crossfades, as lengths and AudioFadeCurve types, when its node is built.
    virtual void adjustAudioClipFades (AudioClipBase&, TimeDuration& /*fadeIn*/, TimeDuration& /*fadeOut*/,
                                       int& /*fadeInType*/, int& /*fadeOutType*/)    {}

    /// Halftrack: how long before its start an arranger audio clip plays, from the material before
    /// its start, so that it fades in over the end of the clip before it.
    virtual TimeDuration getAudioClipPreRoll (AudioClipBase&)                       { return {}; }
]=])
    set(source2 "${source_dir}/modules/tracktion_engine/playback/graph/tracktion_EditNodeBuilder.cpp")
    set(original2 "1a07134c8c4c2a229b4da262dc6b547d3ab1302496fd5e5fbf1d375e9614517a")
    set(patched2 "9e1afcb494391df977c7f7950ce8030410576a2fab891e757d30a0249f87d60c")
    set(count2 10)
    set(before2_0 [=[    auto fIn = clip.getFadeIn();
    auto fOut = clip.getFadeOut();

    if (fIn > 0_td || fOut > 0_td)
    {]=])
    set(after2_0 [=[    auto fIn = clip.getFadeIn();
    auto fOut = clip.getFadeOut();
    // Halftrack: the host may derive the fades (default edge fades and crossfades).
    int fadeInType = (int) clip.getFadeInType(), fadeOutType = (int) clip.getFadeOutType();
    clip.edit.engine.getEngineBehaviour().adjustAudioClipFades (clip, fIn, fOut, fadeInType, fadeOutType);

    if (fIn > 0_td || fOut > 0_td)
    {]=])
    set(before2_1 [=[                                        clip.getFadeInType(), clip.getFadeOutType(),
                                        true);]=])
    set(after2_1 [=[                                        (AudioFadeCurve::Type) fadeInType, (AudioFadeCurve::Type) fadeOutType,
                                        true);]=])
    set(before2_2 [=[std::unique_ptr<tracktion::graph::Node> createNodeForAudioClip (AudioClipBase& clip, EditItemID idToUse, EditTimeRange clipTimeRangeToUse,
                                                                bool includeARA, const CreateNodeParams& params, ClipRole role)
{]=])
    set(after2_2 [=[std::unique_ptr<tracktion::graph::Node> createNodeForAudioClip (AudioClipBase& clip, EditItemID idToUse, EditTimeRange clipTimeRangeToUse,
                                                                bool includeARA, const CreateNodeParams& params, ClipRole role,
                                                                TimeDuration preRoll = {}) // Halftrack: the range starts this early.
{
    // Halftrack: the source is read from this much before the clip's start.
    const auto preRollBeats = clip.edit.tempoSequence.toBeats (clip.getPosition().getStart())
                                - clip.edit.tempoSequence.toBeats (clip.getPosition().getStart() - preRoll);]=])
    set(before2_3 [=[            nodeOffset = clip.getPosition().getOffset();
            loopRange = clip.getLoopRange();]=])
    set(after2_3 [=[            nodeOffset = clip.getPosition().getOffset() - preRoll;
            loopRange = clip.getLoopRange();]=])
    set(before2_4 [=[                                                   toBeats (clipTimeRangeToUse, clip.edit.tempoSequence),
                                                   clip.getOffsetInBeats(),]=])
    set(after2_4 [=[                                                   toBeats (clipTimeRangeToUse, clip.edit.tempoSequence),
                                                   clip.getOffsetInBeats() - preRollBeats,]=])
    set(before2_5 [=[                                               toTime (clipTimeRangeToUse, clip.edit.tempoSequence),
                                               clip.getPosition().getOffset(),
                                               clip.getLoopRange(),]=])
    set(after2_5 [=[                                               toTime (clipTimeRangeToUse, clip.edit.tempoSequence),
                                               clip.getPosition().getOffset() - preRoll,
                                               clip.getLoopRange(),]=])
    set(before2_6 [=[std::unique_ptr<tracktion::graph::Node> createNodeForAudioClip (AudioClipBase& clip, bool includeARA,
                                                                const CreateNodeParams& params, ClipRole role)
{
    if (clip.canUseProxy())
    {
        assert (role == ClipRole::arranger);
        return createNodeForAudioClip (clip, clip.itemID, clip.getEditTimeRange(), includeARA, params, role);
    }

    if (clip.getAutoTempo() || clip.getAutoPitch() || role == ClipRole::launcher)
        return createNodeForAudioClip (clip, clip.itemID, clip.getEditBeatRange(), includeARA, params, role);

    assert (role == ClipRole::arranger);
    return createNodeForAudioClip (clip, clip.itemID, clip.getEditTimeRange(), includeARA, params, role);
}]=])
    set(after2_6 [=[std::unique_ptr<tracktion::graph::Node> createNodeForAudioClip (AudioClipBase& clip, bool includeARA,
                                                                const CreateNodeParams& params, ClipRole role)
{
    // Halftrack: an arranger clip can start early, playing the material before its start, to
    // crossfade with the clip before it. A time-stretched proxy holds nothing before the start.
    auto preRoll = role == ClipRole::arranger ? clip.edit.engine.getEngineBehaviour().getAudioClipPreRoll (clip) : TimeDuration();
    if (clip.canUseProxy() && clip.usesTimeStretchedProxy())
        preRoll = {};
    const auto start = clip.getPosition().getStart();
    const auto earlier = TimeRange (start - preRoll, clip.getPosition().getEnd());

    if (clip.canUseProxy())
    {
        assert (role == ClipRole::arranger);
        return createNodeForAudioClip (clip, clip.itemID, preRoll > 0_td ? EditTimeRange (earlier) : clip.getEditTimeRange(), includeARA, params, role, preRoll);
    }

    if (clip.getAutoTempo() || clip.getAutoPitch() || role == ClipRole::launcher)
    {
        if (preRoll > 0_td)
            return createNodeForAudioClip (clip, clip.itemID, EditTimeRange (BeatRange (clip.edit.tempoSequence.toBeats (start - preRoll), clip.getEditBeatRange().getEnd())),
                                           includeARA, params, role, preRoll);

        return createNodeForAudioClip (clip, clip.itemID, clip.getEditBeatRange(), includeARA, params, role);
    }

    assert (role == ClipRole::arranger);
    return createNodeForAudioClip (clip, clip.itemID, preRoll > 0_td ? EditTimeRange (earlier) : clip.getEditTimeRange(), includeARA, params, role, preRoll);
}]=])
    set(before2_7 [=[//==============================================================================
//==============================================================================
std::unique_ptr<tracktion::graph::Node> createFadeNodeForClip (AudioClipBase& clip, EditTimeRange clipTimeRangeToUse,]=])
    set(after2_7 [=[//==============================================================================
// Halftrack: how long before its start a clip's node plays (see createNodeForAudioClip).
static TimeDuration getClipPreRoll (Clip& clip)
{
    if (auto acb = dynamic_cast<AudioClipBase*> (&clip))
        if (! (acb->canUseProxy() && acb->usesTimeStretchedProxy()))
            return clip.edit.engine.getEngineBehaviour().getAudioClipPreRoll (*acb);

    return {};
}

//==============================================================================
std::unique_ptr<tracktion::graph::Node> createFadeNodeForClip (AudioClipBase& clip, EditTimeRange clipTimeRangeToUse,]=])
    set(before2_8 [=[                timeRange = timeRange.withStart (timeRange.getStart() - clip->getHead())
                                     .withEnd (timeRange.getEnd() + clip->getTail());
                combiner->addInput (std::move (clipNode), timeRange);]=])
    set(after2_8 [=[                timeRange = timeRange.withStart (timeRange.getStart() - clip->getHead() - getClipPreRoll (*clip))
                                     .withEnd (timeRange.getEnd() + clip->getTail());
                combiner->addInput (std::move (clipNode), timeRange);]=])
    set(before2_9 [=[                timeRange = timeRange.withStart (timeRange.getStart() - clip->getHead())
                                     .withEnd (timeRange.getEnd() + clip->getTail());
                clipEntries.push_back ({ std::move (clipNode), timeRange });]=])
    set(after2_9 [=[                timeRange = timeRange.withStart (timeRange.getStart() - clip->getHead() - getClipPreRoll (*clip))
                                     .withEnd (timeRange.getEnd() + clip->getTail());
                clipEntries.push_back ({ std::move (clipNode), timeRange });]=])
    # The later cue patch builds on the engine behaviour and the node builder.
    set(downstream1 "296e079a8c70e5d13fd8aec6c640daa56c325f1cf33e836e69d2b3037c14ecc6")
    set(downstream2 "bbc12a579f50896a7be4a5c13127b63c47863b22986b776290a7d523f80866cf" "05e73d01d1cc0a824f96a7dad772a850e5906e3a645bf56affd072f91d986bcc")
    foreach(index RANGE 0 2)
        file(SHA256 "${source${index}}" hash)
        if(hash STREQUAL "${patched${index}}" OR (DEFINED downstream${index} AND hash IN_LIST downstream${index}))
            set(write${index} FALSE)
            continue()
        endif()
        if(NOT hash STREQUAL "${original${index}}")
            message(FATAL_ERROR "Halftrack clip fades patch requires pinned source ${source${index}}; unexpected SHA256 ${hash}.")
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
            message(FATAL_ERROR "Halftrack clip fades patch produced unexpected SHA256 ${hash} for ${source${index}}.")
        endif()
        set(write${index} TRUE)
    endforeach()
    foreach(index RANGE 0 2)
        if(write${index})
            string(REGEX REPLACE "\n$" "" configure_content "${content${index}}")
            file(CONFIGURE OUTPUT "${source${index}}" CONTENT "@configure_content@" @ONLY NEWLINE_STYLE LF)
            file(SHA256 "${source${index}}" hash)
            if(NOT hash STREQUAL "${patched${index}}")
                message(FATAL_ERROR "Halftrack clip fades patch wrote unexpected SHA256 ${hash}.")
            endif()
        endif()
    endforeach()
endfunction()
