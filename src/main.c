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

#pragma comment (lib, "kernel32.lib")
#pragma comment (lib, "user32.lib")
#pragma comment (lib, "gdi32.lib")
#pragma comment (lib, "shell32.lib")
#pragma comment (lib, "ws2_32.lib")
#pragma comment (lib, "secur32.lib")
#pragma comment (lib, "shlwapi.lib")
#pragma comment (lib, "uxtheme.lib")

#include "tls.c"

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

// TODO(Oskar): Move
#define HR(hr) do { HRESULT _hr = (hr); assert(SUCCEEDED(_hr)); } while (0)

// 
static UINT WM_TASKBARCREATED;

// Global
static HWND GlobalWindow;
static DWORD GlobalBackgroundThreadId;
static HANDLE GlobalBackgroundThreadHandle;

#include "imap.h"
#include "tokenizer.c"
#include "imap.c"
#include "imap_parser.c"

static imap_email_message *Email[100];
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
    };
    StrCpyNW(Data.szTip, MNOTIFY_WINDOW_TITLE, _countof(Data.szTip));
    Shell_NotifyIconW(NIM_ADD, &Data);
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
        imap_email_message *Message = (imap_email_message *)LParam;

        Email[EmailCount++] = Message;
        return 0;
    }
    else if (Message == WM_MNOTIFY_EMAIL_CLEAR)
    {
        for (int Index = 0; Index < EmailCount; ++Index)
        {
            free(Email[Index]);
        }
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
            AppendMenuW(Menu, MF_STRING, CMD_LIST, L"Show");
            AppendMenuW(Menu, MF_STRING, CMD_SETTINGS, L"Settings");
            AppendMenuW(Menu, MF_STRING, CMD_QUIT, L"Exit");

            POINT Mouse;
            GetCursorPos(&Mouse);

            SetForegroundWindow(Window);
            int Command = TrackPopupMenu(Menu, TPM_RETURNCMD | TPM_NONOTIFY, Mouse.x, Mouse.y, 0, Window, NULL);
            if (Command == CMD_MNOTIFY)
            {
                ShellExecuteW(NULL, L"open", L"www.google.se", NULL, NULL, SW_SHOWNORMAL);
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
                char Buffer[65536];
                unsigned int BufferSize = 0;
                for (int Index = 0; Index < EmailCount; ++Index)
                {
                    imap_email_message *Message = Email[Index];
                    BufferSize += sprintf(Buffer + BufferSize, "Subject: %s, From: %s, Date: %s\n",
                        Message->Subject, Message->From, Message->Date);
                }
      
                MessageBox(
                    NULL,
                    Buffer,
                    "Unread Emails",
                    MB_ICONINFORMATION | MB_OK | MB_DEFBUTTON1
                );
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

DWORD WINAPI
ThreadProc(LPVOID lpParameter)
{
    imap Imap;
    if (imap_init(&Imap, "imap.gmail.com", 993) != 0)
    {
        printf("Imap failed\n");
        return -1;
    }
    
    if(imap_login(&Imap, "mail@gmail.com", "password") != 0)
    {
        return -1;
    }

    if(imap_examine(&Imap, "inbox") != 0)
    {
        return -1;
    }

    if(imap_idle(&Imap) != 0)
    {
        return -1;
    }

    for (;;)
    {
        imap_parse(&Imap);
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

    // TODO(Oskar): Load configuration.

    WM_TASKBARCREATED = RegisterWindowMessageW(L"TaskbarCreated");
	assert(WM_TASKBARCREATED);

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