#pragma once

#include <string>

struct UpdateInfo {
	bool is_update_available = false;
	std::string new_version;
	std::string body;
	std::string download_url;
};

class AutoUpdater {
public:
	// Проверяет наличие нового релиза на GitHub для репозитория pup22/WatchFolder
	static UpdateInfo CheckForUpdates(const std::string& currentVersion);

	// Скачивает новый бинарник и запускает процесс обновления (создаёт и запускает .bat, затем завершает приложение)
	static void DownloadAndApplyUpdate(const std::string& downloadUrl);

	// Семантическое сравнение версий: true если newVer > currentVer
	static bool IsVersionNewer(const std::string& currentVer, const std::string& newVer);
	// Возвращает строку версии из ресурса FILEVERSION текущего исполняемого файла, например "1.2.3.4"
	static std::string GetCurrentFileVersion();
};
