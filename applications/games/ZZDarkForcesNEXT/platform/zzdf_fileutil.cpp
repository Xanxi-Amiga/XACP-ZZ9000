/* FileUtil backend for the flat in-memory filesystem. */
#include <TFE_FileSystem/fileutil.h>
#include <cstring>
#include <cstdio>

extern "C" {
    int zzdf_fs_exists(const char* path);
    unsigned int zzdf_fs_mtime(const char* path);
    unsigned int zzdf_fs_count(void);
    void zzdf_fs_trace_dir(const char* path, int supported);
}
#include "zzdf_config.h"

static const zzdf_manifest_entry* zzdf_manifest_ptr()
{
    volatile u32* sh = (volatile u32*)ZZDF_SHARED_ARM;
    return (const zzdf_manifest_entry*)sh[SH_MANIFEST_ADDR];
}

namespace FileUtil
{
	/* Is this path the flat MemFS root? Everything the manifest holds
	   lives there under a bare basename; there is no directory tree. */
	static bool zzdf_is_root(const char* p)
	{
		if (!p) return true;
		while (*p == '.' || *p == '/' || *p == '\\') p++;
		return *p == 0;
	}

	 












	void readDirectory(const char* dir, const char* ext, FileList& fileList)
	{
		if (!zzdf_is_root(dir))
		{
			zzdf_fs_trace_dir(dir, 0);
			return;
		}
		zzdf_fs_trace_dir(dir, 1);
		const zzdf_manifest_entry* m = zzdf_manifest_ptr();
		u32 n = zzdf_fs_count();
		size_t extLen = ext ? strlen(ext) : 0;
		for (u32 i = 0; i < n; i++)
		{
			const char* name = m[i].name;
			size_t len = strlen(name);
			if (extLen && len > extLen)
			{
				const char* tail = name + len - extLen;
				bool match = true;
				for (size_t k = 0; k < extLen; k++)
				{
					char a = tail[k], b = ext[k];
					if (a >= 'A' && a <= 'Z') a += 32;
					if (b >= 'A' && b <= 'Z') b += 32;
					if (a != b) { match = false; break; }
				}
				if (!match) continue;
			}
			fileList.push_back(string(name));
		}
	}
	bool makeDirectory(const char* dir) { (void)dir; return true; }
	void getCurrentDirectory(char* dir) { strcpy(dir, "/"); }
	void getExecutionDirectory(char* dir) { strcpy(dir, "/"); }
	void setCurrentDirectory(const char* dir) { (void)dir; }

	void readSubdirectories(const char* dir, FileList& dirList)
	{
		(void)dir; (void)dirList;
	}

	void getFileNameFromPath(const char* path, char* name, bool includeExt)
	{
		const char* base = path;
		for (const char* p = path; *p; p++)
			if (*p == '/' || *p == '\\') base = p + 1;
		strcpy(name, base);
		if (!includeExt)
		{
			char* dot = strrchr(name, '.');
			if (dot) *dot = 0;
		}
	}
	void getFilePath(const char* filename, char* path)
	{
		const char* base = filename;
		for (const char* p = filename; *p; p++)
			if (*p == '/' || *p == '\\') base = p + 1;
		size_t n = (size_t)(base - filename);
		memcpy(path, filename, n);
		path[n] = 0;
	}
	void getFileExtension(const char* filename, char* extension)
	{
		const char* dot = strrchr(filename, '.');
		if (dot && dot[1]) strcpy(extension, dot + 1);
		else extension[0] = 0;
	}

	void copyFile(const char* srcFile, const char* dstFile)
	{
		(void)srcFile; (void)dstFile;
	}
	void deleteFile(const char* srcFile) { (void)srcFile; }

	bool exists(const char* path)
	{
		return zzdf_fs_exists(path) != 0;
	}
	/* Only the flat root exists. Claiming otherwise made
	   buildSearchPaths() create several search paths that all resolve
	   to the same flat namespace, so the same file could be found more
	   than once. */
	bool directoryExists(const char* path, char* outPath)
	{
		if (!zzdf_is_root(path))
		{
			zzdf_fs_trace_dir(path, 0);
			return false;
		}
		if (outPath) strcpy(outPath, path ? path : "");
		return true;
	}
	 









	u64 getModifiedTime(const char* path)
	{
		return (u64)zzdf_fs_mtime(path);
	}

	void fixupPath(char* path)
	{
		for (char* p = path; *p; p++)
			if (*p == '\\') *p = '/';
	}
	void convertToOSPath(const char* path, char* pathOS)
	{
		strcpy(pathOS, path);
	}

	void replaceExtension(const char* srcPath, const char* newExt, char* outPath)
	{
		strcpy(outPath, srcPath);
		char* dot = strrchr(outPath, '.');
		if (dot) strcpy(dot + 1, newExt);
		else { strcat(outPath, "."); strcat(outPath, newExt); }
	}
	void stripExtension(const char* srcPath, char* outPath)
	{
		strcpy(outPath, srcPath);
		char* dot = strrchr(outPath, '.');
		if (dot) *dot = 0;
	}
}
