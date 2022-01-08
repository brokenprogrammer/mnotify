static imap_response
FailedResponse()
{
    imap_response Response = {0};
    Response.Type = IMAP_RESPONSE_TYPE_FAILED;
    Response.Success = 0;

    return Response;
}

static BOOL
imap_command(imap *Imap, BOOL IncrementCommandNumber, char *Format, ...)
{
    char CommandBuffer[32768];
    size_t CommandLength = 0;

    va_list ArgumentPointer;
    va_start(ArgumentPointer, Format);
    CommandLength = vsnprintf(CommandBuffer, 32768, Format, ArgumentPointer);
    va_end(ArgumentPointer);

    if (tls_write(&Imap->Socket, CommandBuffer, CommandLength) != 0)
    {
        printf("Failed!\n");
        tls_disconnect(&Imap->Socket);
        return FALSE;
    }

    if (IncrementCommandNumber)
    {
        ++Imap->CommandNumber;
    }

    return TRUE;
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
            Buffer[Bytes] = 0;

            // NOTE(Oskar): Push the contents of the buffer back.
            memmove(LocalBuffer, LocalBuffer + Bytes, LocalBufferSize - Bytes);
            LocalBufferSize -= Bytes;

            return Bytes;
        }
        else
        {
            // NOTE(Oskar): Not a full line but we move whats in there.
            memmove(Buffer, LocalBuffer, LocalBufferSize);
            Buffer[LocalBufferSize] = 0;

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

                    Buffer[Position] = 0;
                }
            }
        }
    } while(FoundLine == NULL);

    return Received;
}

static imap_response
imap_read_greeting_response(imap *Imap)
{
    imap_response Response = {0};
    Response.Type = IMAP_RESPONSE_TYPE_GREETING;

    char Line[65536] = {0};
    imap_read_line(Imap, Line, 65536);
    
    tokenizer Tokenizer = Tokenize(Line, strlen(Line));
    if (imap_parse_greeting(&Tokenizer, &Response))
    {
        Response.Success = TRUE;
    }
    else
    {
        Response.Success = FALSE;
    }

    return Response;
}

static imap_response
imap_read_login_response(imap *Imap, char *Tag)
{
    imap_response Response = {0};
    Response.Type = IMAP_RESPONSE_TYPE_LOGIN;

    char Line[65536] = {0};
    imap_read_line(Imap, Line, 65536);
    
    for (;;)
    {
        tokenizer Tokenizer = Tokenize(Line, strlen(Line));
        
        if (StrStrA(Tokenizer.Input, "CAPABILITY") != NULL)
        {
            if (ExpectTokenType(&Tokenizer, Token_Asterisk) != 0)
            {
                Response.Success = FALSE;
            }
            GetToken(&Tokenizer);

            if(imap_parse_capabilities(&Tokenizer, &Response))
            {
                imap_read_line(Imap, Line, 65536);
                continue;
            }
            else
            {
                Response.Success = FALSE;
                break;
            }
        }

        // NOTE(Oskar): Parse tagged ok message
        if (imap_parse_tagged_ok(&Tokenizer, Tag))
        {
            Response.Success = TRUE;
            break;
        }

        imap_read_line(Imap, Line, 65536);
    }

    return Response;
}

static imap_response
imap_read_examine_response(imap *Imap, char *Tag)
{
    imap_response Response = {0};
    Response.Type = IMAP_RESPONSE_TYPE_EXAMINE;

    char Line[65536] = {0};
    imap_read_line(Imap, Line, 65536);

    for (;;)
    {
        tokenizer Tokenizer = Tokenize(Line, strlen(Line));
        token Token = PeekToken(&Tokenizer);
        if (Token.Type == Token_Asterisk)
        {
            // NOTE(Oskar): For now we don't care about theese flags.
            imap_read_line(Imap, Line, 65536);
            continue;
        }
        else
        {
            if(imap_parse_tagged_ok(&Tokenizer, Tag))
            {
                Response.Success = TRUE;
                break;
            }
            else
            {
                Response.Success = FALSE;
                break;
            }
        }
    }

    return Response;
}

static imap_response
imap_read_idle_response(imap *Imap)
{
    imap_response Response = {0};
    Response.Type = IMAP_RESPONSE_TYPE_IDLE;

    char Line[65536] = {0};
    imap_read_line(Imap, Line, 65536);

    tokenizer Tokenizer = Tokenize(Line, strlen(Line));
    if (imap_parse_idle(&Tokenizer))
    {
        Response.Success = TRUE;
    }
    else
    {
        Response.Success = FALSE;
    }

    return Response;
}

static imap_response
imap_read_idle_listen_response(imap *Imap)
{
    imap_response Response = {0};
    Response.Type = IMAP_RESPONSE_TYPE_IDLE_LISTEN;

    char Line[65536] = {0};
    imap_read_line(Imap, Line, 65536);

    tokenizer Tokenizer = Tokenize(Line, strlen(Line));
    Response.IdleMessageType = imap_parse_idle_message(&Tokenizer);
    Response.Success = 1;

    return Response;
}

static imap_response
imap_read_done_response(imap *Imap, char *Tag)
{
    imap_response Response = {0};
    Response.Type = IMAP_RESPONSE_TYPE_IDLE_LISTEN;

    char Line[65536] = {0};
    imap_read_line(Imap, Line, 65536);

    tokenizer Tokenizer = Tokenize(Line, strlen(Line));
    if(imap_parse_tagged_ok(&Tokenizer, Tag))
    {
        Response.Success = TRUE;
    }
    else
    {
        Response.Success = FALSE;
    }

    return Response;
}

static imap_response
imap_read_search_response(imap *Imap, char *Tag)
{
    imap_response Response = {0};
    Response.Type = IMAP_RESPONSE_TYPE_SEARCH;

    char Line[65536] = {0};
    imap_read_line(Imap, Line, 65536);

    for (;;) 
    {
        tokenizer Tokenizer = Tokenize(Line, strlen(Line));

        token Token = PeekToken(&Tokenizer);
        if (Token.Type == Token_Identifier)
        {
            if(imap_parse_tagged_ok(&Tokenizer, Tag))
            {
                Response.Success = TRUE;
                break;
            }
            else
            {
                Response.Success = FALSE;
            }

            break;
        }

        if (!parse_search_result(&Tokenizer, &Response))
        {
            Response.Success = FALSE;
            break;
        }

        imap_read_line(Imap, Line, 65536);
    }

    return Response;
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

    imap_response Response = imap_read_greeting_response(Imap);
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

static BOOL
imap_login(imap *Imap, char *Login, char *Password)
{
    int CommandNumber = Imap->CommandNumber;
    char CommandTag[10];
    sprintf(CommandTag, "A%03d", CommandNumber);

    if (!imap_command(Imap, TRUE, "%s %s %s %s\r\n", CommandTag, IMAP_COMMAND_LOGIN, Login, Password))
    {
        printf("Login failed, exiting!\n");
        return FALSE;
    }

    imap_response Response = imap_read_login_response(Imap, CommandTag);
    if (!Response.Success)
    {
        printf("Failed parsing login message\n");
        return FALSE;
    }

    if (Response.ParsedCapabilities)
    {
        Imap->ParsedCapabilities = 1;
        Imap->HasIdle = Response.HasIdle;
    }
    
    return TRUE;
}

static BOOL
imap_examine(imap *Imap, char *Folder)
{
    int CommandNumber = Imap->CommandNumber;

    char CommandTag[10];
    sprintf(CommandTag, "A%03d", CommandNumber);

    if (!imap_command(Imap, TRUE, "%s %s %s\r\n", CommandTag, IMAP_COMMAND_EXAMINE, Folder))
    {
        printf("Examine failed, exiting!\n");
        return FALSE;
    }

    imap_response Response = imap_read_examine_response(Imap, CommandTag);
    if (!Response.Success)
    {
        printf("Failed parsing examine message\n");
        return FALSE;
    }

    return TRUE;
}

static BOOL
imap_idle(imap *Imap)
{
    // TODO(Oskar): Assert that Imap connection supports idle?
    int CommandNumber = Imap->CommandNumber;
    char CommandTag[10];
    sprintf(CommandTag, "A%03d", CommandNumber);

    if (!imap_command(Imap, TRUE, "%s %s\r\n", CommandTag, IMAP_COMMAND_IDLE))
    {
        printf("Idle failed, exiting!\n");
        return FALSE;
    }
    
    imap_response Response = imap_read_idle_response(Imap);
    if (!Response.Success)
    {
        printf("Failed parsing idle message\n");
        return FALSE;
    }

    return TRUE;
}

static imap_response
imap_idle_listen(imap *Imap)
{
    return imap_read_idle_listen_response(Imap);
}

static BOOL
imap_done(imap *Imap)
{
    if (!imap_command(Imap, FALSE, "%s\r\n", IMAP_COMMAND_DONE))
    {
        printf("Done failed, exiting!\n");
        return FALSE;
    }

    int LastCommandNumber = Imap->CommandNumber - 1;
    char CommandTag[10];
    sprintf(CommandTag, "A%03d", LastCommandNumber);

    imap_response Response = imap_read_done_response(Imap, CommandTag);
    if (!Response.Success)
    {
        printf("Failed parsing done message\n");
        return FALSE;
    }

    return TRUE;
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
    char CommandTag[10];
    sprintf(CommandTag, "A%03d", CommandNumber);

     if (!imap_command(Imap, TRUE, "%s %s %s\r\n", CommandTag, IMAP_COMMAND_SEARCH, Keyword))
    {
        printf("Done failed, exiting!\n");
        return FailedResponse();
    }

    imap_response Response = imap_read_search_response(Imap, CommandTag);
    if (!Response.Success)
    {
        printf("Failed parsing search message\n");
        return FailedResponse();
    }
    
    return Response;
}

#if 0
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
#endif

static void
imap_destroy(imap *Imap)
{
    tls_disconnect(&Imap->Socket);
}