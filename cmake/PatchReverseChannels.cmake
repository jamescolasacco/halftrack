# Pending reversed-source channel correction for pinned Tracktion Engine; preserves GPL notices.
function(halftrack_patch_reverse_channels source_dir)
    set(source "${source_dir}/modules/tracktion_engine/model/clips/tracktion_AudioClipBase.cpp")
    file(SHA256 "${source}" source_hash)
    if(source_hash STREQUAL "1902c1ad6fd72e5a62c9f83778bf190d239e7e8ae8f77229d66ff8ba6de2df57"
       OR source_hash STREQUAL "63336ba34baf1de28fce2e8bc0aff7b06d015ba45f9776cc62b0e7716449c455") # Or after the later Re-Pitch patch.
        return()
    endif()
    if(NOT source_hash STREQUAL "e87f781fd11f8f90236b416bd762577105439ef11eb13f63f1d9770b7ac49826")
        message(FATAL_ERROR "Halftrack reverse-channel patch requires pinned source; unexpected SHA256 ${source_hash}.")
    endif()
    file(READ "${source}" content)
    set(before [=[AudioFileInfo AudioClipBase::getWaveInfo()
{]=])
    set(after [=[AudioFileInfo AudioClipBase::getWaveInfo()
{
    // Halftrack: a pending reversed file has no channel metadata yet. Keep the
    // original layout when constructing its graph; reversal preserves channels.
    if (getIsReversed() && ! getAudioFile().isValid())
        return AudioFile (edit.engine, getOriginalFile()).getInfo();
]=])
    string(REPLACE "${before}" "${after}" content "${content}")
    string(SHA256 patched_hash "${content}")
    if(NOT patched_hash STREQUAL "1902c1ad6fd72e5a62c9f83778bf190d239e7e8ae8f77229d66ff8ba6de2df57")
        message(FATAL_ERROR "Halftrack reverse-channel patch produced unexpected SHA256 ${patched_hash}.")
    endif()
    string(REGEX REPLACE "\n$" "" configure_content "${content}")
    file(CONFIGURE OUTPUT "${source}" CONTENT "@configure_content@" @ONLY NEWLINE_STYLE LF)
    file(SHA256 "${source}" written_hash)
    if(NOT written_hash STREQUAL "1902c1ad6fd72e5a62c9f83778bf190d239e7e8ae8f77229d66ff8ba6de2df57")
        message(FATAL_ERROR "Halftrack reverse-channel patch wrote unexpected SHA256 ${written_hash}.")
    endif()
endfunction()
