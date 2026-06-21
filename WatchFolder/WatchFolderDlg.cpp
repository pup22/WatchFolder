
// WatchFolderDlg.cpp: файл реализации
//

#include "pch.h"
#include "framework.h"
#include "WatchFolder.h"
#include "WatchFolderDlg.h"
#include "afxdialogex.h"
#include "ConfigManager.h"
#include "FileScanner.h"
#include "TelegramBotClient.h"
#include "AutoUpdater.h"

#include <thread>
#include <chrono>
#include <atomic>
#include <sstream>

#include "nlohmann/json.hpp"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

// Строковое описание GetLastError()
std::string GetLastErrorAsUTF8() {
	DWORD errorMessageID = ::GetLastError();
	if (errorMessageID == 0) return "No error";

	LPWSTR messageBuffer = nullptr;

	// 1. Получаем сообщение от системы в формате UTF-16 (Wide)
	size_t size = FormatMessageW(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL, errorMessageID, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPWSTR)&messageBuffer, 0, NULL
	);

	if (size == 0) return "Unknown error";

	// 2. Узнаем, сколько байт потребуется для UTF-8
	int utf8Size = WideCharToMultiByte(CP_UTF8, 0, messageBuffer, (int)size, NULL, 0, NULL, NULL);

	std::string utf8Message;
	if (utf8Size > 0) {
		utf8Message.resize(utf8Size);
		// 3. Конвертируем из UTF-16 в UTF-8
		WideCharToMultiByte(CP_UTF8, 0, messageBuffer, (int)size, &utf8Message[0], utf8Size, NULL, NULL);
	}

	// Освобождаем буфер, выделенный системой
	LocalFree(messageBuffer);

	return utf8Message;
}

// функция получения привилегий
bool EnableShutdownPrivilege() {
	HANDLE hToken;
	TOKEN_PRIVILEGES tkp;

	// Получаем токен процесса
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
		return false;

	// Получаем LUID для привилегии завершения работы
	if (!LookupPrivilegeValue(NULL, SE_SHUTDOWN_NAME, &tkp.Privileges[0].Luid)) {
		CloseHandle(hToken);
		return false;
	}

	tkp.PrivilegeCount = 1;
	tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

	// Включаем привилегию для текущего процесса
	if (!AdjustTokenPrivileges(hToken, FALSE, &tkp, 0, (PTOKEN_PRIVILEGES)NULL, 0)) {
		CloseHandle(hToken);
		return false;
	}

	CloseHandle(hToken);
	return (GetLastError() == ERROR_SUCCESS);
}

// Диалоговое окно CWatchFolderDlg

CWatchFolderDlg::CWatchFolderDlg(CWnd* pParent /*=nullptr*/)
	: CDialogEx(IDD_WATCHFOLDER_DIALOG, pParent)
{
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
}

CWatchFolderDlg::~CWatchFolderDlg() {
	// Остановим потоки
	running = false;
	if (threadCheck) { threadCheck->join(); delete threadCheck; threadCheck = nullptr; }
	if (threadPoll) { threadPoll->join(); delete threadPoll; threadPoll = nullptr; }
	delete bot; bot = nullptr;
	delete scanner; scanner = nullptr;
	delete config; config = nullptr;
	Shell_NotifyIcon(NIM_DELETE, &m_nid);
}

void CWatchFolderDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CWatchFolderDlg, CDialogEx)
	ON_WM_PAINT()
	ON_WM_QUERYDRAGICON()
	ON_BN_CLICKED(IDC_BUTTON_PATH1, &CWatchFolderDlg::OnBnClickedButtonPath1)
	ON_BN_CLICKED(IDC_BUTTON_PATH2, &CWatchFolderDlg::OnBnClickedButtonPath2)
	ON_BN_CLICKED(IDC_RADIO_EXCLUDED, &CWatchFolderDlg::OnBnClickedRadioExcluded)
	ON_BN_CLICKED(IDC_RADIO_INCLUDED, &CWatchFolderDlg::OnBnClickedRadioIncluded)
	ON_BN_CLICKED(IDOK, &CWatchFolderDlg::OnBnClickedOk)
	ON_EN_CHANGE(IDC_EDIT_TOKENBOT, &CWatchFolderDlg::OnEnChangeEditTokenbot)
	ON_EN_CHANGE(IDC_EDIT_USERID, &CWatchFolderDlg::OnEnChangeEditUserid)
	ON_EN_CHANGE(IDC_EDIT_EXCLUDED, &CWatchFolderDlg::OnEnChangeEditExcluded)
	ON_EN_CHANGE(IDC_EDIT_INCLUDED, &CWatchFolderDlg::OnEnChangeEditIncluded)
	ON_EN_CHANGE(IDC_EDIT_PATH1, &CWatchFolderDlg::OnEnChangeEditPath1)
	ON_EN_CHANGE(IDC_EDIT_PATH2, &CWatchFolderDlg::OnEnChangeEditPath2)
	ON_MESSAGE(WM_TRAYICON, &CWatchFolderDlg::OnTrayIcon)
	ON_COMMAND(ID_TRAY_RESTORE, &CWatchFolderDlg::OnTrayRestore)
	ON_COMMAND(ID_TRAY_EXIT, &CWatchFolderDlg::OnTrayExit)
	ON_MESSAGE(WM_UPDATE_AVAILABLE, &CWatchFolderDlg::OnUpdateAvailable)
	ON_BN_CLICKED(IDC_BUTTON_UPDATE, &CWatchFolderDlg::OnBnClickedButtonUpdate)
END_MESSAGE_MAP()


// Обработчики сообщений CWatchFolderDlg

BOOL CWatchFolderDlg::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	// Задает значок для этого диалогового окна.  Среда делает это автоматически,
	//  если главное окно приложения не является диалоговым
	SetIcon(m_hIcon, TRUE);			// Крупный значок
	SetIcon(m_hIcon, FALSE);		// Мелкий значок

	// Безопасная проверка обновлений в отдельном потоке
	HWND hWndDlg = m_hWnd; // Запоминаем хэндл окна для передачи в поток
	std::thread([hWndDlg]() {
		const std::string currentVersion = AutoUpdater::GetCurrentFileVersion();
		try {
			auto info = AutoUpdater::CheckForUpdates(currentVersion);
			if (info.is_update_available && !info.download_url.empty()) {
				// Отправляем сигнал главному окну. Передать выполнение в UI-поток!
				if (AutoUpdater::DownloadUpdate(info.download_url)) {
					::PostMessage(hWndDlg, WM_UPDATE_AVAILABLE, TRUE, 0);
				}
			}
		}
		catch (...) {
			// молча игнорируем ошибки сети/парсинга
		}
		}).detach();

	// Инициализация компонентов
	config = new ConfigManager();
	scanner = new FileScanner();
	
	// Создаем бота и добавляем командное меню
	if (!bot) {
		bot = new TelegramBotClient(config->getBotToken(), config->getChatId());

		// Регистрация команд
		std::vector<std::pair<std::string, std::string>> myCommands = {
			{"check", "Проверка"},
			{"shutdown", "Выключение компьютера"},
			{"cancel_shutdown", "Отменить выключение"}
		};

		std::string err;
		if (!bot->setCommands(myCommands, err)) {
			// Можно логировать ошибку
			OutputDebugStringA(err.c_str());
		}
	}

	// Заполнить editbox текущими путями
	SetDlgItemText(IDC_EDIT_PATH1, CString(config->getFolder1().c_str()));
	SetDlgItemText(IDC_EDIT_PATH2, CString(config->getFolder2().c_str()));
	// Заполнить токен и userid
	SetDlgItemText(IDC_EDIT_TOKENBOT, CString(config->getBotToken().c_str()));
	SetDlgItemText(IDC_EDIT_USERID, CString(config->getChatId().c_str()));

	// Заполнить режим исключения/включения и паттерны
	if (config->isUseExcluded()) CheckRadioButton(IDC_RADIO_EXCLUDED, IDC_RADIO_INCLUDED, IDC_RADIO_EXCLUDED);
	else CheckRadioButton(IDC_RADIO_EXCLUDED, IDC_RADIO_INCLUDED, IDC_RADIO_INCLUDED);

	// Заполнить поля паттернов
	std::string excPatterns;
	for (const auto& p : config->getExcludedPatterns()) {
		if (!excPatterns.empty()) excPatterns += ";";
		excPatterns += p;
	}
	SetDlgItemText(IDC_EDIT_EXCLUDED, CString(excPatterns.c_str()));
	std::string incPatterns;
	for (const auto& p : config->getIncludedPatterns()) {
		if (!incPatterns.empty()) incPatterns += ";";
		incPatterns += p;
	}
	SetDlgItemText(IDC_EDIT_INCLUDED, CString(incPatterns.c_str()));

	running = true;
	// Поток раз в 5 минут — проверка папок
	threadCheck = new std::thread([this]() {
		while (running) {
			try {
				std::string msg;
				// Determining patterns and mode from config
				std::vector<std::string> pats;
				bool isExcludeMode = config->isUseExcluded();
				if (isExcludeMode) {
					pats = config->getExcludedPatterns();
				} else {
					pats = config->getIncludedPatterns();
				}
				// For excluded mode: use as blacklist (whitelistMode=false); for included mode: use as whitelist (whitelistMode=true)
				bool whitelistMode = !isExcludeMode;
				bool ok = scanner->compareFolders(config->getFolder1(), config->getFolder2(), pats, whitelistMode, msg);

				if (ok && !wasEqual) {
					std::string err;
					std::lock_guard<std::mutex> g(botMutex);
					if (bot) bot->sendMessage(std::string("Результат - ") + msg, err);
				}
				wasEqual = ok; // обновляем флаг
			}
			catch (...) {
				// игнорируем
			}
			for (int i = 0; i < 300 && running; ++i) std::this_thread::sleep_for(std::chrono::seconds(1));
		}
	});

	// Поток раз в 5 секунд — опрос команд Telegram
	threadPoll = new std::thread([this]() {
		while (running) {
			try {
				std::string err;
				// lock briefly to get updates from bot
				std::vector<TelegramUpdate> updates;
				{
					std::lock_guard<std::mutex> g(botMutex);
					if (bot) updates = bot->getUpdates(err);
				}
				for (auto& u : updates) {
					// ignore messages from other chats
					if (!config->getChatId().empty() && !u.chatId.empty() && u.chatId != config->getChatId()) continue;
					if (u.text == "/check") {
						std::string msg;
						// Determining patterns and mode from config
						std::vector<std::string> pats;
						bool isExcludeMode = config->isUseExcluded();
						if (isExcludeMode) {
							pats = config->getExcludedPatterns();
						} else {
							pats = config->getIncludedPatterns();
						}
						// For excluded mode: use as blacklist (whitelistMode=false); for included mode: use as whitelist (whitelistMode=true)
						bool whitelistMode = !isExcludeMode;
						bool ok = scanner->compareFolders(config->getFolder1(), config->getFolder2(), pats, whitelistMode, msg);
						std::string err2;
						std::lock_guard<std::mutex> g(botMutex);
						if (bot) bot->sendMessage(std::string(ok ? "Check result: " : "Check failed: ") + msg, err2);
					} else if (u.text == "/shutdown") {
						std::string err2;
						std::lock_guard<std::mutex> g(botMutex);
						// Попробуем выключить систему
						if (EnableShutdownPrivilege()) {
							if (::InitiateSystemShutdownEx(NULL, NULL, 300, FALSE, FALSE, SHTDN_REASON_MAJOR_OTHER)) {
								DWORD err = GetLastError();
								if (bot) bot->sendMessage("Компьютер выключится через 5 минут.", err2);
							}
							else {
								if (bot) bot->sendMessage(GetLastErrorAsUTF8(), err2);
							}
						}
					} else if (u.text == "/cancel_shutdown") {
						std::string err2;
						std::lock_guard<std::mutex> g(botMutex);
						// Отмена выключения системы
						if (EnableShutdownPrivilege()) {
							if (::AbortSystemShutdown(NULL)) {
								DWORD err = GetLastError();
								if (bot) bot->sendMessage("Запланированное завершение работы отменено.", err2);
							}
							else {
								if (bot) bot->sendMessage(GetLastErrorAsUTF8(), err2);
							}
						}
					}
				}
			} catch (...) {}
			for (int i = 0; i < 3 && running; ++i) std::this_thread::sleep_for(std::chrono::seconds(1));
		}
	});

	return TRUE;  // возврат значения TRUE, если фокус не передан элементу управления
}

static std::string pickFolderDialog(HWND owner) {
	// Используем IFileDialog для выбора папки
	std::string out;
	HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
	if (SUCCEEDED(hr)) {
		IFileDialog *pfd = nullptr;
		if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd)))) {
			DWORD dwOptions;
			if (SUCCEEDED(pfd->GetOptions(&dwOptions))) {
				pfd->SetOptions(dwOptions | FOS_PICKFOLDERS);
			}
			if (SUCCEEDED(pfd->Show(owner))) {
				IShellItem *psi = nullptr;
				if (SUCCEEDED(pfd->GetResult(&psi))) {
					PWSTR pszPath = nullptr;
					if (SUCCEEDED(psi->GetDisplayName(SIGDN_FILESYSPATH, &pszPath))) {
						// преобразуем в utf8/ansi
						int len = WideCharToMultiByte(CP_ACP, 0, pszPath, -1, NULL, 0, NULL, NULL);
						if (len > 0) {
							std::string s(len, '\0');
							WideCharToMultiByte(CP_ACP, 0, pszPath, -1, &s[0], len, NULL, NULL);
							if (!s.empty() && s.back() == '\0') s.pop_back();
							out = s;
						}
						CoTaskMemFree(pszPath);
					}
					psi->Release();
				}
			}
			pfd->Release();
		}
		CoUninitialize();
	}
	return out;
}

void CWatchFolderDlg::OnBnClickedButtonPath1() {
	std::string path = pickFolderDialog(m_hWnd);
	if (!path.empty()) {
		config->setFolder1(path);
		// обновим editbox
		SetDlgItemText(IDC_EDIT_PATH1, CString(path.c_str()));
	}
}

void CWatchFolderDlg::OnBnClickedButtonPath2() {
	std::string path = pickFolderDialog(m_hWnd);
	if (!path.empty()) {
		config->setFolder2(path);
		SetDlgItemText(IDC_EDIT_PATH2, CString(path.c_str()));
	}
}

// При добавлении кнопки свертывания в диалоговое окно нужно воспользоваться приведенным ниже кодом,
//  чтобы нарисовать значок.  Для приложений MFC, использующих модель документов или представлений,
//  это автоматически выполняется рабочей областью.

void CWatchFolderDlg::OnPaint()
{
	if (IsIconic())
	{
		CPaintDC dc(this); // контекст устройства для рисования

		SendMessage(WM_ICONERASEBKGND, reinterpret_cast<WPARAM>(dc.GetSafeHdc()), 0);

		// Выравнивание значка по центру клиентского прямоугольника
		int cxIcon = GetSystemMetrics(SM_CXICON);
		int cyIcon = GetSystemMetrics(SM_CYICON);
		CRect rect;
		GetClientRect(&rect);
		int x = (rect.Width() - cxIcon + 1) / 2;
		int y = (rect.Height() - cyIcon + 1) / 2;

		// Нарисуйте значок
		dc.DrawIcon(x, y, m_hIcon);
	}
	else
	{
		CDialogEx::OnPaint();
	}
}

// Система вызывает эту функцию для получения отображения курсора при перемещении
//  свернутого окна.
HCURSOR CWatchFolderDlg::OnQueryDragIcon()
{
	return static_cast<HCURSOR>(m_hIcon);
}


void CWatchFolderDlg::OnBnClickedOk()
{
	config->save();
	HideToTray();
	//CDialogEx::OnOK();
}

void CWatchFolderDlg::OnEnChangeEditTokenbot()
{
	if (!config) return;
	CString s; GetDlgItemText(IDC_EDIT_TOKENBOT, s);
	CT2A conv(s);
	config->setBotToken(std::string(conv));
	// recreate bot with new credentials
	std::lock_guard<std::mutex> g(botMutex);
	delete bot; bot = new TelegramBotClient(config->getBotToken(), config->getChatId());
}

void CWatchFolderDlg::OnEnChangeEditUserid()
{
	if (!config) return;
	CString s; GetDlgItemText(IDC_EDIT_USERID, s);
	CT2A conv(s);
	config->setChatId(std::string(conv));
	std::lock_guard<std::mutex> g(botMutex);
	delete bot; bot = new TelegramBotClient(config->getBotToken(), config->getChatId());
}

void CWatchFolderDlg::OnBnClickedRadioExcluded()
{
	if (!config) return;
	config->setUseExcluded(true);
	// Заполнить edit-поле исключениями
	CString exclude; GetDlgItemText(IDC_EDIT_EXCLUDED, exclude);
	std::string excludeStr;
	CT2A convExc(exclude);
	excludeStr = std::string(convExc);
	std::vector<std::string> excPats;
	if (!excludeStr.empty()) {
		excPats = config->split(excludeStr, ';');
	}
	config->setExcludedPatterns(excPats);

	wasEqual = false; // флаг сравнения папок
}

void CWatchFolderDlg::OnBnClickedRadioIncluded()
{
	if (!config) return;
	config->setUseExcluded(false);
	// Заполнить edit-поле включениями
	CString include; GetDlgItemText(IDC_EDIT_INCLUDED, include);
	std::string includeStr;
	CT2A convInc(include);
	includeStr = std::string(convInc);
	std::vector<std::string> incPats;
	if (!includeStr.empty()) {
		incPats = config->split(includeStr, ';');
	}
	config->setIncludedPatterns(incPats);

	wasEqual = false; // флаг сравнения папок
}

void CWatchFolderDlg::OnEnChangeEditExcluded()
{
	if (!config) return;
	CString s; GetDlgItemText(IDC_EDIT_EXCLUDED, s);
	CT2A conv(s);
	std::string str = std::string(conv);
	std::vector<std::string> pats;
	if (!str.empty()) {
		pats = config->split(str, ';');
	}
	config->setExcludedPatterns(pats);

	wasEqual = false; // флаг сравнения папок
}

void CWatchFolderDlg::OnEnChangeEditIncluded()
{
	if (!config) return;
	CString s; GetDlgItemText(IDC_EDIT_INCLUDED, s);
	CT2A conv(s);
	std::string str = std::string(conv);
	std::vector<std::string> pats;
	if (!str.empty()) {
		pats = config->split(str, ';');
	}
	config->setIncludedPatterns(pats);

	wasEqual = false; // флаг сравнения папок
}

void CWatchFolderDlg::OnEnChangeEditPath1()
{
	wasEqual = false; // флаг сравнения папок
}

void CWatchFolderDlg::OnEnChangeEditPath2()
{
	wasEqual = false; // флаг сравнения папок
}



// В CWatchFolderDlg.cpp
void CWatchFolderDlg::HideToTray() {
	ZeroMemory(&m_nid, sizeof(NOTIFYICONDATA));
	m_nid.cbSize = sizeof(NOTIFYICONDATA);
	m_nid.hWnd = m_hWnd;
	m_nid.uID = 1;
	m_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
	m_nid.uCallbackMessage = WM_TRAYICON; // Сообщение, которое придет при клике
	m_nid.hIcon = LoadIcon(AfxGetInstanceHandle(), MAKEINTRESOURCE(IDR_MAINFRAME));
	wcscpy_s(m_nid.szTip, L"WatchFolder: Работаю в фоне");

	Shell_NotifyIcon(NIM_ADD, &m_nid);
	ShowWindow(SW_HIDE); // Скрываем окно
}

LRESULT CWatchFolderDlg::OnTrayIcon(WPARAM wParam, LPARAM lParam) {
	if (lParam == WM_RBUTTONUP) {
		// Загружаем меню из ресурсов
		CMenu menu;
		menu.LoadMenu(IDR_TRAY_MENU);
		CMenu* pSubMenu = menu.GetSubMenu(0);

		// Устанавливаем текущее окно как фокусное, чтобы меню правильно закрывалось
		SetForegroundWindow();

		// Получаем текущую позицию мыши
		CPoint pt;
		GetCursorPos(&pt);

		// Показываем меню
		pSubMenu->TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, pt.x, pt.y, this);

		return 0;
	}
	else if (lParam == WM_LBUTTONUP) {
		ShowFromTray();
	}
	return 0;
}

void CWatchFolderDlg::ShowFromTray() {
	ShowWindow(SW_SHOW);                  // Показываем окно
	SetForegroundWindow();                // Выводим на передний план
}

void CWatchFolderDlg::OnTrayRestore() {
	ShowFromTray();
}

void CWatchFolderDlg::OnTrayExit() {
	// Удаляем иконку перед выходом
	Shell_NotifyIcon(NIM_DELETE, &m_nid);
	PostMessage(WM_CLOSE); // Или EndDialog(IDOK);
}

LRESULT CWatchFolderDlg::OnUpdateAvailable(WPARAM wParam, LPARAM lParam)
{
	// Этот код выполняется в главном UI-потоке!
	CWnd* pBtn = GetDlgItem(IDC_BUTTON_UPDATE);
	if (pBtn != nullptr) {
		pBtn->EnableWindow(TRUE);
	}
	return 0;
}
void CWatchFolderDlg::OnBnClickedButtonUpdate()
{
	AutoUpdater::ApplyUpdate();
}
