Basic conceptual overview:

stage1 bootloader (bootsector)
    like current design, very simple - loads the remaining 8 sectors into segment 7000h, jumps to it

stage2 bootloader
    looks up the filesystem superblock/metadata, checks for the kernel, tries to load it
    no kernel found? spits out an error

LBA0        boot sector         CHS 0/0/1
LBA1        stage2 sector 1     CHS 0/0/2
...
LBA8        stage2 sector 8     CHS 0/0/9

LBA9        superblock          CHS 0/1/1
LBA10       blockchain (heh)    CHS 0/1/2
LBA11       ...                 CHS 0/1/3
LBA12       ...                 CHS 0/1/4

LBA13       reserved            CHS 0/1/5
LBA14       reserved            CHS 0/1/6
LBA15       reserved            CHS 0/1/7
LBA16       reserved            CHS 0/1/8
LBA17       reserved            CHS 0/1/9


superblock:
    char        magic[5]            "MAT16"
    uint16_t    media_type          0 for now (360 KiB floppy)
    uint16_t    block_size          fixed 512
    uint16_t    block_count         fixed 720 for 360 KiB floppy
    uint16_t    blockchain_len      length in blocks of the blockchain
    uint16_t    kernel_first_block  LBA of kernel first block
    uint16_t    kernel_block_count  how many blocks to load for the kernel - if 0, the stage2 bootloader should assume a fragmented kernel
    uint16_t    root_dir_start      LBA of root directory - if 0, no usable filesystem, but the floppy can still boot a kernel
    uint16_t    root_dir_len        how many blocks for the root directory
    uint16_t    backup_super        LBA of backup superblock (0 if it doesn't exist) - usually in one of the reserved blocks
    uint16_t    blockchain_backup   LBA of backup of blockchain (0 if it doesn't exist) - usually in one of the reserved blocks
    uint16_t    backup_rootdir      LBA of backup root directory - this is a special "emergency" root directory in one of the reserved blocks (0 if it doesn't exist)
    uint16_t    task0               if not 0, specifies the first block for task0 - use the blockchain to follow the whole thing, fragmentation is allowed here
    uint16_t    task1               if not 0, specifies the first block for task1
    uint16_t    task2               if not 0, specifies the first block for task2
    uint16_t    task3               if not 0, specifies the first block for task3
    uint16_t    checksum            very simple sum of all uint16_t values
    zero padding to end

if no tasks are specified, the kernel will try to find a file name DEFAULT.TSK for task0, if that does not exist, the user will have to manually select a task to start

backup_rootdir should contain only tools needed to do emergency tasks
an especially paranoid user might have for example two copies of a repair tool etc, or at least have the backup root directory point to the same repair tool

blockchain:
    simple linear array of uint16_t
    mapping all available blocks on the disk
    
    the first value in each blockchain sector is instead a checksum for that sector, adding up all the values stored for other blocks
    this sacrifices overall 3 blocks worth that we can't reference, but the linear array only starts from LBA18 onwards

    0x0000-0x02CF
        next block number
    0xFFFF
        free block
    0xFFFD
        reserved/system block (this should make up all blocks before LBA18)

directories are basically "files"
    every block of a directory has up to 16 entries of 32 bytes
    first entry is special:
        char        magic[4]        "DIR\0"
        uint16_t    prev_block      redundant, for repair tools to verify everything is sane
        uint16_t    next_block      as above, redundant - if these fields and the blockchain disagree, a repair tool can try to reconstruct something logical
        uint16_t    parent_start    LBA of the parent directory's first block - for the root directory, points to self
        uint16_t    parent_len      how many blocks the parent has - for the root directory, this is redundant to the next field and helps repair tools sanity check
        uint16_t    entry_count     how many actual entries are in this directory
        uint16_t    checksum        simple sum of all uint16_t values
