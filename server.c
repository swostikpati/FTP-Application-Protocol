// Importing dependencies
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdlib.h>
#include <dirent.h>

// Defining symbolic constansts
// Some ports like port 21 are priviledged - so we decided on using other ones
#define CONTROL_CHANNEL 6000
#define DATA_CHANNEL 6001
#define SIZE 100
#define MAX_CLIENT_SIZE 7

// struct to hold client data
struct ClientInfo
{
    char username[SIZE];
    char password[SIZE];
    int usernameOkay;
    int loggedIn;
    char ip[SIZE];
    int port;
    char PWD[SIZE];

    int fd;
    char msg[SIZE];
};

// struct to hold server data
struct ServerInfo
{
    char PWD[SIZE];
};

// function prototypes
int manageClient(int client_sd, struct ClientInfo *clientInfo, struct ServerInfo *);

int serverCommand(char **userInput, int userInputLen, struct ClientInfo *, struct ServerInfo *);

char **split_string(char *str, int *len);

int check_credentials_username(struct ClientInfo *clientInfo);

int check_credentials_password(struct ClientInfo *clientInfo);

void getWD(struct ClientInfo *);

int main()
{
    // socket
    int server_sd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sd < 0)
    {
        perror("socket:");
        exit(-1);
    }
    // setsock
    int value = 1;

    struct timeval tv;
    tv.tv_sec = 2;
    tv.tv_usec = 0;

    // to make addresses reusable
    setsockopt(server_sd, SOL_SOCKET, SO_REUSEADDR, &value, sizeof(value));

    // ref: https://stackoverflow.com/questions/393276/socket-with-recv-timeout-what-is-wrong-with-this-code
    setsockopt(server_sd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);

    // basic setup
    struct sockaddr_in server_addr;
    bzero(&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(CONTROL_CHANNEL);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    // bind
    if (bind(server_sd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("bind failed");
        exit(-1);
    }

    // listen
    if (listen(server_sd, 5) < 0)
    {
        perror("listen failed");
        close(server_sd);
        exit(-1);
    }

    printf("FTP Server is listening...\n");

    int client_sd;
    struct sockaddr_in client_addr;
    int addr_len = sizeof(client_addr);
    struct ServerInfo serverInfo;

    // creating an array of ClientInfo structs to keep track of multiple clients connecting to the server
    struct ClientInfo clientInfo[MAX_CLIENT_SIZE];

    // Initializing fd_sets to be used in the select() function
    fd_set full_fdset;
    fd_set read_fdset;
    FD_ZERO(&full_fdset);

    int max_fd = server_sd;
    int client_zero = max_fd + 1;

    FD_SET(server_sd, &full_fdset);

    while (1)
    {
        // Implementing the control channel over select to handle light requests and responses between the server and multiple simultaneous clients
        printf("CURRENT CLIENT INDEX = %d \n", max_fd);
        read_fdset = full_fdset;

        if (select(max_fd + 1, &read_fdset, NULL, NULL, NULL) < 0)
        {
            // if select fails
            perror("select");
            exit(-1);
        }
        for (int fd = 3; fd <= max_fd; fd++)
        {
            if (FD_ISSET(fd, &read_fdset))
            {
                if (fd == server_sd)
                {
                    // accepting the socket connection from the client side
                    client_sd = accept(server_sd, (struct sockaddr *)&client_addr, (socklen_t *)&addr_len);
                    printf("Client Connected fd = %d \n", client_sd);
                    FD_SET(client_sd, &full_fdset);

                    // initializing clientInfo variables
                    clientInfo[client_sd - client_zero].usernameOkay = 0;
                    clientInfo[client_sd - client_zero].loggedIn = 0;
                    strncpy(clientInfo[client_sd - client_zero].ip, inet_ntoa(client_addr.sin_addr), 100);
                    clientInfo[client_sd - client_zero].port = ntohs(client_addr.sin_port);
                    getWD(&clientInfo[client_sd - client_zero]);
                    clientInfo[client_sd - client_zero].fd = client_sd;

                    if (client_sd > max_fd)
                        max_fd = client_sd;
                }
                else
                {
                    // print connection acknowledgement
                    printf("[%s:%d] Connected \n", clientInfo[fd - client_zero].ip, clientInfo[fd - client_zero].port);

                    // calling the manageClient function with the client socket and the client address information
                    if (manageClient(fd, &clientInfo[fd - client_zero], &serverInfo) == 0)
                    {
                        // handling client disconnection
                        printf("[%s:%d] Client disconnected\n", clientInfo[fd - client_zero].ip, clientInfo[fd - client_zero].port);
                        close(fd);
                        FD_CLR(fd, &full_fdset);
                        if (fd == max_fd)
                        {
                            for (int i = max_fd; i >= 3; i--)
                            {
                                if (FD_ISSET(i, &full_fdset))
                                {
                                    max_fd = i;
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    // closing the socket connection
    close(server_sd);
    return 0;
}

// function to manage client requests
// ref: assignment 3
int manageClient(int client_sd, struct ClientInfo *clientInfo, struct ServerInfo *serverInfo)
{
    // To store the client message
    char buffer[100];
    bzero(buffer, sizeof(buffer));
    int total_bytes;

    // Receiving the message sent by the client
    if ((total_bytes = recv(client_sd, buffer, sizeof(buffer), 0)) > 0)
    {
        printf("[%s:%d] Received Message: %s\n", clientInfo->ip, clientInfo->port, buffer);

        // splitting the client request into an array of strings(character arrays)
        int userInputLen;
        char **userInput = split_string(buffer, &userInputLen);

        // calling the serverCommand function to handle commands in the server side
        int msgStatus = serverCommand(userInput, userInputLen, clientInfo, serverInfo);

        // the msgStatus is received as 1 whenever there is a message to be relayed to the client
        if (msgStatus == 1)
        {
            // relaying the message back to the client
            if (send(client_sd, clientInfo->msg, sizeof(clientInfo->msg), 0) < 0)
            {
                perror("Send error");
                return 0;
            }
        }
        return 1;
    }
    else
    {
        perror("recv error");
        return 0;
    }
}

// the serverCommand to handle requests from client on the server side
int serverCommand(char **userInput, int userInputLen, struct ClientInfo *clientInfo, struct ServerInfo *serverInfo)
{
    int flag = 0; // decides error response later

    // retrieving the command received
    char *command = userInput[0];

    // user authentication
    // unless the user is logged in, any request other than USER or PASS will result in "530 not logged in";
    if (strcmp(command, "USER") != 0 && strcmp(command, "PASS") != 0 && clientInfo->loggedIn == 0)
    {

        bzero(clientInfo->msg, sizeof(clientInfo->msg));
        strcpy(clientInfo->msg, "530 Not logged in");
        return 1;
    }
    if (strcmp(command, "USER") == 0) // handling USER command
    {
        if (userInputLen != 2)
        {
            flag = 1;
            printf("Invalid command\n");
        }
        else
        {
            flag = 0;

            // retrieving in the username the client sent
            bzero(clientInfo->username, sizeof(clientInfo->username));
            strcpy(clientInfo->username, userInput[1]);

            // calling the check_credentials username function to verify username
            clientInfo->usernameOkay = check_credentials_username(clientInfo);

            // saving appropriate message to be relayed later to the client
            if (clientInfo->usernameOkay)
            {
                bzero(clientInfo->msg, sizeof(clientInfo->msg));
                strcpy(clientInfo->msg, "331 Username OK, need password.");
            }
            else
            {
                bzero(clientInfo->msg, sizeof(clientInfo->msg));
                strcpy(clientInfo->msg, "530 Not logged in.");
            }
            return 1;
        }
    }
    else if (strcmp(command, "PASS") == 0) // handling PASS command
    {
        if (userInputLen != 2)
        {
            flag = 1;
            printf("Invalid command\n");
        }
        else
        {
            flag = 0;
            // first verifying whether username is verified or not
            if (clientInfo->usernameOkay)
            {
                // retriving the password sent by the client
                bzero(clientInfo->password, sizeof(clientInfo->password));
                strcpy(clientInfo->password, userInput[1]);

                // calling the check_credentials_password to verify the password
                clientInfo->loggedIn = check_credentials_password(clientInfo);

                // saving server response based on the return value
                if (clientInfo->loggedIn == 1)
                {
                    bzero(clientInfo->msg, sizeof(clientInfo->msg));
                    strcpy(clientInfo->msg, "230 User logged in, proceed");
                }
                else
                {
                    bzero(clientInfo->msg, sizeof(clientInfo->msg));
                    strcpy(clientInfo->msg, "530 Not logged in");
                }
            }
            else
            {
                // if PASS is called before username is authenticated leads to bad sequence response
                bzero(clientInfo->msg, sizeof(clientInfo->msg));
                strcpy(clientInfo->msg, "503 Bad sequence of commands.");
            }
            return 1;
        }
    }
    else if (strcmp(command, "PWD") == 0) // prints working directory of the server
    {
        if (userInputLen != 1)
        {
            flag = 1;
            printf("Invalid command\n");
        }
        else
        {
            // storing the server response which gives back the current working directory
            char temp[SIZE];
            bzero(temp, sizeof(temp));
            bzero(clientInfo->msg, sizeof(clientInfo->msg));
            strcpy(temp, "257 ");
            strcat(temp, clientInfo->PWD);
            strcpy(clientInfo->msg, temp);
            return 1;
        }
    }
    else if (strcmp(command, "CWD") == 0) // changes the working directory of the server
    {
        if (userInputLen != 2)
        {
            flag = 1;
            printf("Invalid command\n");
        }
        else
        {
            // retrieving the directory name requested by the user
            char *dirName = userInput[1];

            // the use of CWD in the variables below refers to the full form "Current Working Directory" rather than "Change Working Directory"
            // initializing two variables to store the new path
            char newCWD[SIZE];
            char newCWD2[SIZE];
            bzero(newCWD, SIZE * sizeof(char));
            bzero(newCWD2, SIZE * sizeof(char));

            // concatanating the directory to the old path after adding a /
            char currCWD[SIZE];
            strcpy(newCWD, clientInfo->PWD);
            strcat(newCWD, "/");
            strcat(newCWD, dirName);

            // used in case of the "." and ".." directories
            // ref: https://pubs.opengroup.org/onlinepubs/009696799/functions/realpath.html
            realpath(newCWD, newCWD2);

            // opening the new directory to check for its existence
            DIR *dir;
            dir = opendir(newCWD2);

            if (!dir)
            {
                // if no such directory exists
                bzero(clientInfo->msg, sizeof(clientInfo->msg));
                strcpy(clientInfo->msg, "550 No such file or directory.");
                printf("Invalid Directory\n");
                return 1;
            }

            // setting the current working directory to the new directory
            bzero(clientInfo->PWD, sizeof(clientInfo->PWD));
            strcpy(clientInfo->PWD, newCWD2);

            // creating the server response message
            bzero(clientInfo->msg, sizeof(clientInfo->msg));
            char temp[SIZE];
            bzero(temp, sizeof(temp));
            strcpy(temp, "200 directory changed to ");
            strcat(temp, clientInfo->PWD);
            strcpy(clientInfo->msg, temp);
            return 1;
        }
    }
    else if (strcmp(command, "PORT") == 0) // handles the port command
    {

        // setting up socket for starting data connection
        int ftpclient_sd = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in addr;
        bzero(&addr, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(DATA_CHANNEL);
        addr.sin_addr.s_addr = INADDR_ANY;

        int value = 1;
        setsockopt(ftpclient_sd, SOL_SOCKET, SO_REUSEADDR, &value, sizeof(int));

        // binding
        if (bind(ftpclient_sd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
        {
            // Sending PORT command failure message
            char server_response[SIZE];
            bzero(server_response, sizeof(server_response));
            strcpy(server_response, "503 Bad sequence of commands.");
            send(clientInfo->fd, server_response, sizeof(server_response), 0);
            perror("Bind error");
            return -1;
        }

        // extracting the IP and port number
        int h1, h2, h3, h4, p1, p2;

        char ftpip[SIZE];
        sscanf(userInput[1], "%d,%d,%d,%d,%d,%d", &h1, &h2, &h3, &h4, &p1, &p2);

        int port = p1 * 256 + p2;

        sprintf(ftpip, "%d.%d.%d.%d", h1, h2, h3, h4);

        // Sending PORT command success message
        char server_response[SIZE];
        bzero(server_response, sizeof(server_response));
        strcpy(server_response, "200 PORT command successful");
        send(clientInfo->fd, server_response, sizeof(server_response), 0);

        // Receiving STOR, RETR, or LIST command
        char commandStr[SIZE];
        bzero(commandStr, sizeof(commandStr));
        if ((recv(clientInfo->fd, commandStr, sizeof(commandStr), 0)) < 0)
        {
            return -1;
        }
        printf("Received: %s\n", commandStr);

        // Using fork to create parent and child process
        int pid = fork();

        if (pid == 0)
        {
            // Child Process
            int userInputLen2;
            char **userInput2 = split_string(commandStr, &userInputLen2);
            struct sockaddr_in ftp_other_addr;
            bzero(&ftp_other_addr, sizeof(ftp_other_addr));
            ftp_other_addr.sin_family = AF_INET;
            ftp_other_addr.sin_port = htons(port);
            ftp_other_addr.sin_addr.s_addr = inet_addr(ftpip);

            if (strcmp(userInput2[0], "STOR") == 0) // handling STOR process
            {
                if (userInputLen2 != 2)
                {
                    flag = 1;
                    exit(0);
                }

                // response sent to client confirming data connection can be opened now
                char servRes2[SIZE];
                bzero(servRes2, sizeof(servRes2));
                strcpy(servRes2, "150 File status okay; about to open data connection.");
                send(clientInfo->fd, servRes2, sizeof(servRes2), 0); // TODO

                // connecting to the data transfer socket
                printf("[%s:%d] Connected \n", inet_ntoa(ftp_other_addr.sin_addr), ntohs(ftp_other_addr.sin_port));
                if (connect(ftpclient_sd, (struct sockaddr *)&ftp_other_addr, sizeof(ftp_other_addr)) < 0)
                {
                    bzero(clientInfo->msg, sizeof(clientInfo->msg));
                    strcpy(clientInfo->msg, "503 Bad sequence of commands.");
                    send(clientInfo->fd, clientInfo->msg, strlen(clientInfo->msg), 0);
                    perror("connect");
                    close(ftpclient_sd);
                    exit(0);
                }
                char path[SIZE];

                // Navigating to the correct path
                strcpy(path, clientInfo->PWD);
                strcat(path, "/");
                strcat(path, userInput2[1]);

                // Opening file in write mode - creates file if not exists
                FILE *f;
                f = fopen(path, "wb");

                if (f == NULL)
                {
                    // When file fails to open
                    bzero(clientInfo->msg, sizeof(clientInfo->msg));
                    strcpy(clientInfo->msg, "550 No such file or directory");
                    send(clientInfo->fd, clientInfo->msg, strlen(clientInfo->msg), 0);
                    close(ftpclient_sd);
                    exit(0);
                }

                // writing into the file
                char buffer[SIZE];
                bzero(buffer, sizeof(buffer));
                int bytes = 0;
                while ((bytes = recv(ftpclient_sd, buffer, sizeof(buffer), 0)) > 0)
                {
                    fwrite(buffer, 1, bytes, f);
                    bzero(buffer, sizeof(buffer));
                }

                // sending the transfer completed response
                bzero(clientInfo->msg, sizeof(clientInfo->msg));
                strcpy(clientInfo->msg, "226 Transfer completed.");
                send(clientInfo->fd, clientInfo->msg, strlen(clientInfo->msg), 0);

                close(ftpclient_sd);

                fclose(f);
                exit(0);
            }
            else if (strcmp(userInput2[0], "RETR") == 0)
            {
                char servRes2[SIZE];
                bzero(servRes2, sizeof(servRes2));
                // similar implementation to STOR but the file here is opened in READ mode
                if (userInputLen2 != 2)
                {
                    strcpy(servRes2, "501 Syntax error in parameters or argument");
                    send(clientInfo->fd, servRes2, sizeof(servRes2), 0);
                    flag = 1;
                    close(ftpclient_sd);
                    exit(0);
                }

                strcpy(servRes2, "150 File status okay; about to open data connection.");
                send(clientInfo->fd, servRes2, sizeof(servRes2), 0);

                printf("[%s:%d] Connected \n", inet_ntoa(ftp_other_addr.sin_addr), ntohs(ftp_other_addr.sin_port));
                if (connect(ftpclient_sd, (struct sockaddr *)&ftp_other_addr, sizeof(ftp_other_addr)) < 0)
                {
                    bzero(clientInfo->msg, sizeof(clientInfo->msg));
                    strcpy(clientInfo->msg, "503 Bad sequence of commands.");
                    send(clientInfo->fd, clientInfo->msg, strlen(clientInfo->msg), 0);
                    perror("connect");
                    close(ftpclient_sd);
                    exit(0);
                }

                FILE *f;

                char path[SIZE];

                strcpy(path, clientInfo->PWD);
                strcat(path, "/");
                strcat(path, userInput2[1]);

                f = fopen(path, "rb");

                if (f == NULL)
                {
                    // When file fails to open
                    bzero(clientInfo->msg, sizeof(clientInfo->msg));
                    strcpy(clientInfo->msg, "550 No such file or directory");
                    send(clientInfo->fd, clientInfo->msg, strlen(clientInfo->msg), 0);
                    close(ftpclient_sd);
                    exit(0);
                }

                char data[SIZE];

                // Reading form the file and sending
                while (fgets(data, SIZE - 1, f) != NULL)
                {
                    if (send(ftpclient_sd, data, strlen(data), 0) < 0)
                    {
                        bzero(clientInfo->msg, sizeof(clientInfo->msg));
                        strcpy(clientInfo->msg, "503 Bad sequence of commands.");
                        send(clientInfo->fd, clientInfo->msg, strlen(clientInfo->msg), 0);
                        perror("send file error");
                        close(ftpclient_sd);
                        fclose(f);
                        exit(0);
                    }

                    bzero(data, SIZE);
                }

                // sending the transfer completed response
                bzero(clientInfo->msg, sizeof(clientInfo->msg));
                strcpy(clientInfo->msg, "226 Transfer completed.");
                send(clientInfo->fd, clientInfo->msg, strlen(clientInfo->msg), 0);
                close(ftpclient_sd);
                fclose(f);

                exit(0);
            }
            else if (strcmp(userInput2[0], "LIST") == 0)
            {
                char servRes2[SIZE];
                // very similar implementation to STOR and RETR
                if (userInputLen2 != 1)
                {
                    strcpy(servRes2, "501 Syntax error in parameters or argument");
                    send(clientInfo->fd, servRes2, sizeof(servRes2), 0);
                    flag = 1;
                    close(ftpclient_sd);
                    exit(0);
                }

                bzero(servRes2, sizeof(servRes2));
                strcpy(servRes2, "150 File status okay; about to open data connection.");
                send(clientInfo->fd, servRes2, sizeof(servRes2), 0); // TODO

                printf("[%s:%d] Connected \n", inet_ntoa(ftp_other_addr.sin_addr), ntohs(ftp_other_addr.sin_port));
                if (connect(ftpclient_sd, (struct sockaddr *)&ftp_other_addr, sizeof(ftp_other_addr)) < 0)
                {
                    bzero(clientInfo->msg, sizeof(clientInfo->msg));
                    strcpy(clientInfo->msg, "503 Bad sequence of commands.");
                    send(clientInfo->fd, clientInfo->msg, strlen(clientInfo->msg), 0);
                    perror("connect");
                    close(ftpclient_sd);
                    exit(0);
                }

                // ref1: https://pubs.opengroup.org/onlinepubs/7908799/xsh/dirent.h.html
                // ref2: https://www.youtube.com/watch?v=j9yL30R6npk

                DIR *dir;
                struct dirent *ent; // to hold directory entries

                // Open the current working directory
                if ((dir = opendir(clientInfo->PWD)) != NULL)
                {
                    char listString[SIZE] = "";
                    // reading through each entry in the directory and displaying them
                    while ((ent = readdir(dir)) != NULL)
                    {
                        strcat(listString, strcat(ent->d_name, "\n"));
                    }
                    if (send(ftpclient_sd, listString, sizeof(listString), 0) < 0)
                    {
                        // if send fails
                        bzero(clientInfo->msg, sizeof(clientInfo->msg));
                        strcpy(clientInfo->msg, "503 Bad sequence of commands.");
                        send(clientInfo->fd, clientInfo->msg, strlen(clientInfo->msg), 0);
                        perror("send error");
                        exit(0);
                    }
                    // closing the directory
                    closedir(dir);
                }
                else
                {
                    // if the directory failed to open
                    bzero(clientInfo->msg, sizeof(clientInfo->msg));
                    strcpy(clientInfo->msg, "550 No such file or directory");
                    send(clientInfo->fd, clientInfo->msg, strlen(clientInfo->msg), 0);
                    // Print an error message if the directory cannot be opened
                    perror("Unable to open directory1.");
                    exit(0);
                }

                close(ftpclient_sd);
                exit(0);
            }
        }
        else
        {
            // parent process
            close(ftpclient_sd);
        }

        return 0;
    }
    else if (strcmp(command, "QUIT") == 0) // handles quit command
    {
        if (userInputLen != 1)
        {
            flag = 1;
            printf("Invalid command\n");
        }
        else
        {

            // relays message to the client
            bzero(clientInfo->msg, sizeof(clientInfo->msg));
            strcpy(clientInfo->msg, "221 Service closing control connection.");

            return 1;
        }
    }
    else
    {
        // invalid command
        flag = 2;
    }
    // handles the scenario when command is correct but parameters are wrong
    if (flag == 1)
    {
        bzero(clientInfo->msg, sizeof(clientInfo->msg));
        strcpy(clientInfo->msg, "501 Syntax error in parameters or argument");
        return 1;
    }
    // handles the scenario when even the command is incorrect
    else if (flag == 2)
    {
        bzero(clientInfo->msg, sizeof(clientInfo->msg));
        strcpy(clientInfo->msg, "202 Command not implemented");
        return 1;
    }
}

// splits the user input - using strtok
// ref: https://www.educative.io/answers/splitting-a-string-using-strtok-in-c
char **split_string(char *str, int *len)
{
    char delim = ' ';
    // printf("String, %s\n", str);
    // Allocate memory for a pointer to an array of strings
    char **words = malloc(sizeof(char *));

    // Initialize a word counter
    int word_count = 0;

    // Use strtok() function to split the string into tokens based on the delimiter character
    char *token = strtok(str, &delim);
    // printf("Token initial: %s\nCommand initial: %s\n", token, str);
    while (token != NULL)
    {
        // Reallocate memory for the array of strings to include the new token
        words = realloc(words, (word_count + 1) * sizeof(char *));

        // Allocate memory for the new string and copy the token into it
        words[word_count] = malloc(strlen(token) + 1);
        strcpy(words[word_count], token);
        // printf("Token x: %s and %s\n", token, words[word_count]);

        // Increment the word counter and get the next token
        word_count++;
        token = strtok(NULL, &delim);
    }

    // Add a NULL pointer to the end of the array to indicate the end of the strings
    words = realloc(words, (word_count + 1) * sizeof(char *));
    words[word_count] = NULL;

    // Set the length of the array to the number of words
    *len = word_count;

    // printf("First: %s\n", words[0]);

    // Return a pointer to the array of strings
    return words;
}

// function to handle password authentication
int check_credentials_password(struct ClientInfo *clientInfo)
{
    char username_data[SIZE];
    char password_data[SIZE];

    FILE *file = fopen("user.txt", "r");
    if (!file)
    {
        printf("File not loaded!\n");
        return 0;
    }

    char line[256];
    while (fgets(line, sizeof(line), file))
    {
        // tokenize line into username and password fields
        char *field = strtok(line, ",");
        strcpy(username_data, field);
        field = strtok(NULL, "\n");
        strcpy(password_data, field);

        if (strcmp(clientInfo->username, username_data) == 0)
        {

            // compare password field to user input
            if (strcmp(clientInfo->password, password_data) == 0)
            {
                printf("User authenticated successfully!\n");
                fclose(file);
                return 1;
            }
            else
            {
                printf("Password verification failed!\n");
                fclose(file);
                return 0;
            }
        }
    }
}

int check_credentials_username(struct ClientInfo *clientInfo)
{
    char username_data[SIZE];

    FILE *file = fopen("user.txt", "r");
    if (!file)
    {
        printf("File not loaded!\n");
        return -1;
    }

    char line[256];
    while (fgets(line, sizeof(line), file))
    {
        // tokenize line into username and password fields
        char *field = strtok(line, ",");
        strcpy(username_data, field);
        if (strcmp(clientInfo->username, username_data) == 0)
        {
            printf("Username verification successful!\n");
            fclose(file);
            return 1;
        }
    }

    printf("Username verification failed!\n");
    fclose(file);
    return 0;
}

// function to get the current working directory and store it in the clientInfo struct
void getWD(struct ClientInfo *clientInfo)
{
    // Call getcwd to get the current working directory
    // ref: https://pubs.opengroup.org/onlinepubs/007904975/functions/getcwd.html
    if (getcwd(clientInfo->PWD, sizeof(clientInfo->PWD)) != NULL)
    {
        return;
    }
    else
    {
        // getcwd returns null pointer otherwise
        perror("getcwd() error");
        return;
    }
}
