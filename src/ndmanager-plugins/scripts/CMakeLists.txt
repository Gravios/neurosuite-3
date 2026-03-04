install(PROGRAMS
    cnt duration
    ndm_clean ndm_concatenate ndm_extractchannels ndm_extractleds
    ndm_extractspikes ndm_extractspikes_sdiff ndm_functions ndm_hipass ndm_lfp ndm_mergedat
    ndm_ncs2dat ndm_nev2evt ndm_nvt2spots ndm_pca ndm_recolorchannels
    ndm_reorderchannels ndm_resample ndm_smi2sts ndm_smr2dat ndm_smr2evt
    ndm_denoiseuniform ndm_klustakwik ndm_spikegrouper
    ndm_start ndm_transcodevideo ndm_tsp2sts ndm_xml2yaml
    ndm_estimatedrift
    ndm_decomposecollisions
    pad process_renameevents_SPWR process_renameevents_TMaze
    process_reorderclusters process_spikegrouper rn
    DESTINATION "${CMAKE_INSTALL_BINDIR}")

# Python helpers installed alongside their bash drivers so each script can
# find its helper via its own directory; also land on PATH for direct use.
install(PROGRAMS
    process_estimatedrift.py
    process_decomposecollisions.py
    DESTINATION "${CMAKE_INSTALL_BINDIR}")

foreach(_page IN ITEMS
        cnt duration ndm ndm_recolorchannels ndm_start ndm_xml2yaml
        ndm_estimatedrift ndm_decomposecollisions pad
        process_reorderclusters rn)
    add_manpage(${_page})
endforeach()
