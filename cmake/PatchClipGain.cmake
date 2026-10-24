# Startup clip-gain correction for pinned Tracktion Engine; preserves GPL notices.
function(halftrack_patch_clip_gain source_dir)
    set(source "${source_dir}/modules/tracktion_engine/playback/graph/tracktion_WaveNode.cpp")
    file(SHA256 "${source}" source_hash)
    if(source_hash STREQUAL "b7c3e5191bb380770db9b6e5aa8da5a3fd2131aee102d28485be669c807c9d76"
       OR source_hash STREQUAL "a723324379bc424d352772f91f538f6a457a9559caa34fd80b9699484e3f7fc2"
       OR source_hash STREQUAL "2f2a0038be5991510a1d0222f166d831fa9865dfe6f9d42e4dbbe4e20c6b2cd0"
       OR source_hash STREQUAL "04ccc34aa76ca5f81534007a7d68da4e595b6680b323879d7ffe6b69d947b269"
       OR source_hash STREQUAL "2e4354f76d96dc25dc489042afa6a3263439dbc3b3095b8639d008cd94b7a7d9") # Later clip-activation, stretch-alignment, warp-timing and stretch-priming patches.
        return()
    endif()
    if(NOT source_hash STREQUAL "1b92d35da33ca2495f3a2627f4a985c54c651285370e63f036491de953cdd980")
        message(FATAL_ERROR "Halftrack clip-gain patch requires pinned source; unexpected SHA256 ${source_hash}.")
    endif()
    file(READ "${source}" content)
    set(before [=[    resamplerReader = resamplerAudioReader.get();
    std::unique_ptr<TimeStretchReaderBase> timeStretchReader;]=])
    set(after [=[    resamplerReader = resamplerAudioReader.get();
    // Halftrack: stretch constructors prefill audio before processSection runs.
    // Use the saved level for that prefill as well as subsequent audio blocks.
    float initialLeftGain, initialRightGain;
    if (destChannels.getNumChannels() == 2)
        clipLevel.getLeftAndRightGains (initialLeftGain, initialRightGain);
    else
        initialLeftGain = initialRightGain = clipLevel.getGainIncludingMute();
    resamplerReader->setGains (initialLeftGain, initialRightGain);
    std::unique_ptr<TimeStretchReaderBase> timeStretchReader;]=])
    string(REPLACE "${before}" "${after}" content "${content}")
    string(SHA256 patched_hash "${content}")
    if(NOT patched_hash STREQUAL "b7c3e5191bb380770db9b6e5aa8da5a3fd2131aee102d28485be669c807c9d76")
        message(FATAL_ERROR "Halftrack clip-gain patch produced unexpected SHA256 ${patched_hash}.")
    endif()
    string(REGEX REPLACE "\n$" "" configure_content "${content}")
    file(CONFIGURE OUTPUT "${source}" CONTENT "@configure_content@" @ONLY NEWLINE_STYLE LF)
    file(SHA256 "${source}" written_hash)
    if(NOT written_hash STREQUAL "b7c3e5191bb380770db9b6e5aa8da5a3fd2131aee102d28485be669c807c9d76")
        message(FATAL_ERROR "Halftrack clip-gain patch wrote unexpected SHA256 ${written_hash}.")
    endif()
endfunction()
