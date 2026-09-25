/* Dark Forces game-loop integration for the ZZ9000 Core1 port. */
#include <TFE_System/system.h>
#include <TFE_Settings/settings.h>
#include <TFE_FileSystem/paths.h>
#include <TFE_Input/input.h>
#include <TFE_Input/inputMapping.h>
#include <TFE_Game/igame.h>
#include <TFE_RenderBackend/renderBackend.h>
#include <TFE_Jedi/Renderer/virtualFramebuffer.h>
#include <TFE_Jedi/Task/task.h>
#include "zzdf_crumbs.h"
#include <TFE_Audio/midiPlayer.h>
#include <TFE_Input/replay.h>
#include <TFE_Game/saveSystem.h>
#include <string>
#include <vector>
namespace TFE_FrontEndUI {
	const std::vector<std::string>& zzdf_getModOverrides();
}
#include <cstring>
#include <cstdio>

extern "C" {
#include "zzdf_config.h"
    extern unsigned int zzdf_rb_engine_frames(void);
    extern unsigned int zzdf_rb_presents(void);
    extern void zzdf_input_pump(void);
    extern void zzdf_input_text_consumed(void);
    extern int  zzdf_rb_target_ready(void);
    extern int  zzdf_cfgscreen_frame(void);
    extern void zzdf_cfgopt_load(void);
    extern void zzdf_cfgopt_apply(void);
    extern void zzdf_log_puts(const char *s);
    extern unsigned int zzdf_us_now(void);
    extern unsigned int zzdf_fs_count(void);
    extern const zzdf_manifest_entry *zzdf_fs_entry(unsigned int idx);
    extern int zzdf_fs_exists(const char *path);
    extern void zzdf_audio_init(void);
    extern void zzdf_audio_pump(void);
    extern void zzdf_midi_tick(void);
    extern void zzdf_midi_init(void);
    extern void zzdf_midi_shutdown(void);
    extern unsigned int zzdf_writeback_run(void);
}

static volatile u32* s_sh = (volatile u32*)ZZDF_SHARED_ARM;

 


static u32 s_zzdfLoads = 0, s_zzdfLoadFail = 0;

 





static u32 s_zzdfLoadCool = 0;
#define ZZDF_LOAD_COOLDOWN 120u

 






static const char* s_zzdfArgv[6];
static int         s_zzdfArgc = 0;

#define STEP(x) do { s_sh[SH_DIAG] = (x); \
    __asm__ volatile("dsb":::"memory"); } while (0)

/* fail: publish the error, leave DIAG on the step that failed, return.
   The caller parks. No exit, no firmware handback - reuse is unsafe. */
static int fail(u32 err, const char* what)
{
	s_sh[SH_ERROR] = err;
	zzdf_log_puts("[ZZDF] FAIL: ");
	zzdf_log_puts(what);
	zzdf_log_puts("\n");
	__asm__ volatile("dsb":::"memory");
	return 1;
}

/* ------------------------------------------------------------------ */
extern "C" int zzdf_run_game(void)
{
	u32 count, i;
	char line[128];

	STEP(ZZDF_E_GAME_ENTRY);
	zzdf_log_puts("[ZZDF] N1 game entry\n");

	/* ---- manifest (mounted and validated by zzdf_fs_init) -------- */
	STEP(ZZDF_E_MANIFEST_HDR);
	count = zzdf_fs_count();
	if (count == 0)
		return fail(ZZDF_FATAL_NO_MANIFEST,
		            "no manifest / rejected by MemFS");
	sprintf(line, "[ZZDF] manifest ok count=%u\n", (unsigned)count);
	zzdf_log_puts(line);

	STEP(ZZDF_E_MANIFEST_ENT);
	for (i = 0; i < count; i++)
	{
		const zzdf_manifest_entry* e = zzdf_fs_entry(i);
		if (!e) return fail(ZZDF_FATAL_BAD_MANIFEST, "manifest entry null");
		sprintf(line, "[ZZDF] mounted %s addr=%08X size=%u\n",
		        e->name, (unsigned)e->base, (unsigned)e->size);
		zzdf_log_puts(line);
	}

	/* Dark Forces cannot start without these four. Checking here turns
	   a mysterious engine failure into a named one. */
	STEP(ZZDF_E_ARCHIVES_MOUNTED);
	{
		static const char* required[] = {
			"DARK.GOB", "SOUNDS.GOB", "SPRITES.GOB", "TEXTURES.GOB"
		};
		u32 found = 0;
		for (i = 0; i < 4; i++)
		{
			if (zzdf_fs_exists(required[i])) { found++; continue; }
			zzdf_log_puts("[ZZDF] MISSING: ");
			zzdf_log_puts(required[i]);
			zzdf_log_puts("\n");
		}
		s_sh[SH_MOUNTED_COUNT] = found;
		if (found < 4)
			return fail(ZZDF_FATAL_NO_ARCHIVES,
			            "a required GOB is not in the manifest");
	}

	/* ---- engine bring-up ---------------------------------------- */
	STEP(ZZDF_E_DF_INIT);

	/* upstream order: paths FIRST, then settings (settings.ini is
	   looked up through the paths). Flat MemFS root everywhere. */
	TFE_Paths::setPath(PATH_PROGRAM, "/");
	TFE_Paths::setPath(PATH_PROGRAM_DATA, "/");
	TFE_Paths::setPath(PATH_USER_DOCUMENTS, "/");
	TFE_Paths::setPath(PATH_SOURCE_DATA, "/");

	{
		bool firstRun = false;
		if (!TFE_Settings::init(firstRun))
			return fail(ZZDF_FATAL_SETTINGS, "TFE_Settings::init failed");
		zzdf_log_heap("settings");
	}

	 









	{
		TFE_Settings_Graphics* g = TFE_Settings::getGraphicsSettings();
		u32 gw = s_sh[SH_FB_WIDTH];
		u32 gh = s_sh[SH_FB_HEIGHT];
		if (gw < ZZDF_GAME_W_MIN || gw > ZZDF_GAME_W_MAX ||
		    gh < ZZDF_GAME_H_MIN || gh > ZZDF_GAME_H_MAX)
		{
			/* An old launcher publishes nothing here. Fall back to
			   the original geometry rather than rendering garbage. */
			sprintf(line, "[ZZDF] resolution %ux%u out of range, using 320x200\n",
			        (unsigned)gw, (unsigned)gh);
			zzdf_log_puts(line);
			gw = 320; gh = 200;
		}
		g->gameResolution.x = (s32)gw;
		g->gameResolution.z = (s32)gh;
		sprintf(line, "[ZZDF] game %ux%u, %s\n", (unsigned)gw, (unsigned)gh,
		        (gw == 320u && gh == 200u) ? "RClassic_Fixed" : "RClassic_Float");
		zzdf_log_puts(line);
		g->rendererIndex    = 0;
		g->colorMode        = COLORMODE_8BIT;
		g->widescreen       = false;
		g->useBilinear      = false;
		g->useMipmapping    = false;
		g->bloomEnabled     = false;
		 








		TFE_Settings::getTempSettings()->skipLoadDelay = false;

		 










		 













		TFE_Settings::getSystemSettings()->gameQuitExitsToMenu = false;

		TFE_Settings_Game* gs = TFE_Settings::getGameSettings();
		if (gs)
		{
			gs->df_jsonAiLogics      = false;  /* vanilla alpha only  */
			gs->df_enableRecording   = false;  /* replay off          */
			gs->df_showReplayCounter = false;
		}
	}

	 




	zzdf_cfgopt_load();

	TFE_System::init(60.0f, false, "ZZDF-N1");
	/* Before game_init(): DarkForces' sound startup runs inside it and
	   calls ImInitializeDigitalAudio(), which registers ImUpdateWave as
	   the mix callback. The ring has to exist and be silent by then. */
	zzdf_audio_init();
	zzdf_midi_init();
	/* Upstream does this from main.cpp, which this port replaces - so
	   without it the midi device is never allocated, the command
	   buffer never initialised and setMaximumNoteLength() never
	   called. The device type is ignored: allocateMidiDevice() is
	   patched to always build the ring-backed one. */
	if (!TFE_MidiPlayer::init(0, MIDI_TYPE_OPL3))
	{
		zzdf_log_puts("[ZZDF] TFE_MidiPlayer::init failed, music off\n");
	}
	 
	zzdf_cfgopt_apply();
	game_init();
	zzdf_log_heap("game_init");
	TFE_Input::inputMapping_startup();

	 










	{
		TFE_Input::InputConfig* cfg = TFE_Input::inputMapping_get();
		u32 b, moved = 0;
		if (cfg && cfg->binds)
		{
			for (b = 0; b < cfg->bindCount; b++)
			{
				if (cfg->binds[b].action != TFE_Input::IADF_PAUSE) { continue; }
				if (cfg->binds[b].type != TFE_Input::ITYPE_KEYBOARD) { continue; }
				cfg->binds[b].keyCode = KEY_P;
				moved++;
				break;
			}
		}
		sprintf(line, "[ZZDF] pause -> P (%u binding moved)\n", (unsigned)moved);
		zzdf_log_puts(line);
	}

	IGame* game = createGame(Game_Dark_Forces);
	if (!game) return fail(ZZDF_FATAL_DF_INIT, "createGame returned null");
	zzdf_log_puts("[ZZDF] DarkForces init\n");

	 
























	TFE_SaveSystem::init();
	TFE_SaveSystem::setCurrentGame(game);
	zzdf_log_puts("[ZZDF] save system ready (Alt+F5 save, Alt+F9 load)\n");

	 












	 



















	TFE_Input::initReplays();
	{
		 



		const char** argv = s_zzdfArgv;
		int& argc = s_zzdfArgc;
		static std::string s_holdArgs[4];
		argv[argc++] = "zzdf";
		 










		if (s_sh[SH_DEMO_PLAY])
		{
			if (!zzdf_fs_exists("base_test.demo"))
			{
				s_sh[SH_DEMO_STATE] = ZZDF_DEMO_NO_FILE;
				zzdf_log_puts("[ZZDF] DEMO: base_test.demo is not mounted, "
				              "playing normally\n");
			}
			else
			{
				 














				TFE_Settings::getTempSettings()->exit_after_replay = true;

				TFE_Input::loadReplayFromPath("base_test.demo");
				s_sh[SH_DEMO_STATE] = ZZDF_DEMO_LOADED;

				{
					const std::vector<std::string>& ov =
						TFE_FrontEndUI::zzdf_getModOverrides();
					int held = 0, gotLevel = 0;
					for (size_t k = 0; k < ov.size() && argc < 5 && held < 4; k++)
					{
						if (ov[k].empty()) continue;
						 




						s_holdArgs[held] = ov[k];
						argv[argc++] = s_holdArgs[held].c_str();
						held++;
						if (ov[k].size() > 2 && ov[k][0] == '-' && ov[k][1] == 'l')
							gotLevel = 1;
						sprintf(line, "[ZZDF] DEMO: override %s\n", ov[k].c_str());
						zzdf_log_puts(line);
					}
					if (!gotLevel)
					{
						s_sh[SH_DEMO_STATE] = ZZDF_DEMO_NO_LEVEL;
						zzdf_log_puts("[ZZDF] DEMO: the replay gave no level "
						              "override - playback will not line up\n");
					}
				}
			}
		}

		if (!game->runGame(argc, argv, 0))
			return fail(ZZDF_FATAL_NO_LEVEL, "runGame failed");
	}
	zzdf_log_puts("[ZZDF] runGame returned, task system takes over\n");
	zzdf_log_heap("runGame");

	if (!(s_sh[SH_FB_ADDR] && s_sh[SH_FB_PITCH]))
		zzdf_log_puts("[ZZDF] no P96 backbuffer: headless, CRC only\n");

	 












	STEP(ZZDF_E_STEADY_LOOP);
	zzdf_log_puts("[ZZDF] steady loop\n");

	{
		u32 lastReport = zzdf_us_now();
		u32 ef0 = zzdf_rb_engine_frames();   /* engine frames at report */
		u32 pr0 = zzdf_rb_presents();        /* presents at report      */
		u32 tGame = 0, tPresent = 0;         /* us accumulated          */
		u32 nGame = 0, nPresent = 0;         /* samples for the averages*/
		 


		u32 logicSteps = 0, taskSteps = 0, lg0 = 0;
		u32 gameUsMax = 0;
		u32 s_pacerWaitSum = 0, s_pacerWaitN = 0;
		 


		u32 loopStart  = zzdf_us_now();
		int sawPresent = 0;
		int sawMission = 0;
		 





		const int demoAsked  = (s_sh[SH_DEMO_STATE] >= ZZDF_DEMO_LOADED);
		int sawDemoRun = 0, sawDemoEnd = 0;

		while (!TFE_System::quitMessagePosted())
		{
			u32 t0, t1, t2, efA, prA;
			if (s_sh[SH_CMD] == 1) break;      /* launcher STOP */
			if (s_sh[SH_ERROR]) return 1;

			/* Nothing on screen after 30 s means the engine never
			   produced a frame we could show - a real failure, and the
			   same one the old bootstrap watchdog caught. Once the
			   first frame IS presented the watchdog is done: the agent
			   menu may sit there as long as the player likes. */
			if (!sawPresent && (zzdf_us_now() - loopStart) > 30000000u
			    && s_sh[SH_FB_ADDR] && s_sh[SH_FB_PITCH])
				return fail(ZZDF_FATAL_NO_FRAME,
				            "watchdog: nothing presented after 30s");

			 












			if (s_sh[SH_PACER_ON])
			{
				u32 w0 = zzdf_us_now(), wNow, wSvc = w0;
				while (!zzdf_rb_target_ready())
				{
					if (s_sh[SH_CMD] == 1) break;
					zzdf_input_pump();
					wNow = zzdf_us_now();
					 















					if (wNow - wSvc >= 4000u)
					{
						wSvc = wNow;
						zzdf_midi_tick();
						zzdf_audio_pump();
					}
					if (wNow - w0 > 50000u) break;
				}
				{
					u32 waited = zzdf_us_now() - w0;
					s_pacerWaitSum += waited;
					s_pacerWaitN++;
					if (s_pacerWaitN >= 64)
					{
						s_sh[SH_PACER_WAIT_US] = s_pacerWaitSum / s_pacerWaitN;
						s_pacerWaitSum = 0; s_pacerWaitN = 0;
					}
				}
				if (s_sh[SH_CMD] == 1) break;
			}

			t0 = zzdf_us_now();

			TFE_System::update();
			/* iMuse's 144 Hz interrupt goes BEFORE the game step: it
			   runs the sound faders and the deferred commands, so the
			   effects queued last frame are live by the time the game
			   logic runs. The MIXER does not belong here - see the call
			   after task_run(). */
			zzdf_midi_tick();
			zzdf_input_pump();
			if (!TFE_Input::inputMapping_handleInputs())
			{
				continue;
			}

			 









			if (zzdf_cfgscreen_frame())
			{
				zzdf_audio_pump();
				TFE_RenderBackend::swap(true);
				zzdf_input_text_consumed();
				TFE_Input::endFrame();
				TFE_Input::inputMapping_endFrame();
				continue;
			}

			efA = zzdf_rb_engine_frames();
			 


			if (s_zzdfLoadCool) s_zzdfLoadCool--;    
			TFE_SaveSystem::update();

			 




























			{
				const char* loadReq = TFE_SaveSystem::loadRequestFilename();
				if (loadReq && loadReq[0] && s_zzdfLoadCool)
				{
					 



					if (s_zzdfLoadCool == ZZDF_LOAD_COOLDOWN)
						zzdf_log_puts("[ZZDF] LOAD request ignored (cooldown)\n");
				}
				else if (loadReq && loadReq[0])
				{
					char req[260];
					strncpy(req, loadReq, sizeof(req) - 1);
					req[sizeof(req) - 1] = 0;

					/* line[] is 128 bytes and req[] can be a full
					   path: bound the conversion, do not trust the
					   filename to be short. */
					sprintf(line, "[ZZDF] LOAD request: %.90s\n", req);
					zzdf_log_puts(line);

					 






					if (!zzdf_fs_exists(req))
					{
						s_sh[SH_SAVE_LOAD_FAIL] = ++s_zzdfLoadFail;
						sprintf(line, "[ZZDF] LOAD: no such save (%.80s)"
						        " - game left running\n", req);
						zzdf_log_puts(line);
						 






						TFE_Input::clearKeyPressed(KEY_F9);
						TFE_Input::inputMapping_clearKeyBinding(KEY_F9);
					}
					else
					{
						s_sh[SH_SAVE_LOADS] = ++s_zzdfLoads;

						freeGame(game);
						game = createGame(Game_Dark_Forces);
						if (!game)
							return fail(ZZDF_FATAL_DF_INIT,
							            "createGame returned null on load");
						TFE_SaveSystem::setCurrentGame(game);

						if (!TFE_SaveSystem::loadGame(req))
						{
							 














							s_sh[SH_SAVE_LOAD_FAIL] = ++s_zzdfLoadFail;
							sprintf(line, "[ZZDF] LOAD FAILED: %.80s"
							        " - restarting at the agent menu\n", req);
							zzdf_log_puts(line);

							freeGame(game);
							game = createGame(Game_Dark_Forces);
							if (!game)
								return fail(ZZDF_FATAL_DF_INIT,
								            "createGame returned null after "
								            "a failed load");
							TFE_SaveSystem::setCurrentGame(game);
							if (!game->runGame(s_zzdfArgc, s_zzdfArgv, 0))
								return fail(ZZDF_FATAL_NO_LEVEL,
								            "runGame failed while recovering "
								            "from a failed load");
							zzdf_log_puts("[ZZDF] agent menu restored\n");
						}
						else
						{
							zzdf_log_puts("[ZZDF] LOAD ok\n");
						}
						 













						TFE_Input::clearKeyPressed(KEY_F9);
						TFE_Input::inputMapping_clearKeyBinding(KEY_F9);
						zzdf_input_text_consumed();
						TFE_Input::endFrame();
						TFE_Input::inputMapping_endFrame();
						s_zzdfLoadCool = ZZDF_LOAD_COOLDOWN;

						/* The instance is new: nothing from this iteration
						   applies to it. Upstream returns to the top of its
						   loop for the same reason. */
						continue;
					}
				}
			}

			game->loopGame();
			logicSteps++;
			s_sh[SH_LOGIC_STEPS] = logicSteps;
			{
				int endInputFrame = TFE_Jedi::task_run() != 0;
				if (endInputFrame) { taskSteps++;
					s_sh[SH_TASK_STEPS] = taskSteps; }

				t1 = zzdf_us_now();

				 









				zzdf_audio_pump();

				/* swap() presents only if an engine frame is pending
				   and the previous PAN is acknowledged; otherwise it
				   drops and returns at once (never blocks). */
				prA = zzdf_rb_presents();
				TFE_RenderBackend::swap(true);
				t2 = zzdf_us_now();

				 




				if (!sawPresent && zzdf_rb_presents() != 0)
				{
					sawPresent = 1;
					STEP(ZZDF_E_FIRST_PRESENT);
					sprintf(line, "[ZZDF] frame 1 presented after %u ms "
					        "(agent menu)\n",
					        (unsigned)((t2 - loopStart) / 1000u));
					zzdf_log_puts(line);
				}
				if (!sawMission && zzdf_first_frame_seen())
				{
					sawMission = 1;
					sprintf(line, "[ZZDF] first mission frame after %u ms\n",
					        (unsigned)((t2 - loopStart) / 1000u));
					zzdf_log_puts(line);
				}

				 





				if (demoAsked)
				{
					const int playing = TFE_Input::isDemoPlayback() ? 1 : 0;
					if (playing && !sawDemoRun)
					{
						sawDemoRun = 1;
						s_sh[SH_DEMO_STATE] = ZZDF_DEMO_RUNNING;
						zzdf_log_puts("[ZZDF] DEMO: playback running\n");
					}
					else if (!playing && sawDemoRun && !sawDemoEnd)
					{
						sawDemoEnd = 1;
						s_sh[SH_DEMO_STATE] = ZZDF_DEMO_ENDED;
						zzdf_log_puts("[ZZDF] DEMO: playback ended cleanly\n");
					}
				}

				if (endInputFrame)
				{
					 

					zzdf_input_text_consumed();
					TFE_Input::endFrame();
					TFE_Input::inputMapping_endFrame();
				}
			}

			/* only iterations that produced an engine frame count for
			   GAME_US_AVG; only real presents for PRESENT_US_AVG */
			if (zzdf_rb_engine_frames() != efA) { tGame += (t1 - t0); nGame++; }
			if ((t1 - t0) > gameUsMax) { gameUsMax = t1 - t0;
				s_sh[SH_GAME_US_MAX] = gameUsMax; }
			if (zzdf_rb_presents() != prA)      { tPresent += (t2 - t1); nPresent++; }

			if (t2 - lastReport >= 1000000u)
			{
				u32 dt   = t2 - lastReport;
				u32 ef   = zzdf_rb_engine_frames() - ef0;
				u32 pr   = zzdf_rb_presents() - pr0;
				u32 efps = (u32)((unsigned long long)ef * 100000000ULL / dt);
				u32 pfps = (u32)((unsigned long long)pr * 100000000ULL / dt);
				u32 lg   = logicSteps - lg0;
				u32 lfps = (u32)((unsigned long long)lg * 100000000ULL / dt);
				s_sh[SH_ENGINE_FPS_X100]  = efps;
				s_sh[SH_PRESENT_FPS_X100] = pfps;
				s_sh[SH_RENDER_FPS_X100]  = efps;   /* RENDER == engine  */
				s_sh[SH_LOGIC_FPS_X100]   = lfps;   /* loopGame() rate   */
				lg0 = logicSteps;
				s_sh[SH_GAME_US_AVG]      = nGame    ? tGame    / nGame    : 0;
				s_sh[SH_PRESENT_US_AVG]   = nPresent ? tPresent / nPresent : 0;
				sprintf(line, "[ZZDF] logic=%u render=%u present=%u fps | "
				        "game=%uus conv=%uus gap=%uus flipwait=%uus drop=%u\n",
				        (unsigned)(lfps / 100), (unsigned)(efps / 100),
				        (unsigned)(pfps / 100),
				        (unsigned)s_sh[SH_GAME_US_AVG],
				        (unsigned)s_sh[SH_CONV_US_LAST],
				        (unsigned)s_sh[SH_READY_GAP_US_AVG],
				        (unsigned)s_sh[SH_FLIPWAIT_US_AVG],
				        (unsigned)s_sh[SH_PRESENT_DROPPED]);
				zzdf_log_puts(line);
				ef0 = zzdf_rb_engine_frames(); pr0 = zzdf_rb_presents();
				tGame = tPresent = 0; nGame = nPresent = 0;
				lastReport = t2;
			}
		}
	}

	/* Flush MIDI note-offs before the launcher closes the CAMD path. */
	zzdf_midi_shutdown();
	{
		u32 t0 = zzdf_us_now();
		while (s_sh[SH_MIDI_ON] &&
		       (s_sh[SH_MIDI_HEAD] != s_sh[SH_MIDI_TAIL]) &&
		       (zzdf_us_now() - t0) < 200000u)
		{
			/* the launcher drains from its own loop; just wait */
		}
	}
	 





	{
		u32 saved = zzdf_writeback_run();
		sprintf(line, "[ZZDF] writeback: %u file(s) handed to the launcher\n",
		        (unsigned)saved);
		zzdf_log_puts(line);
	}

	 



	zzdf_log_puts("[ZZDF] game loop left, writeback done\n");
	return 0;
}
