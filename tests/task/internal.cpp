struct GuestMessage {
    unsigned int cmd;
    unsigned int status;
    unsigned long long arg1;
    unsigned long long arg2;
    char payload[256];
};

extern "C" void guest_syscall(GuestMessage* msg) {
    register unsigned long long rbx_val asm("rbx") = reinterpret_cast<unsigned long long>(msg);
    asm volatile(
        "cpuid"
        : "+r"(rbx_val)
        :
        : "rax", "rcx", "rdx"
    );
}

void guest_strcpy(char* dest, const char* src) {
    while (*src) {
        *dest++ = *src++;
    }
    *dest = '\0';
}

extern "C" __attribute__((section(".text.entry"))) void _start() {
    GuestMessage msg;

    // 1. Get current task ID
    msg.cmd = 1;
    msg.status = 1;
    msg.arg1 = 0;
    msg.arg2 = 0;
    guest_syscall(&msg);
    unsigned long long currentId = msg.arg1;

    // 2. Get parent task ID
    msg.cmd = 2;
    msg.status = 1;
    msg.arg1 = currentId;
    msg.arg2 = 0;
    guest_syscall(&msg);
    unsigned long long parentId = msg.arg1;

    // 3. Send message to parent
    msg.cmd = 3;
    msg.status = 1;
    msg.arg1 = parentId;
    msg.arg2 = 0;
    guest_strcpy(msg.payload, "Hello from guest!");
    guest_syscall(&msg);

    // 4. Exit
    msg.cmd = 4;
    msg.status = 1;
    msg.arg1 = 0;
    msg.arg2 = 0;
    guest_syscall(&msg);

    // Fallback infinite loop
    for (;;) {}
}
