install(PROGRAMS
    cnt duration
    ndm_clean ndm_concatenate ndm_extractchannels ndm_extractemg ndm_extractleds
    ndm_extractspikes ndm_extractspikes_sdiff
    ndm_extractspikes_stderiv ndm_functions ndm_custody ndm_hipass ndm_lfp ndm_mergedat
    ndm_aom2dat ndm_ncs2dat ndm_nev2evt ndm_nvt2spots ndm_pca ndm_pca_stderiv ndm_pca_multichannel ndm_recolorchannels
    ndm_reorderchannels ndm_resample ndm_smi2sts ndm_smr2dat ndm_smr2evt
    ndm_denoiseuniform ndm_klustakwik ndm_spikegrouper
    ndm_spikecleaner
    ndm_start ndm_transcodevideo ndm_tsp2sts
    ndm_estimatedrift
    ndm_applydrift
    ndm_driftcorrect
    ndm_decomposecollisions
    # Automatic cluster curation.  Learns KEEP/DISCARD/MERGE decisions from
    # (raw KlustaKwik, manually curated) .clu pairs using scale-invariant
    # cluster-geometry features (covariance eigenvalue spectrum, Mahalanobis
    # NN gap, split-axis alignment, shank-whitened separation), so one model
    # transfers across shanks/probes.  mode=train accumulates across sessions
    # into a persistent model; mode=apply replays it on fresh output.  Backed
    # by process_autoklusta.py (installed in the Python-helpers block below).
    ndm_autoklusta
    ndm_localise
    ndm_setupgroups
    ndm_stripdat
    ndm_redetectspikes
    # Realign spike snippets to their true energy peak before PCA.  The
    # extracted snippet is centred at the threshold-crossing sample T, but
    # the true waveform peak may sit at T±δ — when δ varies across spikes
    # of the same neuron, PCA sees multiple shifted copies and KlustaKwik
    # over-splits.  Re-extracts each snippet from the raw .fil at the true
    # peak, updates .res / .spk in place, archives the original .res to
    # .res.<g>.prealign, and clears stale .fet / .pca so the caller must
    # re-run ndm_pca / ndm_pca_stderiv.  Backed by process_alignspikes
    # (already installed via src/process_alignspikes/CMakeLists.txt).
    ndm_alignspikes
    # Two-stage per-cluster spike realignment.  Operates AFTER KlustaKwik
    # has produced .clu.N.  Stage 1 aligns each spike to its cluster
    # template via normalised xcorr (similar to Klusters' realignSpikes).
    # Stage 2 rigidly translates each cluster by a single global shift
    # maximising the PCA-projection energy of its mean waveform against
    # the existing .pca.N / .pcaD.N basis — unlike Klusters' --pca-refine
    # (patch82) which acts per-spike and can disperse mixture clusters,
    # this version preserves intra-cluster variance and only corrects
    # systematic per-cluster mis-alignments.  Invalidates .fet/.fetD on
    # exit; re-run ndm_pca / ndm_pca_stderiv to regenerate features.
    # Backed by process_alignspikes_pca (src/process_alignspikes_pca/).
    ndm_alignspikes_pca
    # Second-pass spike re-extraction + shadow clustering.  The _stderiv
    # variant re-uses the spatial-derivative detection pipeline's
    # extensions (.spkD / .fetD / .pcaD); the plain variant works on raw
    # .spk / .fet / .pca.  Both commit atomically via a sandbox --out
    # stem and preserve .bak backups of the pre-merge files.
    ndm_reextractspikes
    ndm_reextractspikes_stderiv
    # Re-cluster the unmatched residual bin left by process_shadowcluster.
    # Runs KlustaKwik on just the .fet rows whose .clu == unmatched_id,
    # then merges sub-cluster IDs back into the live .clu with an offset
    # so they don't collide with existing shadow/parent IDs.
    ndm_subcluster_unmatched
    # Drift-aware template matching per cluster group.  Sibling of
    # KiloKlustaKwik's Phase 6b (mean-subtraction merge) that tracks
    # cluster mean waveforms through sliding time windows and merges
    # pairs whose smoothed trajectories converge.  Backed by
    # process_drifttracker (installed via its own subdirectory).
    ndm_drifttracker
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
    process_driftcorrect.py
    process_autoklusta.py
    process_localise.py
    process_setupgroups.py
    kk_build_prior.py
    kk_resolve_prior.py
    adaptmodel.py
    adapt_format.py
    ndm_resolve_io.py
    DESTINATION "${CMAKE_INSTALL_BINDIR}")

foreach(_page IN ITEMS
        cnt duration ndm ndm_recolorchannels ndm_start
        ndm_estimatedrift ndm_decomposecollisions ndm_driftcorrect
        ndm_setupgroups pad
        process_reorderclusters rn)
    add_manpage(${_page})
endforeach()
