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
    int proc_fd = dirfd(directory);
    if (proc_fd < 0) {
        closedir(directory);
        return -1;
    }

    pid_t found = -1;
    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        if (entry->d_name[0] < '0' || entry->d_name[0] > '9')
            continue;

        char command[4096];
        int pid_fd =
            openat(proc_fd, entry->d_name, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (pid_fd < 0)
            continue;
        int fd = openat(pid_fd, "cmdline", O_RDONLY | O_CLOEXEC);
        close(pid_fd);
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
    if (element_size == 0 || offset > file_size)
        return -1;
    if (count > (file_size - offset) / element_size)
        return -1;
    return 0;
}

int roadcast_vhal_resolve_symbols(const char *executable,
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
        map[EI_CLASS] != ELFCLASS64 || map[EI_DATA] != ELFDATA2LSB ||
        map[EI_VERSION] != EV_CURRENT) {
        errno = ENOEXEC;
        goto done;
    }

    Elf64_Ehdr header;
    memcpy(&header, map, sizeof(header));
    if (header.e_version != EV_CURRENT ||
        header.e_ehsize != sizeof(Elf64_Ehdr) ||
        (header.e_phnum > 0 && header.e_phentsize != sizeof(Elf64_Phdr)) ||
        (header.e_shnum > 0 && header.e_shentsize != sizeof(Elf64_Shdr))) {
        errno = ENOEXEC;
        goto done;
    }

    if (checked_table(file_size, header.e_phoff, header.e_phnum,
                      sizeof(Elf64_Phdr)) < 0 ||
        checked_table(file_size, header.e_shoff, header.e_shnum,
                      sizeof(Elf64_Shdr)) < 0) {
        errno = ENOEXEC;
        goto done;
    }

    uint64_t best_vaddr = UINT64_MAX;
    uint64_t best_offset = 0;
    for (uint16_t i = 0; i < header.e_phnum; i++) {
        Elf64_Phdr program;
        memcpy(&program, map + header.e_phoff + i * sizeof(Elf64_Phdr),
               sizeof(Elf64_Phdr));
        if (program.p_type == PT_LOAD && program.p_vaddr < best_vaddr) {
            best_vaddr = program.p_vaddr;
            best_offset = program.p_offset;
        }
    }
    if (best_vaddr == UINT64_MAX) {
        errno = ENOEXEC;
        goto done;
    }

    Elf64_Shdr symbols_shdr;
    int found_symbols = 0;
    for (uint16_t i = 0; i < header.e_shnum; i++) {
        Elf64_Shdr section;
        memcpy(&section, map + header.e_shoff + i * sizeof(Elf64_Shdr),
               sizeof(Elf64_Shdr));
        if (section.sh_type == SHT_SYMTAB) {
            symbols_shdr = section;
            found_symbols = 1;
            break;
        }
    }
    if (!found_symbols) {
        for (uint16_t i = 0; i < header.e_shnum; i++) {
            Elf64_Shdr section;
            memcpy(&section, map + header.e_shoff + i * sizeof(Elf64_Shdr),
                   sizeof(Elf64_Shdr));
            if (section.sh_type == SHT_DYNSYM) {
                symbols_shdr = section;
                found_symbols = 1;
                break;
            }
        }
    }
    if (!found_symbols || symbols_shdr.sh_link >= header.e_shnum ||
        symbols_shdr.sh_entsize != sizeof(Elf64_Sym) ||
        symbols_shdr.sh_size % sizeof(Elf64_Sym) != 0 ||
        checked_table(file_size, symbols_shdr.sh_offset, symbols_shdr.sh_size,
                      1) < 0) {
        errno = ENOEXEC;
        goto done;
    }

    Elf64_Shdr strings_shdr;
    memcpy(&strings_shdr,
           map + header.e_shoff + symbols_shdr.sh_link * sizeof(Elf64_Shdr),
           sizeof(Elf64_Shdr));
    if (strings_shdr.sh_type != SHT_STRTAB ||
        checked_table(file_size, strings_shdr.sh_offset, strings_shdr.sh_size,
                      1) < 0) {
        errno = ENOEXEC;
        goto done;
    }

    memset(values, 0, ROADCAST_FRAME_COUNT * sizeof(values[0]));
    const char *string_table = (const char *)(map + strings_shdr.sh_offset);
    size_t symbol_count = symbols_shdr.sh_size / sizeof(Elf64_Sym);
    size_t found = 0;
    for (size_t symbol_index = 0;
         symbol_index < symbol_count && found < ROADCAST_FRAME_COUNT;
         symbol_index++) {
        Elf64_Sym sym;
        memcpy(&sym,
               map + symbols_shdr.sh_offset + symbol_index * sizeof(Elf64_Sym),
               sizeof(Elf64_Sym));
        if (sym.st_shndx == SHN_UNDEF || sym.st_name >= strings_shdr.sh_size)
            continue;
        const char *name = string_table + sym.st_name;
        size_t max_name_len = strings_shdr.sh_size - sym.st_name;
        const char *nul = memchr(name, '\0', max_name_len);
        if (!nul)
            continue;
        for (size_t frame_index = 0; frame_index < ROADCAST_FRAME_COUNT;
             frame_index++) {
            char expected[24];
            snprintf(expected, sizeof(expected), "MMI_Rx_%03X",
                     ROADCAST_CAN_IDS[frame_index]);
            if (!values[frame_index] && strcmp(name, expected) == 0) {
                values[frame_index] = sym.st_value;
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

static int open_memory_fd(pid_t pid) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/mem", pid);
    return open(path, O_RDONLY | O_CLOEXEC);
}

static int read_one(const roadcast_vhal_source_t *source, uint64_t address,
                    uint8_t data[8]) {
    if (source->mem_fd < 0)
        return -1;
    ssize_t count = pread(source->mem_fd, data, 8, (off_t)address);
    return count == 8 ? 0 : -1;
}

int roadcast_vhal_open(roadcast_vhal_source_t *source,
                       const char *process_name) {
    memset(source, 0, sizeof(*source));
    source->mem_fd = -1;
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
    if (roadcast_vhal_resolve_symbols(executable, symbol_values, &load_vaddr,
                                      &load_offset) < 0)
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
    source->mem_fd = open_memory_fd(source->pid);
    return 0;
}

int roadcast_vhal_read(roadcast_vhal_source_t *source,
                       roadcast_frame_t frames[ROADCAST_FRAME_COUNT]) {
    size_t resolved = 0;
    for (size_t i = 0; i < ROADCAST_FRAME_COUNT; i++) {
        frames[i].can_id = ROADCAST_CAN_IDS[i];
        frames[i].state = source->addresses[i]
                              ? ROADCAST_OBSERVATION_NEVER_OBSERVED
                              : ROADCAST_OBSERVATION_UNAVAILABLE;
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

    if (source->mem_fd < 0)
        source->mem_fd = open_memory_fd(source->pid);
    int failures = 0;
    for (size_t i = 0; i < ROADCAST_FRAME_COUNT; i++) {
        if (source->addresses[i] &&
            read_one(source, source->addresses[i], frames[i].data) < 0) {
            memset(frames[i].data, 0, sizeof(frames[i].data));
            frames[i].state = ROADCAST_OBSERVATION_UNAVAILABLE;
            failures++;
        }
    }
    return failures ? -1 : 1;
}

void roadcast_vhal_close(roadcast_vhal_source_t *source) {
    if (source->mem_fd >= 0)
        close(source->mem_fd);
    free(source->local_iov);
    free(source->remote_iov);
    memset(source, 0, sizeof(*source));
    source->mem_fd = -1;
}
