# MNotify

Simple email notification application.

MNotify will sit in the background and listen to incoming emails using the
IMAP protocol and give you notifications when you have unread mail.

## Building

In order to build the application you need to run `build.bat`.

## Usage

Before opening MNotify you have to edit the `mnotify.ini` file within the build directory.

The ini file contains the following fields:

| Configuration | Description   | Example       |
| ------------- | ------------- | ------------- |
| host  | The host of your email provider's imap server. | imap.gmail.com |
| port  | The port for your email provider's imap server.  | 993 |
| accountname  | Your account name.  | example@gmail.com |
| password  | Your password.  | RandomGeneratedAppPassword |
| opensite  | The link you want to open in your browser when clicking on MNotify in the system tray.  | <https://mail.google.com/mail/u/0/> |
| folder  | The folder you want to listen for incoming emails in.  | inbox |
| pollingtimer  | If the IMAP service doesn't support IDLE we need to perform polling. This is to specify how often to poll in seconds. Do note that most mail providers has rate limiting so don't choose a too low value.  | 600 |
| retrytime  | If the background thread listening for emails exits it will restart after this many seconds.  | 60 |

When `mnotify.ini` contains your desired options then you can simply run `mnotify.exe` located in the build directory.

In case of errors MNotify will write to a logfile located next to the executable. The icon within the system tray will change to include a warning triangle in the top right corner. In theese cases MNotify will also include an option when right-clicking the application in the system tray to show the error log.

### Tested email providers

The application has been tested against the following providers:

* Google GMail
* Yahoo
* Outlook

Note that for most providers they require you to generate an "App Password".
See the following:

* Google: [https://support.google.com/accounts/answer/185833?hl=en](https://support.google.com/accounts/answer/185833?hl=en)
* Yahoo: [https://help.yahoo.com/kb/SLN15241.html](https://help.yahoo.com/kb/SLN15241.html)
* Outlook: Couldn't find official documentation of how to do this but I managed by first turning on 2FA then navigating to: `My Microsoft Account` -> `Security` -> `Advanced Security Options` -> Scroll down to find "App Passwords".

If you find any problems or have additional feature requests please submit an issue.

## Thanks to

[Mārtiņš Možeiko (mmozeiko)](https://github.com/mmozeiko) for the tls code which is available [here](https://gist.github.com/mmozeiko/c0dfcc8fec527a90a02145d2cc0bfb6d). And the WindowsToast.h header file to add support for Windows 10 style toasts in C.

## License

This is free and unencumbered software released into the public domain.

Anyone is free to copy, modify, publish, use, compile, sell, or
distribute this software, either in source code form or as a compiled
binary, for any purpose, commercial or non-commercial, and by any
means.
