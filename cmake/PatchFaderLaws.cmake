# Halftrack fader laws for pinned Tracktion Engine, after every earlier patch. The host can install a
# volume fader law and a send fader law (a position from 0 to 1 to a gain and back), which the
# engine's volume-fader conversions and its aux sends then use; with none installed the engine's own
# law applies. Halftrack installs the reference's level scales (static reading of its code): volume
# from -70 to +6 dB and sends from -70 to 0 dB, silent at the bottom. Retains all upstream source
# notices. Validates every file before writing any.
function(halftrack_patch_fader_laws source_dir)
    set(source0 "${source_dir}/modules/tracktion_engine/utilities/tracktion_AudioUtilities.h")
    set(original0 "b772588adb0e179a9af328e89cbb11112e89b99f82a112a9cfe18e08a0b0dc05")
    set(patched0 "9e525d79f453f03b3ebc497c8a944bef2f7a3cd0673fc75c749b9581b5997c35")
    set(count0 1)
    set(before0_0 [=[//==============================================================================
float decibelsToVolumeFaderPosition (float dB) noexcept;
float volumeFaderPositionToDB (float position) noexcept;
float volumeFaderPositionToGain (float position) noexcept;
float gainToVolumeFaderPosition (float gain) noexcept;
]=])
    set(after0_0 [=[//==============================================================================
float decibelsToVolumeFaderPosition (float dB) noexcept;
float volumeFaderPositionToDB (float position) noexcept;
float volumeFaderPositionToGain (float position) noexcept;
float gainToVolumeFaderPosition (float gain) noexcept;

/** Halftrack: a fader law the host installs, mapping a fader position from 0 to 1 to a gain and
    back; a gain of 0 is silence, which reads as -100 dB. With none installed, the engine's own law
    applies. The volume law serves track, group and master volume; the send law serves aux sends.
    Install laws before any audio runs; they are read on the audio thread.
*/
struct FaderLaw
{
    float (*positionToGain) (float) = nullptr;
    float (*gainToPosition) (float) = nullptr;
};

void setVolumeFaderLaw (FaderLaw) noexcept;
void setSendFaderLaw (FaderLaw) noexcept;

/** Halftrack: an aux send's position and level, by the send law (or the volume law without one). */
float decibelsToSendFaderPosition (float dB) noexcept;
float sendFaderPositionToDB (float position) noexcept;
float sendFaderPositionToGain (float position) noexcept;
]=])
    set(source1 "${source_dir}/modules/tracktion_engine/utilities/tracktion_AudioUtilities.cpp")
    set(original1 "5073e77b9eccebfbca7d7eb3e151744969305929ee07d7c080a9000031563372")
    set(patched1 "a2f14631df98ccc833e221e57a6e8137ea3ad4282fe9ec18416ed842e22c8b9b")
    set(count1 1)
    set(before1_0 [=[static const float volScaleFactor = 20.0f;

float decibelsToVolumeFaderPosition (float db) noexcept
{
    return (db > -100.0f) ? std::exp ((db - 6.0f) * (1.0f / volScaleFactor)) : 0.0f;
}

float volumeFaderPositionToDB (float pos) noexcept
{
    return (pos > 0.0f) ? (volScaleFactor * std::log (pos)) + 6.0f : -100.0f;
}

float volumeFaderPositionToGain (float pos) noexcept
{
    return (pos > 0.0f) ? std::pow (10.0f, ((volScaleFactor * logf (pos)) + 6.0f) * (1.0f / 20.0f)) : 0.0f;
}

float gainToVolumeFaderPosition (float gain) noexcept
{
    return (gain > 0.0f) ? std::exp (((20.0f * std::log10 (gain)) - 6.0f) * (1.0f / volScaleFactor)) : 0.0f;
}]=])
    set(after1_0 [=[static const float volScaleFactor = 20.0f;

namespace
{
    // Halftrack: the installed laws, if any.
    std::atomic<float (*) (float)> volumeToGain { nullptr }, volumeFromGain { nullptr }, sendToGain { nullptr }, sendFromGain { nullptr };
}

void setVolumeFaderLaw (FaderLaw law) noexcept
{
    volumeToGain.store (law.positionToGain);
    volumeFromGain.store (law.gainToPosition);
}

void setSendFaderLaw (FaderLaw law) noexcept
{
    sendToGain.store (law.positionToGain);
    sendFromGain.store (law.gainToPosition);
}

float decibelsToVolumeFaderPosition (float db) noexcept
{
    if (auto fromGain = volumeFromGain.load (std::memory_order_relaxed))
        return db > -100.0f ? fromGain (dbToGain (db)) : 0.0f;

    return (db > -100.0f) ? std::exp ((db - 6.0f) * (1.0f / volScaleFactor)) : 0.0f;
}

float volumeFaderPositionToDB (float pos) noexcept
{
    if (auto toGain = volumeToGain.load (std::memory_order_relaxed))
        return gainToDb (toGain (pos));

    return (pos > 0.0f) ? (volScaleFactor * std::log (pos)) + 6.0f : -100.0f;
}

float volumeFaderPositionToGain (float pos) noexcept
{
    if (auto toGain = volumeToGain.load (std::memory_order_relaxed))
        return toGain (pos);

    return (pos > 0.0f) ? std::pow (10.0f, ((volScaleFactor * logf (pos)) + 6.0f) * (1.0f / 20.0f)) : 0.0f;
}

float gainToVolumeFaderPosition (float gain) noexcept
{
    if (auto fromGain = volumeFromGain.load (std::memory_order_relaxed))
        return gain > 0.0f ? fromGain (gain) : 0.0f;

    return (gain > 0.0f) ? std::exp (((20.0f * std::log10 (gain)) - 6.0f) * (1.0f / volScaleFactor)) : 0.0f;
}

float decibelsToSendFaderPosition (float db) noexcept
{
    if (auto fromGain = sendFromGain.load (std::memory_order_relaxed))
        return db > -100.0f ? fromGain (dbToGain (db)) : 0.0f;

    return decibelsToVolumeFaderPosition (db);
}

float sendFaderPositionToDB (float pos) noexcept
{
    if (auto toGain = sendToGain.load (std::memory_order_relaxed))
        return gainToDb (toGain (pos));

    return volumeFaderPositionToDB (pos);
}

float sendFaderPositionToGain (float pos) noexcept
{
    if (auto toGain = sendToGain.load (std::memory_order_relaxed))
        return toGain (pos);

    return volumeFaderPositionToGain (pos);
}]=])
    set(source2 "${source_dir}/modules/tracktion_engine/plugins/internal/tracktion_AuxSend.cpp")
    set(original2 "9d343474d1f22add0a39d159e9ec5fc163b6856d9cf89648fda03c9e687ba9c8")
    set(patched2 "13d09a32bd3c5fda3e7488b5664415a316030c72440c0a581c119281faad1388")
    set(count2 4)
    set(before2_0 [=[    gainLevel.referTo (state, IDs::auxSendSliderPos, um, decibelsToVolumeFaderPosition (0.0f));]=])
    set(after2_0 [=[    gainLevel.referTo (state, IDs::auxSendSliderPos, um, decibelsToSendFaderPosition (0.0f)); // Halftrack: the send law]=])
    set(before2_1 [=[                     [] (float value)             { return juce::Decibels::toString (volumeFaderPositionToDB (value)); },
                     [] (const juce::String& s)   { return decibelsToVolumeFaderPosition (dbStringToDb (s)); });]=])
    set(after2_1 [=[                     [] (float value)             { return juce::Decibels::toString (sendFaderPositionToDB (value)); },
                     [] (const juce::String& s)   { return decibelsToSendFaderPosition (dbStringToDb (s)); });]=])
    set(before2_2 [=[    lastGain = volumeFaderPositionToGain (gain->getCurrentValue());]=])
    set(after2_2 [=[    lastGain = sendFaderPositionToGain (gain->getCurrentValue());]=])
    set(before2_3 [=[    float newPos = decibelsToVolumeFaderPosition (newDb);]=])
    set(after2_3 [=[    float newPos = decibelsToSendFaderPosition (newDb);]=])
    set(source3 "${source_dir}/modules/tracktion_engine/plugins/internal/tracktion_AuxSend.h")
    set(original3 "0f162e59bb4b5924cb19889364823050564bae9b2cc0b839f8907842569eb812")
    set(patched3 "dc7238c35191465c52d0adc9cd304ed95490b44869bf69b10d369fe22b5995c2")
    set(count3 1)
    set(before3_0 [=[    float getGainDb() const                 { return volumeFaderPositionToDB (gain->getCurrentValue()); }]=])
    set(after3_0 [=[    float getGainDb() const                 { return sendFaderPositionToDB (gain->getCurrentValue()); }]=])
    set(source4 "${source_dir}/modules/tracktion_engine/playback/graph/tracktion_AuxSendNode.cpp")
    set(original4 "58d79bb59e402f758244e9cba74e2ea49b10c293b830d5899f3842f9d2ed5919")
    set(patched4 "d959b60b746ea5427d6e3eff75c7889198295be03d0ce6eeb19c5867c36dc301")
    set(count4 1)
    set(before4_0 [=[                    auto gain = volumeFaderPositionToGain (sourceSendPlugin.gain->getCurrentValue());]=])
    set(after4_0 [=[                    auto gain = sendFaderPositionToGain (sourceSendPlugin.gain->getCurrentValue());]=])
    foreach(index RANGE 0 4)
        file(SHA256 "${source${index}}" hash)
        if(hash STREQUAL "${patched${index}}" OR (DEFINED downstream${index} AND hash IN_LIST downstream${index}))
            set(write${index} FALSE)
            continue()
        endif()
        if(NOT hash STREQUAL "${original${index}}")
            message(FATAL_ERROR "Halftrack fader laws patch requires pinned source ${source${index}}; unexpected SHA256 ${hash}.")
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
            message(FATAL_ERROR "Halftrack fader laws patch produced unexpected SHA256 ${hash} for ${source${index}}.")
        endif()
        set(write${index} TRUE)
    endforeach()
    foreach(index RANGE 0 4)
        if(write${index})
            string(REGEX REPLACE "\n$" "" configure_content "${content${index}}")
            file(CONFIGURE OUTPUT "${source${index}}" CONTENT "@configure_content@" @ONLY NEWLINE_STYLE LF)
            file(SHA256 "${source${index}}" hash)
            if(NOT hash STREQUAL "${patched${index}}")
                message(FATAL_ERROR "Halftrack fader laws patch wrote unexpected SHA256 ${hash}.")
            endif()
        endif()
    endforeach()
endfunction()
