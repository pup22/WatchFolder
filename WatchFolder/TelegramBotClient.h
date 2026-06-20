#pragma once

#include <string>
#include <vector>
#include <optional>

struct TelegramUpdate {
	int id;
	std::string text;
	std::string chatId; // origin chat id as string
};

class TelegramBotClient {
public:
	TelegramBotClient(const std::string& token, const std::string& chatId);
	bool sendMessage(const std::string& text, std::string& err);
	// Установка команд бота (для отображения в интерфейсе Telegram при вводе "/")
	bool setCommands(const std::vector<std::pair<std::string, std::string>>& commands, std::string& err);
	// Возвращает новые команды (текст) если есть
	std::vector<TelegramUpdate> getUpdates(std::string& err);

private:
	std::string token;
	std::string chatId;
	int lastUpdateId = 0;
};
