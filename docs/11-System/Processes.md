# Child Processes

The **System::Process** module provides a modern, intuitive, and asynchronous interface for managing external executables. It handles the complexity of pipes, file descriptors, and process signals while integrating seamlessly with xic **Streams**.

---

## Basic Execution

Running a process is as simple as defining the command and its arguments.

```cpp
#include "System/Process.hpp"

using namespace System;

Process p;
p.file = "ls";
p.arg = {"-l", "/home/xi"};

// Blocking wait
p.wait();

if (p.exitCode == 0) {
    // Process finished successfully
}
```

---

## Asynchronous I/O (Piping)

Every `Process` object exposes three virtual streams: `stdin`, `stdout`, and `stderr`. These can be used to interact with the child process in real-time without blocking your main application loop.

```cpp
Process p;
p.file = "grep";
p.arg = {"Xi"};
p.exec(); // Start asynchronously

// Send data to the child's stdin
p.stdin.push("Hello from the parent\n");
p.stdin.push("Xi Framework is powerful\n");

// Read from stdout
while (p.stdout.size() > 0) {
    String line = p.stdout.shift();
    println("Grep matched: " + line);
}
```

---

## Process State and Signals

You can monitor the lifecycle of a process and send signals like `SIGINT` or `SIGTERM`.

```cpp
if (!p.exited) {
    p.signal(Process::Signal::TERM);
}

// Check status
p.wait(); // Ensures cleanup and sets exitCode
```

---

## Inheritance and Redirection

By default, processes use isolated pipes. However, you can choose to inherit the parent's standard streams for logging or interactive CLI tools.

```cpp
Process p;
p.file = "htop";
p.inheritStdin = true;
p.inheritStdout = true;
p.exec();
```

---

## Best Practices

1.  **Always Call `wait()` or `destroy()`**: To prevent zombie processes, ensure that every process you `exec()` is eventually waited on or explicitly destroyed.
2.  **Buffers**: The `update()` call for stdout/stderr uses an 8KB buffer. If your child process produces massive amounts of data at high speed, ensure you are shifting data out of the streams frequently to prevent them from growing indefinitely in RAM.
3.  **Path Resolution**: The `exec()` call uses `execvp`, which searches your system's `PATH`. For custom binaries, provide the full absolute path.
