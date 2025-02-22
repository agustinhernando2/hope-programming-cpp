
#include <msg_handler.h>

void send_alert_msqueue(mess_t* send_buffer)
{
    if (msg_id == -1 || msg_id == 0)
    {
        perror("msgget error");
        exit(EXIT_FAILURE);
    }
    if (msgsnd(msg_id, send_buffer, sizeof(send_buffer->message), 0) == -1)
    {
        perror("msgsnd");
        exit(EXIT_FAILURE);
    }
}

int create_message_queue()
{ // If file exists delete it
    if (access(K_MSG, F_OK) != -1)
    {
        if (remove(K_MSG) != 0)
        {
            error_handler("Error deleting key file", __FILE__, __LINE__);
            exit(EXIT_FAILURE);
        }
    }
    // Create file
    FILE* key_file = fopen(K_MSG, "w");
    if (key_file == NULL)
    {
        error_handler("Error creating key file", __FILE__, __LINE__);
        exit(EXIT_FAILURE);
    }
    fclose(key_file);

    // Create a unique key
    key_t key = ftok(K_MSG, 'm');
    if (key == -1)
    {
        error_handler("Error ftok", __FILE__, __LINE__);
        exit(EXIT_FAILURE);
    }
    msg_id = msgget(key, IPC_CREAT | 0666);
    if (msg_id == -1)
    {
        fprintf(stderr, "msg_id = %d\n", msg_id);
        error_handler(". error", __FILE__, __LINE__);
        exit(EXIT_FAILURE);
    }
    printf("Message queue created QID = %d, using key = %s\n", msg_id, K_MSG);

    return msg_id;
}
