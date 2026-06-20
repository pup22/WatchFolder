#pragma once

#include <string>
#include <vector>

class ConfigManager {
public:
	ConfigManager();

	std::string getBotToken() const;
	std::string getChatId() const;
	std::string getFolder1() const;
	std::string getFolder2() const;
	std::vector<std::string> getPatterns() const;
	std::vector<std::string> getExcludedPatterns() const;
	std::vector<std::string> getIncludedPatterns() const;
	bool isUseExcluded() const; // true == excluded mode, false == included mode
	bool isWhitelistMode() const; // true == match (whitelist), false == exclude (legacy, same as !isUseExcluded)

	void setFolder1(const std::string& path);
	void setFolder2(const std::string& path);
	void setBotToken(const std::string& token);
	void setChatId(const std::string& id);
	void setExcludedPatterns(const std::vector<std::string>& pats);
	void setIncludedPatterns(const std::vector<std::string>& pats);
	void setUseExcluded(bool excluded);
	void save();
	void loadFromRegistry();

	// public helper for splitting
	std::vector<std::string> split(const std::string& s, char delim);

private:
	void writeString(const char* key, const std::string& value);

	std::string botToken;
	std::string chatId;
	std::string folder1;
	std::string folder2;
	std::vector<std::string> patterns;
	std::vector<std::string> excludedPatterns;
	std::vector<std::string> includedPatterns;
	bool useExcluded = true; // true == excluded mode, false == included mode
	bool whitelist = false;
};
