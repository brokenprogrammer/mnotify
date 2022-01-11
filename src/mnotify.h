typedef struct 
{
    char Host[256];
    char Account[256];
    char Password[256];
    char OpenSite[256];
    char Folder[256];
    int Port;
    int PollingTimeSeconds;
    int RetryTime;
} mnotify_config;


#define ArrayCount(Array) sizeof(Array) / sizeof(Array[0])