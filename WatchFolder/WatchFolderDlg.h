
// WatchFolderDlg.h: файл заголовка
//

#pragma once

#include <thread>
#include <atomic>
#include <mutex>

// Пользовательское сообщение от трея
#define WM_TRAYICON (WM_USER + 1)
// Сообщение о том, что проверка обновлений завершена успешно
#define WM_UPDATE_AVAILABLE (WM_USER + 2)

// Диалоговое окно CWatchFolderDlg
class CWatchFolderDlg : public CDialogEx
{
// Создание
public:
	CWatchFolderDlg(CWnd* pParent = nullptr);	// стандартный конструктор

	virtual ~CWatchFolderDlg();

	// Компоненты приложения
	class ConfigManager* config = nullptr;
	class FileScanner* scanner = nullptr;
	class TelegramBotClient* bot = nullptr;
	std::mutex botMutex;
	bool running = false;
	std::thread* threadCheck = nullptr;
	std::thread* threadPoll = nullptr;
	bool wasEqual = false; // флаг сравнения папок

	// Системный трей
	NOTIFYICONDATA m_nid {0};
	void HideToTray();
	void ShowFromTray();
	afx_msg LRESULT OnTrayIcon(WPARAM wParam, LPARAM lParam);
	afx_msg void OnTrayRestore();
	afx_msg void OnTrayExit();

	// Проверка наличия обновлений
	afx_msg LRESULT OnUpdateAvailable(WPARAM wParam, LPARAM lParam);

// Данные диалогового окна
#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_WATCHFOLDER_DIALOG };
#endif

	protected:
	virtual void DoDataExchange(CDataExchange* pDX);	// поддержка DDX/DDV
	afx_msg void OnBnClickedButtonPath1();
	afx_msg void OnBnClickedButtonPath2();
	afx_msg void OnBnClickedRadioExcluded();
	afx_msg void OnBnClickedRadioIncluded();
	afx_msg void OnEnChangeEditExcluded();
	afx_msg void OnEnChangeEditIncluded();


// Реализация
protected:
	HICON m_hIcon;

	// Созданные функции схемы сообщений
	virtual BOOL OnInitDialog();
	afx_msg void OnPaint();
	afx_msg HCURSOR OnQueryDragIcon();
	DECLARE_MESSAGE_MAP()
public:
	afx_msg void OnBnClickedOk();
	afx_msg void OnEnChangeEditTokenbot();
	afx_msg void OnEnChangeEditUserid();
	afx_msg void OnEnChangeEditPath1();
	afx_msg void OnEnChangeEditPath2();
	afx_msg void OnBnClickedButtonUpdate();
};
