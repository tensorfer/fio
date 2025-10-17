# FIO GD2FS support

Support GD2FS engine.

# Build

./configure --enable-libgd2fs --enable-cuda

* detect libgd2fs by command: pkg-config --libs libgd2fs
* --enable-libgd2fs enables gd2fs engine
* --enable-cuda enables GPU memory

# GD2FS parameters

* ioengine=gd2fs: Indicates that FIO uses the GD2FS engine.
* gd2fs-cpaddr=ADDRESSES: "CP" stands for control plane. This comma-separated list of addresses represents the GD2FS controller addresses.
* gd2fs-dpaddr=ADDRESSES: "DP" stands for data plane. This comma-separated list of addresses is used for data access between the GD2FS client and server. This parameter configuration controls the selection of one or multiple local network cards for simultaneous use. The GD2FS server can listen to both TCP and RDMA, and the `gd2fs-dpaddr` parameter also allows selecting the transport type for data exchange.
* gd2fs-cluster=gd2fs-cluster0: Specifies the target GD2FS cluster to be accessed.
* gd2fs-io-threads=THREADS: The number of I/O threads used per FIO job. For TCP, using multiple threads may improve performance; a recommended value is 4 or 8. For RDMA, a single thread is recommended.
* gd2fs-substreams=SUBSTREAMS: The number of network substreams used per FIO job. Multiple substreams are evenly distributed across I/O threads. For TCP, using multiple substreams can enhance performance; it is recommended to set this value equal to `gd2fs-io-threads`. For RDMA, a single substream is recommended.

FIO allows specifying the type of local memory, with the default being local DDR memory. Starting from commit 035538530 ("GPUDirect RDMA support"), parameters `mem=cudamalloc` and `gpu_dev_id=ID` are supported, enabling the use of GPU memory as local memory. This requires pre-installation of the kernel driver support via `modprobe nvidia-peermem`.

It is important to note that many AI servers use a low-performance network card (e.g., 25Gbps/100Gbps) for the control plane, while multiple high-performance network cards (e.g., 200Gbps/400Gbps) are allocated to the data plane for GPU usage. However, the system’s default network route may utilize the low-performance card, which can severely impact performance. Therefore, explicitly configuring gd2fs-cpaddrand gd2fs-dpaddris necessary.

# Test

## Config example


```
[global]
ioengine=gd2fs
gd2fs-cpaddr=gd2fs://192.168.122.2:7527,gd2fs://192.168.122.6:7527,gd2fs://192.168.122.10:7527
gd2fs-dpaddr=rdma://192.168.122.100
gd2fs-cluster=gd2fs-cluster0
gd2fs-io-threads=1
gd2fs-substreams=1
direct=0
time_based=0
runtime=60
group_reporting
size=16g
#mem=cudamalloc
#gpu_dev_id=0

[write]
rw=write
filename=/testfile_16g
bs=256m
iodepth=1
numjobs=1
```

## Command

```
./fio fio.conf # or save newly compiled fio as fio-gd2fs
```
