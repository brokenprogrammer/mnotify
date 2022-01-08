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

#define WM_MNOTIFY_ALREADY_RUNNING (WM_USER+1)
#define WM_MNOTIFY_EMAIL_MESSAGE   (WM_USER+2)
#define WM_MNOTIFY_EMAIL_CLEAR     (WM_USER+3)
#define WM_MNOTIFY_COMMAND         (WM_USER+4)

#define CMD_MNOTIFY  1
#define CMD_QUIT     2
#define CMD_SETTINGS 3
#define CMD_LIST     4

#define MNOTIFY_WINDOW_CLASS_NAME L"mnotify_window_class"
#define MNOTIFY_WINDOW_TITLE L"MNotify"
#define MNOTIFY_CONFIG_FILE "./mnotify.ini"
#define MNOTIFY_CONFIG_FILEW L"./mnotify.ini"

#define HR(hr) do { HRESULT _hr = (hr); assert(SUCCEEDED(_hr)); } while (0)

// Global
static UINT WM_TASKBARCREATED;
static HICON gIcon1;
static HICON gIcon2;
static wchar_t MailSite[256];
static char MailFolder[256];

static HWND GlobalWindow;
static DWORD GlobalBackgroundThreadId;
static HANDLE GlobalBackgroundThreadHandle;

#include "imap_client.h"
#include "tokenizer.c"
#include "imap_parser.c"
#include "imap_client.c"

// 
static imap_email_message *Email;
static unsigned int EmailCount;

static void 
ShowNotification(LPCWSTR Message, LPCWSTR Title, DWORD Flags)
{
    NOTIFYICONDATAW Data =
    {
        .cbSize = sizeof(Data),
        .hWnd = GlobalWindow,
        .uFlags = NIF_INFO | NIF_TIP,
        .dwInfoFlags = Flags, // NIIF_INFO, NIIF_WARNING, NIIF_ERROR
    };
    StrCpyNW(Data.szTip, MNOTIFY_WINDOW_TITLE, _countof(Data.szTip));
    StrCpyNW(Data.szInfo, Message, _countof(Data.szInfo));
    StrCpyNW(Data.szInfoTitle, Title ? Title : MNOTIFY_WINDOW_TITLE, _countof(Data.szInfoTitle));
    Shell_NotifyIconW(NIM_MODIFY, &Data);
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
        .hIcon = gIcon1,
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
        PostQuitMessage(0);
        return 0;
    }
    else if (Message == WM_MNOTIFY_EMAIL_MESSAGE)
    {
        // imap_email_message *Emails = (imap_email_message *)WParam;
        int TotalEmails = (int)LParam;

        // Email = Emails;
        EmailCount = TotalEmails;

        if (EmailCount == 0)
        {
            UpdateTrayIcon(gIcon1);
        }
        else
        {
            UpdateTrayIcon(gIcon2);
            wchar_t Data[512];
            swprintf(Data, 512, L"You have %d unread mail.", EmailCount);
            ShowNotification(Data, L"You've got new mails!", NIIF_INFO);
        }

        return 0;
    }
    else if (Message == WM_MNOTIFY_EMAIL_CLEAR)
    {
        // for (int Index = 0; Index < EmailCount; ++Index)
        // {
        //     free(Email[Index].Subject);
        //     free(Email[Index].From);
        //     free(Email[Index].Date);
        // }
        // free(Email);
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
            // AppendMenuW(Menu, MF_STRING, CMD_LIST, L"Show");
            // AppendMenuW(Menu, MF_STRING, CMD_SETTINGS, L"Settings");
            
            AppendMenuW(Menu, MF_STRING, CMD_QUIT, L"Exit");

            POINT Mouse;
            GetCursorPos(&Mouse);

            SetForegroundWindow(Window);
            int Command = TrackPopupMenu(Menu, TPM_RETURNCMD | TPM_NONOTIFY, Mouse.x, Mouse.y, 0, Window, NULL);
            if (Command == CMD_MNOTIFY)
            {
                ShellExecuteW(NULL, L"open", MailSite, NULL, NULL, SW_SHOWNORMAL);
            }
            else if (Command == CMD_QUIT)
            {
                DestroyWindow(Window);
            }
            else if (Command == CMD_SETTINGS)
            {
                // TODO(Oskar): Show settings where u can setup ur email accounts etc..
            }
            else if (Command == CMD_LIST)
            {
                // TODO(Oskar): Show list dialog.
            }

            DestroyMenu(Menu);
        }
        else if (LOWORD(LParam) == WM_LBUTTONDBLCLK)
        {
        }

        return 0;
    }

    return DefWindowProcW(Window, Message, WParam, LParam);
}

static void
ThreadImapPolling(char *Host, int Port, char *Account, char *Password)
{
    int PollingTimeSeconds = GetPrivateProfileInt (
        "Account",
        "pollingtimer",
        -1,
        MNOTIFY_CONFIG_FILE
    );

    for (;;)
    {
        imap Imap;
        if (!imap_init(&Imap, Host, Port))
        {
            printf("Imap connection failed.\n");
            return;
        }
        
        if(imap_login(&Imap, Account, Password) != 0)
        {
            printf("Imap Login failed.\n");
            return;
        }

        if(imap_examine(&Imap, MailFolder) != 0)
        {
            return;
        }

        // NOTE(Oskar): We resort to polling
        imap_response SearchResult = imap_search(&Imap);
        if (!SearchResult.Success)
        {
            return;
        }

        // NOTE(Oskar): Find out which emails we want to get
        if (SearchResult.NumberOfNumbers != EmailCount)
        {
            PostMessageW(GlobalWindow, WM_MNOTIFY_EMAIL_CLEAR, 0, 0);
            PostMessageW(GlobalWindow, WM_MNOTIFY_EMAIL_MESSAGE, 0, (LPARAM)SearchResult.NumberOfNumbers);
        }

        Sleep(PollingTimeSeconds * 1000);
    }
} 

DWORD WINAPI
ThreadProc(LPVOID lpParameter)
{
    // NOTE(Oskar): Readon configuartion. Later some of this should be passed in
    // lpParameter.
    char Host[256];
    char Account[256];
    char Password[256];
    GetPrivateProfileString (
        "Account",
        "host",
        "", 
        Host,
        256,
        MNOTIFY_CONFIG_FILE
    );

    int Port = GetPrivateProfileInt (
        "Account",
        "port",
        -1,
        MNOTIFY_CONFIG_FILE
    );

    GetPrivateProfileString (
        "Account",
        "accountname",
        "",
        Account,
        256,
        MNOTIFY_CONFIG_FILE
    );
    GetPrivateProfileString (
        "Account",
        "password",
        "", 
        Password,
        256,
        MNOTIFY_CONFIG_FILE
    );

    imap Imap;
    if (!imap_init(&Imap, Host, Port))
    {
        printf("Imap connection failed.\n");
        return -1;
    }
    
    if(imap_login(&Imap, Account, Password) != 0)
    {
        printf("Imap Login failed.\n");
        return -1;
    }

    if (Imap.HasIdle)
    {
        // NOTE(Oskar): MailFolder is read in the main thread.
        if(imap_examine(&Imap, MailFolder) != 0)
        {
            return -1;
        }

        for (;;)
        {
            if(imap_idle(&Imap) != 0)
            {
                break;
            }

            imap_idle_message IdleMessage = IMAP_IDLE_MESSAGE_UNKNOWN;
            while (IdleMessage != IMAP_IDLE_MESSAGE_EXISTS)
            {
                imap_response IdleResponse = imap_idle_listen(&Imap);
                if (!IdleResponse.Success)
                {
                    return 0;
                }

                IdleMessage = IdleResponse.IdleMessageType;
            }

            if (!imap_done(&Imap))
            {
                break;
            }

            imap_response SearchResult = imap_search(&Imap);
            if (!SearchResult.Success)
            {
                break;
            }

            // NOTE(Oskar): Find out which emails we want to get
            if (SearchResult.NumberOfNumbers != EmailCount)
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

                PostMessageW(GlobalWindow, WM_MNOTIFY_EMAIL_MESSAGE, 0, (LPARAM)SearchResult.NumberOfNumbers);
            }
        }
    }
    else
    {
        imap_destroy(&Imap);

        ThreadImapPolling(Host, Port, Account, Password);        
    }
    
    imap_destroy(&Imap);

    return (0);
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

    WM_TASKBARCREATED = RegisterWindowMessageW(L"TaskbarCreated");
	assert(WM_TASKBARCREATED);

    gIcon1 = LoadIconW(WindowClass.hInstance, MAKEINTRESOURCEW(1));
	gIcon2 = LoadIconW(WindowClass.hInstance, MAKEINTRESOURCEW(2));
	assert(gIcon1 && gIcon2);

    // NOTE(Oskar): Setup site to open and folder to listen to
    GetPrivateProfileStringW (
        L"Account",
        L"opensite",
        L"", 
        MailSite,
        256,
        MNOTIFY_CONFIG_FILEW
    );

    GetPrivateProfileString (
        "Account",
        "folder",
        "", 
        MailFolder,
        256,
        MNOTIFY_CONFIG_FILE
    );

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

    // NOTE(Oskar): Creating background thread
    // TODO(Oskar): Later create 1 per email account?
    GlobalBackgroundThreadHandle = CreateThread(0, 0, ThreadProc, 0, 0, &GlobalBackgroundThreadId);

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