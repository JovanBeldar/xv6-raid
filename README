# xv6 RAID Implementation

Implementation of RAID structures in the xv6 operating system using multiple disks to provide improved performance and fault tolerance through redundancy.

This project extends xv6 with support for RAID-based logical storage devices while preserving all original operating system functionality. The implementation introduces a kernel-level RAID subsystem, a system call interface for user-space interaction, and synchronization mechanisms for safe concurrent access.

## Overview

RAID (Redundant Array of Independent Disks) combines multiple physical disks into a single logical storage device. Depending on the selected RAID variant, the system can provide:

* Improved read and/or write performance
* Increased storage capacity
* Fault tolerance through data redundancy
* Recovery from disk failures

The following RAID variants are implemented:

* RAID0
* RAID1
* RAID0+1
* RAID4
* RAID5

The RAID subsystem operates independently of the rest of xv6. All functionality present in the original operating system remains unchanged and continues to work as expected.

## Development Environment

The project was developed using the C programming language.

The xv6 operating system executes inside an emulator running on a Linux x64 host system. The complete development environment, including the virtual machine image, emulator, and supporting tools, is available through the Faculty of Electrical Engineering, University of Belgrade operating systems course website:

http://os.etf.bg.ac.rs/OS2/projekat.htm

## Disk Organization

The number of RAID disks and their size can be configured through the Makefile.

Constraints:

* Maximum number of disks: 8
* All RAID disks have identical capacity
* One disk is reserved for storing user programs
* All remaining disks participate in RAID structures

The active RAID configuration is stored persistently inside the last block of the first disk. During system startup, this information is used to reconstruct the RAID subsystem state.

## Low-Level Disk Access

The implementation relies on the following functions for block-level disk access:

```c
void write_block(int diskn, int blkn, uchar* data);
void read_block(int diskn, int blkn, uchar* data);
```

Declarations are located in `defs.h`, while implementations are located in `virtio_disk.c`.

These functions perform block transfers between memory and disks using interrupts. During a transfer, other interrupts remain enabled and may be serviced normally. However, the calling thread blocks until the transfer completes.

## System Call Interface

User-space applications interact with the RAID subsystem exclusively through system calls.

### RAID Type

```c
enum RAID_TYPE {
    RAID0,
    RAID1,
    RAID0_1,
    RAID4,
    RAID5
};
```

### Initialize RAID

```c
int init_raid(enum RAID_TYPE raid);
```

Initializes the RAID subsystem using the specified RAID variant and stores configuration metadata on disk.

### Read Block

```c
int read_raid(int blkn, uchar* data);
```

Reads a logical block from the RAID structure into memory.

### Write Block

```c
int write_raid(int blkn, uchar* data);
```

Writes a logical block from memory into the RAID structure.

### Simulate Disk Failure

```c
int disk_fail_raid(int diskn);
```

Marks a disk as failed. The disk becomes unavailable for future operations. If the selected RAID level supports redundancy, data remains accessible and may be reconstructed from surviving disks.

### Repair Disk

```c
int disk_repaired_raid(int diskn);
```

Marks a previously failed disk as operational. When supported by the RAID level, lost data is automatically reconstructed and restored.

### RAID Information

```c
int info_raid(uint *blkn, uint *blks, uint *diskn);
```

Returns information about maximum logical block count, including block size and number of disks.

### Destroy RAID

```c
int destroy_raid();
```

Destroys the RAID structure and removes RAID metadata. Data stored within the RAID subsystem becomes inaccessible after destruction.

## Synchronization

The RAID subsystem supports concurrent access from multiple processes.

Synchronization is implemented using xv6 sleeplocks and follows the classic readers-writers model.

Reader operations:

* `read_raid()`
* `info_raid()`

Writer operations:

* `init_raid()`
* `write_raid()`
* `disk_fail_raid()`
* `disk_repaired_raid()`
* `destroy_raid()`

Design goals:

* Multiple readers may access the RAID subsystem concurrently.
* At most one writer may modify RAID state at any time.
* Readers and writers are mutually exclusive.
* Starvation prevention mechanisms are implemented to ensure fairness between readers and writers.

This approach improves parallelism while maintaining consistency of RAID metadata and stored data.

## Testing

A separate user-space test application is provided.

The application is compiled independently as a console program and communicates with the operating system exclusively through the RAID system call interface.

The test application uses the `fork()` system call to create multiple concurrent processes and validate both RAID functionality and synchronization behavior.

## Features

* Support for RAID0, RAID1, RAID0+1, RAID4, and RAID5
* Block-level logical disk abstraction
* Persistent RAID metadata
* Disk failure simulation and recovery
* Automatic data reconstruction when supported
* Concurrent access synchronization
* Reader-writer fairness
* Full compatibility with existing xv6 functionality

## What Was Implemented

This project was developed as an extension of the xv6 educational operating system. The original xv6 codebase was provided as part of the course infrastructure.
The RAID subsystem, system call interface, synchronization mechanisms, recovery logic, and testing code were implemented as part of this project.

## Key Technical Challenges

### RAID configuration and edge cases

Designed support for RAID0 and RAID0+1 configurations with both even and odd numbers of disks. For odd disk counts, the final disk pair is formed by mirroring the last available disk, allowing the RAID structure to remain functional without imposing additional configuration restrictions.

### RAID4/RAID5 parity management

Implemented parity generation and update algorithms for RAID4 and RAID5, ensuring data consistency during writes while maintaining fault tolerance.

### Failure recovery

Implemented disk failure simulation and recovery mechanisms, including reconstruction of lost data using mirrored copies or parity information depending on the selected RAID level.

### Persistent metadata

Designed a persistent metadata mechanism that stores RAID configuration information on disk, allowing the RAID subsystem to be automatically reconstructed after operating system restart.

### Synchronization

Developed a readers-writers synchronization scheme using xv6 sleeplocks, allowing concurrent read operations while guaranteeing exclusive access for RAID-modifying operations.

### Fairness

Addressed starvation issues between readers and writers to ensure fair access to RAID resources under concurrent workloads.

## License

This project is based on the xv6 operating system and retains the original xv6 license included in this repository.
