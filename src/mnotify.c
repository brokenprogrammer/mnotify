#define INITGUID
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#define SECURITY_WIN32
#include <security.h>
#include <schannel.h>
#include <shlwapi.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <intrin.h>
#include <shellapi.h>
#include <windowsx.h>
#include <strsafe.h>
#include <stdint.h>

#pragma comment (lib, "kernel32.lib")
#pragma comment (lib, "user32.lib")
#pragma comment (lib, "gdi32.lib")
#pragma comment (lib, "shell32.lib")
#pragma comment (lib, "ws2_32.lib")
#pragma comment (lib, "secur32.lib")
#pragma comment (lib, "shlwapi.lib")
#pragma comment (lib, "uxtheme.lib")
#pragma comment (lib, "winmm.lib")

#include "tls.c"
#include "mnotify.h"
#include "tokenizer.c"
#include "imap_parser.h"
#include "imap_client.h"

#define WM_MNOTIFY_ALREADY_RUNNING (WM_USER+1)
#define WM_MNOTIFY_EMAIL_MESSAGE   (WM_USER+2)
#define WM_MNOTIFY_EMAIL_CLEAR     (WM_USER+3)
#define WM_MNOTIFY_COMMAND         (WM_USER+4)
#define WM_MNOTIFY_ERROR           (WM_USER+5)

#define MNOTIFY_TIMER_RECOVER      1

#define CMD_MNOTIFY  1
#define CMD_QUIT     2
#define CMD_LOG      3

#define MNOTIFY_WINDOW_CLASS_NAME   L"mnotify_window_class"
#define MNOTIFY_WINDOW_TITLE        L"MNotify"
#define MNOTIFY_CONFIG_FILE         "./mnotify.ini"
#define MNOTIFY_CONFIG_FILEW        L"./mnotify.ini"
#define MNOTIFY_LOG_FILE            "log.txt"
#define MNOTIFY_APPID               L"MNotify.MNotify" // CompanyName.ProductName
#define MNOTIFY_LOGO_FILE_NAME      L"mnotify_logo.png"

#define RESTART_IMAP_IDLE_TIMER_ID 1

#define RESTART_IMAP_IDLE_INTERVAL 25 * 1000 * 60 // 25 Minutes

#define HR(hr) do { HRESULT _hr = (hr); assert(SUCCEEDED(_hr)); } while (0)

#include "imap_parser.c"
#include "imap_client.c"
#include "WindowsToast.h"

// Globals
static UINT WM_TASKBARCREATED;

static HICON GlobalOpenIcon;
static HICON GlobalClosedIcon;
static HICON GlobalOpenWarningIcon;
static HICON GlobalClosedWarningIcon;

static HWND GlobalWindow;

static WindowsToast Toast;
static void* Notification;

// NOTE(Oskar): IDLE needs to reset every 29 minutes to prevent server from kicking
// us out so we store the currently used Imap Client globally so if the background thread
// is blocked reading messages the main thread can close the connection forcing a reset.
static imap GlobalImapIdleClient;
static BOOL GlobalImapIdleClientWasReset;

static mnotify_config GlobalConfiguration;
static BOOL GlobalHasErrors;

static unsigned int EmailCount;

static void 
Win32FatalErrorMessage(char *Message) 
{ 
    MessageBox(NULL, Message, "Error", MB_OK); 
    ExitProcess(-1); 
}

static void 
Win32FatalErrorCode(LPSTR Message)
{ 
    LPSTR ErrorMessage;
    DWORD LastError = GetLastError(); 

    FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | 
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        LastError,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPSTR)&ErrorMessage,
        0, NULL );

    int Characters = (lstrlen(ErrorMessage) + lstrlen(Message) + 40);
    LPTSTR MessageBuffer = (LPTSTR)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Characters * sizeof(CHAR));
    StringCchPrintfA(MessageBuffer, Characters, "%s Error failed with error code %d: %s", 
        Message, LastError, ErrorMessage); 

    MessageBox(NULL, MessageBuffer, "Error", MB_OK); 

    LocalFree(ErrorMessage);
    HeapFree(GetProcessHeap(), 0, MessageBuffer);
    ExitProcess(LastError); 
}


static BOOL
FileExists(char *FilePath)
{
    return GetFileAttributesA(FilePath) != INVALID_FILE_ATTRIBUTES;
}

static void
CreateNewFile(char *FilePath)
{
    HANDLE File = {0};

    SECURITY_ATTRIBUTES SecurityAttributes = 
    {
        .nLength = (DWORD)sizeof(SECURITY_ATTRIBUTES),
        .lpSecurityDescriptor = 0,
        .bInheritHandle = 0,
    };

    if ((File = CreateFile(FilePath, 
                           GENERIC_READ | GENERIC_WRITE,
                           0,
                           &SecurityAttributes,
                           CREATE_ALWAYS,
                           0,
                           0)) != INVALID_HANDLE_VALUE)
    {
        CloseHandle(File);
    }
    else
    {
        Win32FatalErrorCode("Failed to create logfile.");
    }
}

static void
AppendToFile(char *FilePath, void *Data, unsigned int DataLength)
{
    HANDLE File = {0};
    
    SECURITY_ATTRIBUTES SecurityAttributes = 
    {
        .nLength = (DWORD)sizeof(SECURITY_ATTRIBUTES),
        .lpSecurityDescriptor = 0,
        .bInheritHandle = 0,
    };

    if ((File = CreateFile(FilePath, 
                           FILE_APPEND_DATA, 
                           0,
                           &SecurityAttributes, 
                           OPEN_ALWAYS,
                           0, 
                           0))
            != INVALID_HANDLE_VALUE)
    {
        void *WriteData = Data;

        SetFilePointer(File, 0, 0, FILE_END);

        DWORD NumberOfBytesWritten = 0;
        WriteFile(File, WriteData, (DWORD)DataLength, &NumberOfBytesWritten, 0);
        
        CloseHandle(File);
    }
    else
    {
        Win32FatalErrorCode("Failed to write to logfile.");
    }
}

static void 
ShowNotification(LPCWSTR Message, LPCWSTR Title, LPCWSTR OpenLink)
{
    static WCHAR ImagePath[MAX_PATH];
    
    WCHAR ExeFolder[MAX_PATH];
	GetModuleFileNameW(NULL, ExeFolder, ARRAYSIZE(ExeFolder));
	PathRemoveFileSpecW(ExeFolder);
    wsprintfW(ImagePath, L"%s/%s", ExeFolder, MNOTIFY_LOGO_FILE_NAME);
    
    for (WCHAR* P = ImagePath; *P; P++)
    {
        if (*P == '\\') *P = '/';
    }

    WCHAR Xml[2048];
    int XmlLength = 0;

    XmlLength = StrCatChainW(Xml, ARRAYSIZE(Xml), XmlLength,
		L"<toast duration=\"short\"><visual><binding template=\"ToastGeneric\">"
		L"<image placement=\"appLogoOverride\" src=\"file:///");
	XmlLength = StrCatChainW(Xml, ARRAYSIZE(Xml), XmlLength, ImagePath);
	XmlLength = StrCatChainW(Xml, ARRAYSIZE(Xml), XmlLength,
		L"\"/>"
		L"<text>{title}</text>"
		L"<text>{message}</text>"
		L"</binding></visual><actions>");

    XmlLength = StrCatChainW(Xml, ARRAYSIZE(Xml), XmlLength, L"<action content=\"Open\" arguments=\"");
	XmlLength = StrCatChainW(Xml, ARRAYSIZE(Xml), XmlLength, OpenLink);
	XmlLength = StrCatChainW(Xml, ARRAYSIZE(Xml), XmlLength,
		L"\"/>"
		L"</actions></toast>");

    LPCWSTR Data[][2] =
	{
		{ L"title",   Title },
		{ L"message", Message },
	};
    Notification = WindowsToast_Create(&Toast, Xml, XmlLength, Data, ARRAYSIZE(Data));
	WindowsToast_Show(&Toast, Notification);
}

static void 
AddTrayIcon(HWND Window)
{
    NOTIFYICONDATAW Data =
    {
        .cbSize = sizeof(Data),
        .hWnd = Window,
        .uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP,
        .uCallbackMessage = WM_MNOTIFY_COMMAND,
        .hIcon = GlobalOpenIcon,
    };
    StrCpyNW(Data.szTip, MNOTIFY_WINDOW_TITLE, _countof(Data.szTip));
    Shell_NotifyIconW(NIM_ADD, &Data);
}

static void 
UpdateTrayIcon(HICON Icon)
{
    NOTIFYICONDATAW Data =
    {
        .cbSize = sizeof(Data),
        .hWnd = GlobalWindow,
        .uFlags = NIF_ICON,
        .hIcon = Icon,
    };
    Shell_NotifyIconW(NIM_MODIFY, &Data);
}

static void 
RemoveTrayIcon(HWND Window)
{
    NOTIFYICONDATAW Data =
    {
        .cbSize = sizeof(Data),
        .hWnd = Window,
    };
    Shell_NotifyIconW(NIM_DELETE, &Data);
}

static void
ReadConfigurationFileString(char *Section, char *Key, char *Out, int OutLength)
{
    GetPrivateProfileString(Section, Key, "", Out, OutLength, MNOTIFY_CONFIG_FILE);
    if (strcmp(Out, "") == 0)
    {
        Win32FatalErrorCode("Reading Configuration failed, check your filename and values.");
    }
}

static int
ReadConfigurationFileInt(char *Section, char *Key)
{
    int Result = GetPrivateProfileInt(Section, Key, -1, MNOTIFY_CONFIG_FILE);
    
    if (Result == -1)
    {
        Win32FatalErrorCode("Reading Configuration failed, check your filename and values.");
    }
    
    return Result;
}

static mnotify_config
LoadConfiguration()
{
    mnotify_config Config = {0};

    ReadConfigurationFileString("Account", "host",        Config.Host,     256);
    ReadConfigurationFileString("Account", "accountname", Config.Account,  256);
    ReadConfigurationFileString("Account", "password",    Config.Password, 256);
    ReadConfigurationFileString("Account", "opensite",    Config.OpenSite, 256);
    ReadConfigurationFileString("Account", "folder",      Config.Folder,   256);
    Config.Port               = ReadConfigurationFileInt("Account", "port");
    Config.PollingTimeSeconds = ReadConfigurationFileInt("Account", "pollingtimer");
    Config.RetryTime          = ReadConfigurationFileInt("Account", "retrytime");

    return Config;
}

static void
ImapPerformPolling(char *Host, int Port, char *Account, char *Password)
{
    for (;;)
    {
        imap Imap;
        if (imap_init(&Imap, Host, Port) != IMAP_CLIENT_ERROR_SUCCESS)
        {
            SendMessage(GlobalWindow, WM_MNOTIFY_ERROR, (WPARAM)Imap.Error, (LPARAM)Imap.ErrorLength);
            return;
        }

        if(imap_login(&Imap, Account, Password) != IMAP_CLIENT_ERROR_SUCCESS)
        {
            SendMessage(GlobalWindow, WM_MNOTIFY_ERROR, (WPARAM)Imap.Error, (LPARAM)Imap.ErrorLength);
            return;
        }

        if(imap_examine(&Imap, GlobalConfiguration.Folder) != IMAP_CLIENT_ERROR_SUCCESS)
        {
            SendMessage(GlobalWindow, WM_MNOTIFY_ERROR, (WPARAM)Imap.Error, (LPARAM)Imap.ErrorLength);
            goto polling_sleep;
        }

        // NOTE(Oskar): We resort to polling
        imap_search_response SearchResult = imap_search(&Imap);
        if (SearchResult.Error != IMAP_CLIENT_ERROR_SUCCESS)
        {
            SendMessage(GlobalWindow, WM_MNOTIFY_ERROR, (WPARAM)Imap.Error, (LPARAM)Imap.ErrorLength);
            goto polling_sleep;
        }

        // NOTE(Oskar): Find out which emails we want to get
        if (SearchResult.NumberOfMails != EmailCount)
        {
            // NOTE(Oskar): We only trigger a notification if stored number of
            // unread emails are lower than what exists in mailbox.
            if (EmailCount < SearchResult.NumberOfMails)
            {
                PostMessageW(GlobalWindow, WM_MNOTIFY_EMAIL_CLEAR, 0, 0);
                PostMessageW(GlobalWindow, WM_MNOTIFY_EMAIL_MESSAGE, 0, (LPARAM)SearchResult.NumberOfMails);
            }
            else
            {
                EmailCount = SearchResult.NumberOfMails;
            }
        }

polling_sleep: ;
        Sleep(GlobalConfiguration.PollingTimeSeconds * 1000);
    }
} 

void CALLBACK
ImapPerformIdleTimerCallback(HWND Hwnd, UINT uMsg, UINT_PTR TimerId, DWORD DwTime)
{
    KillTimer(GlobalWindow, RESTART_IMAP_IDLE_TIMER_ID);
    GlobalImapIdleClientWasReset = TRUE;
    imap_destroy(&GlobalImapIdleClient);
}

static void
ImapPerformIdle(char *Host, int Port, char *Account, char *Password)
{
    for (;;)
    {     
        GlobalImapIdleClientWasReset = FALSE;
        
        if (imap_init(&GlobalImapIdleClient, Host, Port) != IMAP_CLIENT_ERROR_SUCCESS)
        {
            SendMessage(GlobalWindow, WM_MNOTIFY_ERROR, (WPARAM)GlobalImapIdleClient.Error, (LPARAM)GlobalImapIdleClient.ErrorLength);
            return;
        }

        if(imap_login(&GlobalImapIdleClient, Account, Password) != IMAP_CLIENT_ERROR_SUCCESS)
        {
            SendMessage(GlobalWindow, WM_MNOTIFY_ERROR, (WPARAM)GlobalImapIdleClient.Error, (LPARAM)GlobalImapIdleClient.ErrorLength);
            return;
        }

        if(imap_examine(&GlobalImapIdleClient, GlobalConfiguration.Folder) != IMAP_CLIENT_ERROR_SUCCESS)
        {
            SendMessage(GlobalWindow, WM_MNOTIFY_ERROR, (WPARAM)GlobalImapIdleClient.Error, (LPARAM)GlobalImapIdleClient.ErrorLength);
            return;
        }

        for (;;)
        {
            KillTimer(GlobalWindow, RESTART_IMAP_IDLE_TIMER_ID);
            SetTimer(GlobalWindow, RESTART_IMAP_IDLE_TIMER_ID, RESTART_IMAP_IDLE_INTERVAL, ImapPerformIdleTimerCallback);
            
            if(imap_idle(&GlobalImapIdleClient) != IMAP_CLIENT_ERROR_SUCCESS)
            {
                break;
            }

            imap_idle_response_type IdleResponseType = IMAP_IDLE_MESSAGE_UNKNOWN;
            while (IdleResponseType != IMAP_IDLE_MESSAGE_EXISTS)
            {
                imap_idle_response IdleResponse = imap_idle_listen(&GlobalImapIdleClient);
                
                if (IdleResponse.Error != IMAP_CLIENT_ERROR_SUCCESS)
                {
                    break;
                }
                
                IdleResponseType = IdleResponse.Type;
                if (IdleResponseType == IMAP_IDLE_MESSAGE_EXPUNGE)
                {
                    if (EmailCount > 0)
                    {
                        --EmailCount;
                    }
                }
            }

            if (imap_done(&GlobalImapIdleClient) != IMAP_CLIENT_ERROR_SUCCESS)
            {
                break;
            }

            imap_search_response SearchResult = imap_search(&GlobalImapIdleClient);
            if (SearchResult.Error != IMAP_CLIENT_ERROR_SUCCESS)
            {
                break;
            }

            // NOTE(Oskar): Find out which emails we want to get
            if (SearchResult.NumberOfMails != EmailCount)
            {
                PostMessageW(GlobalWindow, WM_MNOTIFY_EMAIL_CLEAR, 0, 0);

                // TODO(Oskar): For later if we want to display the data somehow within the application.
                // The parsing is broken through so need to fix that first. It dies on longer requests as we don't
                // get the full imap message in one go from gmail and idk how to fix it.
                // imap_response FetchResponse = imap_fetch(&Imap, SearchResult.SequenceNumbers, SearchResult.NumberOfNumbers);
                // if (!FetchResponse.Success)
                // {
                //     break;
                // }

                PostMessageW(GlobalWindow, WM_MNOTIFY_EMAIL_MESSAGE, 0, (LPARAM)SearchResult.NumberOfMails);
            }
        }

        // NOTE(Oskar): Check if it was us who reset the connection
        if (!GlobalImapIdleClientWasReset)
        {
            SendMessage(GlobalWindow, WM_MNOTIFY_ERROR, (WPARAM)GlobalImapIdleClient.Error, (LPARAM)GlobalImapIdleClient.ErrorLength);
            imap_destroy(&GlobalImapIdleClient);
        }
    }
}

DWORD WINAPI
ImapBackgroundThread(LPVOID lpParameter)
{
    // TODO(Oskar): How do we recover from init and login errors? Do we add
    // option for user to restart the thread?
    imap Imap;
    if (imap_init(&Imap, GlobalConfiguration.Host, GlobalConfiguration.Port) != IMAP_CLIENT_ERROR_SUCCESS)
    {
        SendMessage(GlobalWindow, WM_MNOTIFY_ERROR, (WPARAM)Imap.Error, (LPARAM)Imap.ErrorLength);
        goto thread_fatal;
    }
 
    if(imap_login(&Imap, GlobalConfiguration.Account, GlobalConfiguration.Password) != IMAP_CLIENT_ERROR_SUCCESS)
    {
        SendMessage(GlobalWindow, WM_MNOTIFY_ERROR, (WPARAM)Imap.Error, (LPARAM)Imap.ErrorLength);
        goto thread_fatal;
    }
    imap_destroy(&Imap);

    if (Imap.HasIdle)
    {
        ImapPerformIdle(GlobalConfiguration.Host, GlobalConfiguration.Port, GlobalConfiguration.Account, GlobalConfiguration.Password);
    }
    else
    {
        // NOTE(Oskar): No IDLE command support so we proceed with polling.
        ImapPerformPolling(GlobalConfiguration.Host, GlobalConfiguration.Port, GlobalConfiguration.Account, GlobalConfiguration.Password); 
    }

thread_fatal: ;
    SetTimer(GlobalWindow, MNOTIFY_TIMER_RECOVER, GlobalConfiguration.RetryTime * 1000, NULL);
    return -1;
}

static LRESULT CALLBACK 
WindowProc(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam)
{
    if (Message == WM_CREATE)
    {
        HR(BufferedPaintInit());
        AddTrayIcon(Window);
        return 0;
    }
    else if (Message == WM_DESTROY)
    {
        RemoveTrayIcon(Window);
        KillTimer(Window, RESTART_IMAP_IDLE_TIMER_ID);
        PostQuitMessage(0);
        return 0;
    }
    else if (Message == WM_TIMER)
    {
        if (WParam == MNOTIFY_TIMER_RECOVER)
        {
            KillTimer(Window, MNOTIFY_TIMER_RECOVER);
            HANDLE Thread = CreateThread(0, 0, ImapBackgroundThread, 0, 0, NULL);
            CloseHandle(Thread);    
        }
    }
    else if (Message == WM_MNOTIFY_EMAIL_MESSAGE)
    {
        int TotalEmails = (int)LParam;
        EmailCount = TotalEmails;

        if (EmailCount == 0)
        {
            GlobalHasErrors ? UpdateTrayIcon(GlobalOpenWarningIcon) : UpdateTrayIcon(GlobalOpenIcon);
        }
        else
        {
            GlobalHasErrors ? UpdateTrayIcon(GlobalClosedWarningIcon) : UpdateTrayIcon(GlobalClosedIcon);
            
            wchar_t Data[512];
            swprintf(Data, 512, L"You have %d unread mail.", EmailCount);

            wchar_t Link[512];
            swprintf(Link, 512, L"%hs", GlobalConfiguration.OpenSite);

            if (Notification != NULL)
            {
                WindowsToast_Release(&Toast, Notification);
            }

            ShowNotification(Data, L"You've got new mails!", Link);
        }

        return 0;
    }
    else if (Message == WM_MNOTIFY_EMAIL_CLEAR)
    {
        EmailCount = 0;
    }
    else if (Message == WM_MNOTIFY_COMMAND)
    {
        if (LOWORD(LParam) == WM_RBUTTONUP)
        {
            HMENU Menu = CreatePopupMenu();
            assert(Menu);

            AppendMenuW(Menu, MF_STRING, CMD_MNOTIFY, MNOTIFY_WINDOW_TITLE);
            AppendMenuW(Menu, MF_SEPARATOR, 0, NULL);

            // TODO(Oskar): Future features?
            if (GlobalHasErrors)
            {
                AppendMenuW(Menu, MF_STRING, CMD_LOG, L"Show Log");
            }
            
            AppendMenuW(Menu, MF_STRING, CMD_QUIT, L"Exit");

            POINT Mouse;
            GetCursorPos(&Mouse);

            SetForegroundWindow(Window);
            int Command = TrackPopupMenu(Menu, TPM_RETURNCMD | TPM_NONOTIFY, Mouse.x, Mouse.y, 0, Window, NULL);
            if (Command == CMD_MNOTIFY)
            {
                ShellExecute(NULL, "open", GlobalConfiguration.OpenSite, NULL, NULL, SW_SHOWNORMAL);
            }
            else if (Command == CMD_QUIT)
            {
                DestroyWindow(Window);
            }
            else if (Command == CMD_LOG)
            {
                ShellExecute(NULL, "open", MNOTIFY_LOG_FILE, NULL, NULL, SW_SHOWNORMAL);
            }

            DestroyMenu(Menu);
        }
        else if (LOWORD(LParam) == WM_LBUTTONDBLCLK)
        {
        }

        return 0;
    }
    else if (Message == WM_MNOTIFY_ERROR)
    {
        GlobalHasErrors = TRUE;

        char *Error = (char *)WParam;
        int ErrorLength = (int)LParam;
        AppendToFile(MNOTIFY_LOG_FILE, Error, ErrorLength);

        if (EmailCount == 0)
        {
            UpdateTrayIcon(GlobalOpenWarningIcon);
        }
        else 
        {
            UpdateTrayIcon(GlobalClosedWarningIcon);
        }

        return 0;
    }

    return DefWindowProcW(Window, Message, WParam, LParam);
}

static void MNotifyToastCallback(WindowsToast* Toast, void* Item, LPCWSTR Action)
{
    LPCWSTR Url = Action;
    ShellExecuteW(NULL, L"open", Url, NULL, NULL, SW_SHOWNORMAL);
}

int CALLBACK
WinMain(HINSTANCE Instance, HINSTANCE PrevInstance, LPSTR CommandLine, int ShowCode)
{
    WNDCLASSEXW WindowClass =
    {
        .cbSize = sizeof(WindowClass),
        .lpfnWndProc = WindowProc,
        .hInstance = GetModuleHandleW(NULL),
        .lpszClassName = L"mnotify_window_class",
    };

    // NOTE(Oskar): Check if running
    HWND MNotifyWindow = FindWindowW(WindowClass.lpszClassName, NULL);
    if (MNotifyWindow)
    {
        PostMessageW(MNotifyWindow, WM_MNOTIFY_ALREADY_RUNNING, 0, 0);
        ExitProcess(0);
    }

    WindowsToast_Init(&Toast, MNOTIFY_WINDOW_TITLE, MNOTIFY_APPID);
    WindowsToast_HideAll(&Toast, MNOTIFY_APPID);
    Toast.OnActivatedCallback = MNotifyToastCallback;

    WM_TASKBARCREATED = RegisterWindowMessageW(L"TaskbarCreated");
    assert(WM_TASKBARCREATED);

    GlobalOpenIcon          = LoadIconW(WindowClass.hInstance, MAKEINTRESOURCEW(1));
    GlobalClosedIcon        = LoadIconW(WindowClass.hInstance, MAKEINTRESOURCEW(2));
    GlobalOpenWarningIcon   = LoadIconW(WindowClass.hInstance, MAKEINTRESOURCEW(3));
    GlobalClosedWarningIcon = LoadIconW(WindowClass.hInstance, MAKEINTRESOURCEW(4));
    assert(GlobalOpenIcon && GlobalClosedIcon && GlobalOpenWarningIcon && GlobalClosedWarningIcon);

    // NOTE(Oskar): Load configuration
    GlobalConfiguration = LoadConfiguration();

    // NOTE(Oskar): Prepare Logfile
    if (FileExists(MNOTIFY_LOG_FILE))
    {
        DeleteFileA(MNOTIFY_LOG_FILE);
    }
    CreateNewFile(MNOTIFY_LOG_FILE);
    GlobalHasErrors = FALSE;


    // NOTE(Oskar): Window Creation
    ATOM Atom = RegisterClassExW(&WindowClass);
    assert(Atom);

    GlobalWindow = CreateWindowExW(
        0, WindowClass.lpszClassName, MNOTIFY_WINDOW_TITLE, WS_POPUP,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        NULL, NULL, WindowClass.hInstance, NULL);
    if (!GlobalWindow)
    {
        ExitProcess(0);
    }

    // TODO(Oskar): Later create 1 per email account?
    HANDLE Thread = CreateThread(0, 0, ImapBackgroundThread, 0, 0, NULL);
    CloseHandle(Thread);

    EmailCount = 0;
    for (;;)
    {
        MSG Message;
        BOOL Result = GetMessageW(&Message, NULL, 0, 0);
        if (Result == 0)
        {
            ExitProcess(0);
        }
        assert(Result > 0);

        TranslateMessage(&Message);
        DispatchMessageW(&Message);
    }
}