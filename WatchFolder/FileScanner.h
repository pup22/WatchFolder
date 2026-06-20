#pragma once

#include <string>
#include <vector>
#include <set>

class FileScanner {
public:
	FileScanner();
	// Возвращает true если найдено совпадение по правилам
	bool compareFolders(const std::string& folder1, const std::string& folder2,
						const std::vector<std::string>& patterns, bool whitelistMode,
						std::string& outMessage);

	size_t countFiles(const std::string& folder) const;
};
