#include "locker.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#include <unistd.h>

#include <opencv2/imgproc.hpp>
#include <opencv2/objdetect.hpp>
#include <opencv2/videoio.hpp>

namespace {

struct Options {
  int timeout_s = 30;
  int camera = 0;
  std::string cascade;
};

void usage(const char *prog) {
  fprintf(stderr,
          "Usage: %s [--timeout <seconds>] [--camera <index>] "
          "[--cascade <path>]\n"
          "  --timeout  seconds with no face before locking (default 30)\n"
          "  --camera   V4L2 camera index (default 0)\n"
          "  --cascade  haar cascade XML path (default: bundled cascade)\n",
          prog);
}

// Returns the value for a flag expecting an argument, or nullptr on error.
const char *takeArg(int argc, char **argv, int &i) {
  if (i + 1 >= argc) {
    fprintf(stderr, "lockd: missing value for %s\n", argv[i]);
    return nullptr;
  }
  return argv[++i];
}

bool parseArgs(int argc, char **argv, Options &opt) {
  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--timeout") == 0) {
      const char *v = takeArg(argc, argv, i);
      if (v == nullptr) {
        return false;
      }
      opt.timeout_s = atoi(v);
      if (opt.timeout_s <= 0) {
        fprintf(stderr, "lockd: --timeout must be a positive integer\n");
        return false;
      }
    } else if (strcmp(argv[i], "--camera") == 0) {
      const char *v = takeArg(argc, argv, i);
      if (v == nullptr) {
        return false;
      }
      opt.camera = atoi(v);
    } else if (strcmp(argv[i], "--cascade") == 0) {
      const char *v = takeArg(argc, argv, i);
      if (v == nullptr) {
        return false;
      }
      opt.cascade = v;
    } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
      usage(argv[0]);
      exit(0);
    } else {
      fprintf(stderr, "lockd: unknown argument '%s'\n", argv[i]);
      usage(argv[0]);
      return false;
    }
  }
  return true;
}

// Locates the bundled haar cascade. LOCKD_CASCADE_PATH is set at build time to
// the install location; a few common spots are tried as a fallback.
std::string defaultCascade() {
  const char *candidates[] = {
#ifdef LOCKD_CASCADE_SRC_PATH
      LOCKD_CASCADE_SRC_PATH,   // source-tree path, works from build dir
#endif
#ifdef LOCKD_CASCADE_PATH
      LOCKD_CASCADE_PATH,       // installed path
#endif
      "/usr/share/lockd/haarcascade_frontalface_alt2.xml",
      "/usr/local/share/lockd/haarcascade_frontalface_alt2.xml",
      "haarcascade_frontalface_alt2.xml",
  };
  for (const char *c : candidates) {
    if (c != nullptr && access(c, R_OK) == 0) {
      return c;
    }
  }
  return {};
}

} // namespace

int main(int argc, char **argv) {
  Options opt;
  if (!parseArgs(argc, argv, opt)) {
    return 2;
  }
  if (opt.cascade.empty()) {
    opt.cascade = defaultCascade();
  }
  if (opt.cascade.empty()) {
    fprintf(stderr, "lockd: could not find a haar cascade; pass --cascade\n");
    return 1;
  }

  cv::CascadeClassifier cascade;
  if (!cascade.load(opt.cascade)) {
    fprintf(stderr, "lockd: failed to load cascade '%s'\n", opt.cascade.c_str());
    return 1;
  }

  cv::VideoCapture cap;
  if (!cap.open(opt.camera, cv::CAP_V4L2) && !cap.open(opt.camera)) {
    fprintf(stderr, "lockd: failed to open camera %d\n", opt.camera);
    return 1;
  }

  fprintf(stderr,
          "lockd: watching camera %d, locking after %d s without a face\n",
          opt.camera, opt.timeout_s);

  using clock = std::chrono::steady_clock;
  clock::time_point last_seen = clock::now();
  bool locked = false;
  const auto timeout = std::chrono::seconds(opt.timeout_s);

  cv::Mat frame;
  cv::Mat gray;
  std::vector<cv::Rect> faces;

  for (;;) {
    if (!cap.read(frame) || frame.empty()) {
      fprintf(stderr, "lockd: camera read failed, retrying\n");
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
      continue;
    }

    cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
    cv::equalizeHist(gray, gray);
    faces.clear();
    cascade.detectMultiScale(gray, faces, 1.1, 3, 0, cv::Size(40, 40));

    const auto now = clock::now();
    if (!faces.empty()) {
      last_seen = now;
      if (locked) {
        fprintf(stderr, "lockd: face detected again\n");
        locked = false;
      }
    } else if (!locked && (now - last_seen) >= timeout) {
      if (lockd::sessionLocked()) {
        locked = true;
      } else if (lockd::triggerLock()) {
        locked = true;
      }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }

  return 0;
}
