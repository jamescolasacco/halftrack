# Pending structural sort hooks for pinned Tracktion 07e2d674884293f1a6c8f35fb8d1b9f469ffa41f.
# Preserve native ordering/undo behavior while allowing the owning UI transaction
# to finish the pending work before a modal export dispatches the message loop.
function(halftrack_patch_edit_sort source_dir)
    set(source "${source_dir}/modules/tracktion_engine/model/tracks/tracktion_TrackUtils.h")
    file(SHA256 "${source}" source_hash)
    if(NOT source_hash STREQUAL "fcad3688f77edb794f819be0e8a070d249a1396bc32278b979a4095a68a541e3"
       AND NOT source_hash STREQUAL "7eaef085fa9693bcb82435b0965ad6b87f0cecb9251517bffe05c30a5c8aacf3")
        message(FATAL_ERROR "Halftrack edit-sort patch requires pinned source: ${source}; unexpected SHA256 ${source_hash}.")
    endif()
    set(source "${source_dir}/modules/tracktion_engine/model/clips/tracktion_ClipOwner.h")
    file(SHA256 "${source}" source_hash)
    if(NOT source_hash STREQUAL "cc332f9847996d75c8af0f20c04557692a22bd1c341afb94ac5aacabaed549db"
       AND NOT source_hash STREQUAL "35fbc1ab47d65d5551f67ce283c6ebb70f6388acf9682bc8b1e7939b82218d90")
        message(FATAL_ERROR "Halftrack edit-sort patch requires pinned source: ${source}; unexpected SHA256 ${source_hash}.")
    endif()
    set(source "${source_dir}/modules/tracktion_engine/model/clips/tracktion_ClipOwner.cpp")
    file(SHA256 "${source}" source_hash)
    if(NOT source_hash STREQUAL "b40eeba8bf5d812cad05ce4485bab191ce39b612d6131b5367ef4b4bf868cb11"
       AND NOT source_hash STREQUAL "2fbdcae339880e99bb1a83005f7063d896f263026604b3e7b9656a5ac3d972c9")
        message(FATAL_ERROR "Halftrack edit-sort patch requires pinned source: ${source}; unexpected SHA256 ${source_hash}.")
    endif()
    set(source "${source_dir}/modules/tracktion_engine/model/tracks/tracktion_TrackUtils.h")
    file(SHA256 "${source}" source_hash)
    if(NOT source_hash STREQUAL "7eaef085fa9693bcb82435b0965ad6b87f0cecb9251517bffe05c30a5c8aacf3")
        file(READ "${source}" content)
        set(before [=[    /** Initialises the TrackList, building the child objects. */
    void initialise();]=])
        set(after [=[    /** Initialises the TrackList, building the child objects. */
    void initialise();

    /** Halftrack: complete pending structural ordering inside the caller's undo transaction. */
    void dispatchPendingSort() { handleUpdateNowIfNeeded(); }]=])
        string(REPLACE "${before}" "${after}" content "${content}")
        file(CONFIGURE OUTPUT "${source}" CONTENT "${content}" @ONLY NEWLINE_STYLE LF)
        file(SHA256 "${source}" patched_hash)
        if(NOT patched_hash STREQUAL "7eaef085fa9693bcb82435b0965ad6b87f0cecb9251517bffe05c30a5c8aacf3")
            message(FATAL_ERROR "Halftrack edit-sort patch produced unexpected source: ${source}; SHA256 ${patched_hash}.")
        endif()
    endif()
    set(source "${source_dir}/modules/tracktion_engine/model/clips/tracktion_ClipOwner.h")
    file(SHA256 "${source}" source_hash)
    if(NOT source_hash STREQUAL "35fbc1ab47d65d5551f67ce283c6ebb70f6388acf9682bc8b1e7939b82218d90")
        file(READ "${source}" content)
        set(before [=[    const juce::Array<Clip*>& getClips() const;]=])
        set(after [=[    const juce::Array<Clip*>& getClips() const;

    /** Halftrack: complete pending structural ordering inside the caller's undo transaction. */
    void dispatchPendingSort();]=])
        string(REPLACE "${before}" "${after}" content "${content}")
        file(CONFIGURE OUTPUT "${source}" CONTENT "${content}" @ONLY NEWLINE_STYLE LF)
        file(SHA256 "${source}" patched_hash)
        if(NOT patched_hash STREQUAL "35fbc1ab47d65d5551f67ce283c6ebb70f6388acf9682bc8b1e7939b82218d90")
            message(FATAL_ERROR "Halftrack edit-sort patch produced unexpected source: ${source}; SHA256 ${patched_hash}.")
        endif()
    endif()
    set(source "${source_dir}/modules/tracktion_engine/model/clips/tracktion_ClipOwner.cpp")
    file(SHA256 "${source}" source_hash)
    if(NOT source_hash STREQUAL "2fbdcae339880e99bb1a83005f7063d896f263026604b3e7b9656a5ac3d972c9")
        file(READ "${source}" content)
        set(before [=[    ~ClipList() override
]=])
        set(after [=[    void dispatchPendingSort() { handleUpdateNowIfNeeded(); }

    ~ClipList() override
]=])
        string(REPLACE "${before}" "${after}" content "${content}")
        set(before [=[const juce::Array<Clip*>& ClipOwner::getClips() const
]=])
        set(after [=[void ClipOwner::dispatchPendingSort()
{
    if (clipList) clipList->dispatchPendingSort();
}

const juce::Array<Clip*>& ClipOwner::getClips() const
]=])
        string(REPLACE "${before}" "${after}" content "${content}")
        file(CONFIGURE OUTPUT "${source}" CONTENT "${content}" @ONLY NEWLINE_STYLE LF)
        file(SHA256 "${source}" patched_hash)
        if(NOT patched_hash STREQUAL "2fbdcae339880e99bb1a83005f7063d896f263026604b3e7b9656a5ac3d972c9")
            message(FATAL_ERROR "Halftrack edit-sort patch produced unexpected source: ${source}; SHA256 ${patched_hash}.")
        endif()
    endif()
endfunction()
