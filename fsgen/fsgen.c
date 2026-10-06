#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define SECTOR_SIZE 512u
#define IMAGE_SECTORS 720u
#define IMAGE_SIZE (SECTOR_SIZE * IMAGE_SECTORS)
#define ENTRY_SIZE 32u
#define ENTRIES_PER_SECTOR (SECTOR_SIZE / ENTRY_SIZE)
#define NAME_SIZE 26u

struct input_file {
    char *name;
    char *path;
    uint16_t size;
    uint16_t sectors;
    uint16_t start_lba;
};

static void put_u16le(unsigned char *dst, uint16_t value)
{
    dst[0] = (unsigned char)(value & 0xffu);
    dst[1] = (unsigned char)(value >> 8);
}

static int compare_files(const void *left, const void *right)
{
    const struct input_file *a = left;
    const struct input_file *b = right;

    return strcmp(a->name, b->name);
}

static int append_file(struct input_file **files, size_t *count,
                       const char *directory, const char *name,
                       const struct stat *st)
{
    struct input_file *grown;
    char *path;
    size_t path_size;

    if (strlen(name) >= NAME_SIZE) {
        fprintf(stderr, "fsgen: filename is too long: %s\n", name);
        return -1;
    }
    if (st->st_size < 0 || st->st_size > UINT16_MAX) {
        fprintf(stderr, "fsgen: file size is outside the MATFS limit: %s\n", name);
        return -1;
    }

    path_size = strlen(directory) + strlen(name) + 2;
    path = malloc(path_size);
    if (path == NULL) {
        perror("fsgen: malloc");
        return -1;
    }
    if (snprintf(path, path_size, "%s/%s", directory, name) >= (int)path_size) {
        fprintf(stderr, "fsgen: input path is too long\n");
        free(path);
        return -1;
    }

    grown = realloc(*files, (*count + 1) * sizeof(**files));
    if (grown == NULL) {
        perror("fsgen: realloc");
        free(path);
        return -1;
    }
    *files = grown;
    (*files)[*count].name = strdup(name);
    (*files)[*count].path = path;
    (*files)[*count].size = (uint16_t)st->st_size;
    (*files)[*count].sectors = (uint16_t)(((uint64_t)st->st_size + SECTOR_SIZE - 1) / SECTOR_SIZE);
    (*files)[*count].start_lba = 0;
    if ((*files)[*count].name == NULL) {
        perror("fsgen: strdup");
        free(path);
        return -1;
    }
    ++*count;
    return 0;
}

static void free_files(struct input_file *files, size_t count)
{
    size_t i;

    for (i = 0; i < count; ++i) {
        free(files[i].name);
        free(files[i].path);
    }
    free(files);
}

static int load_directory(const char *directory, struct input_file **files,
                          size_t *count)
{
    DIR *dir;
    struct dirent *entry;
    int result = -1;

    dir = opendir(directory);
    if (dir == NULL) {
        perror("fsgen: opendir");
        return -1;
    }

    for (;;) {
        struct stat st;
        size_t path_size;
        char *path;

        errno = 0;
        entry = readdir(dir);
        if (entry == NULL) {
            if (errno != 0) {
                perror("fsgen: readdir");
                goto done;
            }
            break;
        }

        path_size = strlen(directory) + strlen(entry->d_name) + 2;
        path = malloc(path_size);

        if (path == NULL) {
            perror("fsgen: malloc");
            goto done;
        }
        if (snprintf(path, path_size, "%s/%s", directory, entry->d_name) >= (int)path_size) {
            fprintf(stderr, "fsgen: input path is too long\n");
            free(path);
            goto done;
        }
        if (stat(path, &st) != 0) {
            fprintf(stderr, "fsgen: cannot inspect %s: %s\n", path, strerror(errno));
            free(path);
            goto done;
        }
        free(path);

        if (!S_ISREG(st.st_mode))
            continue;
        if (append_file(files, count, directory, entry->d_name, &st) != 0)
            goto done;
    }

    result = 0;

done:
    closedir(dir);
    return result;
}

static int write_file_data(FILE *image, const struct input_file *file)
{
    FILE *input = fopen(file->path, "rb");
    unsigned char buffer[SECTOR_SIZE];
    uint32_t remaining = file->size;

    if (input == NULL) {
        fprintf(stderr, "fsgen: cannot open %s: %s\n", file->path, strerror(errno));
        return -1;
    }
    if (fseek(image, (long)file->start_lba * SECTOR_SIZE, SEEK_SET) != 0) {
        perror("fsgen: seek image");
        fclose(input);
        return -1;
    }

    while (remaining != 0) {
        size_t amount = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
        size_t got = fread(buffer, 1, amount, input);

        if (got != amount) {
            fprintf(stderr, "fsgen: short read from %s\n", file->path);
            fclose(input);
            return -1;
        }
        if (fwrite(buffer, 1, got, image) != got) {
            perror("fsgen: write image data");
            fclose(input);
            return -1;
        }
        remaining -= (uint32_t)got;
    }

    if (fclose(input) != 0) {
        perror("fsgen: close input file");
        return -1;
    }
    return 0;
}

static int generate_image(const char *image_path, struct input_file *files,
                          size_t count)
{
    unsigned char *image;
    uint16_t dir_sectors;
    uint32_t next_lba;
    size_t i;
    FILE *out;

    dir_sectors = (uint16_t)((count + ENTRIES_PER_SECTOR - 1) / ENTRIES_PER_SECTOR);
    if (dir_sectors == 0)
        dir_sectors = 1;
    next_lba = 1u + dir_sectors;

    for (i = 0; i < count; ++i) {
        if (next_lba + files[i].sectors > IMAGE_SECTORS) {
            fprintf(stderr, "fsgen: files do not fit in a 360 KiB image\n");
            return -1;
        }
        files[i].start_lba = (uint16_t)next_lba;
        next_lba += files[i].sectors;
    }

    image = calloc(1, IMAGE_SIZE);
    if (image == NULL) {
        perror("fsgen: calloc");
        return -1;
    }

    memcpy(image, "MAT16", 5);
    put_u16le(image + 8, 1);
    put_u16le(image + 10, dir_sectors);

    for (i = 0; i < count; ++i) {
        unsigned char *entry = image + SECTOR_SIZE + i * ENTRY_SIZE;

        put_u16le(entry, files[i].start_lba);
        put_u16le(entry + 2, (uint16_t)(files[i].start_lba + files[i].sectors));
        put_u16le(entry + 4, files[i].size);
        memcpy(entry + 6, files[i].name, strlen(files[i].name));
    }

    out = fopen(image_path, "wb");
    if (out == NULL) {
        fprintf(stderr, "fsgen: cannot create %s: %s\n", image_path, strerror(errno));
        free(image);
        return -1;
    }
    if (fwrite(image, 1, IMAGE_SIZE, out) != IMAGE_SIZE) {
        perror("fsgen: write image");
        fclose(out);
        free(image);
        return -1;
    }
    free(image);

    for (i = 0; i < count; ++i) {
        if (write_file_data(out, &files[i]) != 0) {
            fclose(out);
            return -1;
        }
    }
    if (fclose(out) != 0) {
        perror("fsgen: close image");
        return -1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    struct input_file *files = NULL;
    size_t count = 0;
    int result = EXIT_FAILURE;

    if (argc != 3) {
        fprintf(stderr, "Usage: %s IMAGE_PATH DIRECTORY_PATH\n", argv[0]);
        return EXIT_FAILURE;
    }

    if (load_directory(argv[2], &files, &count) != 0)
        goto done;
    qsort(files, count, sizeof(*files), compare_files);
    if (generate_image(argv[1], files, count) != 0)
        goto done;

    result = EXIT_SUCCESS;

done:
    free_files(files, count);
    return result;
}
