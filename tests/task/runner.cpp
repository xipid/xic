#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <elf.h>
#include "Execution/Task.hpp"

using namespace Execution;
using namespace Xi;

struct GuestMessage {
    unsigned int cmd;
    unsigned int status;
    unsigned long long arg1;
    unsigned long long arg2;
    char payload[256];
};

void* translate_guest_addr(Task& task, usz virtAddr) {
    for (usz i = 0; i < task._state->regions.size(); ++i) {
        MemoryRegion& r = task._state->regions[i];
        if (virtAddr >= r.base && virtAddr < r.base + r.size) {
            usz offset = virtAddr - r.base;
            return r.physical + offset;
        }
    }
    return nullptr;
}

int main() {
    std::printf("=== ELF Loading & Execution Test ===\n");

    // Initialize core 0 for scheduling without timer preemption
    Task::setup(0, false);

    Task root = Task::root();
    Task task = root.spawn();
    task.unmap(); // Full isolation

    // Attempt to open the guest ELF executable
    const char* elfPath = "./tests/task/internal";
    FILE* f = std::fopen(elfPath, "rb");
    if (!f) {
        // Fallback for nested build directories
        f = std::fopen("./build/tests/task/internal", "rb");
    }
    if (!f) {
        // Alternate fallback
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

    // Map and load program segments
    for (int i = 0; i < ehdr.e_phnum; ++i) {
        if (phdrs[i].p_type == PT_LOAD) {
            usz vaddr = phdrs[i].p_vaddr;
            usz memsz = phdrs[i].p_memsz;
            usz filesz = phdrs[i].p_filesz;
            usz offset = phdrs[i].p_offset;

            // Allocate memory inside the isolated task
            task.alloc(vaddr, memsz);

            // Find physical host pointer of the mapped region
            u8* phys = (u8*)translate_guest_addr(task, vaddr);
            if (phys) {
                std::fseek(f, offset, SEEK_SET);
                if (filesz > 0) {
                    if (std::fread(phys, 1, filesz, f) != filesz) {
                        std::printf("Error: Failed to read segment %d\n", i);
                        delete[] phdrs;
                        std::fclose(f);
                        return 1;
                    }
                }
                // Zero out remainder (.bss)
                if (memsz > filesz) {
                    std::memset(phys + filesz, 0, memsz - filesz);
                }
            }
        }
    }

    usz entryPoint = ehdr.e_entry;
    delete[] phdrs;
    std::fclose(f);

    std::printf("[Runner Debug] Regions:\n");
    for (size_t i = 0; i < task._state->regions.size(); ++i) {
        MemoryRegion& r = task._state->regions[i];
        std::printf("  Region %d: base=0x%lx, size=0x%lx, phys=%p\n",
                    (int)i, (long)r.base, (long)r.size, r.physical);
    }

    bool childExited = false;

    // Register CPUID instruction hook callback to service API calls from guest
    task.onInstruction("cpuid", [&]() {
        // Read guest rbx register via inline assembly
        usz msgVirt = 0;
        asm volatile("mov %%rbx, %0" : "=r"(msgVirt));

        GuestMessage* msg = (GuestMessage*)translate_guest_addr(task, msgVirt);
        if (!msg) return;

        if (msg->cmd == 1) {
            // Command 1: Get Current Task ID
            msg->arg1 = task.id();
            msg->status = 0;
        } else if (msg->cmd == 2) {
            // Command 2: Get Parent Task ID
            usz targetId = msg->arg1;
            Task t = Task::findTask(targetId);
            msg->arg1 = t.valid() ? t.parentId() : 0;
            msg->status = 0;
        } else if (msg->cmd == 3) {
            // Command 3: Send IPC Message
            usz receiverId = msg->arg1;
            Task receiver = Task::findTask(receiverId);
            if (receiver.valid()) {
                task.send(receiver, msg->payload);
                msg->status = 0;
            } else {
                msg->status = 1;
            }
        } else if (msg->cmd == 4) {
            // Command 4: Exit Task Execution
            childExited = true;
            task._state->status = TaskStatus::Finished;
            msg->status = 0;
        }
    });

    // Jump to guest entry point
    task.jump(entryPoint);

    // Yield control to run the task
    for (int i = 0; i < 50; ++i) {
        yield();
        if (childExited) break;
    }

    std::printf("[Runner Debug] AOT Cache:\n");
    for (size_t i = 0; i < task._state->aotCache.size(); ++i) {
        AOTRegion& a = task._state->aotCache[i];
        std::printf("  Cache %d: originalAddr=0x%lx, size=0x%lx, patched=%p\n",
                    (int)i, (long)a.originalAddr, (long)a.originalSize, a.patchedCode);
    }

    // Verify task execution and IPC message
    assert(childExited);
    assert(root.inbox().size() > 0);
    std::printf("[Runner] Message received in root inbox: %s (Sender: %d)\n", 
                root.inbox()[0].payload.c_str(), (int)root.inbox()[0].senderId);
    assert(root.inbox()[0].payload == "Hello from guest!");

    std::printf("=== ELF Loading & Execution Test Passed ===\n");
    return 0;
}
