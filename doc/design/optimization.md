# neurosuite-3 — System Optimization Guide

Hardware profile: AMD Ryzen 7 9800X3D · NVIDIA RTX 5070 Ti (Blackwell) ·
64 GiB DDR5-6000 (Kingston KF560C30-32, EXPO enabled) ·
Samsung 990 PRO 2 TB (OS/root) · Samsung 9100 PRO 8 TB (`/data`)

---

## 1  BIOS

### 1.1  Precision Boost Overdrive (PBO)  ⚡ +5–10% sustained clocks

```
Advanced → AMD Overclocking → Precision Boost Overdrive → Enabled
  PBO Limits: Motherboard
  Curve Optimizer: per-core negative offset (start with −10 all-core,
                   tighten if stable under 30 min Prime95 small-FFT)
```

The 9800X3D already boosts to 5271 MHz on lightly threaded code.  PBO
raises the power and thermal budget for the all-core sustained workloads
that dominate KlustaKwik OpenMP loops.

### 1.2  CPPC2 (low-latency frequency ramp)

```
Advanced → AMD CBS → CPU Common Options → CPPC → Enabled
                                           CPPC Preferred Cores → Enabled
```

Reduces the latency for the CPU to ramp to peak boost frequency when a
compute thread wakes from a sleep or I/O wait.

---

## 2  Operating System

### 2.1  CPU frequency governor

```bash
sudo apt install cpufrequtils
echo 'GOVERNOR="performance"' | sudo tee /etc/default/cpufrequtils
sudo systemctl restart cpufrequtils

# Verify
cat /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor | sort -u
# Expected: performance
```

The default `schedutil` governor can stall the CPU at 603 MHz during the
short idle gaps between per-spike Python iterations.  `performance` pins
all cores at the highest non-boost P-state and lets PBO handle turbo.

### 2.2  Transparent Huge Pages (THP)

NumPy `memmap` on multi-GB `.dat` files benefits from 2 MB huge pages:
fewer TLB entries are needed when striding through the recording array,
keeping TLB pressure low during the chunk loop in `process_subtractspikes`.

```bash
# Permanent via systemd:
sudo tee /etc/systemd/system/thp-madvise.service << 'SERVICE'
[Unit]
Description=Set THP to madvise
After=local-fs.target

[Service]
Type=oneshot
ExecStart=/bin/sh -c "echo madvise > /sys/kernel/mm/transparent_hugepage/enabled"
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target
SERVICE
sudo systemctl enable --now thp-madvise.service
```

### 2.3  Swappiness

```bash
echo 'vm.swappiness=1' | sudo tee -a /etc/sysctl.d/99-neurosuite.conf
sudo sysctl -p /etc/sysctl.d/99-neurosuite.conf
```

### 2.4  I/O scheduler for NVMe

```bash
# Persistent via udev:
sudo tee /etc/udev/rules.d/60-nvme-scheduler.rules << 'EOF'
ACTION=="add|change", KERNEL=="nvme[0-9]*", ATTR{queue/scheduler}="none"
EOF
sudo udevadm trigger --type=devices --subsystem-match=block
```

---

## 3  Storage  ⚡ *highest single impact for large recordings*

### 3.1  Migrate `/data` from NTFS to ext4

The Samsung 9100 PRO 8 TB is currently mounted as NTFS via FUSE:

```
/dev/nvme1n1p2 → /data   filesystem=ntfs   mount.fstype=fuseblk
```

Every read and write call crosses a user-kernel FUSE boundary, costing
roughly 20–30% throughput vs native ext4.  For a 9 GB `.dat` file the
read-copy-write performed by `process_subtractspikes` takes approximately:

| Filesystem | Effective sequential throughput |
|---|---|
| NTFS / fuseblk | ~3.5 GB/s |
| ext4 (native) | ~5–6 GB/s (9100 PRO rated seq. write) |

**Migration steps** (back up any Windows data on the partition first):

```bash
sudo umount /data
sudo mkfs.ext4 -L Data \
    -E lazy_itable_init=0,lazy_journal_init=0 \
    /dev/nvme1n1p2

NEW_UUID=$(sudo blkid -s UUID -o value /dev/nvme1n1p2)
# Add to /etc/fstab:
echo "UUID=${NEW_UUID}  /data  ext4  noatime,data=writeback,barrier=0  0  2" \
    | sudo tee -a /etc/fstab

sudo mount /data
```

`noatime` suppresses access-time writes on every read.  `data=writeback`
maximises write throughput (use `data=ordered` for stronger power-loss
guarantees if you later add a UPS).

### 3.2  `/data` mount options if staying on NTFS temporarily

```bash
# /etc/fstab — add big_writes and noatime to the ntfs-3g line:
UUID=4cb073e8-11ac-544d-9ff3-def03f27f171  /data  ntfs-3g  \
    noatime,big_writes,default_permissions,allow_other  0  0
```

`big_writes` allows FUSE to coalesce writes into larger kernel transfers,
recovering roughly half of the FUSE overhead for sequential I/O.

---

## 4  Build system

### 4.1  `cmake/ZenOptimizations.cmake`  *(included in this repo)*

The module applies the following to all Release/RelWithDebInfo builds:

| Flag / feature | Effect |
|---|---|
| `-march=native` | Full host ISA: AVX-512 + BF16 + VNNI on Zen 5 |
| `-ffast-math` | IEEE-754 relaxation; unlocks auto-vectorisation of reduction loops |
| `-funroll-loops` | Loop unrolling; pairs with AVX-512 for inner CEM / covariance passes |
| IPO / LTO | Cross-TU inlining for Qt GUI components and shared-library boundaries |
| `nvcc --threads 0` | Parallel device-code compilation — cuts multi-SM CUDA build time |
| `-Xptxas -dlcm=ca` | L2-cached global loads; beneficial for KlustaKwik covariance kernels |

Disable for cross-compilation targets:

```bash
cmake -B build -DNS_ZEN_OPT=OFF
```

### 4.2  GCC 14 for explicit Zen 5 targeting

GCC 13 (Ubuntu 24.04 default) supports `-march=native` but not
`-march=znver5`.  GCC 14 adds full Zen 5 scheduler parameters:

```bash
sudo add-apt-repository ppa:ubuntu-toolchain-r/test
sudo apt install gcc-14 g++-14
cmake -B build -DCMAKE_C_COMPILER=gcc-14 -DCMAKE_CXX_COMPILER=g++-14
```

With `ZenOptimizations.cmake`, `-march=native` on GCC 14 is equivalent
to `-march=znver5` on this host.

### 4.3  `process_resample` chunk size  *(already applied)*

The original code used `CHUNK_FRAMES = 262144`.
At 64 channels the per-channel float32 planes total 64 MB, pushing the
combined working set past the 96 MB V-Cache before the reinterleave phase.

The CMakeLists now sets `CHUNK_FRAMES = 131072` (32 MB at 64 channels).
Override if needed:

```bash
cmake -B build -DPROCESS_RESAMPLE_CHUNK_FRAMES=65536
```

---

## 5  `process_subtractspikes` chunk loop  *(already applied)*

The previous implementation called `memmap` writes one spike-window at a
time — hundreds of thousands of random small writes over a 2-hour session.

The rewrite uses a single sequential pass across all electrode groups:

1. Read one full-width chunk of `chunk_len` frames into a float32 buffer.
2. Accumulate subtractions from **all** groups into that buffer.
3. Clip, cast to int16, write once.
4. Chunks with no spike overlap are skipped entirely (no write).

**V-Cache sizing** — chunk length is auto-computed to fit within 70% of
96 MB, keeping the working set inside the V-Cache:

```
bytes_per_frame = nChannels × 6   (float32 + int16)
chunk_len       = floor(0.70 × 96 MB / bytes_per_frame)
```

At 64 channels: `chunk_len ≈ 183 500` frames, working set ≈ 70.5 MB.
Override with `--chunk-frames N`.

---

## 6  CUDA / Blackwell (RTX 5070 Ti, sm_120)

### 6.1  Native architecture compilation

`ZenOptimizations.cmake` sets `CMAKE_CUDA_ARCHITECTURES = native` when
`nvidia-smi` is present, generating sm_120 code only and skipping earlier
SM targets — ~75% faster CUDA build vs the default multi-arch list.

```bash
cmake -B build 2>&1 | grep -i "cuda arch"
# Expected: CUDA architectures = native (GPU detected via nvidia-smi)
```

### 6.2  Shared memory budget — 128 KB on Blackwell

The RTX 5070 Ti (GB203) provides **128 KB** shared memory per SM, double
Ampere/Ada.  The KlustaKwik MStep global-atomics fallback is therefore
never triggered on this GPU.  Verify via KlustaKwik stderr:
`MStep: shared-memory kernel` (not `global-atomics fallback`).

### 6.3  Driver and toolkit requirements

Blackwell (sm_120) requires CUDA ≥ 12.8 and driver ≥ 570:

```bash
nvidia-smi | grep "Driver Version\|CUDA Version"
nvcc --version
```

---

## 7  Python (`process_subtractspikes`)

### 7.1  NumPy BLAS backend

```bash
python3 -c "import numpy as np; np.show_config()"
# Look for openblas_info or blas_opt_info with avx/avx512 in CFLAGS
# If missing: pip install --upgrade numpy
```

### 7.2  CuPy *(optional, future)*

For sessions with > 500K spikes per group the projection loop can be
GPU-offloaded.  Install now for future `--cupy` support:

```bash
pip install cupy-cuda12x
```

---

## 8  Priority summary

| # | Action | Effort | Expected gain |
|---|---|---|---|
| 1 | Migrate `/data` to ext4 | 30 min | +20–30% I/O on large `.dat` files |
| 2 | CPU governor → performance | 2 min | Eliminates frequency-ramp stalls |
| 3 | Rebuild with ZenOpt + GCC 14 | 10 min | +10–20% CPU kernel throughput |
| 4 | Enable PBO in BIOS | 5 min | +5–10% sustained all-core clock |
| 5 | Transparent huge pages | 2 min | Reduces TLB pressure on memmaps |
| 6 | swappiness=1 | 1 min | Prevents swap interference |
| 7 | NVMe scheduler → none | 1 min | Removes unnecessary queue overhead |
