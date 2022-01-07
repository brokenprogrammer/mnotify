static imap_response
FailedResponse()
{
    imap_response Response = {0};
    Response.Type = IMAP_RESPONSE_TYPE_FAILED;
    Response.Success = 0;

    return Response;
}

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
imap_init(imap *Imap, char *HostName, int Port)
{
    if (tls_connect(&Imap->Socket, HostName, Port) != 0)
    {
        printf("Error connecting to %s\n", HostName);
        return -1;
    }

    Imap->ParsedCapabilities = 0;
    Imap->HasIdle = 0;
    Imap->HasRecent = 0;
    Imap->CommandNumber = 1;

    memset(Imap->Subject, 0, 1024);
    memset(Imap->From, 0, 1024);
    memset(Imap->Date, 0, 1024);

    imap_response Response = imap_parse(Imap, IMAP_RESPONSE_TYPE_PREAUTH, -1);
    if (!Response.Success)
    {
        printf("Failed reading greeting\n");
        return -1;
    }

    switch (Response.Provider)
    {
        case IMAP_IDENTIFIED_PROVIDER_GMAIL:
        {
            Imap->HasRecent = 0;
        } break;

        case IMAP_IDENTIFIED_PROVIDER_UNKNOWN:
        {

        } break;
    }

    return 0;
}

static int
imap_login(imap *Imap, char *Login, char *Password)
{
    int CommandNumber = Imap->CommandNumber;

    char CommandBuffer[128];
    int CommandLength = sprintf(CommandBuffer, "A%03d LOGIN %s %s\r\n", CommandNumber, Login, Password);
    if(_imap_command(Imap, CommandBuffer, CommandLength, 1) != 0)
    {
        printf("Login failed, exiting!\n");
        return -1;
    }

    imap_response Response = imap_parse(Imap, IMAP_RESPONSE_TYPE_LOGIN, CommandNumber);
    if (!Response.Success)
    {
        printf("Failed parsing login message\n");
        return -1;
    }

    if (Response.ParsedCapabilities)
    {
        Imap->ParsedCapabilities = 1;
        Imap->HasIdle = Response.HasIdle;
    }
    
    return 0;
}

static int
imap_examine(imap *Imap, char *Folder)
{
    int CommandNumber = Imap->CommandNumber;

    char CommandBuffer[128];
    int CommandLength = sprintf(CommandBuffer, "A%03d EXAMINE %s\r\n", CommandNumber, Folder);
    if(_imap_command(Imap, CommandBuffer, CommandLength, 1) != 0)
    {
        printf("Examine failed, exiting!\n");
        return -1;
    }

    imap_response Response = imap_parse(Imap, IMAP_RESPONSE_TYPE_EXAMINE, CommandNumber);
    if (!Response.Success)
    {
        printf("Failed parsing examine message\n");
        return -1;
    }

    return 0;
}

static int
imap_idle(imap *Imap)
{
    int CommandNumber = Imap->CommandNumber;

    char CommandBuffer[128];
    int CommandLength = sprintf(CommandBuffer, "A%03d IDLE\r\n", CommandNumber);
    if(_imap_command(Imap, CommandBuffer, CommandLength, 1) != 0)
    {
        printf("Idle failed, exiting!\n");
        return -1;
    }
    
    imap_response Response = imap_parse(Imap, IMAP_RESPONSE_TYPE_IDLE, CommandNumber);
    if (!Response.Success)
    {
        printf("Failed parsing idle message\n");
        return -1;
    }


    return 0;
}

static int
imap_idle_listen(imap *Imap)
{
    // TODO(Oskar): In the future lets return the data regarding that type of 
    // message we got.
    imap_response Response = imap_parse(Imap, IMAP_RESPONSE_TYPE_IDLE_LISTEN, -1);
    if (!Response.Success)
    {
        printf("Failed parsing idle listen message\n");
        return 0;
    }

    return 1;
}

static int
imap_done(imap *Imap)
{
    char CommandBuffer[128];
    int CommandLength = sprintf(CommandBuffer, "DONE\r\n");
    if(_imap_command(Imap, CommandBuffer, CommandLength, 0) != 0)
    {
        printf("Done failed, exiting!\n");
        return 0;
    }

    int LastCommandNumber = Imap->CommandNumber - 1;
    imap_response Response = imap_parse(Imap, IMAP_RESPONSE_TYPE_DONE, LastCommandNumber);
    if (!Response.Success)
    {
        printf("Failed parsing done message\n");
        return 0;
    }

    return 1;
}

static imap_response
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

    int CommandNumber = Imap->CommandNumber;

    char CommandBuffer[128];
    int CommandLength = sprintf(CommandBuffer, "A%03d SEARCH %s\r\n", CommandNumber, Keyword);
    if(_imap_command(Imap, CommandBuffer, CommandLength, 1) != 0)
    {
        printf("Search failed, exiting!\n");
        return FailedResponse();
    }

    imap_response Response = {0};
    Response.Type = IMAP_RESPONSE_TYPE_SEARCH;

    imap_parse_search(Imap, &Response, CommandNumber);
    if (!Response.Success)
    {
        printf("Failed parsing search message\n");
        return FailedResponse();
    }
    
    while (Response.Success == IMAP_PARSER_ERR_NOT_DONE)
    {
        if(!imap_parse_search(Imap, &Response, CommandNumber))
        {
            printf("Search failed, exiting!\n");
            return FailedResponse();
        }
    }

    if (!Response.Success)
    {
        printf("Failed parsing search message\n");
        return FailedResponse();
    }

    return Response;
}

static imap_response
imap_fetch(imap *Imap, int *SequenceNumbers, int NumberOfNumbers)
{
    // NOTE(Oskar): Build fetch query
    char Query[60000];
    int QueryLength = 0;
    for (int Index = 0; Index < NumberOfNumbers; ++Index)
    {
        char *FormatString = "%d,";
        if (Index == NumberOfNumbers -1)
        {
            FormatString = "%d";
        }

        if (SequenceNumbers[Index] == -1)
        {
            continue;
        }

        QueryLength += sprintf(Query + QueryLength, FormatString, SequenceNumbers[Index]);
    }    

    int CommandNumber = Imap->CommandNumber;
    char CommandBuffer[65536];
    int CommandLength = sprintf(CommandBuffer, "A%03d FETCH %s (internaldate flags body[header.fields (date from subject)])\r\n", CommandNumber, Query);
    if(_imap_command(Imap, CommandBuffer, CommandLength, 1) != 0)
    {
        printf("fetch failed, exiting!\n");
        return FailedResponse();
    }

    imap_response Response = {0};
    Response.Type = IMAP_RESPONSE_TYPE_FETCH;

    Response.Emails = malloc(sizeof(imap_email_message) * NumberOfNumbers);
    Response.TotalNumberOfEmails = NumberOfNumbers;
    Response.ParsedEmails = 0;
    Response.ActiveParse = 0;

    for (int Index = 0; Index < NumberOfNumbers; ++Index)
    {
        imap_email_message *Email = &Response.Emails[Index];
        Email->SequenceNumber = -1;
        Email->Error = 0;
        Email->Subject = malloc(sizeof(char) * 1024);
        Email->From    = malloc(sizeof(char) * 1024);
        Email->Date    = malloc(sizeof(char) * 1024);
    }

    imap_parse_fetch(Imap, &Response, CommandNumber);
    if (!Response.Success)
    {
        Response.Success = 0;
        return FailedResponse();
    }

    while (Response.Success == IMAP_PARSER_ERR_NOT_DONE)
    {
        if(!imap_parse_fetch(Imap, &Response, CommandNumber) && Response.Success != IMAP_PARSER_ERR_NOT_DONE)
        {
            return FailedResponse();
        }
    }

    if (!Response.Success)
    {
        return FailedResponse();
    }

    return Response;
}

static void
imap_destroy(imap *Imap)
{
    tls_disconnect(&Imap->Socket);
}