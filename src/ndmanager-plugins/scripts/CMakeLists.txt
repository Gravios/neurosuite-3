install(PROGRAMS
    cnt duration
    ndm_clean ndm_concatenate ndm_extractchannels ndm_extractleds
    ndm_extractspikes ndm_functions ndm_hipass ndm_lfp ndm_mergedat
    ndm_ncs2dat ndm_nev2evt ndm_nvt2spots ndm_pca ndm_recolorchannels
    ndm_reorderchannels ndm_resample ndm_smi2sts ndm_smr2dat ndm_smr2evt
    ndm_spikegrouper
    ndm_start ndm_transcodevideo ndm_tsp2sts
    pad process_renameevents_SPWR process_renameevents_TMaze
    process_reorderclusters process_spikegrouper rn
    DESTINATION "${CMAKE_INSTALL_BINDIR}")

foreach(_page IN ITEMS
        cnt duration ndm ndm_recolorchannels ndm_start pad
        process_reorderclusters rn)
    add_manpage(${_page})
endforeach()
