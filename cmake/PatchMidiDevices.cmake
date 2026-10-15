# Host MIDI device policy and synchronous user-selected enablement.
# Validate every pinned file before writing; retain upstream GPL notices.
function(halftrack_patch_midi_devices source_dir)
    set(source0 "${source_dir}/modules/tracktion_engine/utilities/tracktion_EngineBehaviour.h")
    set(original0 "09b64a7d5a5e58b45370424262f61a562f620affdb9e2b92829666d0a024347d")
    set(patched0 "fe49e3934dc91d709c99293a2a8c5dbc7f582c10bcd389a2d9aeb15fec8305cf")
    set(before0_0 [=[    virtual bool shouldOpenAudioInputByDefault()                                    { return true; }]=])
    set(after0_0 [=[    virtual bool shouldOpenAudioInputByDefault()                                    { return true; }

    /// Host policy: enumerate physical MIDI devices, and default newly discovered inputs to open.
    virtual bool usePhysicalMidiDevices()                                           { return true; }
    virtual bool shouldOpenMidiInputByDefault()                                     { return true; }]=])
    set(replacements0 0)
    set(source1 "${source_dir}/modules/tracktion_engine/playback/tracktion_DeviceManager.h")
    set(original1 "7ab4590fa518e44b977617dc003597adf84ade0021e6ce01bcf4ebcb6937af50")
    set(patched1 "eaabb54af6e469690fb667efe36b6c88d0f9eefd42c7e8d34ac280271b4f5441")
    set(before1_0 [=[    void rescanMidiDeviceList();]=])
    set(after1_0 [=[    void rescanMidiDeviceList();
    void rescanMidiDeviceListSynchronously();]=])
    set(replacements1 0)
    set(source2 "${source_dir}/modules/tracktion_engine/playback/tracktion_DeviceManager.cpp")
    set(original2 "04869fc7afd45b70fdadebd891742c57c77101ac58e9227298d3531aafef3d32")
    set(patched2 "49374cb08b8752baa385b18806c2824005cd1e0bb67481a373638977b3dbf498")
    set(before2_0 [=[void DeviceManager::timerCallback()
{
    applyNewMidiDeviceList();
}]=])
    set(after2_0 [=[void DeviceManager::rescanMidiDeviceListSynchronously()
{
    TRACKTION_ASSERT_MESSAGE_THREAD
    onlyRescanMidiOnHardwareChange = false;
    applyNewMidiDeviceList();
}

void DeviceManager::timerCallback()
{
    applyNewMidiDeviceList();
}]=])
    set(before2_1 [=[    return hostedAudioDeviceInterface == nullptr
            || hostedAudioDeviceInterface->parameters.useMidiDevices;]=])
    set(after2_1 [=[    return engine.getEngineBehaviour().usePhysicalMidiDevices()
            && (hostedAudioDeviceInterface == nullptr
                || hostedAudioDeviceInterface->parameters.useMidiDevices);]=])
    set(replacements2 1)
    set(source3 "${source_dir}/modules/tracktion_engine/playback/devices/tracktion_PhysicalMidiInputDevice.cpp")
    set(original3 "e72a87226f1448aa73fe4eca1cb1cc1ad8245bcccc38f8dd1f362b669ad467d1")
    set(patched3 "c4221217f5d2c1dfa57a02268d6a762f6c733ea95a17b9c53371a3c035a5a617")
    set(before3_0 [=[    enabled = true;

    controllerParser]=])
    set(after3_0 [=[    enabled = engine.getEngineBehaviour().shouldOpenMidiInputByDefault();

    controllerParser]=])
    set(replacements3 0)
    foreach(index RANGE 0 3)
        file(SHA256 "${source${index}}" hash)
        if(hash STREQUAL "${patched${index}}")
            set(write${index} FALSE)
            continue()
        endif()
        if(NOT hash STREQUAL "${original${index}}")
            message(FATAL_ERROR "Halftrack MIDI device patch requires pinned source ${source${index}}; unexpected SHA256 ${hash}.")
        endif()
        file(READ "${source${index}}" content${index})
        foreach(part RANGE 0 ${replacements${index}})
            string(REPLACE "${before${index}_${part}}" "${after${index}_${part}}" content${index} "${content${index}}")
        endforeach()
        string(SHA256 hash "${content${index}}")
        if(NOT hash STREQUAL "${patched${index}}")
            message(FATAL_ERROR "Halftrack MIDI device patch produced unexpected SHA256 ${hash}.")
        endif()
        set(write${index} TRUE)
    endforeach()
    foreach(index RANGE 0 3)
        if(write${index})
            string(REGEX REPLACE "\n$" "" configure_content "${content${index}}")
            file(CONFIGURE OUTPUT "${source${index}}" CONTENT "@configure_content@" @ONLY NEWLINE_STYLE LF)
            file(SHA256 "${source${index}}" hash)
            if(NOT hash STREQUAL "${patched${index}}")
                message(FATAL_ERROR "Halftrack MIDI device patch wrote unexpected SHA256 ${hash}.")
            endif()
        endif()
    endforeach()
endfunction()
