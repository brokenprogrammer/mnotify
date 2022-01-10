#define IMAP_LOCAL_READ_BUFFER_SIZE 65536
#define IMAP_DEFAULT_ERROR_BUFFER_SIZE 65536
#define IMAP_DEFAULT_READ_BUFFER_SIZE 32768
#define IMAP_END_OF_MESSAGE "\r\n"

#define IMAP_COMMAND_LOGIN "LOGIN"
#define IMAP_COMMAND_EXAMINE "EXAMINE"
#define IMAP_COMMAND_IDLE "IDLE"
#define IMAP_COMMAND_DONE "DONE"
#define IMAP_COMMAND_SEARCH "SEARCH"

typedef enum
{
    IMAP_CLIENT_ERROR_SUCCESS, // Ok Response
    IMAP_CLIENT_ERROR_SOCKET_CONNECTION,
    IMAP_CLIENT_ERROR_SOCKET_RECIEVE,
    IMAP_CLIENT_ERROR_SOCKET_WRITE,
    IMAP_CLIENT_ERROR_SOCKET_DISCONNECTED,
    IMAP_CLIENT_ERROR_PARSE,
    IMAP_CLIENT_ERROR_BAD_RESPONSE,

    IMAP_CLIENT_ERROR_COUNT,
} imap_client_error;

static char *ImapClientErrorStrings[IMAP_CLIENT_ERROR_COUNT] = 
{
    "",
    "Error: Failed to establish TLS connection with server.",
    "Error: Failed to recieve data from server.",
    "Error: Failed to write data to server.",
    "Error: Connection lost",
    "Error: Bad input when parsing response.",
    "Error: Client recieved bad response.",
};

typedef enum
{
    IMAP_RESPONSE_TYPE_UNKNOWN,
    IMAP_RESPONSE_TYPE_STATUS,
    IMAP_RESPONSE_TYPE_DATA,
    IMAP_RESPONSE_TYPE_CONTINUATION,
} imap_response_type;

typedef enum
{
    IMAP_RESPONSE_STATUS_INVALID,
    IMAP_RESPONSE_STATUS_OK,
    IMAP_RESPONSE_STATUS_NO,
    IMAP_RESPONSE_STATUS_BAD,
    IMAP_RESPONSE_STATUS_PREAUTH,
    IMAP_RESPONSE_STATUS_BYE,
} imap_response_status;

typedef enum
{
    IMAP_RESPONSE_CODE_INVALID,
    IMAP_RESPONSE_CODE_ALERT,
    IMAP_RESPONSE_CODE_BADCHARSET,
    IMAP_RESPONSE_CODE_CAPABILITY,
    IMAP_RESPONSE_CODE_PARSE,
    IMAP_RESPONSE_CODE_PERMANENTFLAGS,
    IMAP_RESPONSE_CODE_READ_ONLY,
    IMAP_RESPONSE_CODE_READ_WRITE,
    IMAP_RESPONSE_CODE_TRYCREATE,
    IMAP_RESPONSE_CODE_UIDNEXT,
    IMAP_RESPONSE_CODE_UIDVALIDITY,
    IMAP_RESPONSE_CODE_UNSEEN,
} imap_response_code;

typedef enum
{
    IMAP_IDLE_MESSAGE_UNKNOWN,
    IMAP_IDLE_MESSAGE_EXISTS,
    IMAP_IDLE_MESSAGE_EXPUNGE,
} imap_idle_response_type;

typedef struct
{
    imap_idle_response_type Type;
    imap_client_error Error;
} imap_idle_response;

typedef struct
{
    int NumberOfMails;
    imap_client_error Error;
} imap_search_response;

typedef struct
{
    char Value[256];
} imap_response_data;

typedef struct
{
    imap_response_type Type;

    char Tag[128];

    imap_response_data Data[128];
    unsigned int DataCount;

    imap_response_status Status;
    char *StatusCode;

    imap_response_code Code;

    char Info[256];

} imap_response;


typedef struct
{
    tls_socket Socket;

    BOOL ParsedCapabilities;
    BOOL HasIdle;
    BOOL HasRecent;

    int CommandNumber;

    int ErrorLength;
    char Error[IMAP_DEFAULT_ERROR_BUFFER_SIZE];
} imap;