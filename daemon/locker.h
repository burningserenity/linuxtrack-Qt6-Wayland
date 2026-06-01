#ifndef LOCKD_LOCKER_H
#define LOCKD_LOCKER_H

#include <string>

namespace lockd {

// Invokes the first available Wayland-compatible screen locker found on $PATH,
// trying swaylock, then hyprlock, then `loginctl lock-session`.
// Returns true if a locker was launched successfully.
bool triggerLock();

// Runs a user-supplied lock command. `cmd` is tokenized on whitespace (no shell
// expansion) and the first token is exec'd with the remaining tokens as argv.
// Returns true if the process exited with status 0.
bool triggerLockCmd(const std::string &cmd);

// Returns true if the current session is already locked, so the daemon does not
// lock again on top of an existing lock. Detection is best-effort via
// `loginctl`; if the state cannot be determined it returns false.
bool sessionLocked();

} // namespace lockd

#endif // LOCKD_LOCKER_H
