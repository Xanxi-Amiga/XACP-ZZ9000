/*
 * zzdf_system.cpp - TFE_System implementation for the ZZ9000 Core1
 * target. Replaces TFE_System/system.cpp (SDL). Timing source: Zynq
 * Global Timer via zzdf_gtimer_read (333 MHz, read-only contract).
 * ASCII only.
 */
#include <TFE_System/system.h>

#include <cstring>
#include <cstdio>

extern "C" unsigned long long zzdf_gtimer_read(void);
extern "C" void zzdf_log_puts(const char *s);

namespace TFE_System
{
	f64 c_gameTimeScale = 1.0;

	static u64 s_startTime = 0;
	static u64 s_lastTime = 0;
	static f64 s_dt = 1.0 / 60.0;
	static f64 s_dtRaw = 1.0 / 60.0;
	static bool s_quit = false;
	static bool s_uiReq = false;
	static bool s_sync = false;
	static f64 s_refreshRate = 60.0;
	static char s_versionString[64] = "TFE-ZZ9000";
	static const f64 c_tickToSec = 1.0 / 333000000.0;
	 


























	static const f64 c_maxDt = 1.0 / 10.0;

	void init(f32 refreshRate, bool synced, const char* versionString)
	{
		s_refreshRate = refreshRate;
		s_sync = synced;
		strncpy(s_versionString, versionString, sizeof(s_versionString)-1);
		s_startTime = zzdf_gtimer_read();
		s_lastTime = s_startTime;
	}
	void shutdown() {}
	void resetStartTime()
	{
		s_startTime = zzdf_gtimer_read();
		s_lastTime = s_startTime;
	}
	void setVsync(bool sync) { s_sync = sync; }
	bool getVSync() { return s_sync; }

	void update()
	{
		u64 t = zzdf_gtimer_read();
		/* monotonic guard only: if no time passed, count one timer tick,
		   never a fixed floor (see c_maxDt note). */
		u64 uDt = (t > s_lastTime) ? (t - s_lastTime) : 1ULL;
		f64 dt = f64(uDt) * c_tickToSec;
		s_lastTime = t;
		if (dt > c_maxDt) dt = c_maxDt;
		s_dtRaw = dt;
		s_dt = dt * c_gameTimeScale;
	}
	f64 updateThreadLocal(u64* localTime)
	{
		u64 t = zzdf_gtimer_read();
		f64 dt = (*localTime) ? f64(t - *localTime) * c_tickToSec : 0.0;
		*localTime = t;
		return dt;
	}

	f64 getDeltaTime() { return s_dt; }
	f64 getDeltaTimeRaw() { return s_dtRaw; }
	f64 getTime()
	{
		return f64(zzdf_gtimer_read() - s_startTime) * c_tickToSec;
	}

	u64 getCurrentTimeInTicks() { return zzdf_gtimer_read() - s_startTime; }
	f64 convertFromTicksToSeconds(u64 ticks) { return f64(ticks) * c_tickToSec; }
	f64 convertFromTicksToMillis(u64 ticks) { return f64(ticks) * c_tickToSec * 1000.0; }
	f64 microsecondsToSeconds(f64 mu) { return mu * 1e-6; }
	u64 getStartTime() { return s_startTime; }
	void setStartTime(u64 startTime) { s_startTime = startTime; }

	void getDateTimeString(char* output)
	{
		u64 us = zzdf_gtimer_read() / 333ULL;
		sprintf(output, "T+%llus", (unsigned long long)(us / 1000000ULL));
	}
	void getDateTimeStringForFile(char* output)
	{
		getDateTimeString(output);
	}
	time_t getTimeFromString(const char* str) { (void)str; return 0; }

	bool osShellExecute(const char* pathToExe, const char* exeDir,
	                    const char* param, bool waitForCompletion)
	{
		(void)pathToExe;(void)exeDir;(void)param;(void)waitForCompletion;
		return false;
	}
	void postErrorMessageBox(const char* msg, const char* title)
	{
		zzdf_log_puts("[ERRBOX] ");
		if (title) zzdf_log_puts(title);
		zzdf_log_puts(": ");
		if (msg) zzdf_log_puts(msg);
		zzdf_log_puts("\n");
	}
	void sleep(u32 sleepDeltaMS)
	{
		u64 end = zzdf_gtimer_read() + (u64)sleepDeltaMS * 333000ULL;
		while (zzdf_gtimer_read() < end) { }
	}

	void postQuitMessage() { s_quit = true; }
	bool quitMessagePosted() { return s_quit; }
	void postSystemUiRequest() { s_uiReq = true; }
	bool systemUiRequestPosted() { return s_uiReq; }

	const char* getVersionString() { return s_versionString; }
}
