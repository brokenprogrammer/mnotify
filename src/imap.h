typedef enum 
{
    IMAP_STATE_DISCONNECTED,
    IMAP_STATE_CONNECTED,
    
    IMAP_STATE_NOTAUTHENTICATED,
    IMAP_STATE_LOGIN,
    IMAP_STATE_AUTHENTICATED,
    
    IMAP_STATE_EXAMING,
    IMAP_STATE_SELECTED,

    IMAP_STATE_PREIDLE,
    IMAP_STATE_IDLE,
    IMAP_STATE_POSTIDLE,
    
    IMAP_STATE_PRESEARCHING,
    IMAP_STATE_SEARCHING,
    
    IMAP_STATE_PREFETCH,
    IMAP_STATE_FETCHING
} imap_state;

typedef struct
{
    tls_socket Socket;

    imap_state State;

    int ParsedCapabilities;
    int HasIdle;
    int HasRecent;
    int UpdateAlways;

    int OldMailCount;
    int FetchedRows;

    char Query[1024];
    char Subject[1024];
    char From[1024];
    char Date[1024];


    int CommandNumber;
} imap;