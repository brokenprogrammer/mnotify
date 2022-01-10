static BOOL
imap_parse_is_tag(tokenizer *Tokenizer)
{
    token Token = PeekToken(Tokenizer);

    if (Token.Type == Token_OpenParen    ||
        Token.Type == Token_CloseParen   ||
        Token.Type == Token_CloseParen   ||
        Token.Type == Token_OpenBracket  ||
        Token.Type == Token_CloseBracket ||
        Token.Type == Token_Space        ||
        Token.Type == Token_Percent      ||
        Token.Type == Token_OpenBrace    ||
        Token.Type == Token_CloseBrace)
    {
        return FALSE;
    }

    return TRUE;
}

static imap_response_status
imap_parse_response_status(token Name)
{
    if (strncmp(Name.Text, "OK", Name.TextLength) == 0)
    {
        return IMAP_RESPONSE_STATUS_OK;
    }
    else if (strncmp(Name.Text, "NO", Name.TextLength) == 0)
    {
        return IMAP_RESPONSE_STATUS_NO;
    }
    else if (strncmp(Name.Text, "BAD", Name.TextLength) == 0)
    {
        return IMAP_RESPONSE_STATUS_BAD;
    }
    else if (strncmp(Name.Text, "PREAUTH", Name.TextLength) == 0)
    {
        return IMAP_RESPONSE_STATUS_PREAUTH;
    }
    else if (strncmp(Name.Text, "BYE", Name.TextLength) == 0)
    {
        return IMAP_RESPONSE_STATUS_BYE;
    }
    else
    {
        return IMAP_RESPONSE_STATUS_INVALID;
    }
}

static BOOL
imap_parse_response_list(tokenizer *Tokenizer, 
    imap_response_data *Data, unsigned int *DataCount)
{
    if (ExpectTokenType(Tokenizer, Token_OpenParen))
    {
        // NOTE(Oskar): Missing open paren
        return FALSE;
    }

    if (!imap_parse_response_data(Tokenizer, Data, DataCount))
    {
        // NOTE(Oskar): Invalid list input
        return FALSE;
    }

    if (ExpectTokenType(Tokenizer, Token_CloseParen))
    {
        // NOTE(Oskar): Missing closing paren
        return FALSE;
    }

    return TRUE;
}

static BOOL
imap_parse_response_data(tokenizer *Tokenizer, 
    imap_response_data *Data, unsigned int *DataCount)
{
    for (;;)
    {
        token Token = PeekToken(Tokenizer);

        BOOL Proceeed = TRUE;
        char Content[256];
        switch (Token.Type)
        {
            case Token_OpenBracket:
            {
                // TODO(Oskar): Parse Literal, for mnotify we can ignore for now since we don't use fetch
                while (Token.Type != Token_CloseBracket)
                {
                    if (Token.Type == Token_Unknown ||
                        Token.Type == Token_EndOfLine ||
                        Token.Type == Token_EndOfStream)
                    {
                        // Parsing error
                        return FALSE;
                    }
                    Token = GetToken(Tokenizer);
                }
            } break;

            case Token_String:
            {
                sprintf(Content, "%.*s", Token.TextLength, Token.Text);
            } break;

            case Token_OpenParen:
            {
                if (!imap_parse_response_list(Tokenizer, Data, DataCount))
                {
                    // NOTE(Oskar): Failed to parse list.
                    return FALSE;
                }

                Proceeed = FALSE;
            } break;
            case Token_CloseParen:
            {
                Proceeed = FALSE;
            } break;
            case Token_EndOfLine:
            {
                return TRUE;
            } break;
            default:
            {
                // NOTE(Oskar): Just default to reading an atom
                sprintf(Content, "%.*s", Token.TextLength, Token.Text);                
            } break;
        }

        if (Proceeed)
        {
            sprintf(Data[(*DataCount)++].Value, "%s", Content);
            GetToken(Tokenizer);
        }

        // NOTE(Oskar): Check if next token is an indicator of us ending the fields parse.
        Token = PeekToken(Tokenizer);
        if (Token.Type == Token_Unknown     ||
            Token.Type == Token_EndOfLine   ||
            Token.Type == Token_EndOfStream ||
            Token.Type == Token_CloseParen  ||
            Token.Type == Token_CloseBrace)
        {
            return TRUE;   
        }

        if (ExpectTokenType(Tokenizer, Token_Space))
        {
            // NOTE(Oskar): Missing space
            return FALSE;
        }
    }

    return FALSE;
}

static imap_response_code
imap_parse_response_code(char *Name)
{
    if (strcmp(Name, "ALERT") == 0)
    {
        return IMAP_RESPONSE_CODE_ALERT;
    }
    else if (strcmp(Name, "BADCHARSET") == 0)
    {
        return IMAP_RESPONSE_CODE_BADCHARSET;
    }
    else if (strcmp(Name, "CAPABILITY") == 0)
    {
        return IMAP_RESPONSE_CODE_CAPABILITY;
    }
    else if (strcmp(Name, "PARSE") == 0)
    {
        return IMAP_RESPONSE_CODE_PARSE;
    }
    else if (strcmp(Name, "PERMANENTFLAGS") == 0)
    {
        return IMAP_RESPONSE_CODE_PERMANENTFLAGS;
    }
    else if (strcmp(Name, "READ-ONLY") == 0)
    {
        return IMAP_RESPONSE_CODE_READ_ONLY;
    }
    else if (strcmp(Name, "READ-WRITE") == 0)
    {
        return IMAP_RESPONSE_CODE_READ_WRITE;
    }
    else if (strcmp(Name, "TRYCREATE") == 0)
    {
        return IMAP_RESPONSE_CODE_TRYCREATE;
    }
    else if (strcmp(Name, "UIDNEXT") == 0)
    {
        return IMAP_RESPONSE_CODE_UIDNEXT;
    }
    else if (strcmp(Name, "UIDVALIDITY") == 0)
    {
        return IMAP_RESPONSE_CODE_UIDVALIDITY;
    }
    else if (strcmp(Name, "UNSEEN") == 0)
    {
        return IMAP_RESPONSE_CODE_UNSEEN;
    }
    else
    {
        return IMAP_RESPONSE_CODE_INVALID;
    }
}

static BOOL
imap_parse_response_code_and_data(tokenizer *Tokenizer, imap_response *Response)
{
    if (ExpectTokenType(Tokenizer, Token_OpenBrace))
    {
        // NOTE(Oskar): Missing opening brace
        return FALSE;
    }

    if (!imap_parse_response_data(Tokenizer, Response->Data, &Response->DataCount))
    {
        printf("Error: Failed to parse data!\n");
        return FALSE;
    }

    if (Response->DataCount == 0)
    {
        // Parse Error no data
    }

    // NOTE(Oskar): Use First field as Code.
    Response->Code = imap_parse_response_code(Response->Data[0].Value);

    // NOTE(Oskar): Pushing all fields back
    for (int Index = 0; Index < Response->DataCount - 1; ++Index)
    {
        Response->Data[Index] = Response->Data[Index + 1];
    }
    Response->DataCount--;

    if (ExpectTokenType(Tokenizer, Token_CloseBrace))
    {
        printf("Error Missing close brace!\n");
        return FALSE;
    }

    return TRUE;
}

static BOOL
imap_parse_response_info(tokenizer *Tokenizer, char *Out)
{
    int OutLength = 0;
    token Token = GetToken(Tokenizer);
    while (Token.Type != Token_EndOfLine &&
           Token.Type != Token_EndOfStream)
    {
        OutLength += sprintf(Out + OutLength, "%.*s", Token.TextLength, Token.Text);
        Token = GetToken(Tokenizer);
    }

    return TRUE;
}

static imap_response
imap_parse_response(imap_parser *Parser)
{
    imap_response Response = {0};
    Response.Type = IMAP_RESPONSE_TYPE_UNKNOWN;
    Response.Tag[0] = 0;
    Response.Status = IMAP_RESPONSE_CODE_INVALID;
    Response.DataCount = 0;
    Response.StatusCode = 0;
    Response.Code = IMAP_RESPONSE_CODE_INVALID;
    Response.Info[0] = 0;

    for(int Index = 0; Index < ArrayCount(Response.Data); ++Index)
    {
        Response.Data[Index].Value[0] = 0;
    }


    if (!imap_parse_is_tag(&Parser->Tokenizer))
    {
        Parser->HasError = TRUE;
        sprintf(Parser->Error, "%s", "Response tag is invalid.");
        
        return Response;
    }

    token Tag = GetToken(&Parser->Tokenizer);

    if (Tag.Type == Token_Plus) // Continuation response
    {
        Response.Type = IMAP_RESPONSE_TYPE_CONTINUATION;
        token Atom = GetToken(&Parser->Tokenizer);
        
        if (!imap_parse_response_info(&Parser->Tokenizer, Response.Info))
        {
            Parser->HasError = TRUE;
            sprintf(Parser->Error, "%s", "Response contains invalid ending.");
            
            return Response;
        }

        return Response;
    }

    if (ExpectTokenType(&Parser->Tokenizer, Token_Space))
    {
        Parser->HasError = TRUE;
        sprintf(Parser->Error, "%s", "Response contains invalid format of input.");

        return Response;
    }

    // NOTE(Oskar): Can be both data or status type. We first try with status.
    tokenizer StatusTokenizer = Parser->Tokenizer;
    token Atom = GetToken(&StatusTokenizer);
    if (!ExpectTokenType(&StatusTokenizer, Token_Space))
    {
        imap_response_status Status = imap_parse_response_status(Atom);
        if (Status != IMAP_RESPONSE_STATUS_INVALID)
        {
            Response.Type = IMAP_RESPONSE_TYPE_STATUS;
            sprintf(Response.Tag, "%.*s", Tag.TextLength, Tag.Text);
            Response.Status = Status;

            token NextToken = PeekToken(&StatusTokenizer);
            if (NextToken.Type == Token_OpenBrace)
            {
                // NOTE(Oskar): Has code and arguments, Example
                // * OK [UNSEEN 12]
                if (!imap_parse_response_code_and_data(&StatusTokenizer, &Response))
                {
                    Parser->HasError = TRUE;
                    sprintf(Parser->Error, "%s", "Response contains invalid format section input.");
                    
                    return Response;
                }
            }

            if (!imap_parse_response_info(&StatusTokenizer, Response.Info))
            {
                Parser->HasError = TRUE;
                sprintf(Parser->Error, "%s", "Response contains invalid ending.");
                
                return Response;
            }

            return Response;
        }
    }

    // NOTE(Oskar): It is not a status response so we parse it as data.
    Response.Type = IMAP_RESPONSE_TYPE_DATA;
    if (!imap_parse_response_data(&Parser->Tokenizer, Response.Data, &Response.DataCount))
    {
        Parser->HasError = TRUE;
        sprintf(Parser->Error, "%s", "Error parsing data section.");
        
        return Response;
    } 

    return Response;
}

static imap_parser
imap_create_parser(char *Buffer, unsigned int BufferLength)
{
    imap_parser Parser = {0};
    Parser.HasError = FALSE;
    Parser.Error[0] = '\0';
    Parser.Tokenizer = Tokenize(Buffer, BufferLength);
    return Parser;
}