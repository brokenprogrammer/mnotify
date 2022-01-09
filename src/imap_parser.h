typedef struct
{
    BOOL HasError;
    char Error[1024];
    tokenizer Tokenizer;
} imap_parser;