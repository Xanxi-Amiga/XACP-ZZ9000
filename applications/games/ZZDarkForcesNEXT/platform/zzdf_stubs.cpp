/* Minimal platform stubs for subsystems not used by the Core1 build. */
 















#include <TFE_System/types.h>
#include <TFE_System/system.h>   /* postQuitMessage, see setState() */
#include <TFE_FrontEndUI/frontEndUi.h>
#include <TFE_FrontEndUI/console.h>
#include <TFE_FrontEndUI/modLoader.h>
#include <TFE_A11y/accessibility.h>
#include <TFE_Audio/audioSystem.h>
#include <TFE_Audio/midiPlayer.h>
#include <TFE_Asset/imageAsset.h>
#include <TFE_ForceScript/forceScript.h>
#include <TFE_ForceScript/scriptInterface.h>
#include <cstring>

/* ------------------------- TFE_Console -------------------------- */
namespace TFE_Console
{
	static std::vector<CVar> s_emptyCVars;

	void registerCVarInt(const char* name, u32 flags, s32* var, const char* helpString)
	{ (void)name;(void)flags;(void)var;(void)helpString; }
	void registerCVarFloat(const char* name, u32 flags, f32* var, const char* helpString)
	{ (void)name;(void)flags;(void)var;(void)helpString; }
	void registerCVarBool(const char* name, u32 flags, bool* var, const char* helpString)
	{ (void)name;(void)flags;(void)var;(void)helpString; }
	void registerCVarString(const char* name, u32 flags, char* var, u32 maxLen, const char* helpString)
	{ (void)name;(void)flags;(void)var;(void)maxLen;(void)helpString; }
	void registerCommand(const char* name, ConsoleFunc func, u32 argCount, const char* helpString, bool repeat)
	{ (void)name;(void)func;(void)argCount;(void)helpString;(void)repeat; }

	void addSerializedCVarInt(const char* name, s32 value) { (void)name;(void)value; }
	void addSerializedCVarFloat(const char* name, f32 value) { (void)name;(void)value; }
	void addSerializedCVarBool(const char* name, bool value) { (void)name;(void)value; }
	void addSerializedCVarString(const char* name, const char* value) { (void)name;(void)value; }

	u32 getCVarCount() { return 0; }
	const CVar* getCVarByIndex(u32 index) { (void)index; return nullptr; }
	void addToHistory(const char* str) { (void)str; }
}

/* ------------------------ TFE_FrontEndUI ------------------------ */
namespace TFE_FrontEndUI
{
	static AppState s_state = APP_STATE_GAME;
	static AppState s_menuRet = APP_STATE_GAME;

	 















	void setState(AppState state)
	{
		s_state = state;
		if (state == APP_STATE_QUIT) { TFE_System::postQuitMessage(); }
	}
	void setMenuReturnState(AppState state) { s_menuRet = state; }
	void clearMenuState() {}
	void exitToMenu() {}
	IGame* getCurrentGame() { return nullptr; }
	bool isConsoleOpen() { return false; }
	bool toggleConsole() { return false; }
	void logToConsole(const char* str) { (void)str; }
	bool toggleEnhancements() { return false; }
	void modLoader_read() {}
	void setSelectedMod(const char* mod) { (void)mod; }
	 







	static std::vector<std::string> s_zzdfModOverrides;
	void setModOverrides(std::vector<std::string>& overrides)
	{
		s_zzdfModOverrides = overrides;
	}
	const std::vector<std::string>& zzdf_getModOverrides()
	{
		return s_zzdfModOverrides;
	}
	std::string getPlaybackFramerate() { return std::string(); }
	int getRecordFramerate() { return 0; }
}

/* --------------------------- TFE_A11Y --------------------------- */
namespace TFE_A11Y
{
	void clearActiveCaptions() {}
	bool cutsceneCaptionsEnabled() { return false; }
	bool gameplayCaptionsEnabled() { return false; }
	Vec2f drawCaptions() { Vec2f v = { 0 }; return v; }
	void onSoundPlay(char* name, CaptionEnv env) { (void)name;(void)env; }
	string toLower(string str)
	{
		for (size_t i = 0; i < str.size(); i++)
			if (str[i] >= 'A' && str[i] <= 'Z') str[i] += 32;
		return str;
	}
}

 


/* --------------------------- TFE_Image -------------------------- */
namespace TFE_Image
{
	void init() {}
	void shutdown() {}
	SDL_Surface* get(const char* imagePath) { (void)imagePath; return nullptr; }
	SDL_Surface* loadFromMemory(const u8* buffer, size_t size)
	{ (void)buffer;(void)size; return nullptr; }
	void free(SDL_Surface* image) { (void)image; }
	void freeAll() {}
	void writeImage(const char* path, u32 width, u32 height, u32* pixelData)
	{ (void)path;(void)width;(void)height;(void)pixelData; }
	size_t writeImageToMemory(u8* output, u32 srcw, u32 srch, u32 dstw,
	                          u32 dsth, const u32* pixelData)
	{ (void)output;(void)srcw;(void)srch;(void)dstw;(void)dsth;(void)pixelData; return 0; }
	void readImageFromMemory(SDL_Surface** output, size_t size, const u32* pixelData)
	{ if (output) *output = nullptr; (void)size;(void)pixelData; }
}

/* ------------------------ TFE_ForceScript ----------------------- */
namespace TFE_ForceScript
{
	ModuleHandle getModule(const char* moduleName)
	{ (void)moduleName; return nullptr; }
	ModuleHandle createModule(const char* moduleName, const char* filePath,
	                          bool allowReadFromArchive, u32 accessMask)
	{ (void)moduleName;(void)filePath;(void)allowReadFromArchive;(void)accessMask; return nullptr; }
	void deleteModule(const char* moduleName) { (void)moduleName; }
	FunctionHandle findScriptFuncByDecl(ModuleHandle modHandle, const char* funcDecl)
	{ (void)modHandle;(void)funcDecl; return nullptr; }
	FunctionHandle findScriptFuncByNameNoCase(ModuleHandle modHandle, const char* funcName)
	{ (void)modHandle;(void)funcName; return nullptr; }
	s32 execFunc(FunctionHandle funcHandle, s32 argCount, const ScriptArg* arg)
	{ (void)funcHandle;(void)argCount;(void)arg; return -1; }
	void serialize(Stream* stream) { (void)stream; }
}

namespace TFE_ScriptInterface
{
	void registerScriptInterface(ScriptAPI api) { (void)api; }
	void setAPI(ScriptAPI api, const char* searchPath)
	{ (void)api;(void)searchPath; }
	void reset() {}
}

/* --------------------------- reticle ---------------------------- */
#include <TFE_Game/reticle.h>
void reticle_enable(bool enable) { (void)enable; }

/* ------------------------ newlib leftovers ----------------------- */
extern "C" {
	int _getentropy(void* buf, size_t n)
	{
		unsigned char* p = (unsigned char*)buf;
		extern unsigned long long zzdf_gtimer_read(void);
		for (size_t i = 0; i < n; i++)
			p[i] = (unsigned char)(zzdf_gtimer_read() >> (i & 7));
		return 0;
	}
	int chmod(const char* p, int m) { (void)p;(void)m; return 0; }
	int utime(const char* p, void* t) { (void)p;(void)t; return 0; }
	int symlink(const char* a, const char* b) { (void)a;(void)b; return -1; }
	int ftruncate(int fd, long len) { (void)fd;(void)len; return 0; }
	void _init(void) {}
	void _fini(void) {}
	void* __dso_handle = 0;
}
