/*
 *   This file is part of Shenmue Mod Manager.
 *   By: LemonHaze420
 *
 *   Shenmue Mod Manager is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   Shenmue Mod Manager is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with Shenmue Mod Manager.  If not, see <https://www.gnu.org/licenses/>.
 *
 */
#include "Common.h"

#include <libzippp/libzippp.h>
#include "mini/ini.h"

#include "vdf_parser.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

using namespace std;
using namespace libzippp;
using namespace filesystem;

/* Game Installs */
struct GameInstall_t {
	enum {
		SHENMUE_HD	= 0,
		SHENMUE_3	= 1,
		INVALID		= 0xFF
	} type;

	std::wstring installName;
	std::wstring installDir;
};
std::vector<GameInstall_t> installedGames;

std::wstring installName;
std::wstring installDir;

/* Installation States */
bool g_bInstalled = false;
bool g_bAlreadyInstalled = false;
bool g_bFailedInstall = false;

/* Uninstallation States */
bool g_bUninstalled = false;
bool g_bAlreadyUninstalled = false;
bool g_bFailedUninstall = false;

/* Disable Mod States */
bool g_bDisabledMod = false;
bool g_bModNotInstalled = false;
bool g_bDisableMod_Failed = false;

Parser g_Parser;
std::vector<Mod> g_Mods;

void Init();
bool FindShenmue3(void);
#ifdef _DEBUG
void DebugCode();
#endif
void UpdateModListThread();
void RequestModListUpdate();
void UI_Render();

bool bActive = true;
bool dragWindow = false;
int titleBarHeight = 0;
bool VSync = false;
bool bSelected = false;
const char* currentselection = "";

Mod selectedMod;
ModManagerConfig* g_Config = nullptr;

static mINI::INIFile* conf = nullptr;
static mINI::INIStructure ini_data;

std::vector<UITexture> textures;

UITexture getTextureFromID(int ID) { return ID != ID_LAST ? textures[ID] : UITexture(); }

#define ImgButton(t) ImGui::ImageButton(getTextureFromID(t).texture, ImVec2((float)getTextureFromID(t).width, (float)getTextureFromID(t).height), ImVec2(0, 0), ImVec2(1, 1), 0, ImVec4(0.0,0.0,0.0,1.0))
#define SHEN1_2 "Shenmue I & II"
#define SHEN3	"Shenmue III"

constexpr size_t MAX_FILE_BUFFER = (64 * 20 * 820);
const char* games[] = { SHEN1_2, SHEN3 };
static const char* current_item = SHEN3;

std::vector <std::string> FindFilesOfExtension(std::string searchDir, std::string extension) {
	std::vector <std::string> res;
	for (auto& path : recursive_directory_iterator(searchDir)) {
		if (extension == path.path().extension().string()) {
			res.push_back(path.path().string());
			res.shrink_to_fit();
		}
	}
	return res;
}
std::string GetFilename(std::string fullPath, bool with_extension = true) {
	const size_t last_slash_idx = fullPath.find_last_of("\\/");
	if (std::string::npos != last_slash_idx) {
		fullPath.erase(0, last_slash_idx + 1);
	}
	if (!with_extension) {
		const size_t period_idx = fullPath.rfind('.');
		if (std::string::npos != period_idx)
			fullPath.erase(period_idx);
	}
	return fullPath;
}
std::string getApplicationPath() {
	TCHAR Path[512];
	TCHAR* tmp;
	GetModuleFileName(NULL, Path, 512);
	tmp = _tcsrchr(Path, '\\');
	*tmp = 0;
	return ws2s(Path);
}
uint64_t getHashFromFile(std::string filePath) {
	uint64_t outHash = NULL;

	if (!exists(filePath))
		return outHash;

	std::ifstream ifs(filePath, std::ios::binary);

	ifs.seekg(0, std::ios::end);
	size_t sz = ifs.tellg();
	ifs.seekg(0, std::ios::beg);

	unsigned char* buffer = new unsigned char[sz];
	memset(buffer, 0x00, sz);
	for (int i = 0; i < sz; ++i) 
		buffer[i] = ifs.get();
	ifs.close();

	outHash = crc32(crc32(0L, Z_NULL, 0), (const unsigned char*)buffer, sz);

	delete[] buffer;

	return outHash;
}

const bool ExtractAllFilesFromZip(const std::string& strDirectory, const std::string& strZipFile, size_t& uCount)
{
	bool bRes = false;
	uCount = 0;

	std::string strOutputDirectory(strDirectory);
	if (!exists(strDirectory)) {
		create_directory(strDirectory);
	}

	if (strOutputDirectory.at(strOutputDirectory.size() - 1) != '/'
		&& strOutputDirectory.at(strOutputDirectory.size() - 1) != '\\')
	{
		if (strOutputDirectory.find_first_of('/') != std::string::npos)
			strOutputDirectory.append("/");
		else
			strOutputDirectory.append("\\");
	}

	ZipArchive zf(strZipFile);
	if (!zf.open(ZipArchive::ReadOnly))
		return false; // Zip file couldn't be opened !

	std::vector<ZipEntry> entries = zf.getEntries();
	std::vector<ZipEntry>::iterator it;
	ZipEntry entry;

	// Determine the size (uncompressed) of all the zip entries to send it to the progress callback
	size_t uTotSize = 0;
	size_t uWrittenBytes = 0;
	for (it = entries.begin(); it != entries.end(); ++it)
	{
		entry = *it;
		if (entry.isFile())
			uTotSize += entry.getSize();
	}

	for (it = entries.begin(); it != entries.end(); ++it)
	{
		entry = *it;
		std::string strEntryName = entry.getName();
		size_t uSize = entry.getSize();
		size_t uCRC = entry.getCRC();

		// in rare cases, a directory might be coded incorrectly in a zip file : no '/' is appended at the
		// end of its name, that's why I check uCRC and uSize...
		if (entry.isDirectory() || uSize == 0 && uCRC == 0) {
			if (!create_directory(strOutputDirectory + strEntryName)) {}

			++uCount;
		}
		else if (entry.isFile()) // // Extract Zip entry to a file.
		{
			// to avoid copying a huge zip entry to main memory and causing a memory allocation failure
			// a buffer will be used instead !
			std::ofstream ofUnzippedFile(strOutputDirectory + strEntryName, std::ofstream::binary);
			if (ofUnzippedFile)
			{
				if (entry.readContent(ofUnzippedFile, ZipArchive::Current, MAX_FILE_BUFFER) == 0)
				{
					uWrittenBytes += uSize;
					++uCount;
				}
				else
				{
					wchar_t* messageBuf = new wchar_t[2048];
					memset(messageBuf, 0x00, 2048);
					wsprintf(messageBuf, L"[ERROR] Encountered an error while writing : %s\n", s2ws((strOutputDirectory + strEntryName)).c_str());
					MessageBox(NULL, messageBuf, _T(APP_STR), MB_ICONERROR | MB_OK);
					delete[] messageBuf;

					continue;
				}
			}
			else {
				wchar_t* messageBuf = new wchar_t[2048];
				memset(messageBuf, 0x00, 2048);
				wsprintf(messageBuf, L"[ERROR] Encountered an error while creating : %s\n", s2ws((strOutputDirectory + strEntryName)).c_str());
				MessageBox(NULL, messageBuf, _T(APP_STR), MB_ICONERROR | MB_OK);
				delete[] messageBuf;
			}

			ofUnzippedFile.close();
		}
	}
	bRes = (zf.getNbEntries() == uCount);
	zf.close();

	return bRes;
}
int extractFiles(std::string inZip, std::string outDest) {
	size_t cnt = -1;
	bool bRes = ExtractAllFilesFromZip(outDest, inZip, cnt);
	return bRes ? (int)cnt : 0;
}
std::vector<std::string> getTOCFromZip(std::string inZip) {
	std::vector<std::string> result;
	ZipArchive zf(inZip);
	zf.open(ZipArchive::ReadOnly);
	std::vector<ZipEntry> entries = zf.getEntries();
	std::vector<ZipEntry>::iterator it;
	for(it=entries.begin() ; it!=entries.end(); ++it) {
	  ZipEntry entry = *it;
	  std::string name = entry.getName();
	  result.push_back(name);
	  result.size() % 8 ? result.shrink_to_fit() : (void)0;
	}
	zf.close();
	return result;
}
static size_t write_data(void* ptr, size_t size, size_t nmemb, void* stream)
{
	size_t written = fwrite(ptr, size, nmemb, (FILE*)stream);
	return written;
}
static float g_CurrentDLProgress = 0.f;
static size_t progress_callback(void* ptr, curl_off_t  TotalDownloadSize, curl_off_t  finishedDownloadSize, curl_off_t  TotalToUpload, curl_off_t  NowUploaded) {
	curl_off_t processed = (TotalDownloadSize > TotalToUpload) ? finishedDownloadSize : NowUploaded;
	curl_off_t total = (TotalDownloadSize > TotalToUpload) ? TotalDownloadSize : TotalToUpload;
	if (processed == 0 || total == 0)
		return 0;

	g_CurrentDLProgress = ((float)processed / (float)total) * 1.0f;
	return 0;
}

static bool g_bDownloading = false;
static bool downloadFile(std::string URL, std::string Destination) {
	bool result = false;
	g_bDownloading = true;
	CURL* curl_handle;
	FILE* pagefile;
	curl_global_init(CURL_GLOBAL_ALL);
	curl_handle = curl_easy_init();
	curl_easy_setopt(curl_handle, CURLOPT_URL, URL.c_str());
#ifdef _DEBUG
	curl_easy_setopt(curl_handle, CURLOPT_VERBOSE, 1L);
#else
	curl_easy_setopt(curl_handle, CURLOPT_VERBOSE, 0L);
#endif
	curl_easy_setopt(curl_handle, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(curl_handle, CURLOPT_XFERINFOFUNCTION, progress_callback);
	curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_data);
	pagefile = fopen(Destination.c_str(), "wb");
	if (pagefile) {
		curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, pagefile);
		curl_easy_perform(curl_handle);
		fclose(pagefile);
		result = true;
	}
	curl_easy_cleanup(curl_handle);
	curl_global_cleanup();
	g_bDownloading = false;
	return result;
}
std::string getFilenameFromURL(std::string inURL) {
	return std::string(strrchr(inURL.c_str(), '/') + 1);
}
bool IsForkliftInstalled() {
	return	exists(g_Config->getExecutableDirectory().append("\\Forklift.asi")) &&
			exists(g_Config->getExecutableDirectory().append("\\dsound.dll")) ? true : false;
}

void Quit()
{
	// flush any INI changes before quitting..
	if (conf)
		conf->write(ini_data);

	::PostQuitMessage(0);
}
bool IsModInstalled(Mod mod) {
	std::string fName = getFilenameFromURL(mod.ContentURL);
	std::string dlPath = g_Config->getDownloadDirectory().append("\\").append(fName);
	if (!exists(dlPath)) 
		downloadFile(mod.ContentURL, dlPath);

	std::vector<std::string> TOC = getTOCFromZip(dlPath);
	int cnt = 0, total = (int)TOC.size();
	for (auto file : TOC) {
		std::string filePath = (!mod.CustomExtractDir.empty() ? 
									g_Config->getInstallDir().append("\\").append(mod.CustomExtractDir) 
									: mod.IsScriptMod ?		g_Config->getExecutableDirectory()
																:	g_Config->getPAKDirectory()).append(file);
		if (exists(filePath)) 
			++cnt;
	}
	return cnt == total;
}

bool InstallModByRef(Mod mod) {
	bool result = false;
	std::string fName = getFilenameFromURL(mod.ContentURL);
	std::string dlPath = g_Config->getDownloadDirectory().append("\\").append(fName);

	// before we install, we gotta check if there's any files2backup..
	for (auto file : mod.BackupFiles) {
		std::string tmpPath = g_Config->getInstallDir().append("\\").append(file);
		if (exists(tmpPath)) 
			rename(tmpPath, tmpPath + ".orig");
	}

	if (!exists(dlPath))
		downloadFile(mod.ContentURL, dlPath);

	if (exists(dlPath)) {
		return extractFiles(dlPath, !mod.CustomExtractDir.empty() ? g_Config->getInstallDir().append("\\").append(mod.CustomExtractDir) : mod.IsScriptMod ? g_Config->getExecutableDirectory() : g_Config->getPAKDirectory()) > 0;
	}
	return false;
}
bool InstallForklift() {
	if (!IsForkliftInstalled() && g_Mods.size() > 0)
		return InstallModByRef(g_Mods[0]);
	else return false;
}
bool InstallMod() {
	if (IsModInstalled(selectedMod)) {
		g_bAlreadyInstalled = true;
		return false;
	}
	
	if (selectedMod.RequiresSigBypass && !IsForkliftInstalled())
		InstallForklift();

	bool result = false;
	std::string fName = getFilenameFromURL(selectedMod.ContentURL);
	std::string dlPath = g_Config->getDownloadDirectory().append("\\").append(fName);

	// before we install, we gotta check if there's any files2backup..
	for (auto file : selectedMod.BackupFiles) {
		std::string tmpPath = g_Config->getInstallDir().append("\\").append(file);
		if (exists(tmpPath))
			rename(tmpPath, tmpPath + ".orig");
	}

	if (!exists(dlPath)) 
		downloadFile(selectedMod.ContentURL, dlPath);

	if (exists(dlPath)) {
		g_bInstalled = extractFiles(dlPath, !selectedMod.CustomExtractDir.empty() ? 
												g_Config->getInstallDir().append("\\").append(selectedMod.CustomExtractDir) 
												: 
												selectedMod.IsScriptMod ? 
														g_Config->getExecutableDirectory() 
														: 
														g_Config->getPAKDirectory()) > 0;
		result = g_bInstalled;
	} else {
		g_bFailedInstall = true;
		result = false;
	} 
	return result;
}
bool DisableMod() {
	if (!IsModInstalled(selectedMod)) {
		g_bModNotInstalled = true;
		return false;
	}

	/// @TODO : Fix by utilising a TOC described in mod_list
	bool result = false;
	std::string fName = getFilenameFromURL(selectedMod.ContentURL);
	std::string dlPath = g_Config->getDownloadDirectory().append("\\").append(fName);
	if (!exists(dlPath)) 
		downloadFile(selectedMod.ContentURL, dlPath);

	std::vector<std::string> TOC = getTOCFromZip(dlPath);
	int cnt = 0, total = (int)TOC.size();
	for (auto file : TOC) {
		if (!strstr(file.c_str(), "."))
			continue;

		// we don't want to remove any files listed as BackupFiles..
		if (std::find(selectedMod.BackupFiles.begin(), selectedMod.BackupFiles.end(), file) != selectedMod.BackupFiles.end())
			continue;
		
		std::string filePath = (!selectedMod.CustomExtractDir.empty() ? g_Config->getInstallDir().append("\\").append(selectedMod.CustomExtractDir) : selectedMod.IsScriptMod ? g_Config->getExecutableDirectory() : g_Config->getPAKDirectory()).append(file);
		if (exists(filePath)) {
			remove(filePath);
			++cnt;
		}
	}

	// now we have to restore the backup files
	for (auto file : selectedMod.BackupFiles) {
		std::string tmpPath = g_Config->getInstallDir().append("\\").append(file);
		if (exists(tmpPath + ".orig"))
			rename(tmpPath + ".orig", tmpPath);
	}

	g_bDisabledMod = cnt > 0;
	g_bDisableMod_Failed = !g_bDisabledMod;

	return g_bDisabledMod;
}
bool UninstallMod() {
	if (!IsModInstalled(selectedMod)) {
		g_bAlreadyUninstalled = true;
		return false;
	}

	std::string fName = getFilenameFromURL(selectedMod.ContentURL);
	std::string dlPath = g_Config->getDownloadDirectory().append("\\").append(fName);

	if (!exists(dlPath)) 
		downloadFile(selectedMod.ContentURL, dlPath);

	std::vector<std::string> TOC = getTOCFromZip(dlPath);
	int cnt = 0, total = (int)TOC.size();
	for (auto file : TOC) {
		g_CurrentDLProgress = ((float)cnt / (float)total) * 1.0f;

		if (!strstr(file.c_str(), "."))
			continue;

		// we don't want to remove any files listed as BackupFiles..
		if (std::find(selectedMod.BackupFiles.begin(), selectedMod.BackupFiles.end(), file) != selectedMod.BackupFiles.end())
			continue;

		std::string filePath = (!selectedMod.CustomExtractDir.empty() ? g_Config->getInstallDir().append("\\").append(selectedMod.CustomExtractDir) : selectedMod.IsScriptMod ? g_Config->getExecutableDirectory() : g_Config->getPAKDirectory()).append(file);
		if (exists(filePath)) {
			remove(filePath);
			++cnt;
		}
	}

	// now we have to restore the backup files
	for (auto file : selectedMod.BackupFiles) {
		std::string tmpPath = g_Config->getInstallDir().append("\\").append(file);
		if (exists(tmpPath + ".orig")) 
			rename(tmpPath + ".orig", tmpPath);
	}

	// clean up the DL dir..
	remove(dlPath);

	g_CurrentDLProgress = 1.0f;

	g_bUninstalled = cnt > 0;
	g_bFailedUninstall = !g_bUninstalled;
	return g_bUninstalled;
}

void RequestModListUpdate() {
	CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)&UpdateModListThread, NULL, 0, NULL);
}
void RequestInstallMod() {
	CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)&InstallMod, NULL, 0, NULL);
}
void RequestDisableMod() {
	CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)&DisableMod, NULL, 0, NULL);
}
void RequestUninstallMod() {
	CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)&UninstallMod, NULL, 0, NULL);
}

bool LoadTextureFromFile(const char* filename, ID3D11ShaderResourceView** out_srv, int* out_width, int* out_height)
{
	// Load from disk into a raw RGBA buffer
	int image_width = 0;
	int image_height = 0;
	unsigned char* image_data = stbi_load(filename, &image_width, &image_height, NULL, 4);
	if (image_data == NULL)
		return false;

	// Create texture
	D3D11_TEXTURE2D_DESC desc;
	ZeroMemory(&desc, sizeof(desc));
	desc.Width = image_width;
	desc.Height = image_height;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	desc.CPUAccessFlags = 0;

	ID3D11Texture2D* pTexture = NULL;
	D3D11_SUBRESOURCE_DATA subResource;
	subResource.pSysMem = image_data;
	subResource.SysMemPitch = desc.Width * 4;
	subResource.SysMemSlicePitch = 0;
	g_pd3dDevice->CreateTexture2D(&desc, &subResource, &pTexture);

	// Create texture view
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
	ZeroMemory(&srvDesc, sizeof(srvDesc));
	srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = desc.MipLevels;
	srvDesc.Texture2D.MostDetailedMip = 0;
	g_pd3dDevice->CreateShaderResourceView(pTexture, &srvDesc, out_srv);
	pTexture->Release();

	*out_width = image_width;
	*out_height = image_height;
	stbi_image_free(image_data);

	return true;
}
static bool LoadTextureFromBuffer(unsigned char* buffer, size_t size, ID3D11ShaderResourceView** out_srv, int* out_width, int* out_height)
{
	// Load from disk into a raw RGBA buffer
	int image_width = 0;
	int image_height = 0;

	unsigned char* image_data = stbi_load_from_memory(buffer, (int)size, &image_width, &image_height, NULL, 4);
	if (image_data == NULL)
		return false;

	// Create texture
	D3D11_TEXTURE2D_DESC desc;
	ZeroMemory(&desc, sizeof(desc));
	desc.Width = image_width;
	desc.Height = image_height;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	desc.CPUAccessFlags = 0;

	ID3D11Texture2D* pTexture = NULL;
	D3D11_SUBRESOURCE_DATA subResource;
	subResource.pSysMem = image_data;
	subResource.SysMemPitch = desc.Width * 4;
	subResource.SysMemSlicePitch = 0;
	g_pd3dDevice->CreateTexture2D(&desc, &subResource, &pTexture);

	// Create texture view
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
	ZeroMemory(&srvDesc, sizeof(srvDesc));
	srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = desc.MipLevels;
	srvDesc.Texture2D.MostDetailedMip = 0;
	g_pd3dDevice->CreateShaderResourceView(pTexture, &srvDesc, out_srv);
	pTexture->Release();

	*out_width = image_width;
	*out_height = image_height;
	stbi_image_free(image_data);

	return true;
}

std::vector<std::string> validInstalls;
bool IsInstallValid(std::string dir)
{
	if (!dir.empty())
	{
		auto tmp = dir.append("\\Shenmue3\\Binaries\\Win64\\Shenmue3-Win64-Shipping.exe");
		for (auto& install : validInstalls) {
			if (install == tmp)
				return true;
		}

		if (exists(tmp))
		{
			validInstalls.push_back(tmp);
			return true;
		}
		else
		{
			return false;
		}
	}
	return false;
}
bool IsInstallValid()
{
	for (auto& install : validInstalls) {
		if (install == g_Config->getExecutablePath())
			return true;
	}

	if (g_Config)
	{
		if (exists(g_Config->getExecutablePath()))
		{
			validInstalls.push_back(g_Config->getExecutablePath());
			return true;
		}
		else
		{
			return false;
		}
	}
	return false;
}

void RecheckShenmue3Install()
{
	FindShenmue3();
	if (installDir.empty() || installName.empty()) {
		int responseID = MessageBoxW(NULL, L"Shenmue III could not be found. Try searching again?", _T(APP_STR), MB_YESNO);
		switch (responseID) {
		case IDYES:
			Init();
			break;
		case IDNO:
			break;
		}
	}
	else
	{
		ini_data["General"]["InstallName"] = ws2s(installName);
		ini_data["General"]["InstallDirectory"] = ws2s(installDir);
	}
}

void Init()
{

	std::string basePath = getApplicationPath();
	if (basePath.empty())
		return;

	std::string configPath = basePath.append("\\S3MM");
	if (!exists(configPath))
		create_directories(configPath);

	// INI file reading
	std::string configFilePath = configPath.append("\\system.ini");
	conf = new mINI::INIFile(configFilePath);
	conf->read(ini_data);

	if (!ini_data.has("General") || ini_data.has("InstallName") || ini_data.has("InstallDirectory"))
	{
CHECK:
		// new config, so find the install dir, if we can't, ask if we should search again. 
		RecheckShenmue3Install();
	}
	else {
		// just read the values...
		installName = s2ws(ini_data["General"]["InstallName"]);
		installDir = s2ws(ini_data["General"]["InstallDirectory"]);

		if (!IsInstallValid(ws2s(installDir))) {
			RecheckShenmue3Install();
		}
	}

	// initialize global config..
	g_Config = new ModManagerConfig(installName, installDir);
	
	if (!exists(g_Config->getDownloadDirectory()))
		create_directories(g_Config->getDownloadDirectory());

	if (!exists(g_Config->getPAKDirectory()))
		create_directories(g_Config->getPAKDirectory());

	RequestModListUpdate();

	// Initialize textures to be used in UI
	UITexture tmpTexture;
	ImportTexture(ID_Background, background_raw);
	ImportTexture(ID_InstallMod, installMod_raw);
	ImportTexture(ID_DisableMod, disableMod_raw);
	ImportTexture(ID_MoreInfo, moreInfo_raw);
	ImportTexture(ID_UninstallMod, uninstallMod_raw);
	ImportTexture(ID_UpdateModList, updateModList_raw);
	ImportTexture(ID_TopBanner, topBanner_raw);
	textures.shrink_to_fit();

}
void UI_Render()
{
	UITexture tex;
	//ImGui::GetStyle().ScaleAllSizes(ImGui_ImplWin32_GetDpiScaleForHwnd(nullptr));

	//ImGui::SetNextWindowSize(ImVec2(SCR_WIDTH, SCR_HEIGHT));

	if (!titleBarHeight)
		titleBarHeight = (int)ImGui::GetFrameHeight();

	ImGui::Begin(APP_STR, &bActive, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);
	ImVec2 prevPos = ImGui::GetCursorPos();
#	ifdef _DEBUG
		ImVec2 windowSize = ImGui::GetWindowSize();
		ImGui::Text("X: %f \nY: %f\n", windowSize.x, windowSize.y);
#	endif

	// Setup background and top banner
	ImGui::SetCursorPos(prevPos);
	ImGui::Image(getTextureFromID(ID_Background).texture, ImVec2(SCR_WIDTH, SCR_HEIGHT)); /*- 20, SCR_HEIGHT - 40));*/
	ImGui::SetCursorPos(prevPos);
	ImGui::Image(getTextureFromID(ID_TopBanner).texture, ImVec2(getTextureFromID(ID_TopBanner).width - 636, getTextureFromID(ID_TopBanner).height - 48), ImVec2(), ImVec2(1,1));
	ImGui::SetCursorPos(prevPos);

	// Main panel
	prevPos.x += 75;
	prevPos.y += (SCR_HEIGHT / 2) - 75;
	ImGui::SetCursorPos(prevPos);
#	ifdef DPI_AWARE
		ImGui::Text("Window DPI scale: %f", ImGui::GetWindowDpiScale());
#	endif

	ImGui::SetNextWindowBgAlpha(0.75f);
	ImGui::BeginChild("##mods_general", ImVec2(SCR_WIDTH / 2, SCR_HEIGHT / 2), true);
	if (g_Config) {
		if ((installDir.empty() || installName.empty())) {
			ImGui::Text("ERROR: Shenmue III installation directory not found!\n");

			if (ImGui::Button("Try Again?"))
				FindShenmue3();
		}
		else {
#			ifdef _DEBUG
				ImGui::Text("%s found at %s\n", g_Config->getInstallName().c_str(), g_Config->getInstallDir().c_str());
#			endif
		}

#		ifdef _DEBUG
			ImGui::Checkbox("VSync", &VSync);
			ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
#		endif

		if (g_Mods.size() > 0 && (!installDir.empty() && !installName.empty())) {
			/* Mod selector list */
			if (ImGui::BeginCombo("##combo", currentselection)) {
				for (int i = 0; i < g_Mods.size(); ++i) {
					bool is_selected = (currentselection == g_Mods.at(i).Name.c_str());
					if (ImGui::Selectable(g_Mods.at(i).Name.c_str(), is_selected)) {
						currentselection = g_Mods.at(i).Name.c_str();
						selectedMod = g_Mods.at(i);
					}
					if (is_selected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
		}

		/* Mod Details */
		if (!selectedMod.Name.empty()) {
			ImGui::Text("Author: %s\n", selectedMod.Author.c_str());
			ImGui::TextWrapped("Description: %s\n", selectedMod.Description.c_str());
			ImGui::Text("Version: %s\n", selectedMod.Version.c_str());

			if (selectedMod.Name != "Forklift") {
				bool bNeedsForklift = selectedMod.RequiresSigBypass || selectedMod.IsScriptMod;
				ImGui::TextColored(bNeedsForklift ?
					IsForkliftInstalled() ?
					ImVec4(0.0, 1.0, 0.0, 1.0) :	/*		Green	*/
					ImVec4(1.0, 0.0, 0.0, 1.0)		/*		Red		*/ :
					ImVec4(1.0, 1.0, 1.0, 1.0),		/*		Black	*/
					"Requires Forklift? %s\n", bNeedsForklift ? "yes" : "no");
			}
		}

		/* Installation Progress */
		ImGui::SetNextWindowSize(ImVec2(100, 100));
		if (ImGui::BeginPopupModal("Installing", (bool*)0, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::Text("Please wait...\n");
			ImGui::ProgressBar(g_CurrentDLProgress, ImVec2(-1, 0.0f));
			ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
			if (g_bInstalled || g_bAlreadyInstalled || g_bFailedInstall)
				ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}

		/* Uninstallation Progress */
		ImGui::SetNextWindowSize(ImVec2(100, 100));
		if (ImGui::BeginPopupModal("Uninstalling", (bool*)0, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::Text("Please wait...\n");
			ImGui::ProgressBar(g_CurrentDLProgress, ImVec2(-1, 0.0f));
			ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
			if (g_bUninstalled || g_bAlreadyUninstalled || g_bFailedUninstall)
				ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}

		/* Disabling Progress */
		ImGui::SetNextWindowSize(ImVec2(100, 100));
		if (ImGui::BeginPopupModal("Disabling", (bool*)0, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::Text("Please wait...\n");
			ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
			if (g_bDisabledMod || g_bModNotInstalled || g_bDisableMod_Failed)
				ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}

		/* State Management */

		/* Installation States */
		if (g_bInstalled) {
			ImGui::OpenPopup("Installation Successful");
			g_bInstalled = false;
		}
		else if (g_bAlreadyInstalled) {
			ImGui::OpenPopup("Installation Aborted");
			g_bAlreadyInstalled = false;
		}
		else if (g_bFailedInstall) {
			ImGui::OpenPopup("Installation Failed");
			g_bFailedInstall = false;
		}
		/* Uninstallation States */
		else if (g_bUninstalled) {
			ImGui::OpenPopup("Uninstallation Successful");
			g_bUninstalled = false;
		}
		else if (g_bAlreadyUninstalled) {
			ImGui::OpenPopup("Uninstallation Aborted");
			g_bAlreadyUninstalled = false;
		}
		else if (g_bFailedUninstall) {
			ImGui::OpenPopup("Uninstallation Failed");
			g_bFailedUninstall = false;
		}
		/* Disable Mod States */
		else if (g_bDisabledMod) {
			ImGui::OpenPopup("Disabled Mod");
			g_bDisabledMod = false;
		}
		else if (g_bModNotInstalled) {
			ImGui::OpenPopup("Disabling Mod Aborted");
			g_bModNotInstalled = false;
		}
		else if (g_bDisableMod_Failed) {
			ImGui::OpenPopup("Disable Mod Failed");
			g_bDisableMod_Failed = false;
		}

		/* Popups */
		if (ImGui::BeginPopupModal("Installation Successful", (bool*)0, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::Text("Mod has been installed successfully!");
			if (ImGui::Button("OK"))
				ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
		if (ImGui::BeginPopupModal("Installation Failed", (bool*)0, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::Text("ERROR: Mod was not able to be installed.");
			if (ImGui::Button("OK"))
				ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
		if (ImGui::BeginPopupModal("Installation Aborted", (bool*)0, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::Text("Selected mod is already installed!");
			if (ImGui::Button("OK"))
				ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}

		if (ImGui::BeginPopupModal("Uninstallation Successful", (bool*)0, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::Text("Uninstalled successfully");
			if (ImGui::Button("OK"))
				ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
		if (ImGui::BeginPopupModal("Uninstallation Failed", (bool*)0, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::Text("Uninstall failed.");
			if (ImGui::Button("OK"))
				ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
		if (ImGui::BeginPopupModal("Uninstallation Aborted", (bool*)0, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::Text("Mod already not installed!");
			if (ImGui::Button("OK"))
				ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}

		if (ImGui::BeginPopupModal("Disabled Mod", (bool*)0, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::Text("Mod disabled successfully.");
			if (ImGui::Button("OK"))
				ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
		if (ImGui::BeginPopupModal("Disable Mod Failed", (bool*)0, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::Text("Mod could not be disabled.");
			if (ImGui::Button("OK"))
				ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
		if (ImGui::BeginPopupModal("Disabling Mod Aborted", (bool*)0, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::Text("Mod already disabled!");
			if (ImGui::Button("OK"))
				ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
	}
	ImGui::EndChild();

	/* Mod Buttons */
	prevPos.x = (SCR_WIDTH / 2) + (SCR_WIDTH / 8);
	ImGui::SetCursorPos(prevPos);
	ImGui::BeginChild("##mod_buttons");
	if (!selectedMod.Name.empty()) {
		if (ImgButton(ID_InstallMod)) {
			ImGui::OpenPopup("Installing");
			RequestInstallMod();
		}
		if (ImgButton(ID_DisableMod)) {
			ImGui::OpenPopup("Disabling");
			RequestDisableMod();
		}
		if (ImgButton(ID_UninstallMod)) {
			RequestUninstallMod();
			ImGui::OpenPopup("Uninstalling");
		}
		if (ImgButton(ID_MoreInfo))
			ShellExecute(0, 0, s2ws("\"" + selectedMod.ReleaseURL + "\"").c_str(), 0, 0, SW_SHOW);
	}
	if (ImgButton(ID_UpdateModList))
		RequestModListUpdate();
	ImGui::EndChild();

	/* Game Selection */
	prevPos.x = ((SCR_WIDTH / 2) + (SCR_WIDTH / 3)) + 24;
	ImGui::SetCursorPos(prevPos);
	ImGui::BeginChild("##game_selection");
	if (ImGui::BeginCombo("##combo", current_item)) {
		for (int i = 0; i < IM_ARRAYSIZE(games); ++i) {
			bool is_selected = (current_item == games[i]);
			if (ImGui::Selectable(games[i], is_selected))
				current_item = games[i];
			if (is_selected) 
				ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}
	ImGui::EndChild();

	ImGui::End();
	if (!bActive)
		Quit();
}
void UpdateModListThread() {
	g_Parser = Parser(APP_MODS_URL);
	g_Parser.request();

	Json::Value root = g_Parser.get_json();
	if (root.isMember("General")) {
		if (root["General"].isMember("BuildURL") && root["General"].isMember("Version")) {
			std::string version = root["General"].get("Version", "").asCString();
			std::string buildURL = root["General"].get("BuildURL", "").asCString();

			// check if there's any s3mm updates
			if (!buildURL.empty() && !version.empty()) {
				std::string curVer = std::string(APP_VER);

				if (curVer != version) {
					ShellExecute(0, 0, s2ws("\"" + buildURL + "\"").c_str(), 0, 0, SW_SHOW);
					bActive = false;
				}
			}

			// now build the mods list
			g_Mods.clear();
			if (root.isMember("Mods")) {
				for (auto it : root["Mods"]) {
					Mod newMod;
					newMod.Name = it.get("Name", "").asString();
					newMod.Description = it.get("Desc", "").asCString();
					newMod.Author = it.get("Author", "").asString();
					newMod.Version = it.get("Version", "").asString();
					newMod.ReleaseURL = it.get("ReleaseURL", "").asString();
					newMod.ContentURL = it.get("ContentURL", "").asString();
					newMod.IsScriptMod = it.get("IsScriptMod", false).asBool();
					newMod.RequiresSigBypass = it.get("RequiresSigBypass", false).asBool();

					if (it.isMember("CustomExtractDir")) 
						newMod.CustomExtractDir = it.get("CustomExtractDir", "").asString();
					
					if (it.isMember("BackupFiles")) 
						for (auto file : it["BackupFiles"]) 
							newMod.BackupFiles.push_back(file.asString());
					
					g_Mods.push_back(newMod);
					g_Mods.size() % 16 ? g_Mods.shrink_to_fit() : (void)0;
				}
			}
		}
	}
}

wstring ReadRegValue(HKEY root, wstring key, wstring name)
{
	HKEY hKey;
	if (RegOpenKeyEx(root, key.c_str(), 0, KEY_READ, &hKey) != ERROR_SUCCESS)
		throw "Could not open registry key";

	DWORD type;
	DWORD cbData;
	if (RegQueryValueEx(hKey, name.c_str(), NULL, &type, NULL, &cbData) != ERROR_SUCCESS)
	{
		RegCloseKey(hKey);
		throw "Could not read registry value";
	}

	if (type != REG_SZ)
	{
		RegCloseKey(hKey);
		throw "Incorrect registry value type";
	}

	wstring value(cbData / sizeof(wchar_t), L'\0');
	if (RegQueryValueEx(hKey, name.c_str(), NULL, NULL, reinterpret_cast<LPBYTE>(&value[0]), &cbData) != ERROR_SUCCESS)
	{
		RegCloseKey(hKey);
		throw "Could not read registry value";
	}

	RegCloseKey(hKey);

	size_t firstNull = value.find_first_of(L'\0');
	if (firstNull != string::npos)
		value.resize(firstNull);

	return value;
}

bool FindShenmue3(void)
{
	bool result = false;
	HKEY hUninstKey = NULL;
	HKEY hAppKey = NULL;
	WCHAR sAppKeyName[1024];
	WCHAR sSubKey[1024];
	WCHAR sDisplayName[1024];
	WCHAR sInstallLocation[1024];
	const wchar_t* sRoot = L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall";
	long lResult = ERROR_SUCCESS;
	DWORD dwType = KEY_ALL_ACCESS;
	DWORD dwBufferSize = 0;

	/* EGS install detection */
	if (!result) {
		/// Phase 1 - Find Shenmue III installation via enumerating all sane EGS manifests 
		/// NOTES: This should find every legit Epic Games Store install, everytime.
		TCHAR szPath[MAX_PATH];
		if (SUCCEEDED(SHGetFolderPath(NULL, CSIDL_COMMON_APPDATA, NULL, 0, szPath))) {
			std::wstring programData = szPath;
			programData.append(_T("\\Epic\\EpicGamesLauncher\\Data\\Manifests\\"));

			if (exists(programData)) {
				// Found the directory, EGS must be installed..
				for (auto file : FindFilesOfExtension(ws2s(programData), ".item")) {
					Json::Value root;

					// read json
					std::ifstream tmp(file.c_str(), std::ifstream::binary);
					tmp >> root;

					// sanity check
					if (root.isMember("FormatVersion") && root.isMember("bIsIncompleteInstall") && root.isMember("MandatoryAppFolderName"))
					{
						if (!root["bIsIncompleteInstall"].asBool())
						{
							if (std::string(root["MandatoryAppFolderName"].asCString()) == "Shenmue3")
							{
								std::string newDir(root["InstallLocation"].asCString());
								std::string newName(root["MandatoryAppFolderName"].asCString());

								if (ws2s(installDir) != newDir) {
									installName = s2ws(newName);
									installDir = s2ws(newDir);

									result = true;
								}
							}
						}
					}
					else continue;
				}
			}
		}
	}
	if (!result) {
		/// Phase 2 - Find Shenmue III installation via enumerating the InstallationList member in LauncherInstalled.dat
		/// NOTES: Looks very strange, seems to be lumped in with my UE4 engine installs.

		TCHAR szPath[MAX_PATH];
		if (SUCCEEDED(SHGetFolderPath(NULL, CSIDL_COMMON_APPDATA, NULL, 0, szPath))) {
			std::wstring launcherInstalled = szPath;
			launcherInstalled.append(_T("\\Epic\\UnrealEngineLauncher\\LauncherInstalled.dat"));

			if (exists(launcherInstalled))
			{
				std::ifstream tmp(launcherInstalled.c_str(), std::ifstream::binary);
				Json::Value root;
				tmp >> root;

				if (root.isMember("InstallationList")) {
					for (auto it : root["InstallationList"]) {
						if (it.isMember("AppName") && it.isMember("InstallLocation")) {
							if (it.get("AppName", "").asString() == std::string("Pepper")) {
								if (installDir != s2ws(it.get("InstallLocation", "").asString().c_str())) {
									installDir = s2ws(it.get("InstallLocation", "").asString().c_str());
									installName = s2ws("Shenmue III");

									result = true;
								}
							}
						}
					}
				}
			}
		}
	}
	if (!result) {
		/// Phase 3 - Find Shenmue III installation via enumerating all "Uninstall" keys 
		/// NOTES: Seems to only really find CODEX installs.

		//Open the "Uninstall" key.
		if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, sRoot, 0, KEY_READ, &hUninstKey) != ERROR_SUCCESS) {
			return false;
		}

		//Enumerate all sub keys...
		for (DWORD dwIndex = 0; lResult == ERROR_SUCCESS; ++dwIndex)
		{
			dwBufferSize = sizeof(sAppKeyName);
			if ((lResult = RegEnumKeyEx(hUninstKey, dwIndex, sAppKeyName,
				&dwBufferSize, NULL, NULL, NULL, NULL)) == ERROR_SUCCESS)
			{
				//Open the sub key.
				wsprintf(sSubKey, L"%s\\%s", sRoot, sAppKeyName);
				if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, sSubKey, 0, KEY_READ, &hAppKey) != ERROR_SUCCESS)
				{
					RegCloseKey(hAppKey);
					RegCloseKey(hUninstKey);
					return false;
				}

				//Get the display name value from the application's sub key.
				dwBufferSize = sizeof(sDisplayName);
				if (RegQueryValueEx(hAppKey, L"DisplayName", NULL,
					&dwType, (unsigned char*)sDisplayName, &dwBufferSize) == ERROR_SUCCESS)
				{
					if (wcsstr(sDisplayName, L"Shenmue III"))
					{
						dwBufferSize = sizeof(sInstallLocation);
						if (RegQueryValueEx(hAppKey, L"InstallLocation", NULL,
							&dwType, (unsigned char*)sInstallLocation, &dwBufferSize) == ERROR_SUCCESS)
						{
							std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
							std::wstring tmp(sInstallLocation);

							// Set new installation variables
							if (installDir != tmp) {
								installDir = tmp;
								tmp = std::wstring(sDisplayName), installName = tmp;
#								ifndef RELEASE
									std::wcout << "Found " << installName << " at " << installDir << " [" << sSubKey << "]" << std::endl;
#								endif
								result = true;
							}
						}
						else result = false;
					}
				}
				else result = false;
				RegCloseKey(hAppKey);
			}
		}
		RegCloseKey(hUninstKey);
	}

	/* Steam install detection */
	if (!result)
	{
		auto steamDirectory = ws2s(ReadRegValue(HKEY_CURRENT_USER, L"SOFTWARE\\Valve\\Steam", L"SteamPath"));
		if (!steamDirectory.empty())
		{
			auto configVDF = steamDirectory.append("\\config\\config.vdf");

			// config file exists, so now we'll find the SteamLibrary path..
			if (exists(configVDF)) {
				std::ifstream file(configVDF);
				auto root = tyti::vdf::read(file);
				for (auto& child : root.childs) {
					if (child.first == "Software") {
						for (auto& innerChild : child.second.get()->childs) {
							if (innerChild.first == "Valve") {
								for (auto& innerInnerChild : innerChild.second.get()->childs) {
									if (innerInnerChild.first == "Steam") {
										if (auto SteamSegment = innerInnerChild.second.get()) {
											for (auto SteamAttributes : SteamSegment->attribs) {
												if (strstr(SteamAttributes.first.c_str(), "BaseInstallFolder"))
												{
													auto	installLabel = SteamAttributes.first.c_str(), 
															installLibrary = SteamAttributes.second.c_str();
													auto	tmpShenmueInstall = std::string(installLibrary).append("\\steamapps\\common\\ShenmueIII");
													
													if (IsInstallValid(tmpShenmueInstall))
													{
#														ifndef RELEASE
															printf("Found install at '%s'\n", tmpShenmueInstall.c_str());
#														endif

														std::string newName = std::string("Steam (").append(installLibrary).append(")");
														installName = s2ws(newName);
														installDir = s2ws(tmpShenmueInstall);

														result = true;
													}
												}
											}
										}
									}
								}
							}
						}
					}
				}
			}
		}
	}

	return result;
}

#ifdef _DEBUG
void DebugCode()
{
	AllocConsole();
	freopen_s((FILE**)stdout, "CONOUT$", "w", stdout);
	freopen_s((FILE**)stdin, "CONIN$", "r", stdin);
	freopen_s((FILE**)stderr, "CONOUT$", "w", stderr);

	std::cout << APP_STR << std::endl;
}
#endif