#include <unistd.h>
#include <string.h>

int main(int argc, char* argv[]) {
    // Verify args passed to the guest
    if (argc == 3 &&
        strcmp(argv[1], "hello") == 0 &&
        strcmp(argv[2], "world") == 0) {
        
        write(3, "Hello", 5);
        write(3, " from guest!", 12);

        return 42; // Expected return code
    }

    write(3, "Bad arguments!", 14);
    return 99;
}
