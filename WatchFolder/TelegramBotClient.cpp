#include "pch.h"
#include "TelegramBotClient.h"

#include <windows.h>
#include <winhttp.h>
#include <sstream>
#include <string>
#include <vector>
#include <cctype>
#include <iomanip>

#include "nlohmann/json.hpp"

#pragma comment(lib, "winhttp.lib")

static std::string httpRequest(const std::string& url, const std::string& method,
			const std::string& body, bool& ok, const std::wstring& extraHeaders = L"Content-Type: application/json\r\n") {
	ok = false;
	const std::string prefix1 = "https://";
	std::string host, path;
	if (url.rfind(prefix1, 0) == 0) {
		size_t p = url.find('/', prefix1.size());
		if (p == std::string::npos) return "";
		host = url.substr(prefix1.size(), p - prefix1.size());
		path = url.substr(p);
	} else {
		return "";
	}

	HINTERNET hSession = WinHttpOpen(L"WatchFolder/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!hSession) return "";
	std::wstring whost(host.begin(), host.end());
	HINTERNET hConnect = WinHttpConnect(hSession, whost.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
	if (!hConnect) { WinHttpCloseHandle(hSession); return ""; }

	std::wstring wpath(path.begin(), path.end());
	std::wstring wmethod(method.begin(), method.end());
	HINTERNET hRequest = WinHttpOpenRequest(hConnect, wmethod.c_str(), wpath.c_str(), NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
	if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return ""; }

	if (!extraHeaders.empty()) {
		WinHttpAddRequestHeaders(hRequest, extraHeaders.c_str(), (ULONG)-1L, WINHTTP_ADDREQ_FLAG_ADD);
	}

	BOOL r = FALSE;
	if (!body.empty()) {
		r = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, (LPVOID)body.c_str(), (DWORD)body.size(), (DWORD)body.size(), 0);
	} else {
		r = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
	}
	if (!r) { WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return ""; }
	WinHttpReceiveResponse(hRequest, NULL);

	std::string result;
	do {
		DWORD avail = 0;
		if (!WinHttpQueryDataAvailable(hRequest, &avail)) break;
		if (avail == 0) break;
		std::vector<char> buffer(avail + 1);
		DWORD read = 0;
		if (WinHttpReadData(hRequest, buffer.data(), avail, &read) && read > 0) {
			result.append(buffer.data(), read);
		} else break;
	} while (true);

	WinHttpCloseHandle(hRequest);
	WinHttpCloseHandle(hConnect);
	WinHttpCloseHandle(hSession);

	ok = true;
	return result;
}

using json = nlohmann::json;

TelegramBotClient::TelegramBotClient(const std::string& token_, const std::string& chatId_) : token(token_), chatId(chatId_) {}

bool TelegramBotClient::sendMessage(const std::string& text, std::string& err) {
	if (token.empty() || chatId.empty()) { err = "Token or chatId empty"; return false; }
	std::string url = "https://api.telegram.org/bot" + token + "/sendMessage";
	json j;
	// chat_id may be numeric or string; try numeric if possible
	try {
		// if chatId looks like a number, send as number
		bool isNum = !chatId.empty() && (std::all_of(chatId.begin(), chatId.end(), [](char c){ return (c=='-' || isdigit((unsigned char)c)); }));
		if (isNum) j["chat_id"] = std::stoll(chatId);
		else j["chat_id"] = chatId;
	} catch(...) { j["chat_id"] = chatId; }
	j["text"] = text;
	std::string body = j.dump();
	bool ok;
	std::wstring hdr = L"Content-Type: application/json; charset=utf-8";
	std::string resp = httpRequest(url, "POST", body, ok, hdr);
	if (!ok) { err = "Network error"; return false; }
	try {
		auto rj = json::parse(resp);
		if (rj.contains("ok") && rj["ok"].get<bool>()) return true;
	} catch(...) {
		// fallthrough
	}
	err = "Telegram API error";
	return false;
}

// Установка команд бота (для отображения в интерфейсе Telegram при вводе "/")
bool TelegramBotClient::setCommands(const std::vector<std::pair<std::string, std::string>>& commands, std::string& err) {
	nlohmann::json j;
	j["commands"] = nlohmann::json::array();

	for (const auto& cmd : commands) {
		j["commands"].push_back({
			{"command", cmd.first},
			{"description", cmd.second}
			});
	}

	std::string url = "https://api.telegram.org/bot" + token + "/setMyCommands";
	bool ok = false;
	std::string jsonDump = j.dump();
	// Используем ваш существующий метод httpRequest
	std::string response = httpRequest(url, "POST", jsonDump, ok);

	OutputDebugStringA(("Sending to Telegram: " + jsonDump + "\n").c_str());
	OutputDebugStringA(("Response Telegram: " + response + "\n").c_str());
	OutputDebugStringA(("Url Telegram: " + url + "\n").c_str());

	if (!ok) {
		err = "Failed to set menu commands";
		return false;
	}
	return true;
}

// Примитивный парсер getUpdates: ищет сообщения с текстом
std::vector<TelegramUpdate> TelegramBotClient::getUpdates(std::string& err) {
	std::vector<TelegramUpdate> out;
	if (token.empty()) { err = "Token empty"; return out; }
	std::string url = "https://api.telegram.org/bot" + token + "/getUpdates?offset=" + std::to_string(lastUpdateId + 1);
	bool ok;
	std::string resp = httpRequest(url, "GET", std::string(), ok);
	if (!ok) { err = "Network error"; return out; }
	try {
		auto root = json::parse(resp);
		if (!root.contains("ok") || !root["ok"].get<bool>()) return out;
		if (!root.contains("result")) return out;
		for (auto &item : root["result"]) {
			if (!item.contains("update_id")) continue;
			int id = item["update_id"].get<int>();
			std::string text;
			if (item.contains("message") && item["message"].contains("text")) {
				text = item["message"]["text"].get<std::string>();
			} else if (item.contains("edited_message") && item["edited_message"].contains("text")) {
				text = item["edited_message"]["text"].get<std::string>();
			}
			if (id > lastUpdateId) {
				lastUpdateId = id;
				if (!text.empty()) {
					// convert UTF-8 to ANSI for app
					int wlen = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, NULL, 0);
					if (wlen > 0) {
						std::wstring w;
						w.resize(wlen);
						MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, &w[0], wlen);
						int alen = WideCharToMultiByte(CP_ACP, 0, w.c_str(), -1, NULL, 0, NULL, NULL);
						if (alen > 0) {
							std::string a;
							a.resize(alen);
							WideCharToMultiByte(CP_ACP, 0, w.c_str(), -1, &a[0], alen, NULL, NULL);
							if (!a.empty() && a.back() == '\0') a.pop_back();
							// try to extract chat id from message
							std::string originChatId;
							if (item.contains("message") && item["message"].contains("chat") && item["message"]["chat"].contains("id")) {
								try { originChatId = std::to_string(item["message"]["chat"]["id"].get<long long>()); } catch(...) { originChatId = item["message"]["chat"]["id"].get<std::string>(); }
							}
							out.push_back({id, a, originChatId});
						} else {
						std::string originChatId;
						if (item.contains("message") && item["message"].contains("chat") && item["message"]["chat"].contains("id")) {
							try { originChatId = std::to_string(item["message"]["chat"]["id"].get<long long>()); } catch(...) { originChatId = item["message"]["chat"]["id"].get<std::string>(); }
						}
						out.push_back({id, text, originChatId});
						}
					} else {
						out.push_back({id, text});
					}
				}
			}
		}
	} catch(...) {
		// parse error
	}
	return out;
}
