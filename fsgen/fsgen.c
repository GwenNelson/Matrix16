#define _POSIX_C_SOURCE 200809L

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* MATFS 360 KiB floppy image:
   sector 0      boot sector
   sectors 1-8   stage 2
   sector 9      superblock
   sectors 10-12 blockchain, with one checksum word per sector
   sectors 13-17 reserved; sector 13 holds the optional emergency root
   sectors 18-719 file data, numbered 0-701 in the blockchain

   Usage: fsgen IMAGE --bootsector PATH --stage2 PATH --kernel PATH
          [--task0 NAME=PATH ... --task3 NAME=PATH]
          [--file NAME=PATH ...] [--backup-root-file NAME ...]
          [--backup-copy-data]
 */
enum {
    SECTOR_SIZE = 512,
    DISK_SECTORS = 720,
    STAGE2_SECTORS = 8,
    SUPER_LBA = 9,
    CHAIN_LBA = 10,
    CHAIN_SECTORS = 3,
    BACKUP_ROOT_LBA = 13,
    DATA_LBA = 18,
    DATA_BLOCKS = DISK_SECTORS - DATA_LBA,
    CHAIN_ENTRIES_PER_SECTOR = 255,
    DIRECTORY_ENTRY_SIZE = 32,
    DIRECTORY_ENTRIES_PER_SECTOR = 15,
    MAX_BACKUP_FILES = 15,
    MAX_KERNEL_BLOCKS = 128
};

enum {
    CHAIN_END = 0xf00f,
    CHAIN_FREE = 0xffff
};

struct file {
    char *name;
    char *path;
    size_t size;
    uint16_t blocks;
    /* Directory and superblock pointers use absolute LBAs. */
    uint16_t first_lba;
};

struct backup_file {
    const struct file *original;
    uint16_t first_lba;
};

struct options {
    const char *image_path;
    const char *boot_path;
    const char *stage2_path;
    const char *kernel_path;
    struct file *files;
    size_t file_count;
    size_t task_index[4];
    char *backup_names[MAX_BACKUP_FILES];
    size_t backup_count;
    int copy_backup_data;
};

/* A 360 KiB image is small enough to assemble in memory before writing it. */
static unsigned char image[DISK_SECTORS * SECTOR_SIZE];
/* This array is compact; the on-disk table has three checksum-word gaps. */
static uint16_t chain[DATA_BLOCKS];
static uint16_t next_free_block;

static void fail(const char *reason, const char *detail)
{
    if (detail != NULL)
        fprintf(stderr, "fsgen: %s: %s\n", reason, detail);
    else
        fprintf(stderr, "fsgen: %s\n", reason);
    exit(EXIT_FAILURE);
}

static void put_u16(unsigned char *p, uint16_t value)
{
    p[0] = (unsigned char)value;
    p[1] = (unsigned char)(value >> 8);
}

static uint16_t get_u16(const unsigned char *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

/* The checksum word is omitted; all other words are summed modulo 65536. */
static uint16_t checksum(const unsigned char *sector, size_t first_offset,
                         size_t last_offset, size_t skip_offset)
{
    uint32_t sum = 0;
    size_t offset;

    for (offset = first_offset; offset + 1 < last_offset; offset += 2) {
        if (offset != skip_offset)
            sum += get_u16(sector + offset);
    }
    return (uint16_t)sum;
}

static unsigned char *sector_at(uint16_t lba)
{
    return image + (size_t)lba * SECTOR_SIZE;
}

static char *copy_string(const char *value)
{
    char *copy = strdup(value);
    if (copy == NULL)
        fail("out of memory", NULL);
    return copy;
}

static size_t input_size(const char *path)
{
    struct stat st;

    if (stat(path, &st) != 0)
        fail("cannot inspect input", path);
    if (!S_ISREG(st.st_mode) || st.st_size < 0)
        fail("input is not a regular file", path);
    if ((uintmax_t)st.st_size > (uintmax_t)SIZE_MAX)
        fail("input is too large for this host", path);
    return (size_t)st.st_size;
}

static uint16_t block_count(size_t size)
{
    size_t count = size / SECTOR_SIZE + (size % SECTOR_SIZE != 0);
    if (count > DATA_BLOCKS)
        fail("file does not fit in the data area", NULL);
    return (uint16_t)count;
}

static void init_file(struct file *file, const char *name, const char *path)
{
    if (name[0] == '\0' || strlen(name) >= 26)
        fail("filename must fit in 25 characters", name);
    file->name = copy_string(name);
    file->path = copy_string(path);
    file->size = input_size(path);
    file->blocks = block_count(file->size);
    file->first_lba = 0;
}

static void add_file(struct options *options, const char *argument, int task)
{
    const char *equals = strchr(argument, '=');
    struct file *grown;
    char *name;
    size_t name_length;
    size_t index;

    if (equals == NULL || equals == argument || equals[1] == '\0')
        fail("expected NAME=PATH", argument);
    name_length = (size_t)(equals - argument);
    if (name_length >= 26)
        fail("filename must fit in 25 characters", argument);

    name = malloc(name_length + 1);
    if (name == NULL)
        fail("out of memory", NULL);
    memcpy(name, argument, name_length);
    name[name_length] = '\0';

    for (index = 0; index < options->file_count; index++) {
        if (strcmp(options->files[index].name, name) == 0)
            fail("duplicate filesystem name", name);
    }
    if (strcmp(name, "KERNEL.BIN") == 0)
        fail("KERNEL.BIN is reserved for the kernel", name);

    grown = realloc(options->files,
                    (options->file_count + 1) * sizeof(*options->files));
    if (grown == NULL)
        fail("out of memory", NULL);
    options->files = grown;
    index = options->file_count++;
    init_file(&options->files[index], name, equals + 1);
    if (task >= 0) {
        if (options->files[index].blocks == 0)
            fail("task slot cannot point to an empty file", name);
        options->task_index[task] = index;
    }
    free(name);
}

static void parse_options(struct options *options, int argc, char **argv)
{
    int i;

    memset(options, 0, sizeof(*options));
    for (i = 0; i < 4; i++)
        options->task_index[i] = SIZE_MAX;

    if (argc < 2)
        fail("usage: fsgen IMAGE --bootsector PATH --stage2 PATH --kernel PATH [options]", NULL);
    options->image_path = argv[1];

    for (i = 2; i < argc; i++) {
        const char *option = argv[i];
        const char *value;

        if (strcmp(option, "--backup-copy-data") == 0 ||
            strcmp(option, "--copy-backup-data") == 0) {
            options->copy_backup_data = 1;
            continue;
        }
        if (i + 1 >= argc)
            fail("missing option value", option);
        value = argv[++i];

        if (strcmp(option, "--bootsector") == 0) {
            if (options->boot_path != NULL)
                fail("duplicate option", option);
            options->boot_path = value;
        } else if (strcmp(option, "--stage2") == 0) {
            if (options->stage2_path != NULL)
                fail("duplicate option", option);
            options->stage2_path = value;
        } else if (strcmp(option, "--kernel") == 0) {
            if (options->kernel_path != NULL)
                fail("duplicate option", option);
            options->kernel_path = value;
        } else if (strcmp(option, "--file") == 0) {
            add_file(options, value, -1);
        } else if (strcmp(option, "--backup-root-file") == 0) {
            if (options->backup_count == MAX_BACKUP_FILES)
                fail("backup root holds at most 15 files", NULL);
            options->backup_names[options->backup_count++] = copy_string(value);
        } else if (strlen(option) == 7 &&
                   strncmp(option, "--task", 6) == 0 &&
                   option[6] >= '0' && option[6] <= '3') {
            int task = option[6] - '0';
            if (options->task_index[task] != SIZE_MAX)
                fail("duplicate task option", option);
            add_file(options, value, task);
        } else {
            fail("unknown option", option);
        }
    }

    if (options->boot_path == NULL || options->stage2_path == NULL ||
        options->kernel_path == NULL)
        fail("--bootsector, --stage2, and --kernel are required", NULL);
    if (input_size(options->boot_path) != SECTOR_SIZE)
        fail("boot sector must be exactly 512 bytes", options->boot_path);
    if (input_size(options->stage2_path) == 0 ||
        input_size(options->stage2_path) > STAGE2_SECTORS * SECTOR_SIZE)
        fail("stage 2 must fit in eight sectors", options->stage2_path);
}

static void read_exact(const char *path, unsigned char *destination, size_t size)
{
    FILE *input = fopen(path, "rb");

    if (input == NULL)
        fail("cannot open input", path);
    if (size != 0 && fread(destination, 1, size, input) != size)
        fail("short read from input", path);
    if (fgetc(input) != EOF || ferror(input))
        fail("input changed while being read", path);
    if (fclose(input) != 0)
        fail("cannot close input", path);
}

/* Allocate consecutive data blocks; every allocated block gets a chain entry. */
static uint16_t allocate_blocks(uint16_t count, const char *name)
{
    uint16_t first = next_free_block;
    uint16_t i;

    if ((size_t)next_free_block + count > DATA_BLOCKS)
        fail("data area is full", name);
    for (i = 0; i < count; i++) {
        uint16_t block = (uint16_t)(first + i);
        chain[block] = (i + 1 == count) ? CHAIN_END : (uint16_t)(block + 1);
    }
    next_free_block = (uint16_t)(next_free_block + count);
    return first;
}

static uint16_t allocate_file(struct file *file)
{
    uint16_t first;

    if (file->blocks == 0) {
        file->first_lba = 0;
        return 0;
    }
    first = allocate_blocks(file->blocks, file->name);
    file->first_lba = (uint16_t)(DATA_LBA + first);
    return first;
}

static void write_file_data(const struct file *file, uint16_t first)
{
    FILE *input = fopen(file->path, "rb");
    unsigned char buffer[SECTOR_SIZE];
    size_t remaining = file->size;
    uint16_t i;

    if (input == NULL)
        fail("cannot open input", file->path);
    for (i = 0; i < file->blocks; i++) {
        size_t amount = remaining < SECTOR_SIZE ? remaining : SECTOR_SIZE;
        memset(buffer, 0, sizeof(buffer));
        if (amount != 0 && fread(buffer, 1, amount, input) != amount)
            fail("short read from input", file->path);
        memcpy(sector_at((uint16_t)(DATA_LBA + first + i)), buffer,
               SECTOR_SIZE);
        remaining -= amount;
    }
    if (fgetc(input) != EOF || ferror(input))
        fail("input changed while being read", file->path);
    if (fclose(input) != 0)
        fail("cannot close input", file->path);
}

static uint16_t directory_size(const struct file *file)
{
    /* 0xffff means that the directory does not supply the exact byte size. */
    return file->size >= 0xffff ? 0xffff : (uint16_t)file->size;
}

static void write_directory_entry(unsigned char *entry,
                                  const struct file *file,
                                  uint16_t first_lba)
{
    put_u16(entry, first_lba);
    put_u16(entry + 2, file->blocks);
    put_u16(entry + 4, directory_size(file));
    memcpy(entry + 6, file->name, strlen(file->name));
}

/* prev/next are repair hints; the blockchain remains the normal chain. */
static void write_directory_header(unsigned char *sector, uint16_t previous,
                                   uint16_t next, uint16_t parent,
                                   uint16_t parent_blocks,
                                   uint16_t total_entries)
{
    memcpy(sector, "DIR\0", 4);
    put_u16(sector + 4, previous);
    put_u16(sector + 6, next);
    put_u16(sector + 8, parent);
    put_u16(sector + 10, parent_blocks);
    put_u16(sector + 12, total_entries);
    /* Computed after entries are written, over the whole directory sector. */
}

static uint16_t write_primary_directory(const struct file *kernel,
                                        const struct options *options,
                                        uint16_t *blocks_out)
{
    size_t entry_count = options->file_count + 1;
    size_t required = entry_count / DIRECTORY_ENTRIES_PER_SECTOR +
                      (entry_count % DIRECTORY_ENTRIES_PER_SECTOR != 0);
    uint16_t blocks;
    uint16_t first;
    uint16_t first_lba;
    size_t i;

    if (required > DATA_BLOCKS)
        fail("root directory does not fit in the data area", NULL);
    blocks = (uint16_t)required;
    first = allocate_blocks(blocks, "root directory");
    first_lba = (uint16_t)(DATA_LBA + first);

    for (i = 0; i < blocks; i++) {
        uint16_t lba = (uint16_t)(first_lba + i);
        uint16_t previous = i == 0 ? first_lba : (uint16_t)(lba - 1);
        uint16_t next = i + 1 == blocks ? CHAIN_END : (uint16_t)(lba + 1);
        write_directory_header(sector_at(lba), previous, next, first_lba,
                               blocks, (uint16_t)entry_count);
    }

    for (i = 0; i < entry_count; i++) {
        const struct file *file = i == 0 ? kernel : &options->files[i - 1];
        unsigned char *sector = sector_at((uint16_t)(first_lba + i / 15));
        unsigned char *entry = sector + DIRECTORY_ENTRY_SIZE * (1 + i % 15);
        write_directory_entry(entry, file, file->first_lba);
    }
    for (i = 0; i < blocks; i++) {
        unsigned char *sector = sector_at((uint16_t)(first_lba + i));
        put_u16(sector + 14, checksum(sector, 0, SECTOR_SIZE, 14));
    }
    *blocks_out = blocks;
    return first_lba;
}

static const struct file *find_file(const struct options *options,
                                    const char *name)
{
    size_t i;
    for (i = 0; i < options->file_count; i++) {
        if (strcmp(options->files[i].name, name) == 0)
            return &options->files[i];
    }
    fail("backup file is not in the primary directory", name);
    return NULL;
}

static void prepare_backups(const struct options *options,
                            struct backup_file backups[MAX_BACKUP_FILES])
{
    size_t i, j;

    for (i = 0; i < options->backup_count; i++) {
        const char *name = options->backup_names[i];
        backups[i].original = find_file(options, name);
        for (j = 0; j < i; j++) {
            if (strcmp(options->backup_names[j], name) == 0)
                fail("duplicate backup directory name", name);
        }
        backups[i].first_lba = backups[i].original->first_lba;
        if (options->copy_backup_data && backups[i].original->blocks != 0) {
            uint16_t first = allocate_blocks(backups[i].original->blocks, name);
            backups[i].first_lba = (uint16_t)(DATA_LBA + first);
            write_file_data(backups[i].original, first);
        }
    }
}

static void write_backup_directory(const struct backup_file *backups,
                                   size_t count)
{
    unsigned char *sector = sector_at(BACKUP_ROOT_LBA);
    size_t i;

    write_directory_header(sector, BACKUP_ROOT_LBA, BACKUP_ROOT_LBA,
                           BACKUP_ROOT_LBA, 1, (uint16_t)count);
    for (i = 0; i < count; i++) {
        unsigned char *entry = sector + DIRECTORY_ENTRY_SIZE * (i + 1);
        write_directory_entry(entry, backups[i].original,
                              backups[i].first_lba);
    }
    put_u16(sector + 14, checksum(sector, 0, SECTOR_SIZE, 14));
}

/* Each on-disk chain sector starts with a checksum, then 255 links. */
static void write_blockchain(void)
{
    uint16_t block;
    uint16_t sector_index;

    for (block = 0; block < DATA_BLOCKS; block++) {
        unsigned char *sector = sector_at((uint16_t)(CHAIN_LBA +
                            block / CHAIN_ENTRIES_PER_SECTOR));
        size_t offset = 2 * (1 + block % CHAIN_ENTRIES_PER_SECTOR);
        put_u16(sector + offset, chain[block]);
    }
    for (sector_index = 0; sector_index < CHAIN_SECTORS; sector_index++) {
        unsigned char *sector = sector_at((uint16_t)(CHAIN_LBA + sector_index));
        put_u16(sector, checksum(sector, 0, SECTOR_SIZE, 0));
    }
}

static void write_superblock(const struct file *kernel,
                             const struct options *options,
                             uint16_t root_lba, uint16_t root_blocks)
{
    unsigned char *sector = sector_at(SUPER_LBA);
    uint16_t task;

    memcpy(sector, "MAT16", 5);
    put_u16(sector + 5, 0);                  /* 360 KiB media type */
    put_u16(sector + 7, SECTOR_SIZE);
    put_u16(sector + 9, DISK_SECTORS);
    put_u16(sector + 11, CHAIN_SECTORS);     /* physical table sectors */
    put_u16(sector + 13, kernel->first_lba);
    put_u16(sector + 15, kernel->blocks);    /* nonzero: contiguous kernel */
    put_u16(sector + 17, root_lba);
    put_u16(sector + 19, root_blocks);
    /* Conventional future locations are LBA 14 and LBA 15-17. A repair tool
       may search elsewhere; this generator does not write those backups. */
    put_u16(sector + 21, 0);                 /* no backup superblock */
    put_u16(sector + 23, 0);                 /* no backup blockchain */
    put_u16(sector + 25, options->backup_count ? BACKUP_ROOT_LBA : 0);
    for (task = 0; task < 4; task++) {
        size_t index = options->task_index[task];
        uint16_t first = index == SIZE_MAX ? 0xffff :
                         options->files[index].first_lba;
        put_u16(sector + 27 + task * 2, first);
    }
    /* Packed 16-bit fields begin immediately after the five-byte magic. */
    put_u16(sector + 35, checksum(sector, 5, 37, 35));
}

static void write_output(const struct options *options)
{
    FILE *output = fopen(options->image_path, "wb");

    if (output == NULL)
        fail("cannot create image", options->image_path);
    if (fwrite(image, 1, sizeof(image), output) != sizeof(image))
        fail("cannot write image", options->image_path);
    if (fclose(output) != 0)
        fail("cannot close image", options->image_path);
}

static void free_options(struct options *options)
{
    size_t i;
    for (i = 0; i < options->file_count; i++) {
        free(options->files[i].name);
        free(options->files[i].path);
    }
    for (i = 0; i < options->backup_count; i++)
        free(options->backup_names[i]);
    free(options->files);
}

int main(int argc, char **argv)
{
    struct options options;
    struct file kernel;
    struct backup_file backups[MAX_BACKUP_FILES] = {{0}};
    uint16_t root_lba, root_blocks;
    size_t i;

    parse_options(&options, argc, argv);
    for (i = 0; i < DATA_BLOCKS; i++)
        chain[i] = CHAIN_FREE;

    read_exact(options.boot_path, sector_at(0), SECTOR_SIZE);
    if (sector_at(0)[510] != 0x55 || sector_at(0)[511] != 0xaa)
        fail("boot sector has no 0xAA55 signature", options.boot_path);
    read_exact(options.stage2_path, sector_at(1), input_size(options.stage2_path));
    init_file(&kernel, "KERNEL.BIN", options.kernel_path);
    if (kernel.blocks == 0 || kernel.blocks > MAX_KERNEL_BLOCKS)
        fail("kernel must occupy 1 to 128 blocks", kernel.path);

    write_file_data(&kernel, allocate_file(&kernel));
    for (i = 0; i < options.file_count; i++) {
        struct file *file = &options.files[i];
        uint16_t first = allocate_file(file);
        write_file_data(file, first);
    }
    prepare_backups(&options, backups);
    root_lba = write_primary_directory(&kernel, &options, &root_blocks);
    if (options.backup_count != 0)
        write_backup_directory(backups, options.backup_count);
    write_blockchain();
    write_superblock(&kernel, &options, root_lba, root_blocks);
    write_output(&options);

    printf("kernel: LBA %u, %u blocks\n", kernel.first_lba, kernel.blocks);
    for (i = 0; i < 4; i++) {
        size_t index = options.task_index[i];
        if (index != SIZE_MAX) {
            const struct file *file = &options.files[index];
            printf("task%zu: %s, LBA %u, %u blocks\n", i, file->name,
                   file->first_lba, file->blocks);
        }
    }
    for (i = 0; i < options.file_count; i++) {
        size_t task;
        for (task = 0; task < 4; task++) {
            if (options.task_index[task] == i)
                break;
        }
        if (task == 4) {
            const struct file *file = &options.files[i];
            printf("file: %s, LBA %u, %u blocks\n", file->name,
                   file->first_lba, file->blocks);
        }
    }
    printf("root directory: LBA %u, %u blocks\n", root_lba, root_blocks);
    if (options.backup_count != 0) {
        unsigned copied_blocks = 0;
        if (options.copy_backup_data) {
            for (i = 0; i < options.backup_count; i++)
                copied_blocks += backups[i].original->blocks;
        }
        printf("backup root: LBA %u, %zu files%s\n", BACKUP_ROOT_LBA,
               options.backup_count,
               options.copy_backup_data ? " (copied data)" : " (shared data)");
        printf("backup data: %u additional blocks\n", copied_blocks);
    }
    printf("data blocks used: %u / %u\n", next_free_block, DATA_BLOCKS);

    free(kernel.name);
    free(kernel.path);
    free_options(&options);
    return EXIT_SUCCESS;
}
