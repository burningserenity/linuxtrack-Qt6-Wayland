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
  std::string frontalCascade;
  std::string profileCascade;
  std::string lock_cmd;
  bool debug = false;
};

void usage(const char *prog) {
  fprintf(stderr,
          "Usage: %s [--timeout <seconds>] [--camera <index>] "
          "[--frontal-cascade <path>] [--profile-cascade <path>] "
          "[--lock-cmd <command>]\n"
          "  --timeout          seconds with no face before locking (default 30)\n"
          "  --camera           V4L2 camera index (default 0)\n"
          "  --frontal-cascade  frontal haar cascade XML path (default: bundled cascade)\n"
          "  --profile-cascade  profile haar cascade XML path (default: OpenCV data dir)\n"
          "  --cascade          deprecated alias for --frontal-cascade\n"
          "  --lock-cmd         custom lock command (with args) to run instead of\n"
          "                     the auto-detected locker, e.g. \"swaylock -f\"\n"
          "  --debug            print per-cascade detection counts each round\n",
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
    } else if (strcmp(argv[i], "--frontal-cascade") == 0) {
      const char *v = takeArg(argc, argv, i);
      if (v == nullptr) {
        return false;
      }
      opt.frontalCascade = v;
    } else if (strcmp(argv[i], "--cascade") == 0) {
      fprintf(stderr,
              "lockd: --cascade is deprecated, use --frontal-cascade\n");
      const char *v = takeArg(argc, argv, i);
      if (v == nullptr) {
        return false;
      }
      opt.frontalCascade = v;
    } else if (strcmp(argv[i], "--profile-cascade") == 0) {
      const char *v = takeArg(argc, argv, i);
      if (v == nullptr) {
        return false;
      }
      opt.profileCascade = v;
    } else if (strcmp(argv[i], "--lock-cmd") == 0) {
      const char *v = takeArg(argc, argv, i);
      if (v == nullptr) {
        return false;
      }
      opt.lock_cmd = v;
    } else if (strcmp(argv[i], "--debug") == 0) {
      opt.debug = true;
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
std::string defaultFrontalCascade() {
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

// Locates OpenCV's bundled profile cascade. OpenCV exposes its data directory as
// cv::data::haarcascades when built with that helper; when it is unavailable we
// fall back to probing the standard system install locations.
std::string defaultProfileCascade() {
  const char *name = "haarcascade_profileface.xml";
#ifdef OPENCV_HAARCASCADES_DIR
  {
    std::string p = std::string(OPENCV_HAARCASCADES_DIR) + name;
    if (access(p.c_str(), R_OK) == 0) {
      return p;
    }
  }
#endif
  const char *dirs[] = {
      "/usr/share/opencv4/haarcascades/",
      "/usr/local/share/opencv4/haarcascades/",
      "/usr/share/OpenCV/haarcascades/",
      "/usr/local/share/OpenCV/haarcascades/",
  };
  for (const char *d : dirs) {
    std::string p = std::string(d) + name;
    if (access(p.c_str(), R_OK) == 0) {
      return p;
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
  if (opt.frontalCascade.empty()) {
    opt.frontalCascade = defaultFrontalCascade();
  }
  if (opt.frontalCascade.empty()) {
    fprintf(stderr,
            "lockd: could not find a frontal haar cascade; pass "
            "--frontal-cascade\n");
    return 1;
  }

  cv::CascadeClassifier frontalCascade;
  if (!frontalCascade.load(opt.frontalCascade)) {
    fprintf(stderr, "lockd: failed to load frontal cascade '%s'\n",
            opt.frontalCascade.c_str());
    return 1;
  }

  if (opt.profileCascade.empty()) {
    opt.profileCascade = defaultProfileCascade();
  }
  cv::CascadeClassifier profileCascade;
  if (opt.profileCascade.empty()) {
    fprintf(stderr,
            "lockd: no profile cascade found, continuing with frontal-only "
            "detection\n");
  } else if (access(opt.profileCascade.c_str(), R_OK) != 0 ||
             !profileCascade.load(opt.profileCascade)) {
    fprintf(stderr,
            "lockd: profile cascade '%s' unavailable, continuing with "
            "frontal-only detection\n",
            opt.profileCascade.c_str());
    profileCascade = cv::CascadeClassifier();
  }

  const bool haveProfile = !profileCascade.empty();
  fprintf(stderr,
          "lockd: watching camera %d, locking after %ds without a face (%s)\n",
          opt.camera, opt.timeout_s,
          haveProfile ? "frontal + profile" : "frontal only");

  bool locked = false;
  const auto timeout = std::chrono::seconds(opt.timeout_s);

  cv::Mat frame;
  cv::Mat gray;
  std::vector<cv::Rect> faces;

  for (;;) {
    std::this_thread::sleep_for(timeout);

    cv::VideoCapture cap;
    if (!cap.open(opt.camera, cv::CAP_V4L2) && !cap.open(opt.camera)) {
      fprintf(stderr, "lockd: failed to open camera %d, retrying next cycle\n",
              opt.camera);
      continue;
    }

    // Some cameras emit a black/empty first frame, so warm up before grabbing.
    bool grabbed = false;
    for (int i = 0; i < 3; ++i) {
      grabbed = cap.grab();
    }
    if (grabbed) {
      cap.retrieve(frame);
    }
    cap.release();

    if (!grabbed || frame.empty()) {
      fprintf(stderr, "lockd: camera read failed, retrying next cycle\n");
      continue;
    }

    cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
    cv::equalizeHist(gray, gray);
    cv::Mat grayBlurred;
    cv::GaussianBlur(gray, grayBlurred, cv::Size(3, 3), 0);

    faces.clear();
    frontalCascade.detectMultiScale(gray, faces, 1.1, 3, 0, cv::Size(40, 40));
    int frontalCount = static_cast<int>(faces.size());
    int profileLeftCount = 0;
    int profileRightCount = 0;
    bool found = !faces.empty();

    if (!found && !profileCascade.empty()) {
      // Left profile.
      profileCascade.detectMultiScale(grayBlurred, faces, 1.05, 2, 0,
                                      cv::Size(30, 30));
      profileLeftCount = static_cast<int>(faces.size());
      found = !faces.empty();
    }

    if (!found && !profileCascade.empty()) {
      // Right profile (mirror the frame).
      cv::Mat flipped;
      cv::flip(grayBlurred, flipped, 1);
      profileCascade.detectMultiScale(flipped, faces, 1.05, 2, 0,
                                      cv::Size(30, 30));
      profileRightCount = static_cast<int>(faces.size());
      found = !faces.empty();
    }

    if (opt.debug) {
      fprintf(stderr,
              "lockd: [debug] frontal=%d profile_left=%d profile_right=%d "
              "\xE2\x86\x92 face %s (locked=%s)\n",
              frontalCount, profileLeftCount, profileRightCount,
              found ? "FOUND" : "ABSENT", locked ? "true" : "false");
    }

    if (found) {
      if (locked) {
        fprintf(stderr, "lockd: face detected again\n");
        locked = false;
      }
    } else if (!locked) {
      if (!opt.lock_cmd.empty()) {
        locked = lockd::triggerLockCmd(opt.lock_cmd);
      } else {
        locked = lockd::sessionLocked() ? true : lockd::triggerLock();
      }
    }
  }

  return 0;
}
