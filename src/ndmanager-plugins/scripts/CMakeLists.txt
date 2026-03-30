install(PROGRAMS
    cnt duration
    ndm_clean ndm_concatenate ndm_extractchannels ndm_extractleds
    ndm_extractspikes ndm_extractspikes_sdiff ndm_functions ndm_hipass ndm_lfp ndm_mergedat
    ndm_aom2dat ndm_ncs2dat ndm_nev2evt ndm_nvt2spots ndm_pca ndm_recolorchannels
    ndm_reorderchannels ndm_resample ndm_smi2sts ndm_smr2dat ndm_smr2evt
    ndm_denoiseuniform ndm_klustakwik ndm_spikegrouper
    ndm_spikecleaner
    ndm_start ndm_transcodevideo ndm_tsp2sts
    ndm_estimatedrift
    ndm_applydrift
    ndm_decomposecollisions
    ndm_localise
    ndm_setupgroups
    ndm_stripdat
    ndm_redetectspikes
    ndm_xml2yaml
    pad process_renameevents_SPWR process_renameevents_TMaze
    process_reorderclusters process_spikegrouper rn
    # Spike subtraction pipeline (Python executables without .py extension)
    process_subtractspikes
    process_mergespikes
    DESTINATION "${CMAKE_INSTALL_BINDIR}")

# Python helpers installed alongside their bash drivers so each script can
# find its helper via its own directory; also land on PATH for direct use.
install(PROGRAMS
    process_estimatedrift.py
    process_applydrift.py
    process_decomposecollisions.py
    process_localise.py
    process_setupgroups.py
    DESTINATION "${CMAKE_INSTALL_BINDIR}")

foreach(_page IN ITEMS
        cnt duration ndm ndm_recolorchannels ndm_start
        ndm_estimatedrift ndm_decomposecollisions ndm_setupgroups pad
        process_reorderclusters rn)
    add_manpage(${_page})
endforeach()
