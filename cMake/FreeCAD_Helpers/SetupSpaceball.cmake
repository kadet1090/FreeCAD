macro(SetupSpaceball)
# ------------------------------ Spaceball -------------------------------

    if (WIN32)
        #future
    else(WIN32)
        #find_package(Spnav)
        set (SPNAV_FOUND TRUE)
    endif(WIN32)

endmacro(SetupSpaceball)
