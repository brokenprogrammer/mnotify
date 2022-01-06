static int
_imap_command(imap *Imap, char *Command, int CommandLength, int UsedCommandNumber)
{
    printf("Performing Imap command: %s\n", Command);
    if (tls_write(&Imap->Socket, Command, CommandLength) != 0)
    {
        printf("Failed!\n");
        tls_disconnect(&Imap->Socket);
        return -1;
    }

    if (UsedCommandNumber)
    {
        ++Imap->CommandNumber;
    }

    return 0;
}

static int
imap_read(imap *Imap, char *Buffer, unsigned int BufferLength)
{
    int Received = 0;
    int r = tls_read(&Imap->Socket, Buffer, BufferLength);
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
        Received += r;
    }

    return Received;
}

static int
imap_search(imap *Imap)
{
    char *Keyword = 0;
    if (Imap->HasRecent)
    {
        Keyword = "RECENT";
    }
    else
    {
        Keyword = "UNSEEN";
    }

    char CommandBuffer[128];
    int CommandLength = sprintf(CommandBuffer, "A%03d SEARCH %s\r\n", Imap->CommandNumber, Keyword);
    if(_imap_command(Imap, CommandBuffer, CommandLength, 1) != 0)
    {
        printf("Idle failed, exiting!\n");
        return -1;
    }

    assert(Imap->State == IMAP_STATE_PRESEARCHING);
    Imap->State = IMAP_STATE_SEARCHING;

    return 0;
}

static int
imap_done(imap *Imap)
{
    char CommandBuffer[128];
    int CommandLength = sprintf(CommandBuffer, "DONE\r\n");
    if(_imap_command(Imap, CommandBuffer, CommandLength, 0) != 0)
    {
        printf("Idle failed, exiting!\n");
        return -1;
    }

    assert(Imap->State == IMAP_STATE_IDLE);
    Imap->State = IMAP_STATE_PRESEARCHING;

    return 0;
}

static int
imap_fetch(imap *Imap)
{
    assert(strlen(Imap->Query) > 0);

    char CommandBuffer[1024];
    int CommandLength = sprintf(CommandBuffer, "A%03d FETCH %s (internaldate flags body[header.fields (date from subject)])\r\n", Imap->CommandNumber, Imap->Query);
    if(_imap_command(Imap, CommandBuffer, CommandLength, 1) != 0)
    {
        printf("fetch failed, exiting!\n");
        return -1;
    }

    assert(Imap->State == IMAP_STATE_PREFETCH);
    Imap->State = IMAP_STATE_FETCHING;
    Imap->FetchedRows = 0;

    return 0;
}

static int
imap_init(imap *Imap, char *HostName, int Port)
{
    if (tls_connect(&Imap->Socket, HostName, Port) != 0)
    {
        printf("Error connecting to %s\n", HostName);
        return -1;
    }

    Imap->State = IMAP_STATE_CONNECTED;
    Imap->ParsedCapabilities = -1;
    Imap->HasIdle = 0;
    Imap->HasRecent = 0;
    Imap->UpdateAlways = 0;
    Imap->OldMailCount = 0;
    Imap->FetchedRows = 0;
    Imap->CommandNumber = 1;

    // TODO(Oskar): Clear buffers

    printf("Connected!\n");


    if (imap_parse(Imap) != 0)
    {
        printf("Failed reading greeting\n");
        return -1;
    }

    assert(Imap->State == IMAP_STATE_NOTAUTHENTICATED);

    return 0;
}

static int
imap_login(imap *Imap, char *Login, char *Password)
{
    char CommandBuffer[128];
    int CommandLength = sprintf(CommandBuffer, "A%03d LOGIN %s %s\r\n", Imap->CommandNumber, Login, Password);
    if(_imap_command(Imap, CommandBuffer, CommandLength, 1) != 0)
    {
        printf("Login failed, exiting!\n");
        return -1;
    }

    assert(Imap->State == IMAP_STATE_NOTAUTHENTICATED);
    Imap->State = IMAP_STATE_LOGIN;

    if (imap_parse(Imap) != 0)
    {
        printf("Failed parsing login message\n");
        return -1;
    }

    return 0;
}

static int
imap_examine(imap *Imap, char *Folder)
{
    char CommandBuffer[128];
    int CommandLength = sprintf(CommandBuffer, "A%03d EXAMINE %s\r\n", Imap->CommandNumber, Folder);
    if(_imap_command(Imap, CommandBuffer, CommandLength, 1) != 0)
    {
        printf("Examine failed, exiting!\n");
        return -1;
    }

    assert(Imap->State == IMAP_STATE_AUTHENTICATED);
    Imap->State = IMAP_STATE_EXAMING;

    if (imap_parse(Imap) != 0)
    {
        printf("Failed to read examine response.\n");
        return -1;
    }

    return 0;
}

static int
imap_idle(imap *Imap)
{
    char CommandBuffer[128];
    int CommandLength = sprintf(CommandBuffer, "A%03d IDLE\r\n", Imap->CommandNumber);
    if(_imap_command(Imap, CommandBuffer, CommandLength, 1) != 0)
    {
        printf("Idle failed, exiting!\n");
        return -1;
    }

    assert(Imap->State == IMAP_STATE_SELECTED);
    Imap->State = IMAP_STATE_PREIDLE;

    if (imap_parse(Imap) != 0)
    {
        printf("Failed to read idle response.\n");
        return -1;
    }

    return 0;
}

static void
imap_destroy(imap *Imap)
{
    tls_disconnect(&Imap->Socket);
    Imap->State = IMAP_STATE_DISCONNECTED;
}