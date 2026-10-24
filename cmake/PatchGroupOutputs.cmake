# Halftrack group outputs for pinned Tracktion Engine, after every earlier patch. The reference lets a
# grouped track's Audio To leave its group, for Main, Sends Only, another track or an external output.
# The engine summed every track inside a submix folder into the folder, whatever its output. A track
# marked as leaving its group (the halftrackOutsideGroup property) is now in no submix: the folder
# leaves it out of its sum, and it plays where its own output goes, as a track outside any group.
# Retains all upstream source notices. Validates every file before writing any.
function(halftrack_patch_group_outputs source_dir)
    set(source0 "${source_dir}/modules/tracktion_engine/model/tracks/tracktion_Track.h")
    set(original0 "c373a1d823daf114fbdeb9ed826ab5d5fc3c39915ca15a19f5c951649d96c142")
    set(patched0 "19d0e8ef0c8a1ef0db3f5962ab0dfb4f35e3ff2ce10e4e9b24f959bcf3bc6815")
    set(count0 1)
    set(before0_0 [=[    bool isPartOfSubmix() const;
]=])
    set(after0_0 [=[    bool isPartOfSubmix() const;

    /** Halftrack: whether this track's output leaves the submix folder it is in, as the reference's
        Audio To may send a grouped track to Main or elsewhere. Such a track is in no submix, and
        plays where its own output goes.
    */
    bool leavesGroup() const;
]=])
    set(source1 "${source_dir}/modules/tracktion_engine/model/tracks/tracktion_Track.cpp")
    set(original1 "9350634aa81fce9c3269142a26bc6ecb2f56589a6e0ac5dcc27ce10e7d769a49")
    set(patched1 "b1623886a9bdfa41a210a1bd698475185cb193d0b665a2219c2ccaaf037bb0cc")
    set(count1 1)
    set(before1_0 [=[bool Track::isPartOfSubmix() const
{
    for (auto p = getParentFolderTrack(); p != nullptr; p = p->getParentFolderTrack())
]=])
    set(after1_0 [=[bool Track::leavesGroup() const
{
    return state.getProperty ("halftrackOutsideGroup", false);
}

bool Track::isPartOfSubmix() const
{
    // Halftrack: a track whose output leaves its group plays where its output goes.
    if (leavesGroup())
        return false;

    for (auto p = getParentFolderTrack(); p != nullptr; p = p->getParentFolderTrack())
]=])
    set(source2 "${source_dir}/modules/tracktion_engine/playback/graph/tracktion_EditNodeBuilder.cpp")
    set(original2 "bbc12a579f50896a7be4a5c13127b63c47863b22986b776290a7d523f80866cf")
    set(patched2 "05e73d01d1cc0a824f96a7dad772a850e5906e3a645bf56affd072f91d986bcc")
    set(count2 1)
    set(before2_0 [=[    for (auto t : submixTrack.getAllSubTracks (false))
    {
        if (auto ft = dynamic_cast<AudioTrack*> (t))
            subAudioTracks.add (ft);
]=])
    set(after2_0 [=[    for (auto t : submixTrack.getAllSubTracks (false))
    {
        // Halftrack: a track whose output leaves the group plays where its output goes, not here.
        if (t->leavesGroup())
            continue;

        if (auto ft = dynamic_cast<AudioTrack*> (t))
            subAudioTracks.add (ft);
]=])
    foreach(index RANGE 0 2)
        file(SHA256 "${source${index}}" hash)
        if(hash STREQUAL "${patched${index}}" OR (DEFINED downstream${index} AND hash IN_LIST downstream${index}))
            set(write${index} FALSE)
            continue()
        endif()
        if(NOT hash STREQUAL "${original${index}}")
            message(FATAL_ERROR "Halftrack group outputs patch requires pinned source ${source${index}}; unexpected SHA256 ${hash}.")
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
            message(FATAL_ERROR "Halftrack group outputs patch produced unexpected SHA256 ${hash} for ${source${index}}.")
        endif()
        set(write${index} TRUE)
    endforeach()
    foreach(index RANGE 0 2)
        if(write${index})
            string(REGEX REPLACE "\n$" "" configure_content "${content${index}}")
            file(CONFIGURE OUTPUT "${source${index}}" CONTENT "@configure_content@" @ONLY NEWLINE_STYLE LF)
            file(SHA256 "${source${index}}" hash)
            if(NOT hash STREQUAL "${patched${index}}")
                message(FATAL_ERROR "Halftrack group outputs patch wrote unexpected SHA256 ${hash}.")
            endif()
        endif()
    endforeach()
endfunction()
