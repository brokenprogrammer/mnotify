typedef enum
{
    IMAP_IDENTIFIED_PROVIDER_GMAIL,
    IMAP_IDENTIFIED_PROVIDER_UNKNOWN,
} imap_identified_provider;

typedef struct
{
    int Error;
    int SequenceNumber;
    char *Subject;
    char *From;
    char *Date;
} imap_email_message;

typedef enum
{
    IMAP_RESPONSE_TYPE_FAILED,
    IMAP_RESPONSE_TYPE_PREAUTH,
    IMAP_RESPONSE_TYPE_LOGIN,
    IMAP_RESPONSE_TYPE_EXAMINE,
    IMAP_RESPONSE_TYPE_IDLE,
    IMAP_RESPONSE_TYPE_IDLE_LISTEN,
    IMAP_RESPONSE_TYPE_DONE,
    IMAP_RESPONSE_TYPE_SEARCH,
    IMAP_RESPONSE_TYPE_FETCH,
} imap_response_type;

typedef struct
{
    imap_response_type Type;
    int Success;
    
    union
    {
        struct
        {
            imap_identified_provider Provider;
        }; // Preauth response

        struct
        {
            int ParsedCapabilities;
            int HasIdle;
        }; // Login capability response

        struct
        {
            int Initialized;
            int ParsedTag;

            // TODO(Oskar): Perhaps more depending on how many unread emails.. No idea how many
            // numbers that IMAP can respond with.
            int SequenceNumbers[4096];
            int NumberOfNumbers;
        }; // Search response

        struct
        {
            int ActiveParse;
            imap_email_message *Emails;
            int TotalNumberOfEmails;
            int ParsedEmails;
        }; // Fetch response
    };
} imap_response;

typedef struct
{
    tls_socket Socket;

    int ParsedCapabilities;
    int HasIdle;
    int HasRecent;

    int CommandNumber;
} imap;