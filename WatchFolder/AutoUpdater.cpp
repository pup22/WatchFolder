#include "pch.h"
#include "AutoUpdater.h"

#include <windows.h>
#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif
#include <winhttp.h>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include "nlohmann/json.hpp"

// Принудительно линковка с winhttp при необходимости
#pragma comment(lib, "winhttp.lib")
// Для функций работы с версиями файлов
#pragma comment(lib, "Version.lib")

using json = nlohmann::json;
namespace fs = std::filesystem;

std::string AutoUpdater::GetCurrentFileVersion() {
	wchar_t exePathBuf[MAX_PATH + 1] = {0};
	if (GetModuleFileNameW(NULL, exePathBuf, (DWORD)_countof(exePathBuf)) == 0) {
		return std::string();
	}

	// Получаем размер информации о версии
	DWORD handle = 0;
	DWORD size = GetFileVersionInfoSizeW(exePathBuf, &handle);
	if (size == 0) return std::string();

	std::vector<char> data(size);
	if (!GetFileVersionInfoW(exePathBuf, 0, size, data.data())) return std::string();

	VS_FIXEDFILEINFO* pInfo = nullptr;
	UINT infoLen = 0;
	if (!VerQueryValueW(data.data(), L"\\", reinterpret_cast<LPVOID*>(&pInfo), &infoLen)) return std::string();
	if (pInfo == nullptr) return std::string();

	DWORD verMS = pInfo->dwFileVersionMS;
	DWORD verLS = pInfo->dwFileVersionLS;
	DWORD major = HIWORD(verMS);
	DWORD minor = LOWORD(verMS);
	DWORD build = HIWORD(verLS);
	DWORD revision = LOWORD(verLS);

	char buf[64];
	sprintf_s(buf, "%u.%u.%u.%u", (unsigned)major, (unsigned)minor, (unsigned)build, (unsigned)revision);
	return std::string(buf);
}

static std::string ReadAllFromWinHttpResponse(HINTERNET hRequest) {
	std::string result;
	DWORD dwSize = 0;
	do {
		if (!WinHttpQueryDataAvailable(hRequest, &dwSize))
			break;

		if (dwSize == 0)
			break;

		std::vector<char> buffer(dwSize + 1);
		DWORD dwDownloaded = 0;
		if (!WinHttpReadData(hRequest, buffer.data(), dwSize, &dwDownloaded))
			break;

		buffer[dwDownloaded] = '\0';
		result.append(buffer.data(), dwDownloaded);
	} while (dwSize > 0);

	return result;
}

UpdateInfo AutoUpdater::CheckForUpdates(const std::string& currentVersion) {
	UpdateInfo info;

	HINTERNET hSession = nullptr, hConnect = nullptr, hRequest = nullptr;
	// Открываем сессии WinHTTP
	hSession = WinHttpOpen(L"WatchFolder-App/1.0",
						  WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
						  WINHTTP_NO_PROXY_NAME,
						  WINHTTP_NO_PROXY_BYPASS, 0);
	if (!hSession) {
		return info; // Сетевой стек недоступен
	}

	// Подключаемся к api.github.com
	hConnect = WinHttpConnect(hSession, L"api.github.com",
							  INTERNET_DEFAULT_HTTPS_PORT, 0);
	if (!hConnect) {
		WinHttpCloseHandle(hSession);
		return info;
	}

	// Создаём запрос
	hRequest = WinHttpOpenRequest(hConnect, L"GET",
								  L"/repos/pup22/WatchFolder/releases/latest",
								  NULL, WINHTTP_NO_REFERER,
								  WINHTTP_DEFAULT_ACCEPT_TYPES,
								  WINHTTP_FLAG_SECURE);
	if (!hRequest) {
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
		return info;
	}

	// Заголовок User-Agent обязателен
	LPCWSTR szHeaders = L"User-Agent: WatchFolder-App/1.0";
	BOOL bResult = WinHttpAddRequestHeaders(hRequest, szHeaders, -1L, WINHTTP_ADDREQ_FLAG_ADD);
	if (!bResult) {
		// не фатально — просто пытаемся дальше
	}

	// Отправляем запрос и получаем ответ
	if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
							WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
		WinHttpCloseHandle(hRequest);
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
		return info;
	}

	if (!WinHttpReceiveResponse(hRequest, NULL)) {
		WinHttpCloseHandle(hRequest);
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
		return info;
	}

	std::string response = ReadAllFromWinHttpResponse(hRequest);

	WinHttpCloseHandle(hRequest);
	WinHttpCloseHandle(hConnect);
	WinHttpCloseHandle(hSession);

	if (response.empty())
		return info;

	try {
		auto j = json::parse(response);
		std::string tag_name;
		if (j.contains("tag_name") && j["tag_name"].is_string())
			tag_name = j["tag_name"].get<std::string>();

		std::string body;
		if (j.contains("body") && j["body"].is_string())
			body = j["body"].get<std::string>();

		std::string download_url;
		if (j.contains("assets") && j["assets"].is_array()) {
			for (auto &asset : j["assets"]) {
				if (asset.contains("browser_download_url") && asset["browser_download_url"].is_string()) {
					std::string url = asset["browser_download_url"].get<std::string>();
					// выбираем файл WatchFolder.exe
					if (asset.contains("name") && asset["name"].is_string()) {
						std::string name = asset["name"].get<std::string>();
						if (name == "WatchFolder.exe") {
							download_url = url;
							break;
						}
					}
					// fallback: если имя не задано, возьмём первый
					if (download_url.empty())
						download_url = url;
				}
			}
		}

		std::string cleanTag = tag_name;
		if (!cleanTag.empty() && cleanTag.front() == 'v')
			cleanTag.erase(cleanTag.begin());

		if (!download_url.empty() && IsVersionNewer(currentVersion, cleanTag)) {
			info.is_update_available = true;
			info.new_version = cleanTag;
			info.body = body;
			info.download_url = download_url;
		}
	} catch (...) {
		// Парсинг json провалился — считаем, что обновления нет
		return info;
	}

	return info;
}

bool AutoUpdater::IsVersionNewer(const std::string& currentVer, const std::string& newVer) {
	// Удаляем ведущие 'v' если есть
	auto stripV = [](const std::string &s){
		if (!s.empty() && s.front() == 'v')
			return s.substr(1);
		return s;
	};

	std::string cur = stripV(currentVer);
	std::string neu = stripV(newVer);

	std::vector<int> a, b;
	std::stringstream sc(cur), sn(neu);
	std::string token;
	while (std::getline(sc, token, '.')) {
		try { a.push_back(std::stoi(token)); } catch(...) { a.push_back(0); }
	}
	while (std::getline(sn, token, '.')) {
		try { b.push_back(std::stoi(token)); } catch(...) { b.push_back(0); }
	}

	size_t n = std::min(a.size(), b.size());
	a.resize(n, 0);
	b.resize(n, 0);

	for (size_t i = 0; i < n; ++i) {
		if (b[i] > a[i]) return true;
		if (b[i] < a[i]) return false;
	}
	return false; // равны
}

// Вспомогательная функция: скачивает по полному URL в указанный файл
static bool DownloadFileToPath(const std::string& url, const fs::path& outPath) {
	URL_COMPONENTS urlComp;
	ZeroMemory(&urlComp, sizeof(urlComp));
	urlComp.dwStructSize = sizeof(urlComp);

	// задаём буферы чтобы WinHttpCrackUrl заполнил их
	wchar_t hostName[256] = {0};
	wchar_t urlPath[4096] = {0};
	urlComp.lpszHostName = hostName;
	urlComp.dwHostNameLength = _countof(hostName);
	urlComp.lpszUrlPath = urlPath;
	urlComp.dwUrlPathLength = _countof(urlPath);

	std::wstring wurl;
	{
		// преобразуем в wide
		int needed = MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, NULL, 0);
		wurl.resize(needed - 1);
		MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, &wurl[0], needed);
	}

	if (!WinHttpCrackUrl(wurl.c_str(), (DWORD)wurl.length(), 0, &urlComp))
		return false;

	std::wstring host(urlComp.lpszHostName, urlComp.dwHostNameLength);
	std::wstring path(urlComp.lpszUrlPath, urlComp.dwUrlPathLength);
	INTERNET_PORT port = urlComp.nPort;
	BOOL useHttps = (urlComp.nScheme == INTERNET_SCHEME_HTTPS);

	HINTERNET hSession = WinHttpOpen(L"WatchFolder-App/1.0",
									 WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
									 WINHTTP_NO_PROXY_NAME,
									 WINHTTP_NO_PROXY_BYPASS, 0);
	if (!hSession) return false;

	HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), port, 0);
	if (!hConnect) { WinHttpCloseHandle(hSession); return false; }

	HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(), NULL,
											WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
											useHttps ? WINHTTP_FLAG_SECURE : 0);
	if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return false; }

	LPCWSTR szHeaders = L"User-Agent: WatchFolder-App/1.0";
	WinHttpAddRequestHeaders(hRequest, szHeaders, -1L, WINHTTP_ADDREQ_FLAG_ADD);

	if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
		WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return false;
	}
	if (!WinHttpReceiveResponse(hRequest, NULL)) {
		WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return false;
	}

	std::ofstream ofs(outPath, std::ios::binary);
	if (!ofs.is_open()) {
		WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return false;
	}

	DWORD dwSize = 0;
	do {
		if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
		if (dwSize == 0) break;
		std::vector<char> buffer(dwSize);
		DWORD dwDownloaded = 0;
		if (!WinHttpReadData(hRequest, buffer.data(), dwSize, &dwDownloaded)) break;
		ofs.write(buffer.data(), dwDownloaded);
	} while (dwSize > 0);

	ofs.close();

	WinHttpCloseHandle(hRequest);
	WinHttpCloseHandle(hConnect);
	WinHttpCloseHandle(hSession);

	return fs::exists(outPath) && fs::file_size(outPath) > 0;
}

void AutoUpdater::DownloadAndApplyUpdate(const std::string& downloadUrl) {
	try {
		// Путь текущего исполняемого файла
		wchar_t exePathBuf[MAX_PATH + 1] = {0};
		GetModuleFileNameW(NULL, exePathBuf, (DWORD)_countof(exePathBuf));
		fs::path exePath = exePathBuf;
		fs::path dir = exePath.parent_path();

		fs::path newExe = dir / "WatchFolder_new.exe";
		fs::path batPath = dir / "update.bat";

		if (!DownloadFileToPath(downloadUrl, newExe)) {
			return; // Ошибка скачивания
		}

		// Создаём update.bat
		std::ostringstream bat;
		bat << "ping 127.0.0.1 -n 3 > nul\r\n";
		bat << "del \"WatchFolder.exe\"\r\n";
		bat << "move \"WatchFolder_new.exe\" \"WatchFolder.exe\"\r\n";
		bat << "start \"\" \"WatchFolder.exe\"\r\n";
		bat << "del %~f0\r\n";

		std::ofstream ofs(batPath, std::ios::binary);
		if (!ofs.is_open()) return;
		std::string batStr = bat.str();
		ofs << batStr;
		ofs.close();

		// Запускаем bat скрыто через cmd.exe
		std::wstring batW = batPath.wstring();
		std::wstring cmdLine = L"/C \"" + batW + L"\"";

		STARTUPINFOW si;
		PROCESS_INFORMATION pi;
		ZeroMemory(&si, sizeof(si));
		si.cb = sizeof(si);
		si.dwFlags = STARTF_USESHOWWINDOW;
		si.wShowWindow = SW_HIDE;
		ZeroMemory(&pi, sizeof(pi));

		// Create a mutable buffer for CreateProcess
		std::wstring app = L"cmd.exe";
		std::wstring cmdFull = app + L" " + cmdLine;
		std::vector<wchar_t> cmdBuf(cmdFull.begin(), cmdFull.end());
		cmdBuf.push_back(0);

		if (CreateProcessW(NULL, cmdBuf.data(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, dir.wstring().c_str(), &si, &pi)) {
			// Закрываем handles, не дожидаясь завершения bat
			CloseHandle(pi.hThread);
			CloseHandle(pi.hProcess);
		}

		// Просим приложение завершиться
		PostQuitMessage(0);
	} catch (...) {
		// ничего — не прерываем приложение
	}
}
