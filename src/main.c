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

#pragma comment (lib, "ws2_32.lib")
#pragma comment (lib, "secur32.lib")
#pragma comment (lib, "shlwapi.lib")

#include "tls.c"

#include "imap.h"
#include "tokenizer.c"
#include "imap.c"
#include "imap_parser.c"

int main()
{
    const char* hostname = "www.google.com";
    //const char* hostname = "badssl.com";
    //const char* hostname = "expired.badssl.com";
    //const char* hostname = "wrong.host.badssl.com";
    //const char* hostname = "self-signed.badssl.com";
    //const char* hostname = "untrusted-root.badssl.com";
    const char* path = "/";

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

    while(1)
    {
        imap_parse(&Imap);
    }

    imap_destroy(&Imap);
}