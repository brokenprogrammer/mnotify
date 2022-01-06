// TODO(Oskar): Add support for validating Command tag
static int
imap_parse_ok(tokenizer *Tokenizer, token_type ExpectedType)
{
    if (ExpectTokenType(Tokenizer, ExpectedType) != 0)
    {
        printf("Unexpected token!\n");
        return -1;
    }
    GetToken(Tokenizer);

    token Token = GetToken(Tokenizer);
    if (Token.Type == Token_Identifier)
    {
        if (strstr(Token.Text, "OK"))
        {
            return 0;
        }
    }

    return -1;
}

static int
imap_parse_mail_count(imap *Imap, tokenizer *Tokenizer)
{
    char *Keyword = 0;
    if (Imap->HasRecent)
    {
        Keyword = "RECENT";
    }
    else
    {
        Keyword = "EXISTS";
    }

    tokenizer TempTokenizer = *Tokenizer; 
    if (ExpectTokenType(&TempTokenizer, Token_Asterisk) != 0)
    {
        printf("Unexpected token, Expected '*'!\n");
        return -1;
    }

    char Copy[65536]; 
    strcpy(Copy, TempTokenizer.Input);
    char *First;
    char *Temp = strtok(Copy, " ");
    First = Temp;
    if (!First)
    {
        printf("Was unable to split message.\n");
        return -1;
    }

    char *Second = strtok(NULL, " ");

    // TODO(Oskar): Get messages when new are found here?
    if (strcmp(Second, Keyword) == 0)
    {
        Imap->OldMailCount = atoi(First);
    }
    else
    {
        printf("Keywords doesn't match.\n");
        return -1;
    }

    return 0;
}

static int
imap_parse_capabilities(imap *Imap, tokenizer *Tokenizer)
{
    if (!strstr(Tokenizer->Input, "CAPABILITY"))
    {
        printf("Unexpected message\n");
        return -1;
    }

    if (!strstr(Tokenizer->Input, "IDLE"))
    {
        // NOTE(Oskar): Idle not supported.
        
        printf("Idle is not supported\n");
        Imap->HasIdle = -1;

        return -1;
    }

    Imap->HasIdle = 1;
    Imap->ParsedCapabilities = 1;

    return 0;
}

static int
imap_parse_idle(imap *Imap, tokenizer *Tokenizer)
{
    if (strstr(Tokenizer->Input, "+ idling"))
    {
        Imap->State = IMAP_STATE_IDLE;
        return 0;
    }

    return -1;
}

// TODO(Oskar): This is broken if there are too many unseen emails cause 
// two rows has to be parsed then.
static int
parse_search_result(imap *Imap, tokenizer *Tokenizer)
{
    if (ExpectTokenType(Tokenizer, Token_Asterisk))
    {
        printf("Unexpected token, Expected '*'!\n");
        return -1;
    }
    GetToken(Tokenizer);

    memset(Imap->Query, 0, 1024);

    token Token = GetToken(Tokenizer);
    if (Token.Type == Token_Identifier)
    {
        if (strstr(Token.Text, "SEARCH"))
        {
            Token = GetToken(Tokenizer);

            while (Token.Type != Token_EndOfStream) 
            {
                if (Token.Type == Token_Number)
                {
                    sprintf(Imap->Query + strlen(Imap->Query), "%d,", Token.I32);
                }
                Token = GetToken(Tokenizer);
            }

            // NOTE(Oskar): Remove last ','
            size_t QueryLength = strlen(Imap->Query);
            Imap->Query[QueryLength - 1] = '\0';

            printf("Parsed query: %s\n", Imap->Query);
            printf("SUCCESS!!!");
            return 0;
        }
    }

    return 0;
}

static int
imap_parse_header_fields(imap *Imap, tokenizer *Tokenizer)
{
    token Token = GetToken(Tokenizer);
    while (Token.Type != Token_EndOfStream) 
    {
        if (Token.Type == Token_Identifier)
        {
            if (strstr(Token.Text, "Subject"))
            {
                GetToken(Tokenizer); // :
                GetToken(Tokenizer); // Space
                strcpy(Imap->Subject, Tokenizer->Input);
            }
            else if (strstr(Token.Text, "From"))
            {
                GetToken(Tokenizer); // :
                GetToken(Tokenizer); // Space
                strcpy(Imap->From, Tokenizer->Input);
            }
            else if (strstr(Token.Text, "Date"))
            {
                GetToken(Tokenizer); // :
                GetToken(Tokenizer); // Space
                strcpy(Imap->Date, Tokenizer->Input);
            }
            else
            {
                // NOTE(Oskar): Uninteresting line
                return -1;
            }
        }
        else if (Token.Type == Token_CloseParen)
        {
            // NOTE(Oskar): One mail successfully parsed.
            ++Imap->FetchedRows;
            printf("Finished parsing a full header!\n");
            printf("Subject: %s\n", Imap->Subject);
            printf("From: %s\n", Imap->From);
            printf("Date: %s\n", Imap->Date);
        }
        Token = GetToken(Tokenizer);
    }

    return 0;
}

// TODO(Oskar): I am not happy over how this function controlls the inner state.
// lets return data with the result of the parsed content and create a listen
// function.
static int
imap_parse(imap *Imap)
{
    char ResponseBuffer[65536] = {0};
    if (imap_read(Imap, ResponseBuffer, 65536) <= 0)
    {
        printf("Failed to read buffer!\n");
        return -1;
    }

    printf("Got response:\n\t%s\n", ResponseBuffer);
    printf("Parsing ...\n");

    char *Line;
    char *Temp;
    Line = strtok_s(ResponseBuffer, "\r\n", &Temp);
    do
    {
        tokenizer Tokenizer = Tokenize(Line, strlen(Line));
        
        imap_parse_mail_count(Imap, &Tokenizer);

        switch(Imap->State)
        {
            case IMAP_STATE_CONNECTED:
            {
                int SupportIdle = -1;
                if (imap_parse_ok(&Tokenizer, Token_Asterisk) >= 0)
                {
                    Imap->State = IMAP_STATE_NOTAUTHENTICATED;

                    // TODO(Oskar): Check if gmail supported, for now hardcode.
                    Imap->HasRecent = 0;

                    printf("Parsed greeting message.\n");
                    return 0;
                }
                return -1;
            } break;

            case IMAP_STATE_LOGIN:
            {
                // NOTE(Oskar): Can be both tagged and untagged in different orders.
                if (imap_parse_capabilities(Imap, &Tokenizer) >= 0)
                {
                    continue;
                }

                if (imap_parse_ok(&Tokenizer, Token_Identifier) >= 0)
                {
                    Imap->State = IMAP_STATE_AUTHENTICATED;
                    
                    if (Imap->ParsedCapabilities <= 0)
                    {
                        imap_parse_capabilities(Imap, &Tokenizer);
                    }

                    printf("Parsed login message.\n");
                    return 0;
                }
            } break;

            case IMAP_STATE_EXAMING:
            {
                // NOTE(Oskar): Ignoring the untagged results
                if (imap_parse_ok(&Tokenizer, Token_Identifier) >= 0)
                {
                    Imap->State = IMAP_STATE_SELECTED;

                    printf("Parsed examine OK result.\n");
                    return 0;
                }
            } break;

            case IMAP_STATE_PREIDLE:
            {
                if (imap_parse_idle(Imap, &Tokenizer) != 0)
                {
                    return -1;
                }

                printf("Parsed IDLE OK message. Now in idle mode\n");
                return 0;
            } break;

            case IMAP_STATE_IDLE:
            {
                imap_done(Imap);
                return 0;
            } break;

            case IMAP_STATE_PRESEARCHING:
            {
                imap_search(Imap);
                return 0;
            } break;

            case IMAP_STATE_SEARCHING:
            {
                if (imap_parse_ok(&Tokenizer, Token_Identifier) == 0)
                {
                    printf("Successfully parsed search result. Now fetching\n");
                    Imap->State = IMAP_STATE_PREFETCH;
                    imap_fetch(Imap);
                    return 0;
                }

                if (parse_search_result(Imap, &Tokenizer) != 0)
                {
                    // TODO(Oskar): This is temp.
                    // NOTE(Oskar): Parsing additional ids
                    token Token = GetToken(&Tokenizer);
                    if (Token.Type == Token_Number)
                    {
                        while (Token.Type != Token_EndOfStream) 
                        {
                            if (Token.Type == Token_Number)
                            {
                                sprintf(Imap->Query + strlen(Imap->Query), "%d,", Token.I32);
                            }
                            Token = GetToken(&Tokenizer);
                        }
                    }
                    else
                    {
                        printf("Parsing of search result failed.\n");
                        return -1;
                    }
                }
            } break;

            case IMAP_STATE_FETCHING:
            {
                tokenizer Temp = Tokenizer;
                imap_parse_header_fields(Imap, &Temp);

               if (imap_parse_ok(&Tokenizer, Token_Identifier) == 0)
                {
                    Imap->State = IMAP_STATE_SELECTED;
                    imap_idle(Imap);
                    
                    return 0;
                }
            } break;

            default:
            {
                return -1;
            } break;
        }
    }
    while ((Line = strtok_s(NULL, "\r\n", &Temp)) != NULL);

    return -1;
}