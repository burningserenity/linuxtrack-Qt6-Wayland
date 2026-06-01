#include "locker.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <sys/wait.h>
#include <unistd.h>

namespace lockd {

namespace {

// Searches the directories in $PATH for an executable named `cmd`.
bool onPath(const char *cmd) {
  const char *path = getenv("PATH");
  if (path == nullptr) {
    return false;
  }
  std::string dirs(path);
  size_t start = 0;
  while (start <= dirs.size()) {
    size_t end = dirs.find(':', start);
    if (end == std::string::npos) {
      end = dirs.size();
    }
    std::string dir = dirs.substr(start, end - start);
    if (!dir.empty()) {
      std::string full = dir + "/" + cmd;
      if (access(full.c_str(), X_OK) == 0) {
        return true;
      }
    }
    start = end + 1;
  }
  return false;
}

// Runs `argv` (NULL-terminated) without a shell and waits for it to exit.
// Returns the child's exit code, or -1 if it could not be launched.
int runAndWait(const char *const argv[]) {
  pid_t pid = fork();
  if (pid < 0) {
    return -1;
  }
  if (pid == 0) {
    execvp(argv[0], const_cast<char *const *>(argv));
    _exit(127);
  }
  int status = 0;
  if (waitpid(pid, &status, 0) < 0) {
    return -1;
  }
  if (WIFEXITED(status)) {
    return WEXITSTATUS(status);
  }
  return -1;
}

// Spawns `argv` detached and returns immediately. Used for blocking lockers
// (swaylock/hyprlock) which run until the user unlocks. Returns true on
// successful fork/exec hand-off.
bool spawnDetached(const char *const argv[]) {
  pid_t pid = fork();
  if (pid < 0) {
    return false;
  }
  if (pid == 0) {
    execvp(argv[0], const_cast<char *const *>(argv));
    _exit(127);
  }
  return true;
}

} // namespace

bool sessionLocked() {
  if (!onPath("loginctl")) {
    return false;
  }
  const char *argv[] = {"loginctl", "show-session", "self",
                        "--property=LockedHint", nullptr};
  int fds[2];
  if (pipe(fds) != 0) {
    return false;
  }
  pid_t pid = fork();
  if (pid < 0) {
    close(fds[0]);
    close(fds[1]);
    return false;
  }
  if (pid == 0) {
    dup2(fds[1], STDOUT_FILENO);
    close(fds[0]);
    close(fds[1]);
    execvp(argv[0], const_cast<char *const *>(argv));
    _exit(127);
  }
  close(fds[1]);
  std::string out;
  std::array<char, 128> buf;
  ssize_t n;
  while ((n = read(fds[0], buf.data(), buf.size())) > 0) {
    out.append(buf.data(), static_cast<size_t>(n));
  }
  close(fds[0]);
  int status = 0;
  waitpid(pid, &status, 0);
  return out.find("LockedHint=yes") != std::string::npos;
}

bool triggerLock() {
  if (onPath("swaylock")) {
    const char *argv[] = {"swaylock", nullptr};
    if (spawnDetached(argv)) {
      fprintf(stderr, "lockd: locked via swaylock\n");
      return true;
    }
  }
  if (onPath("hyprlock")) {
    const char *argv[] = {"hyprlock", nullptr};
    if (spawnDetached(argv)) {
      fprintf(stderr, "lockd: locked via hyprlock\n");
      return true;
    }
  }
  if (onPath("loginctl")) {
    const char *argv[] = {"loginctl", "lock-session", nullptr};
    if (runAndWait(argv) == 0) {
      fprintf(stderr, "lockd: locked via loginctl lock-session\n");
      return true;
    }
  }
  fprintf(stderr, "lockd: no usable locker found "
                  "(tried swaylock, hyprlock, loginctl)\n");
  return false;
}

} // namespace lockd
