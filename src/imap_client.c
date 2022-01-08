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
imap_read_line(imap *Imap, char *Buffer, int BufferSize)
{
    static char LocalBuffer[65536];
    static int LocalBufferSize = 0;

    char *FoundLine = NULL;
    int Received = 0;

    // NOTE(Oskar): If we got spare data from last read we copy it over.
    if (LocalBufferSize > 0)
    {
        // NOTE(Oskar): If localbuffer contains a full line we copy that
        char *HasLine = StrStrA(LocalBuffer, IMAP_END_OF_MESSAGE);
        if (HasLine != NULL)
        {
            int Position = HasLine - LocalBuffer;
            Position += 2; // \r\n

            int Bytes = Position;
            memmove(Buffer, LocalBuffer, Bytes);

            // NOTE(Oskar): Push the contents of the buffer back.
            memmove(LocalBuffer, LocalBuffer + Bytes, LocalBufferSize - Bytes);
            LocalBufferSize -= Bytes;

            return Bytes;
        }
        else
        {
            // NOTE(Oskar): Not a full line but we move whats in there.
            memmove(Buffer, LocalBuffer, LocalBufferSize);
            Received = LocalBufferSize;
            LocalBufferSize = 0;
        }
    }

    do
    {
        int Result = tls_read(&Imap->Socket, 
                              Buffer + Received, 
                              BufferSize);
        if (Result < 0)
        {
            printf("Error receiving data\n");
            return -1;
        }
        else if (Result == 0)
        {
            printf("Socket disconnected\n");
            return -1;
        }
        else
        {
            Received += Result;

            // NOTE(Oskar): Check for end of message
            FoundLine = StrStrA(Buffer, IMAP_END_OF_MESSAGE);
            if (FoundLine != NULL)
            {
                int Position = FoundLine - Buffer;
                Position += 2; // \r\n

                // NOTE(Oskar): If we got spare bytes we write it to our local store.
                if(Received > Position)
                {
                    int BytesLeft = Received - Position;
                    memmove(LocalBuffer, Buffer + Position, BytesLeft);
                    LocalBufferSize = BytesLeft;
                }   
            }
        }
    } while(FoundLine == NULL);

    return Received;
}

static int
imap_read(imap *Imap, char *Buffer, int BufferSize)
{
    return imap_read_line(Imap, Buffer, BufferSize);
}

static BOOL
imap_init(imap *Imap, char *HostName, int Port)
{
    if (tls_connect(&Imap->Socket, HostName, Port) != 0)
    {
        printf("Error connecting to %s\n", HostName);
        return FALSE;
    }

    Imap->ParsedCapabilities = 0;
    Imap->HasIdle = 0;
    Imap->HasRecent = 0;
    Imap->CommandNumber = 1;

    char Line[65536] = {0};
    int LineLength = imap_read_line(Imap, Line, 65536);
    imap_response Response = {0};
    Response.Type = IMAP_RESPONSE_TYPE_PREAUTH;
    
    // NOTE(Oskar): Parsing
    {
        tokenizer Tokenizer = Tokenize(Line, strlen(Line));
        if (imap_parse_greeting(&Tokenizer, &Response))
        {
            Response.Success = 1;
        }
    }

    if (!Response.Success)
    {
        printf("Failed reading greeting\n");
        return FALSE;
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

    if (Response.ParsedCapabilities)
    {
        Imap->ParsedCapabilities = 1;
        if (Response.HasIdle)
        {
            Response.HasIdle = 0;
        }
    }

    return TRUE;
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

    char Line[65536] = {0};
    int LineLength = imap_read_line(Imap, Line, 65536);
    imap_response Response = {0};
    Response.Type = IMAP_RESPONSE_TYPE_LOGIN;

    // NOTE(Oskar): Parsing
    BOOL DoneParsing = FALSE;
    while (!DoneParsing)
    {
        tokenizer Tokenizer = Tokenize(Line, strlen(Line));
        
        if (strstr(Tokenizer.Input, "CAPABILITY"))
        {
            if (ExpectTokenType(&Tokenizer, Token_Asterisk) != 0)
            {
                Response.Success = 0;
            }
            GetToken(&Tokenizer);

            if(imap_parse_capabilities(&Tokenizer, &Response))
            {
                LineLength = imap_read_line(Imap, Line, 65536);
                continue;
            }
            else
            {
                Response.Success = 0;
            }
        }

        // NOTE(Oskar): Parse tagged ok message
        char Tag[10];
        sprintf(Tag, "A%03d", CommandNumber);
        if (imap_parse_tagged_ok(&Tokenizer, Tag))
        {
            Response.Success = 1;
            break;
        }

        LineLength = imap_read_line(Imap, Line, 65536);
    }
    
    // imap_response Response = imap_parse(Imap, IMAP_RESPONSE_TYPE_LOGIN, CommandNumber);
    
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


    char Line[65536] = {0};
    int LineLength = imap_read_line(Imap, Line, 65536);
    imap_response Response = {0};
    Response.Type = IMAP_RESPONSE_TYPE_EXAMINE;

    // NOTE(Oskar): Parsing
    for (;;)
    {
        tokenizer Tokenizer = Tokenize(Line, strlen(Line));
        token Token = PeekToken(&Tokenizer);
        if (Token.Type == Token_Asterisk)
        {
            // NOTE(Oskar): For now we don't care about theese flags.
            LineLength = imap_read_line(Imap, Line, 65536);
            continue;
        }
        else
        {
            char Tag[10];
            sprintf(Tag, "A%03d", CommandNumber);
            if(imap_parse_tagged_ok(&Tokenizer, Tag))
            {
                Response.Success = 1;
                break;
            }
            else
            {
                Response.Success = 0;
                break;
            }
        }
    }

    // imap_response Response = imap_parse(Imap, IMAP_RESPONSE_TYPE_EXAMINE, CommandNumber);
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
    
    char Line[65536] = {0};
    int LineLength = imap_read_line(Imap, Line, 65536);
    imap_response Response = {0};
    Response.Type = IMAP_RESPONSE_TYPE_IDLE;

    // NOTE(Oskar): Parsing
    {
        tokenizer Tokenizer = Tokenize(Line, strlen(Line));
        if (imap_parse_idle(&Tokenizer))
        {
            Response.Success = 1;
        }
        else
        {
            Response.Success = 0;
        }
    }

    // imap_response Response = imap_parse(Imap, IMAP_RESPONSE_TYPE_IDLE, CommandNumber);
    if (!Response.Success)
    {
        printf("Failed parsing idle message\n");
        return -1;
    }


    return 0;
}

static imap_response
imap_idle_listen(imap *Imap)
{
    char Line[65536] = {0};
    int LineLength = imap_read_line(Imap, Line, 65536);
    imap_response Response = {0};
    Response.Type = IMAP_RESPONSE_TYPE_IDLE_LISTEN;
    
    // NOTE(Oskar): Parsing
    {
        tokenizer Tokenizer = Tokenize(Line, strlen(Line));
        Response.IdleMessageType = imap_parse_idle_message(&Tokenizer);
        Response.Success = 1;
    }
    // imap_response Response = imap_parse(Imap, IMAP_RESPONSE_TYPE_IDLE_LISTEN, -1);
    
    return Response;
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
    char Line[65536] = {0};
    int LineLength = imap_read_line(Imap, Line, 65536);
    imap_response Response = {0};
    Response.Type = IMAP_RESPONSE_TYPE_IDLE_LISTEN;
    
    // NOTE(Oskar): Parsing
    {
        tokenizer Tokenizer = Tokenize(Line, strlen(Line));

        char Tag[10];
        sprintf(Tag, "A%03d", LastCommandNumber);
        if(imap_parse_tagged_ok(&Tokenizer, Tag))
        {
            Response.Success = 1;
        }
        else
        {
            Response.Success = 0;
        }
    }

    // imap_response Response = imap_parse(Imap, IMAP_RESPONSE_TYPE_DONE, LastCommandNumber);
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

    int LastCommandNumber = Imap->CommandNumber - 1;
    char Line[65536] = {0};
    int LineLength = imap_read_line(Imap, Line, 65536);
    imap_response Response = {0};
    Response.Type = IMAP_RESPONSE_TYPE_SEARCH;

    // NOTE(Oskar): Parsing
    for (;;) 
    {
        tokenizer Tokenizer = Tokenize(Line, strlen(Line));

        token Token = PeekToken(&Tokenizer);
        while (Token.Type == Token_Space)
        {
            Token = GetToken(&Tokenizer);
        }

        if (Token.Type == Token_Identifier)
        {
            char Tag[10];
            sprintf(Tag, "A%03d", CommandNumber);
            if(imap_parse_tagged_ok(&Tokenizer, Tag))
            {
                Response.Success = 1;
                break;
            }
            else
            {
                Response.Success = 0;
            }

            break;
        }

        if (!parse_search_result(&Tokenizer, &Response))
        {
            Response.Success = 0;
            break;
        }

        LineLength = imap_read_line(Imap, Line, 65536);
    }

    // imap_parse_search(Imap, &Response, CommandNumber);
    if (!Response.Success)
    {
        printf("Failed parsing search message\n");
        return FailedResponse();
    }
    
    // while (Response.Success == IMAP_PARSER_ERR_NOT_DONE)
    // {
    //     if(!imap_parse_search(Imap, &Response, CommandNumber))
    //     {
    //         printf("Search failed, exiting!\n");
    //         return FailedResponse();
    //     }
    // }

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