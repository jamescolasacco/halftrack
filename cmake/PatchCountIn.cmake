# Halftrack count-in for pinned Tracktion Engine. The reference counts in for none, one, two or
# four bars, so the count-in modes gain four bars; and with crotchet beats a bar of the first time
# signature is numerator x 4 / denominator beats, not numerator beats, so a count-in in 6/8 is one
# bar of three beats rather than two bars, and 7/8 counts in three and a half. The count-in length
# becomes a double for that. Retains all upstream source notices. Validates every file before
# writing any.
function(halftrack_patch_count_in source_dir)
    set(source0 "${source_dir}/modules/tracktion_engine/model/edit/tracktion_Edit.h")
    set(original0 "48dfd7ac69e95e5257734c24783c9c867c9c499d8af4e396cbafb826d84a5a33")
    set(patched0 "d391c913e0804f6b0d02ff78202f519a20a5c7c4c6ff1a58681a2773bd7fd23d")
    set(count0 2)
    set(before0_0 [=[        twoBeat   = 3,  /**< Two beats count in. */
        oneBeat   = 4   /**< One beat count in. */
    };]=])
    set(after0_0 [=[        twoBeat   = 3,  /**< Two beats count in. */
        oneBeat   = 4,  /**< One beat count in. */
        fourBar   = 5   /**< Halftrack: four bars count in. */
    };]=])
    set(before0_1 [=[    /** Returns the number of beats of the count in. */
    int getNumCountInBeats() const;]=])
    set(after0_1 [=[    /** Returns the number of beats of the count in. Halftrack: a bar is as many beats as the
        first time signature's bar holds, which with crotchet beats need not be whole. */
    double getNumCountInBeats() const;]=])
    set(source1 "${source_dir}/modules/tracktion_engine/model/edit/tracktion_Edit.cpp")
    set(original1 "ecaf4380982c22801e1e7a7e0b6d0e76965f36cba091605b32039f6093215c7c")
    set(patched1 "385e1d6ad1551a3b38c7d8860c50cd9b364c46f40aecd3693f441b7fa7ebb57a")
    set(count1 1)
    set(before1_0 [=[int Edit::getNumCountInBeats() const
{
    switch (getCountInMode())
    {
        case CountIn::none:       return 0;
        case CountIn::oneBar:     return tempoSequence.getTimeSig(0)->numerator;
        case CountIn::twoBar:     return tempoSequence.getTimeSig(0)->numerator * 2;
        case CountIn::oneBeat:    return 1;
        case CountIn::twoBeat:    return 2;
        default:                  jassertfalse; break;
    }

    return 0;
}]=])
    set(after1_0 [=[double Edit::getNumCountInBeats() const
{
    // Halftrack: with crotchet beats a bar of 6/8 is three beats, not six.
    const auto& sig = *tempoSequence.getTimeSig (0);
    const double barBeats = engine.getEngineBehaviour().lengthOfOneBeatDependsOnTimeSignature()
                              ? (double) sig.numerator
                              : sig.numerator * 4.0 / sig.denominator;

    switch (getCountInMode())
    {
        case CountIn::none:       return 0;
        case CountIn::oneBar:     return barBeats;
        case CountIn::twoBar:     return barBeats * 2;
        case CountIn::fourBar:    return barBeats * 4;
        case CountIn::oneBeat:    return 1;
        case CountIn::twoBeat:    return 2;
        default:                  jassertfalse; break;
    }

    return 0;
}]=])
    foreach(index RANGE 0 1)
        file(SHA256 "${source${index}}" hash)
        if(hash STREQUAL "${patched${index}}")
            set(write${index} FALSE)
            continue()
        endif()
        if(NOT hash STREQUAL "${original${index}}")
            message(FATAL_ERROR "Halftrack count-in patch requires pinned source ${source${index}}; unexpected SHA256 ${hash}.")
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
            message(FATAL_ERROR "Halftrack count-in patch produced unexpected SHA256 ${hash} for ${source${index}}.")
        endif()
        set(write${index} TRUE)
    endforeach()
    foreach(index RANGE 0 1)
        if(write${index})
            string(REGEX REPLACE "\n$" "" configure_content "${content${index}}")
            file(CONFIGURE OUTPUT "${source${index}}" CONTENT "@configure_content@" @ONLY NEWLINE_STYLE LF)
            file(SHA256 "${source${index}}" hash)
            if(NOT hash STREQUAL "${patched${index}}")
                message(FATAL_ERROR "Halftrack count-in patch wrote unexpected SHA256 ${hash}.")
            endif()
        endif()
    endforeach()
endfunction()
