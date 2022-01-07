# mnotify

Simple email notification application.

MNotify will sit in the background and listen to incoming emails using the
IMAP protocol and give you notifications when you have unread mail.

## Building

In order to build the application you need to run `vcvarsall.bat` then `build.bat`.

## Usage

Before opening mnotify you have to edit the `mnotify.ini` file within the build directory.

The ini file contains the following fields:

* host: The host of your email provider's imap server.
* port: the port for your email provider's imap server.
* accountname: Your account name.
* password: Your password.
* opensite: The link you want to open in your browser when clicking on MNotify in the system tray.
* folder: The folder you want to listen for incoming emails in.