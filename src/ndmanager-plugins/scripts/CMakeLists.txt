install(PROGRAMS
    cnt duration
    ndm_clean ndm_concatenate ndm_extractchannels ndm_extractleds
    ndm_extractspikes ndm_extractspikes_sdiff ndm_functions ndm_hipass ndm_lfp ndm_mergedat
    ndm_ncs2dat ndm_nev2evt ndm_nvt2spots ndm_pca ndm_recolorchannels
    ndm_reorderchannels ndm_resample ndm_smi2sts ndm_smr2dat ndm_smr2evt
    ndm_denoiseuniform ndm_klustakwik ndm_spikegrouper
    ndm_start ndm_transcodevideo ndm_tsp2sts ndm_xml2yaml
    ndm_estimatedrift
    pad process_renameevents_SPWR process_renameevents_TMaze
    process_reorderclusters process_spikegrouper rn
    DESTINATION "${CMAKE_INSTALL_BINDIR}")

# Python helper installed alongside the bash driver so the script can find it
# via its own directory; also lands on PATH for direct invocation.
install(PROGRAMS
    process_estimatedrift.py
    DESTINATION "${CMAKE_INSTALL_BINDIR}")

foreach(_page IN ITEMS
        cnt duration ndm ndm_recolorchannels ndm_start ndm_xml2yaml ndm_estimatedrift pad
        process_reorderclusters rn)
    add_manpage(${_page})
endforeach()
