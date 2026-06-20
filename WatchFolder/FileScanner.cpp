#include "pch.h"
#include "FileScanner.h"

#include <windows.h>
#include <set>
#include <sstream>

#include <Shlwapi.h>
#pragma comment(lib, "Shlwapi.lib")

static std::set<std::string> namesInFolder(const std::string& folder) {
	std::set<std::string> names;
	WIN32_FIND_DATAA fd;
	std::string spec = folder;
	if (!spec.empty() && spec.back() != '\\') spec += "\\";
	spec += "*";
	HANDLE h = FindFirstFileA(spec.c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE) return names;
	do {
		if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
			names.insert(fd.cFileName);
		}
	} while (FindNextFileA(h, &fd));
	FindClose(h);
	return names;
}

static bool matchesPattern(const std::string& name, const std::string& pattern) {
	if (pattern.empty()) return true;
	return name.find(pattern) != std::string::npos;
}

FileScanner::FileScanner() {}

bool FileScanner::compareFolders(const std::string& folder1, const std::string& folder2,
    const std::vector<std::string>& patterns, bool whitelistMode, std::string& outMessage) {

    std::set<std::string> a = namesInFolder(folder1);
    std::set<std::string> b = namesInFolder(folder2);

    if (a.empty()) {
        outMessage = "Папка источник пуста или недоступна";
        return false;
    }

    // 1. Фильтруем списки файлов согласно режимам
    auto filterList = [&](const std::set<std::string>& input) {
        std::set<std::string> filtered;
        for (const auto& name : input) {
            bool matches = false;
            if (patterns.empty()) {
				return input; // Если паттернов нет, не фильтруем
            }
            else {
                for (const auto& pat : patterns) {
                    if (PathMatchSpecA(name.c_str(), pat.c_str())) {
                        matches = true;
                        break;
                    }
                }
            }

            if (whitelistMode) {
                // В белом списке: берем только те, что совпали
                if (matches) filtered.insert(name);
            }
            else {
                // В черном списке: берем только те, что НЕ совпали
                if (!matches) filtered.insert(name);
            }
        }
        return filtered;
        };

	std::set<std::string> setA = filterList(a);
    std::set<std::string> setB = filterList(b);

    // 2. Сравниваем отфильтрованные наборы
    if (setA.empty() && setB.empty()) {
        outMessage = "После фильтрации файлы не найдены";
        return false;
    }

    if (setA == setB) {
        std::ostringstream ss;
        ss << "Полное совпадение списков файлов.\n" << setA.size() << "/" << setB.size();
        outMessage = ss.str();
        return true;
    }
    else {
        std::ostringstream ss;
        ss << "Списки файлов в папках не совпадают.\n" << setA.size() << "/" << setB.size();
        outMessage = ss.str();
        return false;
    }
}

size_t FileScanner::countFiles(const std::string& folder) const {
	size_t cnt = 0;
	WIN32_FIND_DATAA fd;
	std::string spec = folder;
	if (!spec.empty() && spec.back() != '\\') spec += "\\";
	spec += "*";
	HANDLE h = FindFirstFileA(spec.c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE) return 0;
	do { if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) ++cnt; } while (FindNextFileA(h, &fd));
	FindClose(h);
	return cnt;
}
