#include <stdio.h>
#include <string.h> //strlen
#include <stdlib.h> //strlen
#include <sys/socket.h>
#include <arpa/inet.h> //inet_addr
#include <unistd.h>    //write
#include <pthread.h>   //for threading , link with lpthread
#include <list>
#include <signal.h>
// #include <braft/node.h>

int src_port, dest_port;
char *src;
char *dest;

char **src_replace_strings;
char **dest_replace_strings;
int replace_pairs_count;
int delay_time;
bool running = true;

char *reorder_buffer[3];

//the thread function
void *connection_handler1(void *);

//the thread function
void *connection_handler2(void *);

int connect_to_server();

struct connection_pair
{
    int src_sock;
    int dest_sock;
    pthread_t thread1;
    pthread_t thread2;
};

std::list<struct connection_pair *> connection_pairs;

static void cleanup(int signo)
{
    running = false;
    perror("cleaning up...\n");
    for (struct connection_pair *cp : connection_pairs)
    {
        printf("src_sock %d, dest_sock %d\n", cp->src_sock, cp->dest_sock);
        close(cp->src_sock);
        close(cp->dest_sock);
        if (!pthread_tryjoin_np(cp->thread1, NULL))
        {
            pthread_join(cp->thread1, NULL);
        }
        if (!pthread_tryjoin_np(cp->thread2, NULL))
        {
            pthread_join(cp->thread2, NULL);
        }
        // pthread_join(cp->thread1, NULL);
        // pthread_join(cp->thread2, NULL);
    }
    exit(0);
}

int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        printf("usage: ./proxy_server src_port dest_port delay_time [src1 dest1 src2 dest2]\n");
        exit(0);
    }

    int socket_desc, client_sock, c;
    struct sockaddr_in server, client;
    signal(SIGINT, cleanup);
    signal(SIGTERM, cleanup);

    int arg_index = 1;

    src = argv[arg_index++];
    dest = argv[arg_index++];

    src_port = atoi(src);
    dest_port = atoi(dest);

    delay_time = atoi(argv[arg_index++]);

    replace_pairs_count = (argc - arg_index) / 2;
    printf("replace pairs count is %d\n", replace_pairs_count);

    src_replace_strings = (char **)malloc(sizeof(char **) * replace_pairs_count);
    dest_replace_strings = (char **)malloc(sizeof(char **) * replace_pairs_count);

    for (int i = arg_index, k = 0; i < argc - 1; i += 2, k++)
    {
        src_replace_strings[k] = argv[i];
        dest_replace_strings[k] = argv[i + 1];
    }

    printf("src_port = %d, dest_port = %d\n", src_port, dest_port);

    //Create socket
    socket_desc = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_desc == -1)
    {
        printf("Could not create socket\n");
        exit(-1);
    }
    puts("Socket created");

    int enable = 0;
    if (setsockopt(socket_desc, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0)
    {
        printf("setsockopt(SO_REUSEADDR) failed\n");
        exit(-1);
    }
    // if (setsockopt(socket_desc, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(int)) < 0)
    // {
    //     printf("setsockopt(SO_REUSEPORT) failed\n");
    //     exit(-1);
    // }

    //Prepare the sockaddr_in structure
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = inet_addr("127.0.1.1");
    server.sin_port = htons(src_port);

    //Bind
    if (bind(socket_desc, (struct sockaddr *)&server, sizeof(server)) < 0)
    {
        //print the error message
        perror("bind failed. Error");
        return 1;
    }
    puts("bind done");

    //Listen
    listen(socket_desc, 3);

    //Accept and incoming connection
    puts("Waiting for incoming connections...");
    c = sizeof(struct sockaddr_in);
    while (running && (client_sock = accept(socket_desc, (struct sockaddr *)&client, (socklen_t *)&c)))
    {
        printf("Connection accepted, client port is %d\n", client.sin_port);

        pthread_t sniffer_thread1;
        pthread_t sniffer_thread2;
        struct connection_pair *current_connection = (struct connection_pair *)malloc(1 * sizeof(struct connection_pair));
        current_connection->src_sock = client_sock;
        current_connection->dest_sock = connect_to_server();
        if (current_connection->dest_sock < 0)
        {
            close(current_connection->src_sock);
            continue;
        }

        if (pthread_create(&sniffer_thread1, NULL, connection_handler1, (void *)current_connection) < 0)
        {
            perror("could not create thread");
            return 1;
        }

        if (pthread_create(&sniffer_thread2, NULL, connection_handler2, (void *)current_connection) < 0)
        {
            perror("could not create thread");
            return 1;
        }

        current_connection->thread1 = sniffer_thread1;
        current_connection->thread2 = sniffer_thread2;

        //Now join the thread , so that we dont terminate before the thread
        //pthread_join( sniffer_thread , NULL);
        puts("Handler assigned");
        connection_pairs.push_back(current_connection);
    }

    if (client_sock < 0)
    {
        perror("accept failed");
        return 1;
    }

    close(socket_desc);
    cleanup(0);

    return 0;
}

int connect_to_server()
{
    int sock;
    struct sockaddr_in server;

    //Create socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1)
    {
        printf("Could not create socket");
    }
    puts("Socket created");

    int enable;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0)
    {
        printf("setsockopt(SO_REUSEADDR) failed\n");
        exit(-1);
    }

    server.sin_addr.s_addr = inet_addr("127.0.1.1");
    server.sin_family = AF_INET;
    server.sin_port = htons(dest_port);

    //Connect to remote server
    if (connect(sock, (struct sockaddr *)&server, sizeof(server)) < 0)
    {
        perror("connect failed. Error");
        return -1;
    }

    puts("Connected\n");
    return sock;
}

bool is_replace_str(char *target, char *replace_str)
{
    for (int i = 0; i < strlen(replace_str); i++)
    {
        if (target[i] != replace_str[i])
        {
            return false;
        }
    }
    return true;
}

/*
 * receive from src and send to dest
 * */
void *connection_handler1(void *cp)
{
    struct connection_pair *current_pair = (struct connection_pair *)cp;
    int read_size;
    char *message, client_message[2000];

    //Receive a message from client
    while (running && (read_size = recv(current_pair->src_sock, client_message, 2000, 0)) > 0)
    {
        //Send the message back to client
        printf("message from %d : size = %d\n", current_pair->src_sock, read_size);
        for (int i = 0; i < read_size; i++)
        {
            printf("%c", client_message[i]);
        }
        printf("\n");
        for (int i = 0; i < read_size; i++)
        {
            for (int j = 0; j < replace_pairs_count; j++)
            {
                if (is_replace_str((char *)client_message + i, src_replace_strings[j]))
                {
                    for (int k = 0; k < strlen(src_replace_strings[j]); k++)
                    {
                        client_message[i] = dest_replace_strings[j][k];
                        i++;
                    }
                }
            }
        }
        printf("\n");
        printf("Message replaced!\n");
        for (int i = 0; i < read_size; i++)
        {
            printf("%c", client_message[i]);
        }
        printf("\n");

        usleep(delay_time);
        write(current_pair->dest_sock, client_message, read_size);
    }

    close(current_pair->src_sock);
    close(current_pair->dest_sock);

    if (read_size == 0)
    {
        puts("Client disconnected");
        fflush(stdout);
    }
    else if (read_size == -1)
    {
        perror("recv failed");
    }

    //Free the socket pointer
    free(current_pair);
    connection_pairs.remove(current_pair);
}

/*
 * receive from dest and send back to src
 * */
void *connection_handler2(void *cp)
{
    struct connection_pair *current_pair = (struct connection_pair *)cp;
    int read_size;
    char *message, client_message[2000];

    usleep(10000);
    while (running && (read_size = recv(current_pair->dest_sock, client_message, 2000, 0)) > 0)
    {
        //Send the message back to client
        printf("message from dest_sock: size = %d\n", read_size);
        for (int i = 0; i < read_size; i++)
        {
            printf("%c", client_message[i]);
        }
        printf("\n");
        for (int i = 0; i < read_size; i++)
        {
            for (int j = 0; j < replace_pairs_count; j++)
            {
                if (is_replace_str((char *)client_message + i, dest_replace_strings[j]))
                {
                    for (int k = 0; k < strlen(dest_replace_strings[j]) && client_message[i] == dest_replace_strings[j][k]; k++)
                    {
                        client_message[i] = src_replace_strings[j][k];
                        i++;
                    }
                }
            }
        }
        printf("\n");
        printf("Message replaced!\n");
        for (int i = 0; i < read_size; i++)
        {
            printf("%c", client_message[i]);
        }
        printf("\n");

        usleep(delay_time);
        write(current_pair->src_sock, client_message, read_size);

        usleep(1);
    }
}