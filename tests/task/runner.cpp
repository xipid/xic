#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <elf.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <map>
#include <string>
#include <vector>
#include "Task/Task.hpp"

using namespace Task;
using TaskClass = Task::Task;
#define Task TaskClass
using namespace Xi;

struct GuestArgs {
    int argc;
    char** argv;
    char** envp;
};

struct OpenFile {
    std::string path;
    FILE* hostFile = nullptr;
    usz size = 0;
};

// Map guest FD -> OpenFile
static std::map<usz, OpenFile> openFiles;

std::string readGuestString(Task& task, usz guestAddr) {
    std::string s;
    char c;
    while (true) {
        usz phys = task.translate(guestAddr++, 1);
        if (!phys) break;
        c = *reinterpret_cast<char*>(phys);
        if (c == '\0') break;
        s.push_back(c);
    }
    return s;
}

usz findElfSymbol(FILE* f, const Elf64_Ehdr& ehdr, const char* name) {
    if (ehdr.e_shoff == 0 || ehdr.e_shnum == 0) return 0;

    std::fseek(f, ehdr.e_shoff, SEEK_SET);
    Elf64_Shdr* shdrs = new Elf64_Shdr[ehdr.e_shnum];
    if (std::fread(shdrs, sizeof(Elf64_Shdr), ehdr.e_shnum, f) != ehdr.e_shnum) {
        delete[] shdrs;
        return 0;
    }

    Elf64_Shdr shstr = shdrs[ehdr.e_shstrndx];
    char* shstrtab = new char[shstr.sh_size];
    std::fseek(f, shstr.sh_offset, SEEK_SET);
    std::fread(shstrtab, 1, shstr.sh_size, f);

    Elf64_Shdr symtab_shdr;
    Elf64_Shdr strtab_shdr;
    bool found_symtab = false;
    bool found_strtab = false;

    for (int i = 0; i < ehdr.e_shnum; ++i) {
        if (shdrs[i].sh_type == SHT_SYMTAB) {
            symtab_shdr = shdrs[i];
            found_symtab = true;
            if (symtab_shdr.sh_link < ehdr.e_shnum) {
                strtab_shdr = shdrs[symtab_shdr.sh_link];
                found_strtab = true;
            }
        }
    }

    if (!found_symtab || !found_strtab) {
        delete[] shstrtab;
        delete[] shdrs;
        return 0;
    }

    usz num_syms = symtab_shdr.sh_size / sizeof(Elf64_Sym);
    Elf64_Sym* syms = new Elf64_Sym[num_syms];
    std::fseek(f, symtab_shdr.sh_offset, SEEK_SET);
    std::fread(syms, sizeof(Elf64_Sym), num_syms, f);

    char* strtab = new char[strtab_shdr.sh_size];
    std::fseek(f, strtab_shdr.sh_offset, SEEK_SET);
    std::fread(strtab, 1, strtab_shdr.sh_size, f);

    usz symbol_addr = 0;
    for (usz i = 0; i < num_syms; ++i) {
        const char* sym_name = &strtab[syms[i].st_name];
        if (std::strcmp(sym_name, name) == 0) {
            symbol_addr = syms[i].st_value;
            break;
        }
    }

    delete[] strtab;
    delete[] syms;
    delete[] shstrtab;
    delete[] shdrs;
    return symbol_addr;
}

int main() {
    std::printf("=== ELF Loading & Execution Test ===\n");

    Task::setup(0, false);

    Task root = Task::root();
    Task task = root.spawn();
    task.unmap(); // Full isolation

    root.setMaxChildrenMemory(24576);

    task.onSwap(0x50000000, 0x50003000, [&](usz base, usz size) {
        std::printf("[Runner Swap] Evicting region: base=0x%lx, size=0x%lx\n", (long)base, (long)size);
    });

    const char* elfPath = "./tests/task/internal";
    FILE* f = std::fopen(elfPath, "rb");
    if (!f) {
        f = std::fopen("./build/tests/task/internal", "rb");
    }
    if (!f) {
        f = std::fopen("./tests/tests/task/internal", "rb");
    }
    if (!f) {
        std::printf("Error: Could not open ELF file %s\n", elfPath);
        return 1;
    }

    Elf64_Ehdr ehdr;
    if (std::fread(&ehdr, 1, sizeof(ehdr), f) != sizeof(ehdr)) {
        std::printf("Error: Failed to read ELF header\n");
        std::fclose(f);
        return 1;
    }

    if (ehdr.e_ident[EI_MAG0] != ELFMAG0 || ehdr.e_ident[EI_MAG1] != ELFMAG1 ||
        ehdr.e_ident[EI_MAG2] != ELFMAG2 || ehdr.e_ident[EI_MAG3] != ELFMAG3) {
        std::printf("Error: Invalid ELF magic\n");
        std::fclose(f);
        return 1;
    }

    std::fseek(f, ehdr.e_phoff, SEEK_SET);
    Elf64_Phdr* phdrs = new Elf64_Phdr[ehdr.e_phnum];
    if (std::fread(phdrs, sizeof(Elf64_Phdr), ehdr.e_phnum, f) != ehdr.e_phnum) {
        std::printf("Error: Failed to read program headers\n");
        delete[] phdrs;
        std::fclose(f);
        return 1;
    }

    // Register demand paging for guest PT_LOAD segments
    for (int i = 0; i < ehdr.e_phnum; ++i) {
        if (phdrs[i].p_type == PT_LOAD) {
            usz vaddr = phdrs[i].p_vaddr;
            usz memsz = phdrs[i].p_memsz;
            usz filesz = phdrs[i].p_filesz;
            usz offset = phdrs[i].p_offset;

            std::printf("[Runner Loader] Registering demand paging for segment: vaddr=0x%lx, size=0x%lx\n", (long)vaddr, (long)memsz);

            task.onFetch(vaddr, vaddr + memsz, [=, &task](usz faultStart, usz faultEnd) {
                usz pageStart = faultStart & ~4095;
                usz pageEnd = (faultEnd + 4095) & ~4095;

                for (usz pageAddr = pageStart; pageAddr < pageEnd; pageAddr += 4096) {
                    if (task.isMapped(pageAddr, 4096)) continue;

                    // std::printf("[Runner PageFault] Loading page at 0x%lx on demand\n", (long)pageAddr);

                    u8 buf[4096];
                    std::memset(buf, 0, 4096);

                    if (pageAddr < vaddr + filesz) {
                        usz fileOffset = offset + (pageAddr - vaddr);
                        usz bytesToRead = 4096;
                        if (pageAddr + bytesToRead > vaddr + filesz) {
                            bytesToRead = (vaddr + filesz) - pageAddr;
                        }
                        std::fseek(f, fileOffset, SEEK_SET);
                        std::fread(buf, 1, bytesToRead, f);
                    }

                    task.alloc(pageAddr, 4096);
                    task.physicalCopy(reinterpret_cast<usz>(buf), pageAddr, 4096);
                }
            });
        }
    }

    // Check if guest is dynamically linked
    const char* interpPath = nullptr;
    for (int i = 0; i < ehdr.e_phnum; ++i) {
        if (phdrs[i].p_type == PT_INTERP) {
            char* interp = new char[phdrs[i].p_filesz];
            std::fseek(f, phdrs[i].p_offset, SEEK_SET);
            std::fread(interp, 1, phdrs[i].p_filesz, f);
            interpPath = interp;
            std::printf("[Runner Loader] Found PT_INTERP: %s\n", interpPath);
            break;
        }
    }

    usz entryPoint = 0;
    usz interpreterBase = 0x70000000;
    FILE* finterp = nullptr;

    if (interpPath) {
        // Dynamic loading path
        std::string hostInterp = "/lib/ld-musl-x86_64.so.1";
        finterp = std::fopen(hostInterp.c_str(), "rb");
        assert(finterp != nullptr);

        Elf64_Ehdr iehdr;
        std::fread(&iehdr, 1, sizeof(iehdr), finterp);

        std::fseek(finterp, iehdr.e_phoff, SEEK_SET);
        Elf64_Phdr* iphdrs = new Elf64_Phdr[iehdr.e_phnum];
        std::fread(iphdrs, sizeof(Elf64_Phdr), iehdr.e_phnum, finterp);

        // Map dynamic linker
        for (int i = 0; i < iehdr.e_phnum; ++i) {
            if (iphdrs[i].p_type == PT_LOAD) {
                usz ivaddr = interpreterBase + iphdrs[i].p_vaddr;
                usz imemsz = iphdrs[i].p_memsz;
                usz ifilesz = iphdrs[i].p_filesz;
                usz ioffset = iphdrs[i].p_offset;

                usz ip_vaddr = iphdrs[i].p_vaddr;

                std::printf("[Runner Loader] Registering demand paging for interpreter: vaddr=0x%lx, size=0x%lx\n", (long)ivaddr, (long)imemsz);

                task.onFetch(ivaddr, ivaddr + imemsz, [=, &task](usz faultStart, usz faultEnd) {
                    usz pageStart = faultStart & ~4095;
                    usz pageEnd = (faultEnd + 4095) & ~4095;

                    for (usz pageAddr = pageStart; pageAddr < pageEnd; pageAddr += 4096) {
                        if (task.isMapped(pageAddr, 4096)) continue;

                        // std::printf("[Runner PageFault] Loading interpreter page at 0x%lx on demand\n", (long)pageAddr);

                        u8 buf[4096];
                        std::memset(buf, 0, 4096);

                        usz relPage = pageAddr - interpreterBase;
                        if (relPage < ip_vaddr + ifilesz) {
                            usz fileOffset = ioffset + (relPage - ip_vaddr);
                            usz bytesToRead = 4096;
                            if (relPage + bytesToRead > ip_vaddr + ifilesz) {
                                bytesToRead = (ip_vaddr + ifilesz) - relPage;
                            }
                            std::fseek(finterp, fileOffset, SEEK_SET);
                            std::fread(buf, 1, bytesToRead, finterp);
                        }

                        task.alloc(pageAddr, 4096);
                        task.physicalCopy(reinterpret_cast<usz>(buf), pageAddr, 4096);
                    }
                });
            }
        }

        entryPoint = interpreterBase + iehdr.e_entry;
        delete[] iphdrs;
    } else {
        // Static loading path
        entryPoint = findElfSymbol(f, ehdr, "main");
        assert(entryPoint != 0);
    }

    delete[] phdrs;

    // Allocate args and stub page at 0x60000000
    task.alloc(0x60000000, 4096);
    u8* args_phys = reinterpret_cast<u8*>(task.translate(0x60000000, 4096));
    assert(args_phys != nullptr);

    const char* args[] = { "./internal", "hello", "world" };
    int argc = 3;

    usz guest_rsp = 0;

    if (interpPath) {
        // 1. Build standard System V stack using task's registered stack context
        usz stack_size = 65536;
        usz stack_vaddr = 0x7ffffff00000;
        task.alloc(stack_vaddr, stack_size);
        u8* stack_phys = reinterpret_cast<u8*>(task.translate(stack_vaddr, stack_size));
        assert(stack_phys != nullptr);

        task._state->stack = stack_phys;
        task._state->stackSize = stack_size;

        // Position the System V stack structure at stack_phys + 64512
        usz stack_top_offset = 64512;
        u8* stack_top_phys = stack_phys + stack_top_offset;
        usz stack_top_vaddr = stack_vaddr + stack_top_offset;

        // Position strings at stack_phys + 65280
        usz strings_offset = 65280;
        u8* strings_phys = stack_phys + strings_offset;
        usz strings_vaddr = stack_vaddr + strings_offset;

        // Position random bytes at stack_phys + 65264
        usz random_offset = 65264;
        u8* random_phys = stack_phys + random_offset;
        usz random_vaddr = stack_vaddr + random_offset;
        std::memset(random_phys, 0x42, 16);

        // Copy argv strings
        usz current_str_rel_offset = 0;
        usz argv_guest_addrs[3];
        for (int i = 0; i < argc; ++i) {
            argv_guest_addrs[i] = strings_vaddr + current_str_rel_offset;
            std::strcpy(reinterpret_cast<char*>(strings_phys + current_str_rel_offset), args[i]);
            current_str_rel_offset += std::strlen(args[i]) + 1;
        }

        // Layout standard SysV stack structure at stack_top_phys
        u64* stack_u64 = reinterpret_cast<u64*>(stack_top_phys);
        stack_u64[0] = argc;
        stack_u64[1] = argv_guest_addrs[0];
        stack_u64[2] = argv_guest_addrs[1];
        stack_u64[3] = argv_guest_addrs[2];
        stack_u64[4] = 0; // argv NULL
        stack_u64[5] = 0; // envp NULL (empty)

        // Auxv
        int aux_idx = 6;
        auto push_aux = [&](u64 type, u64 val) {
            stack_u64[aux_idx++] = type;
            stack_u64[aux_idx++] = val;
        };

        usz phdr_guest_addr = 0x50000000 + ehdr.e_phoff;

        push_aux(AT_PHDR, phdr_guest_addr);
        push_aux(AT_PHNUM, ehdr.e_phnum);
        push_aux(AT_PHENT, sizeof(Elf64_Phdr));
        push_aux(AT_BASE, interpreterBase);
        push_aux(AT_ENTRY, ehdr.e_entry); // We verified this is absolute 0x500010f0
        push_aux(AT_PAGESZ, 4096);
        push_aux(AT_RANDOM, random_vaddr);
        push_aux(AT_NULL, 0);

        // Assemble stub for linker:
        // movabs $stack_top_phys, %rsp  ; 48 bc [8 bytes of stack_top_phys]
        // mov $0, %rdx                  ; 48 c7 c2 00 00 00 00
        // movabs $entryPoint, %rax      ; 48 b8 [8 bytes of entryPoint]
        // jmp *%rax                     ; ff e0
        u8 stub[] = {
            0x48, 0xbc, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x48, 0xc7, 0xc2, 0x00, 0x00, 0x00, 0x00,
            0x48, 0xb8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0xff, 0xe0
        };
        u64 rsp_val = reinterpret_cast<u64>(stack_top_phys);
        std::memcpy(&stub[2], &rsp_val, sizeof(rsp_val));
        std::memcpy(&stub[19], &entryPoint, sizeof(usz));
        std::memcpy(args_phys + 0xf00, stub, sizeof(stub));

    } else {
        // Static layout
        GuestArgs* ga = reinterpret_cast<GuestArgs*>(args_phys);
        ga->argc = argc;
        ga->argv = reinterpret_cast<char**>(0x60000000 + sizeof(GuestArgs));
        ga->envp = nullptr;

        char** guest_argv = reinterpret_cast<char**>(args_phys + sizeof(GuestArgs));
        usz current_str_offset = sizeof(GuestArgs) + 4 * sizeof(char*);

        for (int i = 0; i < argc; ++i) {
            guest_argv[i] = reinterpret_cast<char*>(0x60000000 + current_str_offset);
            std::strcpy(reinterpret_cast<char*>(args_phys + current_str_offset), args[i]);
            current_str_offset += std::strlen(args[i]) + 1;
        }
        guest_argv[argc] = nullptr;

        u8 stub[] = {
            0x48, 0x8b, 0x77, 0x08,
            0x48, 0x8b, 0x57, 0x10,
            0x8b, 0x7f, 0x00,
            0x48, 0xb8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0xff, 0xd0,
            0x48, 0x89, 0xc7,
            0xb8, 0x04, 0x63, 0x69, 0x78,
            0x0f, 0x05
        };
        std::memcpy(&stub[13], &entryPoint, sizeof(usz));
        std::memcpy(args_phys + 0xf00, stub, sizeof(stub));
    }

    usz pipeId = task.openPipe();
    assert(pipeId == 3);

    // --- Syscall Hook Interceptors ---
    
    auto handle_open = [&](const std::string& raw_path, int flags) -> usz {
        std::string host_path = raw_path;
        if (raw_path == "./tests/task/internal" || raw_path == "./build/tests/task/internal") {
            host_path = "./build/tests/task/internal";
        } else if (raw_path == "/lib/ld-musl-x86_64.so.1") {
            host_path = "/lib/ld-musl-x86_64.so.1";
        } else if (raw_path == "/lib/libc.so" || raw_path == "libc.so" || raw_path.find("libc.so") != std::string::npos) {
            host_path = "/lib/ld-musl-x86_64.so.1";
        }

        FILE* hf = std::fopen(host_path.c_str(), "rb");
        if (!hf) {
            std::printf("[Runner VFS] Open failed: %s\n", host_path.c_str());
            return (usz)-2; // -ENOENT
        }

        std::fseek(hf, 0, SEEK_END);
        usz size = std::ftell(hf);
        std::fseek(hf, 0, SEEK_SET);

        usz filePipeId = task.openPipe();
        Pipe* filePipe = task.getPipe(filePipeId);
        filePipe->setLogicalSize(size);
        filePipe->setCaching(0);

        filePipe->onRead = [=](usz pos, usz readerTaskId, usz length) {
            std::vector<u8> buf(length);
            std::fseek(hf, pos, SEEK_SET);
            usz n = std::fread(buf.data(), 1, length, hf);
            if (n > 0) {
                task.pwrite(filePipe, buf.data(), n, pos);
            } else {
                filePipe->dummyWrite(pos, length);
            }
        };

        OpenFile of;
        of.path = host_path;
        of.hostFile = hf;
        of.size = size;
        openFiles[filePipeId] = of;

        std::printf("[Runner VFS] Opened %s as FD %lu (size=%lu)\n", host_path.c_str(), (unsigned long)filePipeId, (unsigned long)size);
        return filePipeId;
    };

    task.onInstruction("syscall:2", [&]() {
        GuestRegs* regs = xi_guest_regs;
        std::string path = readGuestString(task, regs->rdi);
        regs->rax = handle_open(path, (int)regs->rsi);
    });

    task.onInstruction("syscall:257", [&]() {
        GuestRegs* regs = xi_guest_regs;
        std::string path = readGuestString(task, regs->rsi);
        regs->rax = handle_open(path, (int)regs->rdx);
    });

    task.onInstruction("syscall:9", [&]() {
        GuestRegs* regs = xi_guest_regs;
        usz addr = regs->rdi;
        usz length = regs->rsi;
        int prot = (int)regs->rdx;
        int flags = (int)regs->r10;
        int fd = (int)regs->r8;
        usz offset = regs->r9;

        if (fd >= 3 && openFiles.count(fd)) {
            OpenFile& of = openFiles[fd];

            if (addr == 0) {
                if (task._state->mmapAddr == 0) {
                    task._state->mmapAddr = 0x40000000;
                }
                addr = task._state->mmapAddr;
                task._state->mmapAddr += (length + 4095) & ~4095;
            }
            addr = addr & ~4095;
            length = (length + 4095) & ~4095;

            task.unmap(addr, length);
            task.alloc(addr, length);

            std::vector<u8> buf(length, 0);
            if (offset < of.size) {
                std::fseek(of.hostFile, offset, SEEK_SET);
                usz to_read = (offset + length > of.size) ? (of.size - offset) : length;
                std::fread(buf.data(), 1, to_read, of.hostFile);
            }
            task.physicalCopy(reinterpret_cast<usz>(buf.data()), addr, length);

            bool writable = (prot & 2) != 0;
            bool executable = (prot & 4) != 0;
            for (usz i = 0; i < task._state->regions.size(); ++i) {
                MemoryRegion& r = task._state->regions[i];
                if (r.base == addr && r.size == length) {
                    r.writable = writable;
                    r.executable = executable;
                    break;
                }
            }

            regs->rax = addr;
            std::printf("[Runner VFS] File-backed mmap mapped %s at 0x%lx\n", of.path.c_str(), (long)addr);
        } else {
            if (addr == 0) {
                if (task._state->mmapAddr == 0) {
                    task._state->mmapAddr = 0x40000000;
                }
                addr = task._state->mmapAddr;
                task._state->mmapAddr += (length + 4095) & ~4095;
            }
            addr = addr & ~4095;
            length = (length + 4095) & ~4095;

            task.unmap(addr, length);
            task.alloc(addr, length);

            bool writable = (prot & 2) != 0;
            bool executable = (prot & 4) != 0;
            for (usz i = 0; i < task._state->regions.size(); ++i) {
                MemoryRegion& r = task._state->regions[i];
                if (r.base == addr && r.size == length) {
                    r.writable = writable;
                    r.executable = executable;
                    break;
                }
            }
            regs->rax = addr;
            std::printf("[Runner VFS] Anonymous mmap mapped at 0x%lx\n", (long)addr);
        }
    });

    auto handle_stat = [&](const OpenFile& of, usz statbuf_guest) -> usz {
        usz phys = task.translate(statbuf_guest, 144);
        if (!phys) return (usz)-14; // -EFAULT

        std::memset(reinterpret_cast<void*>(phys), 0, 144);

        // st_mode = S_IFREG | 0755
        reinterpret_cast<u32*>(phys + 24)[0] = 0100755;
        // st_size
        reinterpret_cast<u64*>(phys + 48)[0] = (u64)of.size;
        // st_blksize
        reinterpret_cast<u64*>(phys + 56)[0] = 4096;
        // st_blocks
        reinterpret_cast<u64*>(phys + 64)[0] = (u64)((of.size + 511) / 512);

        return 0;
    };

    task.onInstruction("syscall:262", [&]() {
        GuestRegs* regs = xi_guest_regs;
        std::string path = readGuestString(task, regs->rsi);
        usz statbuf = regs->rdx;

        std::string host_path = path;
        if (path == "./tests/task/internal" || path == "./build/tests/task/internal") {
            host_path = "./build/tests/task/internal";
        } else if (path == "/lib/ld-musl-x86_64.so.1") {
            host_path = "/lib/ld-musl-x86_64.so.1";
        }

        FILE* hf = std::fopen(host_path.c_str(), "rb");
        if (!hf) {
            regs->rax = (usz)-2; // -ENOENT
            return;
        }
        std::fseek(hf, 0, SEEK_END);
        usz size = std::ftell(hf);
        std::fclose(hf);

        OpenFile of;
        of.path = host_path;
        of.size = size;
        regs->rax = handle_stat(of, statbuf);
    });

    task.onInstruction("syscall:5", [&]() {
        GuestRegs* regs = xi_guest_regs;
        usz fd = regs->rdi;
        usz statbuf = regs->rsi;

        if (openFiles.count(fd)) {
            regs->rax = handle_stat(openFiles[fd], statbuf);
        } else {
            regs->rax = (usz)-9; // -EBADF
        }
    });

    task.onInstruction("syscall:3", [&]() {
        GuestRegs* regs = xi_guest_regs;
        usz fd = regs->rdi;

        if (openFiles.count(fd)) {
            OpenFile& of = openFiles[fd];
            if (of.hostFile) {
                std::fclose(of.hostFile);
            }
            openFiles.erase(fd);
            task.closePipe(fd);
            regs->rax = 0;
        } else {
            task.closePipe(fd);
            regs->rax = 0;
        }
    });

    usz stub_vaddr = 0x60000f00;
    usz args_vaddr = 0x60000000;

    task.jump(reinterpret_cast<void(*)(void*)>(stub_vaddr), reinterpret_cast<void*>(args_vaddr));

    for (int i = 0; i < 1000; ++i) {
        yield();
        if (task.status() == TaskStatus::Finished) break;
    }

    // commented out to avoid output truncation

    std::fclose(f);
    if (finterp) std::fclose(finterp);

    char msg[64] = {0};
    Pipe* pipe = task.getPipe(3);
    assert(pipe != nullptr);
    task.read(pipe, msg, 17);

    std::printf("[Runner Debug] Task log has %lu entries:\n", (unsigned long)task.log().size());
    for (usz i = 0; i < task.log().size(); ++i) {
        LogEntry entry = task.log()[i];
        std::printf("  Log %lu: size=%lu, data=%.*s\n", (unsigned long)i, (unsigned long)entry.size, (int)entry.size, (const char*)entry.ptr);
    }

    std::printf("[Runner Assert] task.status = %d\n", (int)task.status());
    std::printf("[Runner Assert] guest message = %s\n", msg);
    std::printf("[Runner Assert] task.hasReturnValue = %d\n", (int)task.hasReturnValue());
    if (task.hasReturnValue()) {
        std::printf("[Runner Assert] task.returnValue = %d\n", task.returnValue<int>());
    }

    assert(task.status() == TaskStatus::Finished);
    assert(std::strcmp(msg, "Hello from guest!") == 0);

    assert(task.hasReturnValue());
    int guest_rc = task.returnValue<int>();
    assert(guest_rc == 42);

    std::printf("=== ELF Loading & Execution Test Passed ===\n");
    return 0;
}
