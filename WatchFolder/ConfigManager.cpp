#include "pch.h"
#include "ConfigManager.h"
#include "WatchFolder.h"

#include <afx.h>

ConfigManager::ConfigManager() {
	loadFromRegistry();
}

void ConfigManager::writeString(const char* key, const std::string& value) {
	CString name(key);
	CString val(value.c_str());
	AfxGetApp()->WriteProfileString(_T("Settings"), name, val);
}

void ConfigManager::setFolder1(const std::string& path) {
	folder1 = path; writeString("Folder1", path);
}

void ConfigManager::setFolder2(const std::string& path) {
	folder2 = path; writeString("Folder2", path);
}

void ConfigManager::setBotToken(const std::string& token) {
	botToken = token; writeString("BotToken", token);
}

void ConfigManager::setChatId(const std::string& id) {
	chatId = id; writeString("ChatId", id);
}

void ConfigManager::setExcludedPatterns(const std::vector<std::string>& pats) {
	excludedPatterns = pats;
	std::string joined;
	for (size_t i = 0; i < pats.size(); ++i) {
		if (i > 0) joined += ";";
		joined += pats[i];
	}
	writeString("ExcludedPatterns", joined);
}

void ConfigManager::setIncludedPatterns(const std::vector<std::string>& pats) {
	includedPatterns = pats;
	std::string joined;
	for (size_t i = 0; i < pats.size(); ++i) {
		if (i > 0) joined += ";";
		joined += pats[i];
	}
	writeString("IncludedPatterns", joined);
}

void ConfigManager::setUseExcluded(bool excluded) {
	useExcluded = excluded;
	AfxGetApp()->WriteProfileInt(_T("Settings"), _T("UseExcluded"), excluded ? 1 : 0);
}

void ConfigManager::save() {
	writeString("BotToken", botToken);
	writeString("ChatId", chatId);
	writeString("Folder1", folder1);
	writeString("Folder2", folder2);
	std::string excJoined;
	for (size_t i = 0; i < excludedPatterns.size(); ++i) {
		if (i > 0) excJoined += ";";
		excJoined += excludedPatterns[i];
	}
	writeString("ExcludedPatterns", excJoined);
	std::string incJoined;
	for (size_t i = 0; i < includedPatterns.size(); ++i) {
		if (i > 0) incJoined += ";";
		incJoined += includedPatterns[i];
	}
	writeString("IncludedPatterns", incJoined);
	AfxGetApp()->WriteProfileInt(_T("Settings"), _T("UseExcluded"), useExcluded ? 1 : 0);
}

std::string ConfigManager::getBotToken() const { return botToken; }
std::string ConfigManager::getChatId() const { return chatId; }
std::string ConfigManager::getFolder1() const { return folder1; }
std::string ConfigManager::getFolder2() const { return folder2; }
std::vector<std::string> ConfigManager::getPatterns() const { return patterns; }
std::vector<std::string> ConfigManager::getExcludedPatterns() const { return excludedPatterns; }
std::vector<std::string> ConfigManager::getIncludedPatterns() const { return includedPatterns; }
bool ConfigManager::isUseExcluded() const { return useExcluded; }
bool ConfigManager::isWhitelistMode() const { return whitelist; }

void ConfigManager::loadFromRegistry() {
	// Используем профиль приложения (реестр через CWinApp)
	CString token = AfxGetApp()->GetProfileString(_T("Settings"), _T("BotToken"), _T(""));
	CString chat = AfxGetApp()->GetProfileString(_T("Settings"), _T("ChatId"), _T(""));
	CString f1 = AfxGetApp()->GetProfileString(_T("Settings"), _T("Folder1"), _T(""));
	CString f2 = AfxGetApp()->GetProfileString(_T("Settings"), _T("Folder2"), _T(""));
	CString pats = AfxGetApp()->GetProfileString(_T("Settings"), _T("Patterns"), _T(""));
	CString excPats = AfxGetApp()->GetProfileString(_T("Settings"), _T("ExcludedPatterns"), _T(""));
	CString incPats = AfxGetApp()->GetProfileString(_T("Settings"), _T("IncludedPatterns"), _T(""));
	int w = AfxGetApp()->GetProfileInt(_T("Settings"), _T("Whitelist"), 0);
	int useExc = AfxGetApp()->GetProfileInt(_T("Settings"), _T("UseExcluded"), 1); // default excluded

	auto toString = [](const CString& s)->std::string {
		CT2A conv(s);
		return std::string(conv);
	};
	botToken = toString(token);
	chatId = toString(chat);
	folder1 = toString(f1);
	folder2 = toString(f2);
	std::string sp = toString(pats);
	patterns = split(sp, ';');
	std::string excStr = toString(excPats);
	excludedPatterns = split(excStr, ';');
	std::string incStr = toString(incPats);
	includedPatterns = split(incStr, ';');
	whitelist = (w != 0);
	useExcluded = (useExc != 0);
}

std::vector<std::string> ConfigManager::split(const std::string& s, char delim) {
	std::vector<std::string> out;
	size_t start = 0;
	while (start < s.size()) {
		size_t pos = s.find(delim, start);
		if (pos == std::string::npos) pos = s.size();
		if (pos > start) out.push_back(s.substr(start, pos - start));
		start = pos + 1;
	}
	return out;
}
