This is intentionally simple and crappy, it is NOT meant to be a good filesystem, it is meant to be simple to read after generating ahead of time

Block size is 512 (to match sectors)
Blocks are referenced using LBA

LBA0 is the superblock:
    5 bytes magic
        MAT16
    3 bytes padding (zero bytes, so the above magic is padded out to 8 bytes)
    uint16_t dir_start
        specifies what LBA the directory starts at
    uint16_t dir_sectors
        how many sectors the directory takes up
    rest padded out to end of disk

From dir_start up to dir_start+dir_sectors is just the root directory sectors

Directory format is a simple array of entries, 16 entries per sector, 32 bytes per entry
    uint16_t start_lba
    uint16_t end_lba
    uint16_t size
    char[26] name

    end_lba is exclusive, the file occupies sectors [start_lba,end_lba]

    name[0]=='\0' is for unused files

all uint16_t fields are little endian
