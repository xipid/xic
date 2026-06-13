/**
 * @file Process.hpp
 * @brief Modern, intuitive child process management for the Xi framework.

 */

#ifndef XI_SYSTEM_PROCESS_HPP
#define XI_SYSTEM_PROCESS_HPP

#include "../Collection/Stream.hpp"
#include "../Collection/String.hpp"
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

namespace System {

using namespace Collection;

/**
 * @class Process
 * @brief Represents an external process with asynchronous stream I/O.
 */
class Process {
private:
  pid_t _pid = -1;
  int _pipe_in[2] = {-1, -1};
  int _pipe_out[2] = {-1, -1};
  int _pipe_err[2] = {-1, -1};

  /**
   * @class PipeStream
   * @brief Concrete implementation of VirtualStream for process pipes.
   */
  class PipeStream : public VirtualStream<String> {
  public:
    int fd = -1;
    bool isWrite = false;
    Array<String> buffer;
    bool closed = false;

    PipeStream(bool writeMode) : isWrite(writeMode) {}

    void push(const String &val) override {
      if (closed || fd == -1)
        return;
      if (isWrite) {
        ::write(fd, val.data(), val.length());
      } else {
        buffer.push(val);
        if (onPush)
          onPush(val);
      }
    }

    void unshift(const String &val) override { buffer.unshift(val); }

    usz size() const override { return buffer.size(); }

    String shift() override { return buffer.shift(); }

    String pop() override { return buffer.pop(); }

    void splice(usz start, usz length) override {
      buffer.splice(start, length);
    }

    void destroy() override {
      if (fd != -1) {
        ::close(fd);
        fd = -1;
      }
      closed = true;
      buffer.clear();
    }

    /** @brief Reads available data from the pipe into the buffer. */
    bool update() {
      if (closed || fd == -1 || isWrite)
        return false;
      char tmp[8192];
      ssize_t n = ::read(fd, tmp, sizeof(tmp) - 1);
      if (n > 0) {
        tmp[n] = 0;
        push(String(tmp));
        return true;
      } else if (n == 0) {
        closed = true;
        return false;
      }
      return false;
    }
  };

  PipeStream _in{true};
  PipeStream _out{false};
  PipeStream _err{false};

public:
  String file;       ///< Executable path or name.
  Array<String> arg; ///< Command line arguments.

  // Polymorphic stream access
  VirtualStream<String> &stdin = _in;   ///< Standard input stream.
  VirtualStream<String> &stdout = _out; ///< Standard output stream.
  VirtualStream<String> &stderr = _err; ///< Standard error stream.

  bool inheritStdin = false;
  bool inheritStderr = false;
  bool inheritStdout = false;

  int exitCode = 0;
  bool exited = false;

  Process() = default;

  /** @brief Destructor automatically cleans up or terminates the process. */
  ~Process() { destroy(); }

  /**
   * @brief Starts the external process.
   */
  void exec() {
    if (_pid != -1)
      return;

    if (!inheritStdin && ::pipe(_pipe_in) != 0)
      return;
    if (!inheritStdout && ::pipe(_pipe_out) != 0)
      return;
    if (!inheritStderr && ::pipe(_pipe_err) != 0)
      return;

    _pid = ::fork();
    if (_pid == 0) {
      // Child process
      if (!inheritStdin) {
        ::dup2(_pipe_in[0], STDIN_FILENO);
        ::close(_pipe_in[1]);
      }
      if (!inheritStdout) {
        ::dup2(_pipe_out[1], STDOUT_FILENO);
        ::close(_pipe_out[0]);
      }
      if (!inheritStderr) {
        ::dup2(_pipe_err[1], STDERR_FILENO);
        ::close(_pipe_err[0]);
      }

      Array<const char *> argv;
      argv.push(file.c_str());
      for (usz i = 0; i < arg.size(); ++i) {
        argv.push(arg[i].c_str());
      }
      argv.push(nullptr);

      ::execvp(file.c_str(), (char *const *)argv.data());
      ::_exit(127);
    } else if (_pid > 0) {
      // Parent process
      if (!inheritStdin) {
        ::close(_pipe_in[0]);
        _in.fd = _pipe_in[1];
        ::fcntl(_in.fd, F_SETFL, O_NONBLOCK);
      }
      if (!inheritStdout) {
        ::close(_pipe_out[1]);
        _out.fd = _pipe_out[0];
        ::fcntl(_out.fd, F_SETFL, O_NONBLOCK);
      }
      if (!inheritStderr) {
        ::close(_pipe_err[1]);
        _err.fd = _pipe_err[0];
        ::fcntl(_err.fd, F_SETFL, O_NONBLOCK);
      }
    }
  }

  /**
   * @brief Terminates the process if it's still running and clean up resources.
   */
  void destroy() {
    if (_pid > 0 && !exited) {
      ::kill(_pid, SIGTERM);
      int status;
      ::waitpid(_pid, &status, WNOHANG);
    }
    _in.destroy();
    _out.destroy();
    _err.destroy();
    _pid = -1;
  }

  /**
   * @brief Releases the process from management (orphans it).
   */
  void detach() { _pid = -1; }

  /**
   * @brief Sends a signal to the process.
   */
  void signal(int code) {
    if (_pid > 0)
      ::kill(_pid, code);
  }

  /**
   * @brief Blocks until the process finishes. Calls exec() if not started.
   */
  void wait() {
    if (_pid == -1)
      exec();

    // We use poll to wait for events on the pipes efficiently
    struct pollfd fds[2];
    fds[0].fd = _out.fd;
    fds[0].events = POLLIN;
    fds[1].fd = _err.fd;
    fds[1].events = POLLIN;

    while (!exited) {
      // This blocks the thread efficiently (0% CPU) until data is ready
      // or 10ms passes so we can check waitpid
      ::poll(fds, 2, 10);

      _out.update();
      _err.update();

      int status;
      pid_t r = ::waitpid(_pid, &status, WNOHANG);
      if (r == _pid) {
        exited = true;
        if (WIFEXITED(status))
          exitCode = WEXITSTATUS(status);
        else if (WIFSIGNALED(status))
          exitCode = -WTERMSIG(status);
      }
    }
  }

  /**
   * @brief Standard signal codes.
   */
  struct Signal {
    static const int INT = SIGINT;
    static const int TERM = SIGTERM;
    static const int KILL = SIGKILL;
    static const int HUP = SIGHUP;
    static const int STOP = SIGSTOP;
    static const int CONT = SIGCONT;
    static const int USR1 = SIGUSR1;
    static const int USR2 = SIGUSR2;
  };
};

} // namespace System

#endif // XI_SYSTEM_PROCESS_HPP
