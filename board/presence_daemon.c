#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "sample_utils.h"
#include "tdl_sdk.h"

#define WIDTH 960
#define HEIGHT 540

#define PERSON_CLASS_ID 4
#define SCORE_THRESHOLD 0.5f
#define ABSENCE_SECONDS 25
#define BREAK_REMINDER_SECONDS (50 * 60)

#define STATE_FILE "/tmp/presence_state"
#define STATE_FILE_TMP "/tmp/presence_state.tmp"
#define BREAK_FLAG_FILE "/tmp/break_reminder_flag"

static volatile sig_atomic_t g_running = 1;
static int g_debug = 0;

static void handle_signal(int sig) {
  (void)sig;
  g_running = 0;
}

static void log_event(const char *event) {
  time_t now = time(NULL);
  struct tm tm_now;
  localtime_r(&now, &tm_now);
  char ts[32];
  strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S", &tm_now);
  printf("%s presence_daemon event=%s\n", ts, event);
  fflush(stdout);
}

/* Write atomically (write to temp file, then rename) so a poller reading
 * STATE_FILE never sees a partial write mid-update. */
static void write_state_file(const char *state) {
  FILE *f = fopen(STATE_FILE_TMP, "w");
  if (!f) return;
  fprintf(f, "%s\n", state);
  fclose(f);
  rename(STATE_FILE_TMP, STATE_FILE);
}

static void touch_break_flag(void) {
  FILE *f = fopen(BREAK_FLAG_FILE, "w");
  if (f) fclose(f);
}

int main(int argc, char *argv[]) {
  const char *model_path =
      (argc > 1) ? argv[1]
                 : "/mnt/cvimodel/yolov8n_det_person_vehicle_384_640_INT8_cv181x.cvimodel";
  int chn = 0;
  g_debug = (getenv("PRESENCE_DEBUG") != NULL);

  signal(SIGINT, handle_signal);
  signal(SIGTERM, handle_signal);

  TDLObject obj_meta = {0};
  TDLModel model_id = TDL_MODEL_YOLOV8N_DET_PERSON_VEHICLE;
  TDLHandle tdl_handle = TDL_CreateHandle(chn);

  int ret = InitCamera(tdl_handle, WIDTH, HEIGHT, IMAGE_YUV420SP_UV, 3);
  if (ret != 0) {
    printf("InitCamera failed: %#x\n", ret);
    TDL_DestroyHandle(tdl_handle);
    return ret;
  }

  ret = TDL_OpenModel(tdl_handle, model_id, model_path, NULL);
  if (ret != 0) {
    printf("open model failed: %#x\n", ret);
    DestoryCamera(tdl_handle);
    TDL_DestroyHandle(tdl_handle);
    return ret;
  }

  /* -1 = unknown (startup); 0 = away; 1 = present */
  int present_state = -1;
  time_t last_person_seen = 0;
  time_t session_start = 0;
  int break_reminded = 0;

  log_event("daemon_started");
  write_state_file("unknown");

  while (g_running) {
    TDLImage image = GetCameraFrame(tdl_handle, chn);
    if (image == NULL) {
      usleep(100 * 1000);
      continue;
    }

    ret = TDL_Detection(tdl_handle, model_id, image, &obj_meta);
    int person_in_frame = 0;
    if (ret == 0) {
      if (g_debug) {
        if (obj_meta.size == 0) {
          printf("[debug] frame: no detections\n");
        }
        for (int i = 0; i < obj_meta.size; i++) {
          printf("[debug] frame: class_id=%d score=%.3f bbox=[%.1f %.1f %.1f %.1f]\n",
                 obj_meta.info[i].class_id, obj_meta.info[i].score,
                 obj_meta.info[i].box.x1, obj_meta.info[i].box.y1,
                 obj_meta.info[i].box.x2, obj_meta.info[i].box.y2);
        }
        fflush(stdout);
      }
      for (int i = 0; i < obj_meta.size; i++) {
        if (obj_meta.info[i].class_id == PERSON_CLASS_ID &&
            obj_meta.info[i].score >= SCORE_THRESHOLD) {
          person_in_frame = 1;
          break;
        }
      }
      TDL_ReleaseObjectMeta(&obj_meta);
    }

    time_t now = time(NULL);
    if (person_in_frame) {
      last_person_seen = now;
    }

    int currently_present = (now - last_person_seen) < ABSENCE_SECONDS;

    if (present_state != currently_present) {
      if (currently_present) {
        log_event("present");
        write_state_file("present");
        session_start = now;
        break_reminded = 0;
      } else {
        log_event("away");
        write_state_file("away");
        session_start = 0;
      }
      present_state = currently_present;
    }

    if (present_state == 1 && !break_reminded &&
        (now - session_start) >= BREAK_REMINDER_SECONDS) {
      log_event("break_reminder");
      touch_break_flag();
      break_reminded = 1;
    }

    ReleaseCameraFrame(tdl_handle, chn);
    TDL_DestroyImage(image);
    usleep(200 * 1000); /* ~5 fps is plenty for presence detection */
  }

  log_event("daemon_stopped");
  TDL_CloseModel(tdl_handle, model_id);
  DestoryCamera(tdl_handle);
  TDL_DestroyHandle(tdl_handle);
  return 0;
}
