/*
    Demo recording and playback for KyroSpades.

    Recording: compatible with aos_replay format (unchanged from original).
    Playback: adapted from ZeroSpades' DemoPlayer/DemoNetClient design.
      - Packets pre-loaded for O(log n) seeking (binary search on timestamp)
      - bootstrap_end_time prevents seeks that land before world is initialised
      - demo_seeking suppresses side-effects during fast backward-seek replay
      - Initial VXL snapshot allows backward seeks without re-parsing the file
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#ifndef _WIN32
#include <dirent.h>
#endif

#include "demo.h"
#include "window.h"
#include "network.h"
#include "map.h"
#include "player.h"
#include "chunk.h"
#include "log.h"
#include "common.h"


/* ── File listing ─────────────────────────────────────────────────── */

int demo_list_files(char*** out) {
	const char* dir = "demos";
	*out = NULL;
	int count = 0, cap = 0;

#ifdef _WIN32
	char pattern[256];
	snprintf(pattern, sizeof(pattern), "%s\\*.demo", dir);
	WIN32_FIND_DATAA fd;
	HANDLE h = FindFirstFileA(pattern, &fd);
	if (h == INVALID_HANDLE_VALUE) return 0;
	do {
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
		if (count == cap) {
			cap = cap ? cap * 2 : 16;
			*out = realloc(*out, cap * sizeof(char*));
		}
		char path[512];
		snprintf(path, sizeof(path), "%s/%s", dir, fd.cFileName);
		(*out)[count++] = strdup(path);
	} while (FindNextFileA(h, &fd));
	FindClose(h);
#else
	DIR* d = opendir(dir);
	if (!d) return 0;
	struct dirent* e;
	while ((e = readdir(d))) {
		size_t n = strlen(e->d_name);
		if (n < 5 || strcmp(e->d_name + n - 5, ".demo") != 0) continue;
		if (count == cap) {
			cap = cap ? cap * 2 : 16;
			*out = realloc(*out, cap * sizeof(char*));
		}
		char path[512];
		snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
		(*out)[count++] = strdup(path);
	}
	closedir(d);
#endif

	if (count > 1) {
		/* simple insertion sort — demo lists are short */
		for (int i = 1; i < count; i++) {
			char* key = (*out)[i];
			int j = i - 1;
			while (j >= 0 && strcmp((*out)[j], key) > 0) {
				(*out)[j + 1] = (*out)[j];
				j--;
			}
			(*out)[j + 1] = key;
		}
	}
	return count;
}

/* ═══════════════════════════════════════════════════════════════════
   Recording  (original KyroSpades code, unchanged)
   ═══════════════════════════════════════════════════════════════════ */

struct Demo CurrentDemo;
static const struct Demo ResetStruct;

FILE* create_demo_file(void) {
    char file_name[128];
    char dir_path[] = "demos";

    mkdir(dir_path, 0755);

    time_t demo_time;
    time(&demo_time);
    struct tm* tm_info = localtime(&demo_time);
    snprintf(file_name, sizeof(file_name),
             "%s/%04d-%02d-%02d_%02d-%02d-%02d.demo", dir_path,
             tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday,
             tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);

    FILE* file = fopen(file_name, "wb");
    if (!file) {
        log_info("Demo failed to create file");
        return NULL;
    }

    /* aos_replay header: file version 1, protocol 0.75 (3) */
    unsigned char value = 1; fwrite(&value, 1, 1, file);
    value = 3;               fwrite(&value, 1, 1, file);
    return file;
}

void register_demo_packet(ENetPacket* packet) {
    if (!CurrentDemo.fp) return;

    float          c_time = (float)(window_time() - CurrentDemo.start_time);
    unsigned short len    = (unsigned short)packet->dataLength;

    fwrite(&c_time,         sizeof(float),          1, CurrentDemo.fp);
    fwrite(&len,            sizeof(unsigned short),  1, CurrentDemo.fp);
    fwrite(packet->data,    packet->dataLength,      1, CurrentDemo.fp);
}

void demo_start_record(void) {
    CurrentDemo.fp         = create_demo_file();
    CurrentDemo.start_time = (float)window_time();
    log_info("Demo Recording started.");
}

void demo_stop_record(void) {
    if (CurrentDemo.fp) fclose(CurrentDemo.fp);
    CurrentDemo = ResetStruct;
    log_info("Demo Recording ended.");
}

bool demo_is_server_omited_packet(int id) {
    int omited_ids[] = {
        PACKET_INPUTDATA_ID, PACKET_WEAPONINPUT_ID, PACKET_SETTOOL_ID,
        PACKET_SETCOLOR_ID,  PACKET_WEAPONRELOAD_ID,
    };
    for (int i = 0; i < (int)(sizeof(omited_ids) / sizeof(int)); i++)
        if (omited_ids[i] == id) return true;
    return false;
}

/* ═══════════════════════════════════════════════════════════════════
   Playback  (adapted from ZeroSpades DemoPlayer / DemoNetClient)
   ═══════════════════════════════════════════════════════════════════ */

struct DemoPlayback DemoPlaybackState;
bool demo_seeking = false;

/* Convenience: safe cast because packets[] is declared in network.h */
extern void (*packets[256])(void* data, int len);

/* ── Helpers ──────────────────────────────────────────────────────── */

bool demo_is_playing(void)  { return DemoPlaybackState.active; }
bool demo_is_seeking(void)  { return demo_seeking; }
/* True during a seek replay: callers should suppress one-shot effects
   (sounds, particles) but STILL replay chat / join / leave messages so
   the chat log is complete up to the seek target. */
bool demo_mute_effects(void) { return demo_seeking; }

/* ── Game clock ───────────────────────────────────────────────────────
   Returns a monotonic clock that equals window_time() during normal play
   but STOPS advancing whenever the demo is frozen (paused or finished).

   Invariant: game_time() == window_time() - g_paused_total, where
   g_paused_total is the total wall-clock time spent frozen so far.  While
   frozen we additionally subtract the current freeze interval, so the
   returned value holds perfectly still.  On unfreeze that interval is
   folded into g_paused_total, so the clock resumes exactly where it
   stopped — no backward jump, no divergence from stored timers (which are
   themselves written using game_time()).                                  */

static double g_paused_total = 0.0;  /* total seconds spent frozen          */
static double g_freeze_start = 0.0;  /* window_time() when current freeze began */
static bool   g_is_frozen    = false;

bool demo_is_frozen(void) {
	return DemoPlaybackState.active &&
	       (DemoPlaybackState.paused || DemoPlaybackState.finished);
}

/* Call whenever the frozen state may have changed.  Idempotent. */
static void game_clock_sync(void) {
	bool want = demo_is_frozen();
	if (want && !g_is_frozen) {
		g_freeze_start = window_time();
		g_is_frozen = true;
	} else if (!want && g_is_frozen) {
		g_paused_total += window_time() - g_freeze_start;
		g_is_frozen = false;
	}
}

float game_time(void) {
	/* Keep the freeze bookkeeping current even if nobody called sync yet. */
	game_clock_sync();
	double t = window_time() - g_paused_total;
	if (g_is_frozen)
		t -= window_time() - g_freeze_start; /* hold still while frozen */
	return (float)t;
}

static void free_packets(void) {
    if (!DemoPlaybackState.packets) return;
    for (int i = 0; i < DemoPlaybackState.packet_count; i++)
        free(DemoPlaybackState.packets[i].data);
    free(DemoPlaybackState.packets);
    DemoPlaybackState.packets       = NULL;
    DemoPlaybackState.packet_count  = 0;
    DemoPlaybackState.packet_capacity = 0;
}

/* Dispatch a single pre-loaded packet through the existing handler table. */
static inline void dispatch_packet(const struct DemoPacketEntry* e) {
    unsigned char id = e->data[0];
    if (packets[id])
        (*packets[id])(e->data + 1, (int)(e->length - 1));
}

/* ── Bootstrap-end detection ─────────────────────────────────────── *
 *  ZeroSpades calls this "bootstrapEndTime".  The recorder writes     *
 *  MapStart / MapChunk / StateData / ExistingPlayer immediately after  *
 *  the stopwatch starts, so they all have very small but positive      *
 *  timestamps.  Seek(0) must not land before them or the world is     *
 *  uninitialised.  We keep the timestamp of the last such packet.     */
static void detect_bootstrap_end(void) {
    DemoPlaybackState.bootstrap_end_time = 0.0f;
    for (int i = 0; i < DemoPlaybackState.packet_count; i++) {
        unsigned char t = DemoPlaybackState.packets[i].data[0];
        if (t != PACKET_MAPSTART_ID && t != PACKET_MAPCHUNK_ID &&
            t != PACKET_STATEDATA_ID && t != PACKET_EXISTINGPLAYER_ID)
            break;
        DemoPlaybackState.bootstrap_end_time =
            DemoPlaybackState.packets[i].timestamp;
    }
}

/* ── Open ─────────────────────────────────────────────────────────── */

bool demo_playback_open(const char* filename) {
    demo_playback_close(); /* clean up any previous session */

    FILE* f = fopen(filename, "rb");
    if (!f) {
        log_error("Demo: cannot open '%s'", filename);
        return false;
    }

    /* 2-byte header */
    unsigned char header[2];
    if (fread(header, 1, 2, f) != 2) {
        log_error("Demo: file too short to have a header");
        fclose(f);
        return false;
    }
    if (header[0] != 1) {
        log_error("Demo: unsupported file version %d (expected 1)", header[0]);
        fclose(f);
        return false;
    }
    int proto = header[1];
    if (proto != 3 && proto != 4) {
        log_error("Demo: unsupported protocol version %d", proto);
        fclose(f);
        return false;
    }

    /* Pre-load all packets into memory so seeking is O(log n). */
    int capacity = 4096;
    struct DemoPacketEntry* pkts =
        malloc((size_t)capacity * sizeof(struct DemoPacketEntry));
    if (!pkts) { fclose(f); return false; }

    int count = 0;
    float last_ts = 0.0f;

    while (!feof(f)) {
        float          ts;
        unsigned short len;

        if (fread(&ts,  sizeof(float),          1, f) != 1) break;
        if (fread(&len, sizeof(unsigned short),  1, f) != 1) break;
        if (len == 0) break;

        unsigned char* data = malloc(len);
        if (!data) break;
        if (fread(data, 1, len, f) != len) { free(data); break; }

        if (count == capacity) {
            capacity *= 2;
            struct DemoPacketEntry* tmp =
                realloc(pkts, (size_t)capacity * sizeof(struct DemoPacketEntry));
            if (!tmp) { free(data); break; }
            pkts = tmp;
        }

        pkts[count].timestamp = ts;
        pkts[count].length    = len;
        pkts[count].data      = data;
        count++;
        last_ts = ts;
    }
    fclose(f);

    if (count == 0) {
        log_error("Demo: no packets found in '%s'", filename);
        for (int i = 0; i < count; i++) free(pkts[i].data);
        free(pkts);
        return false;
    }

    DemoPlaybackState.packets          = pkts;
    DemoPlaybackState.packet_count     = count;
    DemoPlaybackState.packet_capacity  = capacity;
    DemoPlaybackState.protocol_version = proto;
    DemoPlaybackState.duration         = last_ts;

    detect_bootstrap_end();

    DemoPlaybackState.active               = true;
    DemoPlaybackState.paused               = false;
    DemoPlaybackState.finished             = false;
    DemoPlaybackState.current_time         = 0.0f;
    DemoPlaybackState.speed                = 1.0f;
    DemoPlaybackState.current_packet_index = 0;
    DemoPlaybackState.last_real_time       = window_time();
    DemoPlaybackState.initial_map_data     = NULL;
    DemoPlaybackState.initial_map_size     = 0;

    /* Signal to the game that we are "connected" and loading a map.
       The packet handlers (MapStart, MapChunk, StateData) will drive
       network_map_transfer and network_logged_in normally. */
    network_connected  = 1;
    network_logged_in  = 0;
    network_map_transfer = 1;

    log_info("Demo: opened '%s'  proto=%d  %.1fs  %d packets",
             filename, proto, last_ts, count);
    return true;
}

/* ── Close ────────────────────────────────────────────────────────── */

void demo_playback_close(void) {
    free_packets();
    if (DemoPlaybackState.initial_map_data) {
        free(DemoPlaybackState.initial_map_data);
        DemoPlaybackState.initial_map_data = NULL;
        DemoPlaybackState.initial_map_size  = 0;
    }
    memset(&DemoPlaybackState, 0, sizeof(DemoPlaybackState));
    demo_seeking = false;
    network_connected  = 0;
    network_logged_in  = 0;
}

/* ── Update (called every frame when demo_is_playing()) ──────────── */

void demo_playback_update(void) {
    if (!DemoPlaybackState.active || DemoPlaybackState.finished) return;

    double now = window_time();
    if (!DemoPlaybackState.paused) {
        float dt = (float)(now - DemoPlaybackState.last_real_time);
        DemoPlaybackState.current_time +=
            dt * DemoPlaybackState.speed;
    }
    DemoPlaybackState.last_real_time = now;

    /* Dispatch all packets whose timestamp is <= current_time. */
    while (DemoPlaybackState.current_packet_index <
           DemoPlaybackState.packet_count) {
        struct DemoPacketEntry* e =
            &DemoPlaybackState.packets[DemoPlaybackState.current_packet_index];
        if (e->timestamp > DemoPlaybackState.current_time) break;
        dispatch_packet(e);
        DemoPlaybackState.current_packet_index++;
    }

    if (DemoPlaybackState.current_packet_index >=
        DemoPlaybackState.packet_count) {
        DemoPlaybackState.finished = true;
        game_clock_sync(); /* begin freezing the game clock at end-of-demo */
        log_info("Demo: playback finished (%.1fs)", DemoPlaybackState.duration);
    }
}

/* ── Initial-map snapshot ─────────────────────────────────────────── *
 *  Called by read_PacketStateData() after the VXL is decompressed,    *
 *  but only on the first map load (not during seeking).               */
void demo_playback_save_initial_map(const void* data, size_t size) {
    if (!DemoPlaybackState.active || DemoPlaybackState.initial_map_data)
        return; /* only save once */
    DemoPlaybackState.initial_map_data = malloc(size);
    if (!DemoPlaybackState.initial_map_data) return;
    memcpy(DemoPlaybackState.initial_map_data, data, size);
    DemoPlaybackState.initial_map_size = size;
    log_info("Demo: saved initial map snapshot (%zu bytes)", size);
}

/* ── World reset (for backward seeks) ─────────────────────────────── *
 *  Restores the map and player table to their initial state so that    *
 *  the fast-replay pass builds a consistent world from t=0.           */
static void demo_reset_world(void) {
    if (!DemoPlaybackState.initial_map_data) return;

    map_vxl_load(DemoPlaybackState.initial_map_data,
                 DemoPlaybackState.initial_map_size);
    chunk_rebuild_all();
    player_init();

    /* Wipe chat so the silent replay rebuilds it from scratch instead of
       appending duplicate join/disconnect lines on every backward seek. */
    chat_clear(0);
    chat_clear(1);
    chat_clear(2);

    network_logged_in    = 0;
    network_map_transfer = 0;
}

/* ── Fast replay (silent pass used by backward seeks) ─────────────── *
 *  Replays all packets from the start up to target_time with           *
 *  demo_seeking=true so that packet handlers suppress sounds,          *
 *  particles, and chat messages (same pattern as ZeroSpades'           *
 *  SilentWorldListener + seekingMode).                                 *
 *                                                                      *
 *  MapStart and MapChunk are skipped because demo_reset_world() has    *
 *  already restored the map from the saved snapshot.                   */
static void demo_fast_replay_to(float target_time) {
    demo_seeking = true;

    for (int i = 0; i < DemoPlaybackState.packet_count; i++) {
        struct DemoPacketEntry* e = &DemoPlaybackState.packets[i];
        if (e->timestamp > target_time) break;

        /* Skip map-loading packets — map was pre-loaded in demo_reset_world() */
        unsigned char id = e->data[0];
        if (id == PACKET_MAPSTART_ID || id == PACKET_MAPCHUNK_ID) continue;

        if (packets[id])
            (*packets[id])(e->data + 1, (int)(e->length - 1));
    }

    demo_seeking = false;
}

/* ── Seek ─────────────────────────────────────────────────────────── */

void demo_playback_seek(float time) {
    if (!DemoPlaybackState.active) return;

    /* Never seek before the bootstrap region (same as ZeroSpades). */
    float replay_time = time;
    if (replay_time < DemoPlaybackState.bootstrap_end_time)
        replay_time = DemoPlaybackState.bootstrap_end_time;

    /* Seeking (either direction) rebuilds the world from t=0 by replaying
       the whole timeline up to the target.  Effects (sounds, particles) are
       muted, but chat / join / leave messages ARE replayed so the chat log
       is always complete up to wherever we seek to.  We clear chat first so
       the rebuild produces exactly the messages up to the target with no
       duplicates. */
    if (DemoPlaybackState.initial_map_data) {
        chat_clear(0);
        chat_clear(1);
        chat_clear(2);
        demo_reset_world();
        demo_fast_replay_to(replay_time);
    }

    /* Clamp and position the forward-playback cursor. */
    if (time < 0.0f) time = 0.0f;
    if (time > DemoPlaybackState.duration) time = DemoPlaybackState.duration;
    DemoPlaybackState.current_time = time;
    DemoPlaybackState.finished     = false;

    /* Binary search for the first packet strictly after the target time. */
    int lo = 0, hi = DemoPlaybackState.packet_count;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (DemoPlaybackState.packets[mid].timestamp <= time)
            lo = mid + 1;
        else
            hi = mid;
    }
    DemoPlaybackState.current_packet_index = lo;
    DemoPlaybackState.last_real_time = window_time();
}

void demo_playback_fast_forward(float seconds) {
    demo_playback_seek(DemoPlaybackState.current_time + seconds);
}

void demo_playback_pause(void) {
    if (DemoPlaybackState.active) {
        DemoPlaybackState.paused = true;
        game_clock_sync();
    }
}

void demo_playback_resume(void) {
    if (DemoPlaybackState.active) {
        /* Re-running a finished demo from a pause is not supported; treat
           resume as a no-op once finished so physics stays frozen. */
        if (DemoPlaybackState.finished)
            return;
        DemoPlaybackState.paused        = false;
        DemoPlaybackState.last_real_time = window_time();
        game_clock_sync(); /* fold the paused interval into the clock */
    }
}

void demo_playback_toggle_pause(void) {
    if (DemoPlaybackState.paused) demo_playback_resume();
    else                          demo_playback_pause();
}

void demo_playback_set_speed(float speed) {
    if (speed < 0.25f) speed = 0.25f;
    if (speed > 8.0f)  speed = 8.0f;
    DemoPlaybackState.speed = speed;
}
