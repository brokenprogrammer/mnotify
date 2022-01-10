static void
imap_write_error(imap *Imap, imap_client_error ErrorType, char *Data)
{
    int Length = 0;
    Length = sprintf(Imap->Error, "%s", ImapClientErrorStrings[ErrorType]);

    if (Data != NULL)
    {
        Length += sprintf(Imap->Error + Length, ": %s\n", Data);
    }
    else 
    {
        Length += sprintf(Imap->Error + Length, "\n");
    }

    Imap->ErrorLength = Length;
}

static void
imap_write_parser_error(imap *Imap, imap_parser *Parser)
{
    int Length = 0;
    Length = sprintf(Imap->Error, "%s\n", Parser->Error);

    Imap->ErrorLength = Length;
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
imap_read_line(imap *Imap, char *Buffer, int BufferSize, BOOL ClearBuffer)
{
    static char LocalBuffer[IMAP_LOCAL_READ_BUFFER_SIZE];
    static int LocalBufferSize = 0;

    if (ClearBuffer)
    {
        LocalBufferSize = 0;
        memset(LocalBuffer, 0, IMAP_LOCAL_READ_BUFFER_SIZE);
    }

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
            return -2;
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

static imap_client_error
imap_check_read_result(int ReadResult)
{
    if (ReadResult > 0)
    {
        return IMAP_CLIENT_ERROR_SUCCESS;
    }
    else if (ReadResult == -1)
    {
        return IMAP_CLIENT_ERROR_SOCKET_DISCONNECTED;
    }
    else
    {
        return IMAP_CLIENT_ERROR_SOCKET_RECIEVE;
    }
}

static void
imap_read_capabilities(imap *Imap, imap_response_data *Data, unsigned int DataCount)
{
    for (unsigned int Index = 0; Index < DataCount; ++Index)
    {
        if (strcmp(Data[Index].Value, "IDLE") == 0)
        {
            Imap->HasIdle = TRUE;
        }
    }

    Imap->ParsedCapabilities = TRUE;
}

static imap_client_error
imap_init(imap *Imap, char *HostName, int Port)
{
    if (tls_connect(&Imap->Socket, HostName, Port) != 0)
    {
        imap_write_error(Imap, IMAP_CLIENT_ERROR_SOCKET_CONNECTION, NULL);
        return IMAP_CLIENT_ERROR_SOCKET_CONNECTION;
    }

    Imap->ParsedCapabilities = FALSE;
    Imap->HasIdle = FALSE;
    Imap->HasRecent = FALSE;
    Imap->CommandNumber = 1;

    char Line[IMAP_DEFAULT_READ_BUFFER_SIZE] = {0};
    int ReadLength = imap_read_line(Imap, Line, IMAP_DEFAULT_READ_BUFFER_SIZE, TRUE);
    imap_client_error ReadResult = imap_check_read_result(ReadLength);
    
    if (ReadResult != IMAP_CLIENT_ERROR_SUCCESS)
    {
        imap_write_error(Imap, ReadResult, NULL);
        return ReadResult;
    }

    imap_parser Parser = imap_create_parser(Line, strlen(Line));
    imap_response Response = imap_parse_response(&Parser);    

    if (Parser.HasError)
    {
        imap_write_parser_error(Imap, &Parser);
        return IMAP_CLIENT_ERROR_PARSE;
    }

    if (Response.Type == IMAP_RESPONSE_TYPE_STATUS)
    {
        if (Response.Status == IMAP_RESPONSE_STATUS_OK)
        {
            if (Response.Code == IMAP_RESPONSE_CODE_CAPABILITY)
            {
                imap_read_capabilities(Imap, Response.Data, Response.DataCount);
            }
        }
        else 
        {
            imap_write_error(Imap, IMAP_CLIENT_ERROR_BAD_RESPONSE, Line);
            return IMAP_CLIENT_ERROR_BAD_RESPONSE;
        }
    }
    else if (Response.Type == IMAP_RESPONSE_TYPE_DATA)
    {
        for (int Index = 0; Index < Response.DataCount; ++Index)
        {
            if (strcmp(Response.Data[Index].Value, "Gimap") == 0)
            {
                Imap->HasRecent = FALSE;
            }
        }
    }
    else
    {
        imap_write_error(Imap, IMAP_CLIENT_ERROR_BAD_RESPONSE, Line);
        return IMAP_CLIENT_ERROR_BAD_RESPONSE;
    }

    return IMAP_CLIENT_ERROR_SUCCESS;
}

static imap_client_error
imap_login(imap *Imap, char *Login, char *Password)
{
    int CommandNumber = Imap->CommandNumber;
    char CommandTag[10];
    sprintf(CommandTag, "A%03d", CommandNumber);

    if (!imap_command(Imap, TRUE, "%s %s %s %s\r\n", CommandTag, IMAP_COMMAND_LOGIN, Login, Password))
    {
        imap_write_error(Imap, IMAP_CLIENT_ERROR_SOCKET_WRITE, NULL);
        return IMAP_CLIENT_ERROR_SOCKET_WRITE;
    }

    char Line[IMAP_DEFAULT_READ_BUFFER_SIZE] = {0};
    BOOL FoundTag = FALSE;
    BOOL ClearBuffer = TRUE;
    while (!FoundTag)
    {
        int ReadLength = imap_read_line(Imap, Line, IMAP_DEFAULT_READ_BUFFER_SIZE, ClearBuffer);
        ClearBuffer = FALSE;

        imap_client_error ReadResult = imap_check_read_result(ReadLength);
        if (ReadResult != IMAP_CLIENT_ERROR_SUCCESS)
        {
            imap_write_error(Imap, ReadResult, NULL);
            return ReadResult;
        }

        imap_parser Parser = imap_create_parser(Line, strlen(Line));
        imap_response Response = imap_parse_response(&Parser);

        if (Parser.HasError)
        {
            imap_write_parser_error(Imap, &Parser);
            return IMAP_CLIENT_ERROR_PARSE;
        }

        if (Response.Type == IMAP_RESPONSE_TYPE_STATUS)
        {
            if (Response.Status == IMAP_RESPONSE_STATUS_OK)
            {
                if (Response.Code == IMAP_RESPONSE_CODE_CAPABILITY)
                {
                    imap_read_capabilities(Imap, Response.Data, Response.DataCount);
                }

                if (strcmp(Response.Tag, CommandTag) == 0)
                {
                    FoundTag = TRUE;
                }
            }
            else
            {
                imap_write_error(Imap, IMAP_CLIENT_ERROR_BAD_RESPONSE, Line);
                return IMAP_CLIENT_ERROR_BAD_RESPONSE;
            }
        }
        else if (Response.Type == IMAP_RESPONSE_TYPE_DATA)
        {
            char *Command = Response.Data[0].Value;
            if (strcmp(Command, "CAPABILITY") == 0)
            {
                imap_read_capabilities(Imap, Response.Data, Response.DataCount);
            }
        }
    }
    
    return IMAP_CLIENT_ERROR_SUCCESS;
}

static imap_client_error
imap_examine(imap *Imap, char *Folder)
{
    int CommandNumber = Imap->CommandNumber;

    char CommandTag[10];
    sprintf(CommandTag, "A%03d", CommandNumber);

    if (!imap_command(Imap, TRUE, "%s %s %s\r\n", CommandTag, IMAP_COMMAND_EXAMINE, Folder))
    {
        imap_write_error(Imap, IMAP_CLIENT_ERROR_SOCKET_WRITE, NULL);
        return IMAP_CLIENT_ERROR_SOCKET_WRITE;
    }

    char Line[IMAP_DEFAULT_READ_BUFFER_SIZE] = {0};
    BOOL FoundTag = FALSE;
    BOOL ClearBuffer = TRUE;
    while (!FoundTag)
    {
        int ReadLength = imap_read_line(Imap, Line, IMAP_DEFAULT_READ_BUFFER_SIZE, ClearBuffer);
        ClearBuffer = FALSE;

        imap_client_error ReadResult = imap_check_read_result(ReadLength);
        if (ReadResult != IMAP_CLIENT_ERROR_SUCCESS)
        {
            imap_write_error(Imap, ReadResult, NULL);
            return ReadResult;
        }

        imap_parser Parser = imap_create_parser(Line, strlen(Line));
        imap_response Response = imap_parse_response(&Parser);

        if (Parser.HasError)
        {
            imap_write_parser_error(Imap, &Parser);
            return IMAP_CLIENT_ERROR_PARSE;
        }

        // NOTE(Oskar): We are doing this request just to select the
        // mailbox. Ignore the data we get back except tagged OK.
        if (Response.Type == IMAP_RESPONSE_TYPE_STATUS)
        {
            if (Response.Status == IMAP_RESPONSE_STATUS_OK)
            {
                if (strcmp(Response.Tag, CommandTag) == 0)
                {
                    FoundTag = TRUE;
                }
            }
            else
            {
                imap_write_error(Imap, IMAP_CLIENT_ERROR_BAD_RESPONSE, Line);
                return IMAP_CLIENT_ERROR_BAD_RESPONSE;
            }
        }
    }

    return IMAP_CLIENT_ERROR_SUCCESS;
}

static imap_client_error
imap_idle(imap *Imap)
{
    int CommandNumber = Imap->CommandNumber;
    char CommandTag[10];
    sprintf(CommandTag, "A%03d", CommandNumber);

    if (!imap_command(Imap, TRUE, "%s %s\r\n", CommandTag, IMAP_COMMAND_IDLE))
    {
        imap_write_error(Imap, IMAP_CLIENT_ERROR_SOCKET_WRITE, NULL);
        return IMAP_CLIENT_ERROR_SOCKET_WRITE;
    }
    
    char Line[IMAP_DEFAULT_READ_BUFFER_SIZE] = {0};
    BOOL FoundTag = FALSE;
    BOOL ClearBuffer = TRUE;
    int ReadLength = imap_read_line(Imap, Line, IMAP_DEFAULT_READ_BUFFER_SIZE, ClearBuffer);
    
    imap_client_error ReadResult = imap_check_read_result(ReadLength);
    if (ReadResult != IMAP_CLIENT_ERROR_SUCCESS)
    {
        imap_write_error(Imap, ReadResult, NULL);
        return ReadResult;
    }

    imap_parser Parser = imap_create_parser(Line, strlen(Line));
    imap_response Response = imap_parse_response(&Parser);

    if (Parser.HasError)
    {
        imap_write_parser_error(Imap, &Parser);
        return IMAP_CLIENT_ERROR_PARSE;
    }

    if (Response.Type != IMAP_RESPONSE_TYPE_CONTINUATION)
    {
        imap_write_error(Imap, IMAP_CLIENT_ERROR_BAD_RESPONSE, Line);
        return IMAP_CLIENT_ERROR_BAD_RESPONSE;
    }

    return IMAP_CLIENT_ERROR_SUCCESS;
}

static imap_idle_response
imap_idle_listen(imap *Imap)
{
    imap_idle_response Result = {0};
    Result.Error = IMAP_CLIENT_ERROR_SUCCESS;

    char Line[IMAP_DEFAULT_READ_BUFFER_SIZE] = {0};
    BOOL FoundTag = FALSE;
    BOOL ClearBuffer = TRUE;
    int ReadLength = imap_read_line(Imap, Line, IMAP_DEFAULT_READ_BUFFER_SIZE, ClearBuffer);

    imap_client_error ReadResult = imap_check_read_result(ReadLength);
    if (ReadResult != IMAP_CLIENT_ERROR_SUCCESS)
    {
        imap_write_error(Imap, ReadResult, NULL);
        Result.Error = ReadResult;
        return Result;
    }

    imap_parser Parser = imap_create_parser(Line, strlen(Line));
    imap_response Response = imap_parse_response(&Parser);

    if (Parser.HasError)
    {
        imap_write_parser_error(Imap, &Parser);
        Result.Error = IMAP_CLIENT_ERROR_PARSE;
        return Result;
    }

    if (Response.Type == IMAP_RESPONSE_TYPE_DATA &&
        Response.DataCount >= 2)
    {
        if (strstr(Response.Data[1].Value, "EXISTS"))
        {
            Result.Type = IMAP_IDLE_MESSAGE_EXISTS;
        }
        else if(strstr(Response.Data[1].Value, "EXPUNGE"))
        {
            Result.Type = IMAP_IDLE_MESSAGE_EXPUNGE;
        }
        else
        {
            Result.Type = IMAP_IDLE_MESSAGE_UNKNOWN;
        }
    }
    else
    {
        Result.Type = IMAP_IDLE_MESSAGE_UNKNOWN;
    }

    return Result;
}

static imap_client_error
imap_done(imap *Imap)
{
    if (!imap_command(Imap, FALSE, "%s\r\n", IMAP_COMMAND_DONE))
    {
        imap_write_error(Imap, IMAP_CLIENT_ERROR_SOCKET_WRITE, NULL);
        return IMAP_CLIENT_ERROR_SOCKET_WRITE;
    }

    int LastCommandNumber = Imap->CommandNumber - 1;
    char CommandTag[10];
    sprintf(CommandTag, "A%03d", LastCommandNumber);

    char Line[IMAP_DEFAULT_READ_BUFFER_SIZE] = {0};
    int ReadLength = imap_read_line(Imap, Line, IMAP_DEFAULT_READ_BUFFER_SIZE, TRUE);
    
    imap_client_error ReadResult = imap_check_read_result(ReadLength);
    if (ReadResult != IMAP_CLIENT_ERROR_SUCCESS)
    {
        imap_write_error(Imap, ReadResult, NULL);
        return ReadResult;
    }

    imap_parser Parser = imap_create_parser(Line, strlen(Line));
    imap_response Response = imap_parse_response(&Parser);

    if (Parser.HasError)
    {
        imap_write_parser_error(Imap, &Parser);
        return IMAP_CLIENT_ERROR_PARSE;
    }

    if (Response.Type == IMAP_RESPONSE_TYPE_STATUS)
    {
        if (Response.Status == IMAP_RESPONSE_STATUS_OK)
        {
            if (strcmp(Response.Tag, CommandTag) == 0)
            {
                return IMAP_CLIENT_ERROR_SUCCESS;
            }
        }
        else
        {
            imap_write_error(Imap, IMAP_CLIENT_ERROR_BAD_RESPONSE, Line);
            return IMAP_CLIENT_ERROR_BAD_RESPONSE;
        }
    }
    else
    {
        imap_write_error(Imap, IMAP_CLIENT_ERROR_BAD_RESPONSE, Line);
        return IMAP_CLIENT_ERROR_BAD_RESPONSE;
    }

    imap_write_error(Imap, IMAP_CLIENT_ERROR_BAD_RESPONSE, Line);
    return IMAP_CLIENT_ERROR_BAD_RESPONSE;
}

static imap_search_response
imap_search(imap *Imap)
{
    imap_search_response Result = {0};
    Result.Error = IMAP_CLIENT_ERROR_SUCCESS;

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
        imap_write_error(Imap, IMAP_CLIENT_ERROR_SOCKET_WRITE, NULL);
        return Result;
    }

    char Line[IMAP_DEFAULT_READ_BUFFER_SIZE] = {0};
    BOOL FoundTag = FALSE;
    BOOL ClearBuffer = TRUE;
    while (!FoundTag)
    {
        int ReadLength = imap_read_line(Imap, Line, IMAP_DEFAULT_READ_BUFFER_SIZE, ClearBuffer);
        ClearBuffer = FALSE;

        imap_client_error ReadResult = imap_check_read_result(ReadLength);
        if (ReadResult != IMAP_CLIENT_ERROR_SUCCESS)
        {
            imap_write_error(Imap, ReadResult, NULL);
            Result.Error = ReadResult;
            return Result;
        }

        imap_parser Parser = imap_create_parser(Line, strlen(Line));
        imap_response Response = imap_parse_response(&Parser);

        if (Parser.HasError)
        {
            imap_write_parser_error(Imap, &Parser);
            Result.Error = IMAP_CLIENT_ERROR_PARSE;
            return Result;
        }

        // NOTE(Oskar): We are doing this request just to select the
        // mailbox. Ignore the data we get back except tagged OK.
        if (Response.Type == IMAP_RESPONSE_TYPE_STATUS)
        {
            if (Response.Status == IMAP_RESPONSE_STATUS_OK)
            {
                if (strcmp(Response.Tag, CommandTag) == 0)
                {
                    FoundTag = TRUE;
                }
            }
            else
            {
                imap_write_error(Imap, IMAP_CLIENT_ERROR_BAD_RESPONSE, Line);
                Result.Error = IMAP_CLIENT_ERROR_BAD_RESPONSE;
                return Result;
            }
        }
        else if (Response.Type == IMAP_RESPONSE_TYPE_DATA)
        {
            if (strcmp(Response.Data[0].Value, "SEARCH") == 0)
            {
                Result.NumberOfMails = Response.DataCount - 1;
                Result.Error = IMAP_CLIENT_ERROR_SUCCESS;
            }
            else
            {
                imap_write_error(Imap, IMAP_CLIENT_ERROR_BAD_RESPONSE, Line);
                Result.Error = IMAP_CLIENT_ERROR_BAD_RESPONSE;
                return Result;
            }
        }
    }
    
    return Result;
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
    char CommandBuffer[IMAP_DEFAULT_READ_BUFFER_SIZE];
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