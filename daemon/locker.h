#ifndef LOCKD_LOCKER_H
#define LOCKD_LOCKER_H

namespace lockd {

// Invokes the first available Wayland-compatible screen locker found on $PATH,
// trying swaylock, then hyprlock, then `loginctl lock-session`.
// Returns true if a locker was launched successfully.
bool triggerLock();

// Returns true if the current session is already locked, so the daemon does not
// lock again on top of an existing lock. Detection is best-effort via
// `loginctl`; if the state cannot be determined it returns false.
bool sessionLocked();

} // namespace lockd

#endif // LOCKD_LOCKER_H
