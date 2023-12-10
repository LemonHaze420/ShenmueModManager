/*
	Total Libs Used: imgui jsoncpp libzippp curl zlib mINI TinyTinni/ValveFileVDF
*/

#pragma once
#define RELEASE_DEBUG_BYPASS
#define DEBUG_CONSOLE
#define DEV_LIST
//#define DPI_AWARE

#define APP_TITLE       "Shenmue III Mod Manager"
#define APP_VER         "0.0.1"
#define APP_STR         APP_TITLE " v" APP_VER
#ifdef DEV_LIST
#define APP_MODS_URL    "https://wulinshu.com/api/mod_list_dev.json"
#else
#define APP_MODS_URL    "https://wulinshu.com/api/mod_list.json"
#endif

// could spend the time to figure this out, but for now, use (width + 4) and (height - 4)
#define SCR_WIDTH   1284//1280//1920
#define SCR_HEIGHT  796//800//1080
#define OKMSG(x) MessageBox(NULL, x, _T(APP_STR), MB_OK)

#define CURL_STATICLIB

#include <iostream>
#include <fstream>

#include <tchar.h>
#include <locale>
#include <codecvt>
#include <string>
#include <Shlobj.h>
#include <filesystem>	

#include <zlib.h>

#include "Parser.h"

#define IMGUI_DISABLE_DEMO_WINDOWS
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <d3d11.h>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <tchar.h>

#ifndef RELEASE_DEBUG_BYPASS
#ifdef _RELEASE
#define DEBUG_CONSOLE
#else
#undef DEBUG_CONSOLE
#endif
#endif

struct Mod {
	std::string Name;
	std::string Description;
	std::string Author;
	std::string Version;
	std::string ReleaseURL;
	std::string ContentURL;
	std::string CustomExtractDir;
	std::vector<std::string> BackupFiles;
	bool IsScriptMod;
	bool RequiresSigBypass;
};

static std::wstring s2ws(const std::string& str)
{
	using convert_typeX = std::codecvt_utf8<wchar_t>;
	std::wstring_convert<convert_typeX, wchar_t> converterX;

	return converterX.from_bytes(str);
}
static std::string ws2s(const std::wstring& wstr)
{
	using convert_typeX = std::codecvt_utf8<wchar_t>;
	std::wstring_convert<convert_typeX, wchar_t> converterX;

	return converterX.to_bytes(wstr);
}

class ModManagerConfig {
public:
	ModManagerConfig(std::string n, std::string d) : installName(s2ws(n)), installDir(s2ws(d)) {}
	ModManagerConfig(std::wstring n, std::wstring d) : installName(n), installDir(d) {}
	std::string getInstallName() {
		return ws2s(installName);
	}
	std::string getInstallDir() {
		return ws2s(installDir);
	}
	std::string getSystemDirectory() {
		return ws2s(installDir).append("\\S3MM");
	}
	std::string getDownloadDirectory() {
		return getSystemDirectory().append("\\Downloads");
	}
	std::string getPAKDirectory() {
		return ws2s(installDir).append("\\Shenmue3\\Content\\Paks\\~mods\\");
	}
	std::string getExecutableDirectory() {
		return ws2s(installDir).append("\\Shenmue3\\Binaries\\Win64\\");
	}
	std::string getExecutablePath() {
		return getExecutableDirectory().append("Shenmue3-Win64-Shipping.exe");
	}
private:
	std::wstring installName;
	std::wstring installDir;
};

extern ID3D11Device * g_pd3dDevice;

/// UI resources
enum TextureID {
	ID_Background,
	ID_InstallMod,
	ID_DisableMod,
	ID_MoreInfo,
	ID_UninstallMod,
	ID_UpdateModList,
	ID_TopBanner,


	/*ID_Background_S1,
	ID_Background_S2,
	ID_InstallMod_S12,
	ID_DisableMod_S12,
	ID_MoreInfo_S12,
	ID_UninstallMod_S12,
	ID_UpdateModList_S12,*/

	ID_LAST
};

struct UITexture {
	TextureID ID;
	int width = 0;
	int height = 0;
	ID3D11ShaderResourceView* texture = NULL;
};
#define ImportTexture(i,raw) 		tmpTexture = UITexture();	\
									tmpTexture.ID = i; \
									LoadTextureFromBuffer(raw, sizeof(raw), &tmpTexture.texture, &tmpTexture.width, &tmpTexture.height); \
									textures.push_back(tmpTexture)

/// Raw PNG bytes here for all textures..
// ------------------------------------------------------------------------------------------------------------------------------
#include "Shenfist.h"		// ID_Background
#include "InstallMod.h"
#include "DisableMod.h"
#include "MoreInfo.h"
#include "UninstallMod.h"
#include "UpdateModList.h"
#include "TopBanner.h"