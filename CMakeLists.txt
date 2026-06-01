cmake_minimum_required(VERSION 3.16)
project(lockd LANGUAGES CXX)

# lockd is a minimal lock-on-away daemon: it watches a camera for a face and
# triggers a Wayland screen locker after a configurable absence timeout.
# The full daemon lives under daemon/.
add_subdirectory(daemon)
