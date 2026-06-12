#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <elf.h>
#include "Execution/Task.hpp"

using namespace Execution;
using namespace Xi;

int main() {
    std::printf("=== ELF Loading & Execution Test ===\n");

    // Initialize core 0 for scheduling without timer preemption
    Task::setup(0, false);

    Task root = Task::root();
    Task task = root.spawn();
    task.unmap(); // Full isolation

    // Set memory limit to 4 pages (16KB) to drive swapping!
    task.setMaxChildrenMemory(16384);

    task.setOnSwap([&](usz base, usz size) {
        std::printf("[Runner Swap] Evicting region: base=0x%lx, size=0x%lx\n", (long)base, (long)size);
    });

    // Attempt to open the guest ELF executable
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

    // Check ELF magic
    if (ehdr.e_ident[EI_MAG0] != ELFMAG0 || ehdr.e_ident[EI_MAG1] != ELFMAG1 ||
        ehdr.e_ident[EI_MAG2] != ELFMAG2 || ehdr.e_ident[EI_MAG3] != ELFMAG3) {
        std::printf("Error: Invalid ELF magic\n");
        std::fclose(f);
        return 1;
    }

    // Read program headers
    std::fseek(f, ehdr.e_phoff, SEEK_SET);
    Elf64_Phdr* phdrs = new Elf64_Phdr[ehdr.e_phnum];
    if (std::fread(phdrs, sizeof(Elf64_Phdr), ehdr.e_phnum, f) != ehdr.e_phnum) {
        std::printf("Error: Failed to read program headers\n");
        delete[] phdrs;
        std::fclose(f);
        return 1;
    }

    // We register onFetch callbacks for all PT_LOAD segments
    for (int i = 0; i < ehdr.e_phnum; ++i) {
        if (phdrs[i].p_type == PT_LOAD) {
            usz vaddr = phdrs[i].p_vaddr;
            usz memsz = phdrs[i].p_memsz;
            usz filesz = phdrs[i].p_filesz;
            usz offset = phdrs[i].p_offset;

            std::printf("[Runner Loader] Registering demand paging for segment: vaddr=0x%lx, size=0x%lx\n", (long)vaddr, (long)memsz);

            task.setOnFetch(vaddr, vaddr + memsz, [=, &task](usz faultStart, usz faultEnd) {
                usz pageStart = faultStart & ~4095;
                usz pageEnd = (faultEnd + 4095) & ~4095;

                for (usz pageAddr = pageStart; pageAddr < pageEnd; pageAddr += 4096) {
                    // Check if already mapped
                    bool alreadyMapped = false;
                    for (usz j = 0; j < task._state->regions.size(); ++j) {
                        if (pageAddr >= task._state->regions[j].base && 
                            pageAddr < task._state->regions[j].base + task._state->regions[j].size) {
                            alreadyMapped = true;
                            break;
                        }
                    }
                    if (alreadyMapped) continue;

                    std::printf("[Runner PageFault] Loading page at 0x%lx on demand\n", (long)pageAddr);

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
                    task.copy(reinterpret_cast<usz>(buf), pageAddr, 4096);
                }
            });
        }
    }

    usz entryPoint = ehdr.e_entry;
    delete[] phdrs;

    // Jump to guest entry point
    task.jump(entryPoint);

    // Yield control to run the task
    for (int i = 0; i < 100; ++i) {
        yield();
        if (task.status() == TaskStatus::Finished) break;
    }

    std::printf("[Runner Debug] Final Mapped Regions:\n");
    for (size_t i = 0; i < task._state->regions.size(); ++i) {
        MemoryRegion& r = task._state->regions[i];
        std::printf("  Region %d: base=0x%lx, size=0x%lx, phys=%p\n",
                    (int)i, (long)r.base, (long)r.size, r.physical);
    }

    std::fclose(f);

    // Verify task execution and IPC message
    assert(task.status() == TaskStatus::Finished);
    assert(root.inbox().size() > 0);
    std::printf("[Runner] Message received in root inbox: %s (Sender: %d)\n", 
                root.inbox()[0].payload.c_str(), (int)root.inbox()[0].senderId);
    assert(root.inbox()[0].payload == "Hello from guest!");

    std::printf("=== ELF Loading & Execution Test Passed ===\n");
    return 0;
}
