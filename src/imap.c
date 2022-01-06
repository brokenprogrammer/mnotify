typedef struct
{
    tls_socket Socket;

    int CommandNumber;
} imap;

static int
imap_init(imap *Imap, char *HostName, int Port)
{
    if (tls_connect(&Imap->Socket, HostName, Port) != 0)
    {
        printf("Error connecting to %s\n", HostName);
        return -1;
    }

    printf("Connected!\n");

    // Get server response message
    int received = 0;
    char buf[65536];
    int r = tls_read(&Imap->Socket, buf, sizeof(buf));
    if (r < 0)
    {
        printf("Error receiving data\n");
        return -1;
    }
    else if (r == 0)
    {
        printf("Socket disconnected\n");
        return -1;
    }
    else
    {
        // fwrite(buf, 1, r, f);
        received += r;
        printf("Received %d bytes\n", received);
        printf("Content: %s\n", buf);
    }

    Imap->CommandNumber = 1;

    return 0;
}

static int
_imap_command(imap *Imap, char *Command, int CommandLength)
{
    printf("Performing Imap command: %s\n", Command);
    if (tls_write(&Imap->Socket, Command, CommandLength) != 0)
    {
        printf("Failed!\n");
        tls_disconnect(&Imap->Socket);
        return -1;
    }

    ++Imap->CommandNumber;

    return 0;
}

static int
_imap_read(imap *Imap)
{
    int received = 0;
    char buf[65536];
    int r = tls_read(&Imap->Socket, buf, sizeof(buf));
    if (r < 0)
    {
        printf("Error receiving data\n");
        return -1;
    }
    else if (r == 0)
    {
        printf("Socket disconnected\n");
        return -1;
    }
    else
    {
        received += r;
        printf("Received %d bytes\n", received);
        printf("Content: %s\n", buf);
    }

    return 0;
}

static int
imap_login(imap *Imap, char *Login, char *Password)
{
    char CommandBuffer[128];
    int CommandLength = sprintf(CommandBuffer, "A%03d LOGIN %s %s\r\n", Imap->CommandNumber, Login, Password);
    if(_imap_command(Imap, CommandBuffer, CommandLength) != 0)
    {
        printf("Login failed, exiting!\n");
        return -1;
    }

    if (_imap_read(Imap) != 0)
    {
        printf("Failed to read login response.\n");
        return -1;
    }

    return 0;
}

static int
imap_select(imap *Imap, char *Folder)
{
    char CommandBuffer[128];
    int CommandLength = sprintf(CommandBuffer, "A%03d SELECT %s\r\n", Imap->CommandNumber, Folder);
    if(_imap_command(Imap, CommandBuffer, CommandLength) != 0)
    {
        printf("Login failed, exiting!\n");
        return -1;
    }

    if (_imap_read(Imap) != 0)
    {
        printf("Failed to read login response.\n");
        return -1;
    }

    return 0;
}

static void
imap_destroy(imap *Imap)
{
    tls_disconnect(&Imap->Socket);
}