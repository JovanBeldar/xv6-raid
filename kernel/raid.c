#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "spinlock.h"
#include "fs.h"

typedef enum {RAID0, RAID1, RAID0_1, RAID4, RAID5} RAID_TYPE;
RAID_TYPE raidType = 0;
int SIZE_OF_DISK = 128 * 1024 * 1024; //128MB

//first disk not used for RAID
int raidDisksCondition[VIRTIO_RAID_DISK_START + VIRTIO_RAID_DISK_END]; //0 = disk failure ; 1 = disk in function
int isDestroyed = 1; //0 = RAID is working; 1 = RAID structure is destroyed

struct {
    struct spinlock lock;

    int readers;
    int writer;
    int writers_waiting;
} rw;

void writer_entry() {
    acquire(&rw.lock);

    rw.writers_waiting++;

    while(rw.writer || rw.readers > 0)
        sleep(&rw, &rw.lock);

    rw.writers_waiting--;
    rw.writer = 1;

    release(&rw.lock);
}

void writer_exit() {
    acquire(&rw.lock);

    rw.writer = 0;

    wakeup(&rw);

    release(&rw.lock);
}

void reader_entry() {
    acquire(&rw.lock);

    while(rw.writer || rw.writers_waiting)
        sleep(&rw, &rw.lock);

    rw.readers++;

    release(&rw.lock);
}

void reader_exit() {
    acquire(&rw.lock);

    rw.readers--;

    if(rw.readers == 0)
        wakeup(&rw);

    release(&rw.lock);
}

uint64 sys_init_raid(void) {
    writer_entry();
    int typeOfRaid;
    argint(0, &typeOfRaid);
    raidType = (RAID_TYPE)typeOfRaid;
    switch (raidType) {
        case RAID0:
            if(VIRTIO_RAID_DISK_END < 2) {
                writer_exit();
                return -1;
            }
            break;
        case RAID1:
            if(VIRTIO_RAID_DISK_END < 2) {
                writer_exit();
                return -1;
            }
            break;
        case RAID0_1:
            if(VIRTIO_RAID_DISK_END < 4) {
                writer_exit();
                return -1;
            }
            break;
        case RAID4:
            if(VIRTIO_RAID_DISK_END < 3) {
                writer_exit();
                return -1;
            }
            break;
        case RAID5:
            if(VIRTIO_RAID_DISK_END < 3) {
                writer_exit();
                return -1;
            }
            break;
        default:
            writer_exit();
            return -1;
    }

    isDestroyed = 0;
    raidDisksCondition[0] = 1; //this disk does not belong in RAID structure
    for(int i = VIRTIO_RAID_DISK_START; i <= VIRTIO_RAID_DISK_END; i++) {
        raidDisksCondition[i] = 1;
    }

    uchar data[BSIZE];
    memset(data, 0, sizeof(data));

    *((int *)data) = BSIZE; //size of block
    *((int *)(data + 4)) = VIRTIO_RAID_DISK_END; // number of RAID disks
    *((int *)(data + 8)) = raidType; //RAID level
    *((int *)(data + 12)) = SIZE_OF_DISK; //size of disks
    for(int i = 0; i <= VIRTIO_RAID_DISK_END; i++) {
        *((int *)(data + 12 + i * 4)) = raidDisksCondition[i];
    }

    int lastBlock =  SIZE_OF_DISK / BSIZE - 1;
    if(raidType == RAID5 && ((SIZE_OF_DISK / BSIZE - 1) % (VIRTIO_RAID_DISK_END) == 0)) {
    	write_block(VIRTIO_RAID_DISK_START + 1, lastBlock, data);
    } else {
    	write_block(VIRTIO_RAID_DISK_START, lastBlock, data);
	}
    writer_exit();
    return 0;
}

uint64 sys_read_raid(void) {
    reader_entry();
    if(isDestroyed) {
        reader_exit();
        return -1;
    }
    int blkn;
    argint(0, &blkn);
    if(blkn < 0) {
        reader_exit();
        return -1;
    }
    uint64 dataAddr;
    argaddr(1, &dataAddr);
    uchar data[BSIZE];
    int disk;
    int blk;
    int numOfBlocksInDisk = SIZE_OF_DISK / BSIZE;
    int metadataBlock;
	int numOfDisksForData;
    int odd;
    uchar tempData[BSIZE];
    switch(raidType) {
        case RAID0:
            metadataBlock = (VIRTIO_RAID_DISK_END) * (numOfBlocksInDisk - 1);
            if(blkn >= metadataBlock) {
                blkn = blkn + 1;
            }
            disk = blkn % (VIRTIO_RAID_DISK_END) + VIRTIO_RAID_DISK_START;
            blk = blkn / (VIRTIO_RAID_DISK_END);
            if(blk >= numOfBlocksInDisk) {
                reader_exit();
                return -1;
            }
            if(raidDisksCondition[disk] == 0) {
                reader_exit();
                return -1;
            }
            read_block(disk, blk, data);
            break;
        case RAID1:
            numOfDisksForData = (VIRTIO_RAID_DISK_END) / 2;
            metadataBlock = numOfBlocksInDisk - 1;
            if(blkn >= metadataBlock) {
            	blkn = blkn + 1;
            }
			disk = blkn / numOfBlocksInDisk + VIRTIO_RAID_DISK_START;
            blk = blkn % numOfBlocksInDisk;
            if(disk > numOfDisksForData) {
                reader_exit();
            	return -1;
            }
            if(raidDisksCondition[disk] == 1) {
            	read_block(disk, blk, data);
            } else {
            	disk += numOfDisksForData;
                if(raidDisksCondition[disk] == 1) {
                	read_block(disk, blk, data);
                } else {
					odd = ((VIRTIO_RAID_DISK_END) % 2 == 0) ? 0 : 1;
                    if(odd == 1 && disk == (VIRTIO_RAID_DISK_END - 1) && raidDisksCondition[VIRTIO_RAID_DISK_END] == 1) {
                    	read_block(VIRTIO_RAID_DISK_END, blk, data);
                    } else {
                        reader_exit();
                    	return -1;
                    }
                }
            }
            break;
        case RAID0_1:
        	numOfDisksForData = (VIRTIO_RAID_DISK_END) / 2;
            metadataBlock = numOfDisksForData * (numOfBlocksInDisk - 1);
            if(blkn >= metadataBlock) {
            	blkn = blkn + 1;
            }
            disk = blkn % numOfDisksForData + VIRTIO_RAID_DISK_START;
            blk = blkn / numOfDisksForData;
            if(blk >= numOfBlocksInDisk) {
                reader_exit();
            	return -1;
            }
            if(raidDisksCondition[disk] == 1) {
				read_block(disk, blk, data);
            } else {
            	disk += numOfDisksForData;
                if(raidDisksCondition[disk] == 1) {
                	read_block(disk, blk, data);
                } else {
                	odd = ((VIRTIO_RAID_DISK_END) % 2 == 0) ? 0 : 1;
                    if(odd == 1 && disk == (VIRTIO_RAID_DISK_END - 1) && raidDisksCondition[VIRTIO_RAID_DISK_END] == 1) {
                    	read_block(VIRTIO_RAID_DISK_END, blk, data);
                    } else {
                        reader_exit();
                    	return -1;
                    }
                }
            }
            break;
        case RAID4:
            numOfDisksForData = (VIRTIO_RAID_DISK_END) - 1;
            metadataBlock = numOfDisksForData * (numOfBlocksInDisk - 1);
            if(blkn >= metadataBlock) {
                blkn = blkn + 1;
            }
            disk = blkn % numOfDisksForData + VIRTIO_RAID_DISK_START;
            blk = blkn / numOfDisksForData;
            if(blk >= numOfBlocksInDisk) {
                reader_exit();
                return -1;
            }
            if(raidDisksCondition[disk] == 0) {
                for(int i = 0; i < BSIZE; i++) {
                    data[i] = 0;
                }
                for(int i = VIRTIO_RAID_DISK_START; i <= VIRTIO_RAID_DISK_END; i++) {
                    if(i == disk) continue;
                    if(raidDisksCondition[i] == 0) {
                        reader_exit();
                        return -1;
                    }
                    read_block(i, blk, tempData);
                    for(int j = 0; j < BSIZE; j++) {
                        data[j] ^= tempData[j];
                    }
                }
            } else {
                read_block(disk, blk, data);
            }
            break;
        case RAID5:
            numOfDisksForData = (VIRTIO_RAID_DISK_END) - 1;
			metadataBlock = (numOfBlocksInDisk - 1) * numOfDisksForData;
            if(blkn >= metadataBlock) {
                blkn = blkn + 1;
            }
            disk = blkn % numOfDisksForData;
            if(disk >= ((blkn / numOfDisksForData) % VIRTIO_RAID_DISK_END)) {
                disk++;
            }
            disk += VIRTIO_RAID_DISK_START;
            blk = blkn / numOfDisksForData;
            if(blk >= numOfBlocksInDisk) {
                reader_exit();
                return -1;
            }
            if(raidDisksCondition[disk] == 1) {
                read_block(disk, blk, data);
            } else {
                for(int i = 0; i < BSIZE; i++) {
                    data[i] = 0;
                }
                for(int i = VIRTIO_RAID_DISK_START; i <= VIRTIO_RAID_DISK_END; i++) {
                    if(i == disk) continue;
                    if(raidDisksCondition[i] == 0) {
                        reader_exit();
                        return -1;
                    }
                    read_block(i, blk, tempData);
                    for(int j = 0; j < BSIZE; j++) {
                        data[j] ^= tempData[j];
                    }
                }
            }
            break;
    }
    either_copyout(1, dataAddr, data, sizeof(data));
    reader_exit();
    return 0;
}

uint64 sys_write_raid(void) {
    writer_entry();
    if(isDestroyed) {
        writer_exit();
        return -1;
    }
    int blkn;
    argint(0, &blkn);
    if(blkn < 0) {
        writer_exit();
        return -1;
    }
    uchar data[BSIZE];
    uint64 dataAddr;
    argaddr(1, &dataAddr);
    either_copyin(data, 1, dataAddr, sizeof(data));
    int disk;
    int blk;
    int numOfBlocksInDisk = SIZE_OF_DISK / BSIZE;
    int metadataBlock;
    int numOfDisksForData;
    int odd;
    int errorCounter;
    int failedDisk;
    uchar data2[BSIZE];
    switch(raidType) {
        case RAID0:
            metadataBlock = (VIRTIO_RAID_DISK_END) * (numOfBlocksInDisk - 1);
            if(blkn >= metadataBlock) {
                blkn = blkn + 1;
            }
            disk = blkn % (VIRTIO_RAID_DISK_END) + VIRTIO_RAID_DISK_START;
            blk = blkn / (VIRTIO_RAID_DISK_END);
            if(blk >= numOfBlocksInDisk) {
                writer_exit();
                return -1;
            }
            if(raidDisksCondition[disk] == 0) {
                writer_exit();
                return -1;
            }
            write_block(disk, blk, data);
            break;
        case RAID1:
        	numOfDisksForData = (VIRTIO_RAID_DISK_END) / 2;
        	metadataBlock = numOfBlocksInDisk - 1;
            if(blkn >= metadataBlock) {
              blkn = blkn + 1;
            }
            disk = blkn / numOfBlocksInDisk + VIRTIO_RAID_DISK_START;
            blk = blkn % numOfBlocksInDisk;
            if(disk > numOfDisksForData) {
                writer_exit();
            	return -1;
            }
            if(raidDisksCondition[disk] == 1) {
            	write_block(disk, blk, data);
            }
            disk += numOfDisksForData;
            if(raidDisksCondition[disk] == 1) {
            	write_block(disk, blk, data);
            }
			odd = ((VIRTIO_RAID_DISK_END) % 2 == 0) ? 0 : 1;
            if(odd == 1 && disk == (VIRTIO_RAID_DISK_END - 1) && raidDisksCondition[VIRTIO_RAID_DISK_END] == 1) {
            	write_block(VIRTIO_RAID_DISK_END, blk, data);
            }
            if(odd == 1) {
            	if(raidDisksCondition[disk] == 0 && raidDisksCondition[VIRTIO_RAID_DISK_END] == 0 && raidDisksCondition[disk - numOfDisksForData] == 0) {
            	    writer_exit();
                	return -1;
            	}
            } else {
            	if(raidDisksCondition[disk] == 0 && raidDisksCondition[disk - numOfDisksForData] == 0) {
            	    writer_exit();
                	return -1;
            	}
            }
            break;
        case RAID0_1:
        	numOfDisksForData = (VIRTIO_RAID_DISK_END) / 2;
            metadataBlock = numOfDisksForData * (numOfBlocksInDisk - 1);
            if(blkn >= metadataBlock) {
            	blkn = blkn + 1;
            }
            disk = blkn % numOfDisksForData + VIRTIO_RAID_DISK_START;
            blk = blkn / numOfDisksForData;
            if(blk >= numOfBlocksInDisk) {
                writer_exit();
            	return -1;
            }
            if(raidDisksCondition[disk] == 1) {
            	write_block(disk, blk, data);
            }
            disk += numOfDisksForData;
            if(raidDisksCondition[disk] == 1) {
            	write_block(disk, blk, data);
            }
            odd = ((VIRTIO_RAID_DISK_END) % 2 == 0) ? 0 : 1;
            if(odd == 1) {
            	if(raidDisksCondition[VIRTIO_RAID_DISK_END] == 1 && disk == (VIRTIO_RAID_DISK_END - 1)) {
                	write_block(VIRTIO_RAID_DISK_END, blk, data);
            	}
                if(raidDisksCondition[VIRTIO_RAID_DISK_END] == 0 && raidDisksCondition[disk] == 0 && raidDisksCondition[disk - numOfDisksForData] == 0) {
                    writer_exit();
                	return -1;
                }
            } else {
            	if(raidDisksCondition[disk] == 0 && raidDisksCondition[disk - numOfDisksForData] == 0) {
            	    writer_exit();
                	return -1;
            	}
            }
            break;
        case RAID4:
            numOfDisksForData = (VIRTIO_RAID_DISK_END) - 1;
            metadataBlock = numOfDisksForData * (numOfBlocksInDisk - 1);
            if(blkn >= metadataBlock) {
                blkn = blkn + 1;
            }
            disk = blkn % numOfDisksForData + VIRTIO_RAID_DISK_START;
            blk = blkn / numOfDisksForData;
            if(blk >= numOfBlocksInDisk) {
                writer_exit();
                return -1;
            }
            if(raidDisksCondition[disk] == 0) {
                writer_exit();
                return -1;
            }
            errorCounter = 0;
            for(int i = VIRTIO_RAID_DISK_START; i <= VIRTIO_RAID_DISK_END; i++) {
                if(raidDisksCondition[i] == 0) {
                    errorCounter++;
                }
            }
            if(raidDisksCondition[VIRTIO_RAID_DISK_END] == 0) {
                writer_exit();
                return -1;
            }
            if(errorCounter > 1) {
                writer_exit();
                return -1;
            }
            for(int i = 0; i < BSIZE; i++) {
                data2[i] = 0;
            }
            if(errorCounter == 1) {
                for(int i = VIRTIO_RAID_DISK_START; i < VIRTIO_RAID_DISK_END; i++) {
                    if(raidDisksCondition[i] == 0) {
                        failedDisk = i;
                        break;
                    }
                }
                for(int i = VIRTIO_RAID_DISK_START; i <= VIRTIO_RAID_DISK_END; i++) {
                    if(i == failedDisk) continue;
                    read_block(i, blk, data);
                    for(int j = 0; j < BSIZE; j++) {
                        data2[j] ^= data[j];
                    }
                }
            }
            either_copyin(data, 1, dataAddr, sizeof(data));
            write_block(disk, blk, data);
            for(int i = VIRTIO_RAID_DISK_START; i < VIRTIO_RAID_DISK_END; i++) {
                if(raidDisksCondition[i] == 1) {
                    read_block(i, blk, data);
                    for(int j = 0; j < BSIZE; j++) {
                        data2[j] ^= data[j];
                    }
                }
            }
            write_block(VIRTIO_RAID_DISK_END, blk, data2);
            break;
        case RAID5:
            numOfDisksForData = (VIRTIO_RAID_DISK_END) - 1;
			metadataBlock = numOfDisksForData * (numOfBlocksInDisk - 1);
            if(blkn >= metadataBlock) {
                blkn = blkn + 1;
            }
            disk = blkn % numOfDisksForData;
            if(disk >= ((blkn / numOfDisksForData) % VIRTIO_RAID_DISK_END)) {
                disk++;
            }
            disk += VIRTIO_RAID_DISK_START;
            blk = blkn / numOfDisksForData;
            if(blk >= numOfBlocksInDisk) {
                writer_exit();
                return -1;
            }
            if(raidDisksCondition[disk] == 0) {
                writer_exit();
                return -1;
            }
            errorCounter = 0;
            for(int i = VIRTIO_RAID_DISK_START; i <= VIRTIO_RAID_DISK_END; i++) {
                if(raidDisksCondition[i] == 0) {
                    errorCounter++;
                }
            }
            if(raidDisksCondition[((blkn / numOfDisksForData) % VIRTIO_RAID_DISK_END) + VIRTIO_RAID_DISK_START] == 0) {
                writer_exit();
                return -1;
            }
            if(errorCounter > 1) {
                writer_exit();
                return -1;
            }
            for(int i = 0; i < BSIZE; i++) {
                data2[i] = 0;
            }
            if(errorCounter == 1) {
                for(int i = VIRTIO_RAID_DISK_START; i <= VIRTIO_RAID_DISK_END; i++) {
                    if(raidDisksCondition[i] == 0) {
                        failedDisk = i;
                        break;
                    }
                }
                for(int i = VIRTIO_RAID_DISK_START; i <= VIRTIO_RAID_DISK_END; i++) {
                    if(i == failedDisk) continue;
                    read_block(i, blk, data);
                    for(int j = 0; j < BSIZE; j++) {
                        data2[j] ^= data[j];
                    }
                }
            }
            either_copyin(data, 1, dataAddr, sizeof(data));
            write_block(disk, blk, data);
            for(int i = VIRTIO_RAID_DISK_START; i <= VIRTIO_RAID_DISK_END; i++) {
                if(i == (((blkn / numOfDisksForData) % VIRTIO_RAID_DISK_END) + VIRTIO_RAID_DISK_START)) continue;
                if(raidDisksCondition[i] == 1) {
                    read_block(i, blk, data);
                    for(int j = 0; j < BSIZE; j++) {
                        data2[j] ^= data[j];
                    }
                }
            }
            write_block(((blkn / numOfDisksForData) % VIRTIO_RAID_DISK_END + VIRTIO_RAID_DISK_START), blk, data2);
            break;
    }
    writer_exit();
    return 0;
}

uint64 sys_disk_fail_raid(void) {
    writer_entry();
    if(isDestroyed == 1) {
        writer_exit();
        return -1;
    }
    int diskn;
    argint(0, &diskn);
    if(diskn < VIRTIO_RAID_DISK_START || diskn > VIRTIO_RAID_DISK_END) {
        writer_exit();
        return -1;
    }
    raidDisksCondition[diskn] = 0;
    writer_exit();
    return 0;
}

uint64 sys_disk_repaired_raid(void) {
    writer_entry();
    if(isDestroyed == 1) {
        writer_exit();
        return -1;
    }
    int diskn;
    argint(0, &diskn);
    if(diskn < VIRTIO_RAID_DISK_START || diskn >= VIRTIO_RAID_DISK_END) {
        writer_exit();
        return -1;
    }
    if(raidDisksCondition[diskn] == 1) {
        writer_exit();
        return -1;
    }
    raidDisksCondition[diskn] = 1;
    int numOfDisksForData;
    int diskForRepair;
    int odd;
    int numOfBlocksInDisk = SIZE_OF_DISK / BSIZE;
    uchar data[BSIZE];
    uchar tempData[BSIZE];
    switch (raidType) {
        case RAID0:
            writer_exit();
            return -1;
            break;
        case RAID1:
            numOfDisksForData = (VIRTIO_RAID_DISK_END) / 2;
            if(diskn > numOfDisksForData) {
                diskForRepair = diskn - numOfDisksForData;
            } else {
                diskForRepair = diskn + numOfDisksForData;
            }
            odd = ((VIRTIO_RAID_DISK_END) % 2 == 0) ? 0 : 1;
            if(odd == 1 && diskn == (VIRTIO_RAID_DISK_END)) {
                diskForRepair = VIRTIO_RAID_DISK_END - 1;
                if(raidDisksCondition[VIRTIO_RAID_DISK_END - 1] == 0) {
                    diskForRepair = VIRTIO_RAID_DISK_END - 1 - numOfDisksForData;
                    if(raidDisksCondition[diskForRepair] == 0) {
                        writer_exit();
                        return -1;
                    }
                }
            }
            if(raidDisksCondition[diskForRepair] == 0) {
                writer_exit();
                return -1;
            }
            for(int i = 0; i < numOfBlocksInDisk; i++) {
                read_block(diskForRepair, i, data);
                write_block(diskn, i, data);
            }
            break;
        case RAID0_1:
            numOfDisksForData = (VIRTIO_RAID_DISK_END) / 2;
            if(diskn > numOfDisksForData) {
                diskForRepair = diskn - numOfDisksForData;
            } else {
                diskForRepair = diskn + numOfDisksForData;
            }
            odd = ((VIRTIO_RAID_DISK_END) % 2 == 0) ? 0 : 1;
            if(odd == 1 && diskn == (VIRTIO_RAID_DISK_END )) {
                diskForRepair = VIRTIO_RAID_DISK_END - 1;
                if(raidDisksCondition[VIRTIO_RAID_DISK_END - 1] == 0) {
                    diskForRepair = VIRTIO_RAID_DISK_END - 1 - numOfDisksForData;
                    if(raidDisksCondition[diskForRepair] == 0) {
                        writer_exit();
                        return -1;
                    }
                }
            }
            if(raidDisksCondition[diskForRepair] == 0) {
                writer_exit();
                return -1;
            }
            for(int i = 0; i < numOfBlocksInDisk; i++) {
                read_block(diskForRepair, i, data);
                write_block(diskn, i, data);
            }
            break;
        case RAID4:
            for(int i = VIRTIO_RAID_DISK_START; i <= VIRTIO_RAID_DISK_END; i++) {
                if(raidDisksCondition[i] == 0) {
                    writer_exit();
                    return -1;
                }
            }
            for(int i = 0; i < numOfBlocksInDisk; i++) {
                for(int j = 0; j < BSIZE; j++) {
                    data[j] = 0;
                }
                for(int j = VIRTIO_RAID_DISK_START; j <= VIRTIO_RAID_DISK_END; j++) {
                    if(j == diskn) continue;
                    read_block(j, i, tempData);
                    for(int k = 0; k < BSIZE; k++) {
                        data[k] ^= tempData[k];
                    }
                }
                write_block(diskn, i, data);
            }
            break;
        case RAID5:
            for(int i = VIRTIO_RAID_DISK_START; i <= VIRTIO_RAID_DISK_END; i++) {
                if(raidDisksCondition[i] == 0) {
                    writer_exit();
                    return -1;
                }
            }
            for(int i = 0; i < numOfBlocksInDisk; i++) {
                for(int j = 0; j < BSIZE; j++) {
                    data[j] = 0;
                }
                for(int j = VIRTIO_RAID_DISK_START; j <= VIRTIO_RAID_DISK_END; j++) {
                    if(j == diskn) continue;
                    read_block(j, i, tempData);
                    for(int k = 0; k < BSIZE; k++) {
                        data[k] ^= tempData[k];
                    }
                }
                write_block(diskn, i, data);
            }
            break;
    }
    writer_exit();
    return 0;
}

uint64 sys_info_raid(void) {
    reader_entry();
    if(isDestroyed == 1) {
        reader_exit();
        return -1;
    }
    uint64 blknArg;
    argaddr(0, &blknArg);
    uint64 blksArg;
    argaddr(1, &blksArg);
    uint64 disknArg;
    argaddr(2, &disknArg);
    uint* blkn = (uint*)kalloc();
    uint* blks = (uint*)kalloc();
    uint* diskn = (uint*)kalloc();
    int numOfDisksForData;
    int numOfBlocksInDisk = SIZE_OF_DISK / BSIZE;
    switch (raidType) {
        case RAID0:
            numOfDisksForData = VIRTIO_RAID_DISK_END;
            break;
        case RAID1:
            numOfDisksForData = (VIRTIO_RAID_DISK_END) / 2;
            break;
        case RAID0_1:
            numOfDisksForData = (VIRTIO_RAID_DISK_END) / 2;
            break;
        case RAID4:
            numOfDisksForData = (VIRTIO_RAID_DISK_END) - 1;
            break;
        case RAID5:
            numOfDisksForData = (VIRTIO_RAID_DISK_END) - 1;
            break;
    }
    *blkn = numOfDisksForData * numOfBlocksInDisk;
    *blks = BSIZE;
    *diskn = VIRTIO_RAID_DISK_END;
    either_copyout(1, blknArg, blkn, 4);
    either_copyout(1, blksArg, blks, 4);
    either_copyout(1, disknArg, diskn, 4);
    reader_exit();
    return 0;
}

uint64 sys_destroy_raid(void) {
    writer_entry();
    if(isDestroyed == 1) {
        writer_exit();
        return -1;
    }
    isDestroyed = 1;
    for(int i = VIRTIO_RAID_DISK_START; i < VIRTIO_RAID_DISK_END; i++) {
        raidDisksCondition[i] = 0;
    }
    writer_exit();
    return 0;
}