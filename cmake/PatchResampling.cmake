# Halftrack Resampling for pinned Tracktion Engine, after every earlier patch: the reference's audio
# tracks can take Resampling as their input, recording the main output (its manual, section 17.4).
# The device manager adds a stereo "Resampling" wave input and feeds it, after each block renders,
# with the main output's two channels as they rendered, stamped like the hardware inputs, so the
# input placement patch puts each block at the edit time of that output; it takes no input latency
# adjustment. As the reference suppresses the recording track's own output during resampling, a
# track whose Resampling input records or monitors plays nothing, which also keeps the main output
# from feeding back into it. Retains all upstream source notices. Validates every file before
# writing any.
function(halftrack_patch_resampling source_dir)
    set(source0 "${source_dir}/modules/tracktion_engine/playback/tracktion_DeviceManager.h")
    set(original0 "eaabb54af6e469690fb667efe36b6c88d0f9eefd42c7e8d34ac280271b4f5441")
    set(patched0 "afa6eede76941aca16c98f83f7fd8538d8728c3b48ab1e97d5b911a1d5db8c65")
    set(count0 1)
    set(before0_0 [=[    std::vector<int> activeOutputChannels;
]=])
    set(after0_0 [=[    std::vector<int> activeOutputChannels;
    // Halftrack: the main output's channels in the device, which Resampling takes after each block renders.
    std::atomic<int> resamplingLeft { -1 }, resamplingRight { -1 };
    void updateResamplingChannels();
]=])
    set(source1 "${source_dir}/modules/tracktion_engine/playback/tracktion_DeviceManager.cpp")
    set(original1 "e653f519cdc6ae62cd56be45919cdfd5030e041a35e84ee77c18fadc4b008c91")
    set(patched1 "12383b707e12ce57113f93d9d447a06a4b14a4fc90a76483f0912e4c4ddf8e63")
    set(count1 6)
    set(before1_0 [=[    for (auto& d : deviceDescriptionList.inputs)
    {
        auto wi = new WaveInputDevice (engine, d, InputDevice::waveDevice);
        newWaveInputs.add (wi);

        TRACKTION_LOG_DEVICE ("Wave In: " + wi->getName() + (wi->isEnabled() ? " (enabled): " : ": ")
                               + wi->getChannels().getDescription());
    }
]=])
    set(after1_0 [=[    for (auto& d : deviceDescriptionList.inputs)
    {
        auto wi = new WaveInputDevice (engine, d, InputDevice::waveDevice);
        newWaveInputs.add (wi);

        TRACKTION_LOG_DEVICE ("Wave In: " + wi->getName() + (wi->isEnabled() ? " (enabled): " : ": ")
                               + wi->getChannels().getDescription());
    }

    // Halftrack: Resampling, the main output as a stereo input, fed after each block renders.
    {
        auto wi = new WaveInputDevice (engine, WaveDeviceDescription::withNumChannels ("Resampling", 0, 2, true), InputDevice::waveDevice);
        wi->resampling = true;
        newWaveInputs.add (wi);
    }
]=])
    set(before1_1 [=[    reloadAllContextDevices();
    saveSettings();
    checkDefaultDevicesAreValid();
    sendChangeMessage();
]=])
    set(after1_1 [=[    reloadAllContextDevices();
    saveSettings();
    checkDefaultDevicesAreValid();
    updateResamplingChannels();
    sendChangeMessage();
]=])
    set(before1_2 [=[                defaultWaveOutID = deviceID;
]=])
    set(after1_2 [=[                defaultWaveOutID = deviceID;
                updateResamplingChannels();
]=])
    set(before1_3 [=[void DeviceManager::setDefaultWaveInDevice (juce::String deviceID)
{]=])
    set(after1_3 [=[// Halftrack: the default wave output's channels, the main output that Resampling takes; a mono one feeds both sides.
void DeviceManager::updateResamplingChannels()
{
    int channels[2] = { -1, -1 };

    if (auto main = getDefaultWaveOutDevice())
    {
        int n = 0;

        for (const auto& ci : main->getChannels())
            if (n < 2)
                channels[n++] = ci.indexInDevice;

        if (n == 1)
            channels[1] = channels[0];
    }

    resamplingLeft.store (channels[0], std::memory_order_relaxed);
    resamplingRight.store (channels[1], std::memory_order_relaxed);
}

void DeviceManager::setDefaultWaveInDevice (juce::String deviceID)
{]=])
    set(before1_4 [=[                for (auto wi : waveInputs)
                    wi->consumeNextAudioBlock (inputChannelData, numInputChannels, numSamples, streamTime);
]=])
    set(after1_4 [=[                for (auto wi : waveInputs)
                    if (! wi->resampling)
                        wi->consumeNextAudioBlock (inputChannelData, numInputChannels, numSamples, streamTime);
]=])
    set(before1_5 [=[                for (auto chan : activeOutputChannels)
                    if (chan >= 0 && chan < totalNumOutputChannels)
                        if (auto* dest = outputChannelData[chan])
                            for (int j = 0; j < numSamples; ++j)
                                if (! std::isnormal (dest[j]))
                                    dest[j] = 0;
]=])
    set(after1_5 [=[                for (auto chan : activeOutputChannels)
                    if (chan >= 0 && chan < totalNumOutputChannels)
                        if (auto* dest = outputChannelData[chan])
                            for (int j = 0; j < numSamples; ++j)
                                if (! std::isnormal (dest[j]))
                                    dest[j] = 0;

                // Halftrack: Resampling takes the main output as this block rendered it, stamped as the inputs were.
                {
                    const int left = resamplingLeft.load (std::memory_order_relaxed), right = resamplingRight.load (std::memory_order_relaxed);

                    if (juce::isPositiveAndBelow (left, totalNumOutputChannels) && juce::isPositiveAndBelow (right, totalNumOutputChannels)
                        && outputChannelData[left] != nullptr && outputChannelData[right] != nullptr)
                    {
                        const float* mainOutput[2] = { outputChannelData[left], outputChannelData[right] };

                        for (auto wi : waveInputs)
                            if (wi->resampling)
                                wi->consumeNextAudioBlock (mainOutput, 2, numSamples, streamTime);
                    }
                }
]=])
    set(source2 "${source_dir}/modules/tracktion_engine/playback/devices/tracktion_WaveInputDevice.h")
    set(original2 "b6253e9c14f60c5d29bfee19824380426d454e1ce65038e183b47ee90d245776")
    set(patched2 "98f9df2a3da1ad8751a5208ac9cd54ab0c799947a2ccf4a153caf3d072420468")
    set(count2 1)
    set(before2_0 [=[    //==============================================================================
    WaveDeviceDescription deviceDescription;
]=])
    set(after2_0 [=[    //==============================================================================
    WaveDeviceDescription deviceDescription;

    /** Halftrack: Resampling, the main output as an input. The device manager feeds it what each block rendered; it takes
        no input latency adjustment, and a track whose Resampling input records or monitors plays nothing. */
    bool resampling = false;
]=])
    set(source3 "${source_dir}/modules/tracktion_engine/playback/devices/tracktion_WaveInputDevice.cpp")
    set(original3 "b7ea47bf3f5680c433197eaa3426c459ffbdf58ae6f1a513708af52d1b8feb2d")
    set(patched3 "626cda1af31c966d42441f8fde2e8caa97222e2b4fa2bd9e761c816568b57ca7")
    set(count3 1)
    set(before3_0 [=[TimeDuration WaveInputDevice::getAdjustmentSeconds()
{
    auto& dm = engine.getDeviceManager();]=])
    set(after3_0 [=[TimeDuration WaveInputDevice::getAdjustmentSeconds()
{
    // Halftrack: the main output arrives as it rendered, with no input latency to take away.
    if (resampling)
        return {};

    auto& dm = engine.getDeviceManager();]=])
    set(source4 "${source_dir}/modules/tracktion_engine/playback/graph/tracktion_TrackMutingNode.h")
    set(original4 "775942d56223e25a388fd6dbd09d1a2532c9a79e6773df7cdcd8b7c90d64f057")
    set(patched4 "42d12792de2ce67de8bae7831e8812cc6f8b88f06d270969595dd311d8791061")
    set(count4 1)
    set(before4_0 [=[    bool callInputWhileMuted = false;
    bool processMidiWhileMuted = false;
    juce::Array<InputDeviceInstance*> inputDevicesToMuteFor;
]=])
    set(after4_0 [=[    bool callInputWhileMuted = false;
    bool processMidiWhileMuted = false;
    juce::Array<InputDeviceInstance*> inputDevicesToMuteFor;
    bool resampling = false; // Halftrack: the track records or monitors Resampling, so plays nothing.
]=])
    set(source5 "${source_dir}/modules/tracktion_engine/playback/graph/tracktion_TrackMutingNode.cpp")
    set(original5 "56dcdb650c3c3a20b8acd57353c92c4c9de07cc8ea7588436109137b0acdb918")
    set(patched5 "2160f71d390de445a3787ea84096a4596eabda718142664b6cb7e0c9c9bdaba9")
    set(count5 2)
    set(before5_0 [=[    if (muteForInputsWhenRecording)
        if (auto at = dynamic_cast<AudioTrack*> (&t))
            for (auto in : edit.getAllInputDevices())
                if (in->isRecordingActive (t.itemID) && in->getTargets().contains (at->itemID))
                    inputDevicesToMuteFor.add (in);
]=])
    set(after5_0 [=[    if (muteForInputsWhenRecording)
        if (auto at = dynamic_cast<AudioTrack*> (&t))
            for (auto in : edit.getAllInputDevices())
                if (in->isRecordingActive (t.itemID) && in->getTargets().contains (at->itemID))
                    inputDevicesToMuteFor.add (in);

    // Halftrack: as the reference suppresses the recording track's own output during resampling, a track whose
    // Resampling input records or monitors plays nothing, so the main output it takes does not feed back into it.
    if (auto at = dynamic_cast<AudioTrack*> (&t))
        for (auto in : edit.getAllInputDevices())
            if (auto wave = dynamic_cast<WaveInputDevice*> (&in->getInputDevice()); wave != nullptr && wave->resampling
                && in->getTargets().contains (at->itemID) && (in->isRecordingActive (t.itemID) || in->isLivePlayEnabled (t)))
                resampling = true;
]=])
    set(before5_1 [=[bool TrackMuteState::isBeingPlayed() const
{
    bool playing = track != nullptr ? track->shouldBePlayed() : ! edit.areAnyTracksSolo();
]=])
    set(after5_1 [=[bool TrackMuteState::isBeingPlayed() const
{
    if (resampling)
        return false;

    bool playing = track != nullptr ? track->shouldBePlayed() : ! edit.areAnyTracksSolo();
]=])
    foreach(index RANGE 0 5)
        file(SHA256 "${source${index}}" hash)
        if(hash STREQUAL "${patched${index}}" OR (DEFINED downstream${index} AND hash IN_LIST downstream${index}))
            set(write${index} FALSE)
            continue()
        endif()
        if(NOT hash STREQUAL "${original${index}}")
            message(FATAL_ERROR "Halftrack Resampling patch requires pinned source ${source${index}}; unexpected SHA256 ${hash}.")
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
            message(FATAL_ERROR "Halftrack Resampling patch produced unexpected SHA256 ${hash} for ${source${index}}.")
        endif()
        set(write${index} TRUE)
    endforeach()
    foreach(index RANGE 0 5)
        if(write${index})
            string(REGEX REPLACE "\n$" "" configure_content "${content${index}}")
            file(CONFIGURE OUTPUT "${source${index}}" CONTENT "@configure_content@" @ONLY NEWLINE_STYLE LF)
            file(SHA256 "${source${index}}" hash)
            if(NOT hash STREQUAL "${patched${index}}")
                message(FATAL_ERROR "Halftrack Resampling patch wrote unexpected SHA256 ${hash}.")
            endif()
        endif()
    endforeach()
endfunction()
