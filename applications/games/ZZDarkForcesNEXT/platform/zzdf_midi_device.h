/* TFE MidiDevice bridge for CAMD output through shared memory. */
#pragma once
#include <TFE_Audio/midiDevice.h>

/* Publish the number of iMuse sequencer steps executed on Core1. */
extern "C" void zzdf_imuse_step(void);
 


extern "C" int  zzdf_midi_maxstep(void);
extern "C" void zzdf_midi_step_dropped(int n);
 
extern "C" int  zzdf_midi_use_sf2(void);
 
extern "C" int  zzdf_midi_dropdebt(void);
 

extern "C" void zzdf_sched_tick(unsigned int lateUs);
extern "C" void zzdf_sched_multi(void);

namespace TFE_Audio
{
	class ZZDFRingMidiDevice : public MidiDevice
	{
	public:
		ZZDFRingMidiDevice() {}
		~ZZDFRingMidiDevice() override { exit(); }

		MidiDeviceType getType() override { return MIDI_TYPE_OPL3; }

		void exit() override;
		/* False, so upstream scales CC7 by the master volume itself -
		   which is what a device with no volume control of its own
		   needs, and what out.0 is: whatever is listening there has no
		   idea what our settings say. */
		bool hasGlobalVolumeCtrl() override { return false; }
		const char* getName() override { return "CAMD out.0 (via Core1 ring)"; }

		u32  getOutputCount() override { return 1; }
		void getOutputName(s32 index, char* buffer, u32 maxLength) override;
		bool selectOutput(s32 index) override { (void)index; return true; }
		s32  getActiveOutput(void) override { return 0; }

		/* No synthesis here: the notes leave as MIDI and something else
		   makes the sound. canRender() false keeps synthesizeMidi() out
		   of the audio mixer entirely. */
		bool render(f32* buffer, u32 sampleCount) override
		{ (void)buffer; (void)sampleCount; return false; }
		bool canRender() override { return false; }

		void message(u8 type, u8 arg1, u8 arg2 = 0) override;
		void message(const u8* msg, u32 len) override;

		void noteAllOff() override;
		void setVolume(f32 volume) override { (void)volume; }
	};
}

 





extern "C" void zzdf_instr_off_orphan(int channel, int note);
extern "C" void zzdf_instr_on_double(int channel, int note);
