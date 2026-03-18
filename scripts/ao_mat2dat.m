function ao_mat2dat(inputMat, outputBasename, options)
%AO_MAT2DAT  Convert AlphaOmega v7.3 .mat → neurosuite .dat + session YAML
%
%  ao_mat2dat(INPUT_MAT, OUTPUT_BASENAME)
%  ao_mat2dat(INPUT_MAT, OUTPUT_BASENAME, Name=Value, ...)
%
%  Required:
%    INPUT_MAT        Path to AlphaOmega .mat file (v7.3 / HDF5)
%    OUTPUT_BASENAME  Stem for output files (no extension)
%
%  Name-Value Options:
%    Channels       (int vector)  1-based channel numbers to export.
%                                 Default: all CRAW_* found, sorted.
%
%    Topology       (char)        Mixed probe topology string.
%                                 Format: 'FIRST-LAST:SIZE,...'  (1-based, inclusive)
%                                 Group type is inferred from SIZE:
%                                   SIZE == 4  -> tetrode
%                                   SIZE == 1  -> single
%                                   other      -> linear
%                                 Example (your recording):
%                                   Topology='1-16:16,17-32:4'
%                                   -> 1 linear group  (ch 0-15)
%                                   -> 4 tetrode groups (ch 16-19, 20-23, 24-27, 28-31)
%                                 Overrides Groups when provided.
%
%    Groups         (int)         Uniform channels per group (ignored when
%                                 Topology is given).  Default: 8
%
%    ChunkSamples   (int)         Samples per read chunk. Default: 10 000 000
%                                 (~610 MB for 32ch - tuned for 128 GB RAM)
%    OutDir         (char)        Output directory. Default: directory of INPUT
%    NoYaml         (logical)     Skip YAML generation. Default: false
%    DryRun         (logical)     Print metadata and exit without writing. Default: false
%
%  Outputs:
%    OUTPUT_BASENAME.dat    Interleaved little-endian int16, sample-major order
%                           Layout: [ch0_s0 ch1_s0 ... chN_s0 ch0_s1 ch1_s1 ...]
%    OUTPUT_BASENAME.yaml   neurosuite-3 session YAML
%
%  Notes:
%    Uses matfile() for streaming HDF5 access - never loads full file into RAM.
%    Chunk buffer = ChunkSamples x nChannels x 2 bytes (610 MB at defaults).
%    YAML channels are 0-based to match neurosuite-3 convention.
%    voltageRange is left as null; compute from BitResolution if needed:
%      V_fullscale_mV = BitResolution_uV x 32768 / 1000
%
%  Examples:
%    % Your recording - linear shank (ch 1-16) + tetrodes (ch 17-32):
%    ao_mat2dat('recording.mat', 'jg05-20120316', Topology='1-16:16,17-32:4')
%
%    % Uniform Buzsaki 64ch grouping:
%    ao_mat2dat('recording.mat', 'jg05-20120316', Groups=8)
%
%    % Export only tetrode channels:
%    ao_mat2dat('recording.mat', 'jg05-20120316_tet', Channels=17:32, Topology='17-32:4')
%
%    % Dry run - inspect metadata without writing:
%    ao_mat2dat('recording.mat', 'jg05-20120316', Topology='1-16:16,17-32:4', DryRun=true)

    arguments
        inputMat        (1,:) char
        outputBasename  (1,:) char
        options.Channels       (1,:) double = []
        options.Topology       (1,:) char   = ''
        options.Groups         (1,1) double = 8
        options.ChunkSamples   (1,1) double = 10e6
        options.OutDir         (1,:) char   = ''
        options.NoYaml         (1,1) logical = false
        options.DryRun         (1,1) logical = false
    end

    % ── resolve paths ────────────────────────────────────────────────────────
    [matDir, ~, ~] = fileparts(inputMat);
    if isempty(matDir), matDir = pwd; end
    if isempty(options.OutDir)
        outDir = matDir;
    else
        outDir = options.OutDir;
        if ~exist(outDir, 'dir'), mkdir(outDir); end
    end

    datPath  = fullfile(outDir, [outputBasename '.dat']);
    yamlPath = fullfile(outDir, [outputBasename '.yaml']);

    % ── open via matfile (streaming HDF5, no full load) ──────────────────────
    fprintf('Opening %s ...\n', inputMat);
    m = matfile(inputMat, 'Writable', false);

    allVars  = who(m);
    crawMask = ~cellfun(@isempty, regexp(allVars, '^CRAW_\d+$'));
    crawVars = sort(allVars(crawMask));

    if isempty(crawVars)
        error('ao_mat2dat:noChannels', 'No CRAW_NNN datasets found in %s', inputMat);
    end

    % ── select channels ───────────────────────────────────────────────────────
    if isempty(options.Channels)
        selVars = crawVars;
    else
        selVars = arrayfun(@(n) sprintf('CRAW_%03d', n), ...
                           sort(options.Channels), 'UniformOutput', false)';
        missing = selVars(~ismember(selVars, crawVars));
        if ~isempty(missing)
            error('ao_mat2dat:missingChannels', ...
                  'Requested channels not in file: %s', strjoin(missing, ', '));
        end
    end

    nChannels = numel(selVars);
    fprintf('Channels selected : %d  (%s ... %s)\n', ...
            nChannels, selVars{1}, selVars{end});

    % ── metadata from first channel ───────────────────────────────────────────
    ch1    = selVars{1};
    srKhz  = double(m.(sprintf('%s_KHz',           ch1)));
    bitRes = double(m.(sprintf('%s_BitResolution', ch1)));
    gain   = double(m.(sprintf('%s_Gain',          ch1)));
    tBegin = double(m.(sprintf('%s_TimeBegin',     ch1)));
    tEnd   = double(m.(sprintf('%s_TimeEnd',       ch1)));
    srHz   = round(srKhz * 1000);

    % ── validate sample counts ────────────────────────────────────────────────
    nSampsList = zeros(1, nChannels);
    for ci = 1:nChannels
        sz = size(m, selVars{ci});
        nSampsList(ci) = sz(end);
    end
    if numel(unique(nSampsList)) > 1
        warning('ao_mat2dat:lengthMismatch', ...
                'Channels have different lengths - using minimum.');
        nSamples = min(nSampsList);
    else
        nSamples = nSampsList(1);
    end

    durS  = nSamples / srHz;
    expGB = nSamples * nChannels * 2 / 1e9;

    fprintf('Samples           : %s\n',  num2sep(nSamples));
    fprintf('Sampling rate     : %d Hz\n', srHz);
    fprintf('Duration          : %.2f min  (%.1f s)\n', durS/60, durS);
    fprintf('BitResolution     : %.4f uV/count\n', bitRes);
    fprintf('Gain              : %g\n', gain);
    fprintf('Output .dat       : %s\n', datPath);
    fprintf('Expected size     : %.2f GB\n', expGB);

    % ── build electrode groups ────────────────────────────────────────────────
    % groupChans{g} : 0-based channel index vector for spike group g
    % groupTypes{g} : 'linear' | 'tetrode' | 'single'
    if ~isempty(options.Topology)
        [groupChans, groupTypes] = parse_topology(options.Topology, nChannels);
    else
        [groupChans, groupTypes] = make_uniform_groups(nChannels, options.Groups);
    end

    nGroups = numel(groupChans);
    summary = arrayfun(@(g) sprintf('%dch %s', numel(groupChans{g}), groupTypes{g}), ...
                       1:nGroups, 'UniformOutput', false);
    fprintf('Spike groups      : %d  (%s)\n', nGroups, strjoin(summary, ', '));

    if options.DryRun
        fprintf('\n[dry-run] Exiting before write.\n');
        return
    end

    % ── write .dat ────────────────────────────────────────────────────────────
    chunk   = round(options.ChunkSamples);
    nChunks = ceil(nSamples / chunk);
    fprintf('\nWriting %s in %d chunks of %s samples ...\n', ...
            [outputBasename '.dat'], nChunks, num2sep(chunk));

    fid = fopen(datPath, 'wb', 'ieee-le');
    if fid == -1
        error('ao_mat2dat:openFail', 'Cannot open %s for writing', datPath);
    end

    t0 = tic;
    bytesWritten = 0;

    for ci = 1:nChunks
        s = (ci-1)*chunk + 1;
        e = min(s + chunk - 1, nSamples);
        n = e - s + 1;

        % buf: (nChannels x n) int16 — column-major fwrite = sample-interleaved output
        buf = zeros(nChannels, n, 'int16');
        for chi = 1:nChannels
            raw = m.(selVars{chi})(1, s:e);
            buf(chi, :) = int16(raw);
        end

        fwrite(fid, buf, 'int16', 0, 'ieee-le');
        bytesWritten = bytesWritten + n * nChannels * 2;

        elapsed = toc(t0);
        rateMBs = bytesWritten / elapsed / 1e6;
        fprintf('  chunk %4d/%-4d  %5.1f%%  %5.2f GB  %4.0f MB/s\r', ...
                ci, nChunks, ci/nChunks*100, bytesWritten/1e9, rateMBs);
    end

    fclose(fid);
    fprintf('\nDone. %.3f GB written in %.1f s\n', bytesWritten/1e9, toc(t0));

    % ── write YAML ────────────────────────────────────────────────────────────
    if ~options.NoYaml
        fprintf('\nWriting %s ...\n', yamlPath);
        write_yaml(yamlPath, outputBasename, selVars, ...
                   srHz, bitRes, gain, tBegin, tEnd, nSamples, ...
                   groupChans, groupTypes);
        fprintf('YAML written : %s\n', yamlPath);
    end

    fprintf('\nConversion complete.\n');
end


% ── topology helpers ──────────────────────────────────────────────────────────

function ptype = probe_type_str(groupSize)
    if groupSize == 4
        ptype = 'tetrode';
    elseif groupSize == 1
        ptype = 'single';
    else
        ptype = 'linear';
    end
end


function [groupChans, groupTypes] = parse_topology(spec, nChannels)
%PARSE_TOPOLOGY  Parse 'FIRST-LAST:SIZE,...' into 0-based index cells + type strings.
    groupChans = {};
    groupTypes = {};
    tokens = strsplit(strtrim(spec), ',');
    for ti = 1:numel(tokens)
        tok = strtrim(tokens{ti});
        if isempty(tok), continue; end

        parts = strsplit(tok, ':');
        if numel(parts) ~= 2
            error('ao_mat2dat:badTopology', ...
                  'Invalid topology token "%s". Expected FIRST-LAST:SIZE.', tok);
        end
        szVal = round(str2double(parts{2}));
        if isnan(szVal)
            error('ao_mat2dat:badTopology', 'Non-numeric size in token "%s".', tok);
        end

        rngParts = strsplit(parts{1}, '-');
        first = round(str2double(rngParts{1}));
        last  = first;
        if numel(rngParts) == 2
            last = round(str2double(rngParts{2}));
        end
        if isnan(first) || isnan(last)
            error('ao_mat2dat:badTopology', 'Non-numeric range in token "%s".', tok);
        end

        ch0range = (first-1):(last-1);   % 1-based → 0-based
        if any(ch0range >= nChannels) || any(ch0range < 0)
            error('ao_mat2dat:badTopology', ...
                  'Token "%s" references channels outside exported range (1-%d).', ...
                  tok, nChannels);
        end

        ptype = probe_type_str(szVal);
        for i = 1 : szVal : numel(ch0range)
            groupChans{end+1} = ch0range(i : min(i+szVal-1, numel(ch0range))); %#ok<AGROW>
            groupTypes{end+1} = ptype;                                          %#ok<AGROW>
        end
    end

    if isempty(groupChans)
        error('ao_mat2dat:badTopology', ...
              'Topology string produced no groups: "%s"', spec);
    end
end


function [groupChans, groupTypes] = make_uniform_groups(nChannels, groupSize)
%MAKE_UNIFORM_GROUPS  Split 0:nChannels-1 uniformly into groups of groupSize.
    ptype      = probe_type_str(groupSize);
    groupChans = {};
    groupTypes = {};
    for i = 0 : groupSize : nChannels-1
        groupChans{end+1} = i : min(i+groupSize-1, nChannels-1); %#ok<AGROW>
        groupTypes{end+1} = ptype;                                %#ok<AGROW>
    end
end


% ── YAML writer ───────────────────────────────────────────────────────────────

function write_yaml(path, name, selVars, srHz, bitRes, gain, ...
                    tBegin, tEnd, nSamples, groupChans, groupTypes)
    fid = fopen(path, 'wt', 'native', 'UTF-8');
    if fid == -1
        error('ao_mat2dat:yamlFail', 'Cannot open %s for writing', path);
    end
    c = onCleanup(@() fclose(fid)); %#ok<NASGU>

    nChannels = numel(selVars);
    durS      = tEnd - tBegin;
    nGroups   = numel(groupChans);

    fprintf(fid, 'session:\n');
    fprintf(fid, '  name: %s\n',               name);
    fprintf(fid, '  nChannels: %d\n',          nChannels);
    fprintf(fid, '  samplingRate: %d\n',       srHz);
    fprintf(fid, '  nBits: 16\n');
    fprintf(fid, '  voltageRange: null\n');
    fprintf(fid, '  amplification: %d\n',      round(gain));
    fprintf(fid, '  bitResolution_uV: %.6f\n', bitRes);
    fprintf(fid, '  nSamples: %d\n',           nSamples);
    fprintf(fid, '  duration_s: %.4f\n',       durS);
    fprintf(fid, '  recordingBegin_s: %.4f\n', tBegin);
    fprintf(fid, '  recordingEnd_s: %.4f\n',   tEnd);

    fprintf(fid, 'anatomicalGroups:\n');
    for g = 1:nGroups
        fprintf(fid, '- group: %d\n',      g-1);
        fprintf(fid, '  probeType: %s\n',  groupTypes{g});
        fprintf(fid, '  channels: [%s]\n', strtrim(num2str(groupChans{g}, '%d ')));
    end

    fprintf(fid, 'spikeGroups:\n');
    for g = 1:nGroups
        fprintf(fid, '- group: %d\n',         g-1);
        fprintf(fid, '  probeType: %s\n',     groupTypes{g});
        fprintf(fid, '  channels: [%s]\n',    strtrim(num2str(groupChans{g}, '%d ')));
        fprintf(fid, '  nSamples: 32\n');
        fprintf(fid, '  peakSampleIndex: 16\n');
    end
end


% ── helper: thousands-separated integer string ────────────────────────────────
function s = num2sep(n)
    raw = num2str(round(n));
    parts = {};
    while numel(raw) > 3
        parts{end+1} = raw(end-2:end); %#ok<AGROW>
        raw = raw(1:end-3);
    end
    parts{end+1} = raw;
    s = strjoin(fliplr(parts), ',');
end
