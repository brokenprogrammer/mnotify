#define IMAP_DEFAULT_READ_BUFFER_SIZE 32768
#define IMAP_END_OF_MESSAGE "\r\n"

#define IMAP_COMMAND_LOGIN "LOGIN"
#define IMAP_COMMAND_EXAMINE "EXAMINE"
#define IMAP_COMMAND_IDLE "IDLE"
#define IMAP_COMMAND_DONE "DONE"
#define IMAP_COMMAND_SEARCH "SEARCH"

typedef enum
{
    IMAP_IDENTIFIED_PROVIDER_GMAIL,
    IMAP_IDENTIFIED_PROVIDER_YAHOO,
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
    IMAP_RESPONSE_TYPE_GREETING,
    IMAP_RESPONSE_TYPE_LOGIN,
    IMAP_RESPONSE_TYPE_EXAMINE,
    IMAP_RESPONSE_TYPE_IDLE,
    IMAP_RESPONSE_TYPE_IDLE_LISTEN,
    IMAP_RESPONSE_TYPE_DONE,
    IMAP_RESPONSE_TYPE_SEARCH,
    IMAP_RESPONSE_TYPE_FETCH,
} imap_response_type;

typedef enum
{
    IMAP_IDLE_MESSAGE_UNKNOWN,
    IMAP_IDLE_MESSAGE_EXISTS,
    IMAP_IDLE_MESSAGE_EXPUNGE,
} imap_idle_message;

typedef struct
{
    imap_response_type Type;
    BOOL Success;
    
    int ParsedCapabilities;
    int HasIdle;
    
    union
    {
        struct
        {
            imap_identified_provider Provider;
        }; // Greeting response

        // struct
        // {
        // }; // Login capability response

        struct
        {
            imap_idle_message IdleMessageType;
        }; // Idle message

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