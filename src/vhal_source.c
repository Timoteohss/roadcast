#define _GNU_SOURCE

#include "roadcast_vhal.h"

#include <dirent.h>
#if defined(__has_include)
#if __has_include(<elf.h>)
#include <elf.h>
#elif __has_include(<libelf/libelf.h>)
#include <libelf/libelf.h>
#else
#error "An ELF header implementation is required"
#endif
#else
#include <elf.h>
#endif
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static pid_t find_pid(const char *needle) {
    DIR *directory = opendir("/proc");
    if (!directory)
        return -1;

    pid_t found = -1;
    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        if (entry->d_name[0] < '0' || entry->d_name[0] > '9')
            continue;

        char path[64];
        char command[4096];
        snprintf(path, sizeof(path), "/proc/%s/cmdline", entry->d_name);
        int fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0)
            continue;
        ssize_t length = read(fd, command, sizeof(command) - 1);
        close(fd);
        if (length <= 0)
            continue;

        for (ssize_t i = 0; i < length; i++) {
            if (command[i] == '\0')
                command[i] = ' ';
        }
        command[length] = '\0';
        if (strstr(command, needle)) {
            found = (pid_t)atoi(entry->d_name);
            break;
        }
    }
    closedir(directory);
    return found;
}

static int read_executable_path(pid_t pid, char *out, size_t capacity) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/exe", pid);
    ssize_t length = readlink(path, out, capacity - 1);
    if (length < 0)
        return -1;
    out[length] = '\0';
    return 0;
}

static int find_mapping_base(pid_t pid, const char *executable,
                             uint64_t wanted_offset, uint64_t *base) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/maps", pid);
    FILE *maps = fopen(path, "r");
    if (!maps)
        return -1;

    int result = -1;
    char line[600];
    while (fgets(line, sizeof(line), maps)) {
        uint64_t start;
        uint64_t end;
        uint64_t offset;
        char permissions[8];
        int path_offset = 0;
        if (sscanf(line, "%" SCNx64 "-%" SCNx64 " %7s %" SCNx64 " %*s %*s %n",
                   &start, &end, permissions, &offset, &path_offset) < 4)
            continue;
        char *mapped_path = line + path_offset;
        while (*mapped_path == ' ')
            mapped_path++;
        char *newline = strchr(mapped_path, '\n');
        if (newline)
            *newline = '\0';
        if (offset == wanted_offset && strcmp(mapped_path, executable) == 0) {
            *base = start;
            result = 0;
            break;
        }
    }
    fclose(maps);
    return result;
}

static int checked_table(size_t file_size, uint64_t offset, uint64_t count,
                         size_t element_size) {
    if (offset > file_size)
        return -1;
    if (count > (file_size - offset) / element_size)
        return -1;
    return 0;
}

static int resolve_symbols(const char *executable,
                           uint64_t values[ROADCAST_FRAME_COUNT],
                           uint64_t *load_vaddr, uint64_t *load_offset) {
    int fd = open(executable, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return -1;

    struct stat stat_buffer;
    if (fstat(fd, &stat_buffer) < 0 || stat_buffer.st_size <= 0) {
        close(fd);
        return -1;
    }
    size_t file_size = (size_t)stat_buffer.st_size;
    uint8_t *map = mmap(NULL, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (map == MAP_FAILED)
        return -1;

    int result = -1;
    if (file_size < sizeof(Elf64_Ehdr) || memcmp(map, ELFMAG, SELFMAG) != 0 ||
        map[EI_CLASS] != ELFCLASS64)
        goto done;

    const Elf64_Ehdr *header = (const Elf64_Ehdr *)map;
    if (checked_table(file_size, header->e_phoff, header->e_phnum,
                      sizeof(Elf64_Phdr)) < 0 ||
        checked_table(file_size, header->e_shoff, header->e_shnum,
                      sizeof(Elf64_Shdr)) < 0)
        goto done;

    const Elf64_Phdr *programs = (const Elf64_Phdr *)(map + header->e_phoff);
    uint64_t best_vaddr = UINT64_MAX;
    uint64_t best_offset = 0;
    for (uint16_t i = 0; i < header->e_phnum; i++) {
        if (programs[i].p_type == PT_LOAD && programs[i].p_vaddr < best_vaddr) {
            best_vaddr = programs[i].p_vaddr;
            best_offset = programs[i].p_offset;
        }
    }
    if (best_vaddr == UINT64_MAX)
        goto done;

    const Elf64_Shdr *sections = (const Elf64_Shdr *)(map + header->e_shoff);
    const Elf64_Shdr *symbols = NULL;
    for (uint16_t i = 0; i < header->e_shnum; i++) {
        if (sections[i].sh_type == SHT_SYMTAB) {
            symbols = &sections[i];
            break;
        }
    }
    if (!symbols) {
        for (uint16_t i = 0; i < header->e_shnum; i++) {
            if (sections[i].sh_type == SHT_DYNSYM) {
                symbols = &sections[i];
                break;
            }
        }
    }
    if (!symbols || symbols->sh_link >= header->e_shnum ||
        checked_table(file_size, symbols->sh_offset, symbols->sh_size, 1) < 0)
        goto done;

    const Elf64_Shdr *strings = &sections[symbols->sh_link];
    if (checked_table(file_size, strings->sh_offset, strings->sh_size, 1) < 0)
        goto done;

    memset(values, 0, ROADCAST_FRAME_COUNT * sizeof(values[0]));
    const char *string_table = (const char *)(map + strings->sh_offset);
    const Elf64_Sym *symbol_table =
        (const Elf64_Sym *)(map + symbols->sh_offset);
    size_t symbol_count = symbols->sh_size / sizeof(Elf64_Sym);
    size_t found = 0;
    for (size_t symbol_index = 0;
         symbol_index < symbol_count && found < ROADCAST_FRAME_COUNT;
         symbol_index++) {
        if (symbol_table[symbol_index].st_name >= strings->sh_size)
            continue;
        const char *name = string_table + symbol_table[symbol_index].st_name;
        for (size_t frame_index = 0; frame_index < ROADCAST_FRAME_COUNT;
             frame_index++) {
            char expected[24];
            snprintf(expected, sizeof(expected), "MMI_Rx_%03X",
                     ROADCAST_CAN_IDS[frame_index]);
            if (!values[frame_index] && strcmp(name, expected) == 0) {
                values[frame_index] = symbol_table[symbol_index].st_value;
                found++;
                break;
            }
        }
    }

    *load_vaddr = best_vaddr;
    *load_offset = best_offset;
    result = 0;

done:
    munmap(map, file_size);
    return result;
}

static int read_one(pid_t pid, uint64_t address, uint8_t data[8]) {
#if defined(__linux__) || defined(__ANDROID__)
    struct iovec local = {.iov_base = data, .iov_len = 8};
    struct iovec remote = {
        .iov_base = (void *)(uintptr_t)address,
        .iov_len = 8,
    };
    if (process_vm_readv(pid, &local, 1, &remote, 1, 0) == 8)
        return 0;
#endif

    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/mem", pid);
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return -1;
    ssize_t count = pread(fd, data, 8, (off_t)address);
    close(fd);
    return count == 8 ? 0 : -1;
}

int roadcast_vhal_open(roadcast_vhal_source_t *source,
                       const char *process_name) {
    memset(source, 0, sizeof(*source));
    source->pid = find_pid(process_name);
    if (source->pid < 0) {
        errno = ESRCH;
        return -1;
    }

    char executable[4096];
    if (read_executable_path(source->pid, executable, sizeof(executable)) < 0)
        return -1;

    uint64_t symbol_values[ROADCAST_FRAME_COUNT];
    uint64_t load_vaddr;
    uint64_t load_offset;
    uint64_t map_base;
    if (resolve_symbols(executable, symbol_values, &load_vaddr, &load_offset) <
        0)
        return -1;
    if (find_mapping_base(source->pid, executable, load_offset, &map_base) <
            0 &&
        find_mapping_base(source->pid, executable, 0, &map_base) < 0)
        return -1;

    source->local_iov =
        calloc(ROADCAST_FRAME_COUNT, sizeof(*source->local_iov));
    source->remote_iov =
        calloc(ROADCAST_FRAME_COUNT, sizeof(*source->remote_iov));
    if (!source->local_iov || !source->remote_iov) {
        roadcast_vhal_close(source);
        errno = ENOMEM;
        return -1;
    }

    uint64_t bias = map_base - load_vaddr;
    for (size_t i = 0; i < ROADCAST_FRAME_COUNT; i++) {
        if (!symbol_values[i])
            continue;
        source->addresses[i] = bias + symbol_values[i];
        source->remote_iov[source->resolved_count].iov_base =
            (void *)(uintptr_t)source->addresses[i];
        source->remote_iov[source->resolved_count].iov_len = 8;
        source->resolved_count++;
    }
    if (!source->resolved_count) {
        roadcast_vhal_close(source);
        errno = ENOENT;
        return -1;
    }
    return 0;
}

int roadcast_vhal_read(roadcast_vhal_source_t *source,
                       roadcast_frame_t frames[ROADCAST_FRAME_COUNT]) {
    size_t resolved = 0;
    for (size_t i = 0; i < ROADCAST_FRAME_COUNT; i++) {
        frames[i].can_id = ROADCAST_CAN_IDS[i];
        frames[i].present = source->addresses[i] ? 1u : 0u;
        if (!source->addresses[i]) {
            memset(frames[i].data, 0, sizeof(frames[i].data));
            continue;
        }
        source->local_iov[resolved].iov_base = frames[i].data;
        source->local_iov[resolved].iov_len = 8;
        resolved++;
    }

#if defined(__linux__) || defined(__ANDROID__)
    ssize_t expected = (ssize_t)(resolved * 8);
    if (process_vm_readv(source->pid, source->local_iov, resolved,
                         source->remote_iov, resolved, 0) == expected)
        return 0;
#endif

    int failures = 0;
    for (size_t i = 0; i < ROADCAST_FRAME_COUNT; i++) {
        if (source->addresses[i] &&
            read_one(source->pid, source->addresses[i], frames[i].data) < 0) {
            memset(frames[i].data, 0, sizeof(frames[i].data));
            failures++;
        }
    }
    return failures ? -1 : 0;
}

void roadcast_vhal_close(roadcast_vhal_source_t *source) {
    free(source->local_iov);
    free(source->remote_iov);
    memset(source, 0, sizeof(*source));
}
