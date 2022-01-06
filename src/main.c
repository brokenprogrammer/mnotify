#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#define SECURITY_WIN32
#include <security.h>
#include <schannel.h>
#include <shlwapi.h>
#include <assert.h>
#include <stdio.h>

#pragma comment (lib, "ws2_32.lib")
#pragma comment (lib, "secur32.lib")
#pragma comment (lib, "shlwapi.lib")

#include "tls.c"
#include "imap.c"

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

    if(imap_select(&Imap, "inbox") != 0)
    {
        return -1;
    }

    imap_destroy(&Imap);


    // tls_socket s;
    // if (tls_connect(&s, hostname, 443) != 0)
    // {
    //     printf("Error connecting to %s\n", hostname);
    //     return -1;
    // }

    // printf("Connected!\n");

    // // send request
    // char req[1024];
    // int len = sprintf(req, "GET / HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", hostname);
    // if (tls_write(&s, req, len) != 0)
    // {
    //     tls_disconnect(&s);
    //     return -1;
    // }

    // // write response to file
    // FILE* f = fopen("response.txt", "wb");
    // int received = 0;
    // for (;;)
    // {
    //     char buf[65536];
    //     int r = tls_read(&s, buf, sizeof(buf));
    //     if (r < 0)
    //     {
    //         printf("Error receiving data\n");
    //         break;
    //     }
    //     else if (r == 0)
    //     {
    //         printf("Socket disconnected\n");
    //         break;
    //     }
    //     else
    //     {
    //         fwrite(buf, 1, r, f);
    //         received += r;
    //     }
    // }
    // fclose(f);

    // printf("Received %d bytes\n", received);

    // tls_disconnect(&s);
}