#define IMAP_PARSER_ERR_NOT_DONE 2

static int
imap_parse_tagged_ok(tokenizer *Tokenizer, char *Tag)
{
    // NOTE(Oskar): Expecting response in format:
    // [Tag] OK
    token Token = GetToken(Tokenizer);
    if (!Token.Type == Token_Identifier ||
        !strstr(Token.Text, Tag))
    {
        printf("Tag does not match\n");
        return 0;
    }
    GetToken(Tokenizer); // Space

    Token = GetToken(Tokenizer);
    if (!Token.Type == Token_Identifier ||
        !strstr(Token.Text, "OK"))
    {
        printf("Response is not OK\n");
        return 0;
    }

    return 1;
}

static int
imap_parse_capabilities(tokenizer *Tokenizer, imap_response *Response)
{
    Response->ParsedCapabilities = 1;
    Response->HasIdle = 0;

    token Token = GetToken(Tokenizer);
    if (!strstr(Token.Text, "CAPABILITY"))
    {
        printf("Expected CAPABILITY identifier!\n");
        return 0;
    }

    // NOTE(Oskar): Check one capability at the time.    
    char Capability[128];
    Token = GetToken(Tokenizer);
    while (Token.Type != Token_EndOfStream)
    {
        if (Token.Type == Token_Identifier)
        {
            strncpy(Capability, Token.Text, Token.TextLength);
            Capability[Token.TextLength] = '\0';
            if (strstr(Capability, "IDLE"))
            {
                Response->HasIdle = 1;
            }
        }
        Token = GetToken(Tokenizer);
    }

    return 1;
}

static imap_identified_provider
imap_parse_greeting(tokenizer *Tokenizer, imap_response *Response)
{
    Response->Provider = IMAP_IDENTIFIED_PROVIDER_UNKNOWN;

    // NOTE(Oskar): Expect untagged response in format:
    // * Ok .. Gimap
    if (ExpectTokenType(Tokenizer, Token_Asterisk) != 0)
    {
        printf("Failed to parse untagged greeting message!\n");
        return 0;
    }
    GetToken(Tokenizer);

    if (strstr(Tokenizer->Input, "Gimap"))
    {
        Response->Provider = IMAP_IDENTIFIED_PROVIDER_GMAIL;
    }
    else
    {
        token Token = GetToken(Tokenizer);

        if (Token.Type == Token_Identifier &&
            strstr(Token.Text, "OK"))
        {
            Token = GetToken(Tokenizer); // Space
            Token = GetToken(Tokenizer);
            if (Token.Type == Token_OpenBrace)
            {
                if (strstr(Token.Text, "CAPABILITY"))
                {
                    // NOTE(Oskar): Not 100% sure this is always guaranteed to be yahoo but for now sure.
                    Response->Provider = IMAP_IDENTIFIED_PROVIDER_YAHOO;
                    return imap_parse_capabilities(Tokenizer, Response);
                }
            }
        }
    }

    return 1;
}

static int
imap_parse_idle(tokenizer *Tokenizer)
{
    token PlusToken = GetToken(Tokenizer);
    token Space = GetToken(Tokenizer);
    token Idling = GetToken(Tokenizer);

    if (PlusToken.Type == Token_Plus && 
        Space.Type == Token_Space &&
        Idling.Type == Token_Identifier)
    {
        if (strstr(Idling.Text, "idling"))
        {
            return 1;
        }
    }

    return 0;
}

static imap_idle_message
imap_parse_idle_message(tokenizer *Tokenizer)
{
    if (ExpectTokenType(Tokenizer, Token_Asterisk))
    {
        printf("Unexpected token, Expected '*'!\n");
        return IMAP_IDLE_MESSAGE_UNKNOWN;
    }

    if (ExpectTokenType(Tokenizer, Token_Space))
    {
        printf("Unexpected token, Expected Space!\n");
        return IMAP_IDLE_MESSAGE_UNKNOWN;
    }

    // TODO(Oskar): Double check that there is always a number here.
    if (ExpectTokenType(Tokenizer, Token_Number))
    {
        printf("Unexpected token, Expected Space!\n");
        return IMAP_IDLE_MESSAGE_UNKNOWN;
    }

    if (ExpectTokenType(Tokenizer, Token_Space))
    {
        printf("Unexpected token, Expected Space!\n");
        return IMAP_IDLE_MESSAGE_UNKNOWN;
    }

    token Token = GetToken(Tokenizer);
    if (Token.Type != Token_Identifier)
    {
        printf("Unexpected token, Expected Space!\n");
        return IMAP_IDLE_MESSAGE_UNKNOWN;
    }
    else
    {
        if (strstr(Token.Text, "EXISTS"))
        {
            return IMAP_IDLE_MESSAGE_EXISTS;
        }
        else if(strstr(Token.Text, "EXPUNGE"))
        {
            return IMAP_IDLE_MESSAGE_EXPUNGE;
        }
        else
        {
            return IMAP_IDLE_MESSAGE_UNKNOWN;
        }
    }
}

// TODO(Oskar): This is broken if there are too many unseen emails cause 
// two rows has to be parsed then.
static int
parse_search_result(tokenizer *Tokenizer, imap_response *Response)
{
    if (!Response->Initialized)
    {
        Response->NumberOfNumbers = 0;
        Response->Initialized = 1;
    }

    if (!Response->ParsedTag)
    {
        if (ExpectTokenType(Tokenizer, Token_Asterisk))
        {
            printf("Unexpected token, Expected '*'!\n");
            return 0;
        }
        GetToken(Tokenizer); // Space

        token Token = GetToken(Tokenizer);
        if (Token.Type != Token_Identifier &&
            strstr(Token.Text, "SEARCH"))
        {
            return 0;
        }

        Response->ParsedTag = 1;
    }

    token Token = GetToken(Tokenizer);
    while (Token.Type != Token_EndOfStream) 
    {
        if (Token.Type == Token_Number)
        {
            Response->SequenceNumbers[Response->NumberOfNumbers++] = Token.I32;
        }
        Token = GetToken(Tokenizer);
    }

    return 1;
}

// static int 
// imap_parse_search(imap *Imap, imap_response *Response, int CommandNumber)
//{
    // char ResponseBuffer[65536] = {0};
    // int BufferLength = 0;
    // int Received = imap_read(Imap, ResponseBuffer, 65536);
    // while (Received >= 0 && 
    //        !strstr(ResponseBuffer, "\r\n"))
    // {
    //     BufferLength += Received;
    //     Received = imap_read(Imap, ResponseBuffer + BufferLength, 65536 - BufferLength);
    // }

    // if (Received <= 0)
    // {
    //     printf("Failed to read buffer!\n");
    //     Response->Success = 0;
    //     return -1;
    // }

//     char *Line;
//     char *Temp;
//     Line = strtok_s(ResponseBuffer, "\r\n", &Temp);
//     do
//     {
//         tokenizer Tokenizer = Tokenize(Line, strlen(Line));

//         token Token = PeekToken(&Tokenizer);
//         while (Token.Type == Token_Space)
//         {
//             Token = GetToken(&Tokenizer);
//         }

//         if (Token.Type == Token_Identifier)
//         {
//             char Tag[10];
//             sprintf(Tag, "A%03d", CommandNumber);
//             if(imap_parse_tagged_ok(&Tokenizer, Tag))
//             {
//                 Response->Success = 1;
//                 return 1;
//             }
//             else
//             {
//                 Response->Success = 0;
//             }

//             return 0;
//         }
        
//         if (!parse_search_result(&Tokenizer, Response))
//         {
//             Response->Success = 0;
//             return 0;
//         }
//     }
//     while ((Line = strtok_s(NULL, "\r\n", &Temp)) != NULL);

//     Response->Success = IMAP_PARSER_ERR_NOT_DONE;
//     return 0;
// }

// NOTE(Oskar): Returns true if parse is done
static int
imap_parse_fetch_single(tokenizer *Tokenizer, imap_email_message *Email)
{
    token Token = PeekToken(Tokenizer);
    if (Token.Type == Token_Asterisk)
    {
        GetToken(Tokenizer);
        GetToken(Tokenizer); // Space

        Token = GetToken(Tokenizer);
        if (Token.Type != Token_Number)
        {
            //Email->Error = 1;
            return 0;
        }

        Email->SequenceNumber = Token.I32;
    }

    if (Token.Type == Token_Identifier)
    {
        if (strstr(Token.Text, "Subject"))
        {
            GetToken(Tokenizer); // Subject
            GetToken(Tokenizer); // :
            GetToken(Tokenizer); // Space
            sprintf(Email->Subject, Token.Text);
        }
        else if (strstr(Token.Text, "From"))
        {
            GetToken(Tokenizer); // From
            GetToken(Tokenizer); // :
            GetToken(Tokenizer); // Space
            sprintf(Email->From, Token.Text);
        }
        else if (strstr(Token.Text, "Date"))
        {
            GetToken(Tokenizer); // Date
            GetToken(Tokenizer); // :
            GetToken(Tokenizer); // Space
            sprintf(Email->Date, Token.Text);
        }
    }
    else if (Token.Type == Token_CloseParen)
    {
        return 1;
    }
    else 
    {
        return 0;
    }

    return 0;
}

static int
imap_parse_fetch(imap *Imap, imap_response *Response, int CommandNumber)
{
    char ResponseBuffer[65536] = {0};
    int BufferLength = 0;
    int Received = imap_read(Imap, ResponseBuffer, 65536);
    while (Received >= 0 && 
           !strstr(ResponseBuffer, "\r\n"))
    {
        BufferLength += Received;
        Received = imap_read(Imap, ResponseBuffer + BufferLength, 65536 - BufferLength);
    }

    if (Received <= 0)
    {
        printf("Failed to read buffer!\n");
        Response->Success = 0;
        return -1;
    }

    imap_email_message *CurrentEmail = &Response->Emails[Response->ParsedEmails];

    char *Line;
    char *Temp;
    Line = strtok_s(ResponseBuffer, "\r\n", &Temp);
    do
    {
        tokenizer Tokenizer = Tokenize(Line, strlen(Line));

        if (Response->Emails[Response->ParsedEmails].Error)
        {
            Response->Success = 0;
            return -1;
        }
        
        if (Response->ActiveParse)
        {
            if (imap_parse_fetch_single(&Tokenizer, CurrentEmail))
            {
                Response->ActiveParse = 0;
                Response->ParsedEmails++;
                CurrentEmail = &Response->Emails[Response->ParsedEmails];
            }
        }
        else
        {
            token Token = PeekToken(&Tokenizer);
            if (Token.Type == Token_Asterisk)
            {
                Response->ActiveParse = 1;
                imap_parse_fetch_single(&Tokenizer, CurrentEmail);
            }
            else
            {
                char Tag[10];
                sprintf(Tag, "A%03d", CommandNumber);
                if(imap_parse_tagged_ok(&Tokenizer, Tag))
                {
                    Response->Success = 1;
                    return 1;
                }
            }
        }
    }
    while ((Line = strtok_s(NULL, "\r\n", &Temp)) != NULL);

    Response->Success = IMAP_PARSER_ERR_NOT_DONE;
    return 0;
}