// Importing dependencies
#include <dirent.h>
#include <stdio.h>      // header for input and output from console : printf, perror
#include <string.h>     // strcmp
#include <sys/socket.h> // for socket related functions
#include <arpa/inet.h>  // htons
#include <netinet/in.h> // structures for addresses
#include <unistd.h>     // contains fork() and unix standard functions
#include <stdlib.h>     // header for general fcuntions declarations: exit()

// Defining symbolic constansts
// Some ports like port 21 are priviledged - so we decided on using other ones
#define CONTROL_CHANNEL 6000
#define SIZE 100
#define CLIENT_IP "127.0.0.1"

// Defining global variables
int base_port = 1024;

// Creating client struct to store client info
struct ClientInfo
{
    char PWD[SIZE];
    int port;
    int server_sd;
    char server_response[SIZE];
};

// Defining function prototypes
int clientCommand(char **, int, struct ClientInfo *);

char **split_string(char *str, char delim, int *len);

void getWD(struct ClientInfo *clientInfo);

int main()
{
    // Intializing the clientInfo struct
    struct ClientInfo clientInfo;
    clientInfo.server_sd = socket(AF_INET, SOCK_STREAM, 0); // Initializing the socket
    if (clientInfo.server_sd < 0)
    {
        perror("socket:");
        exit(-1);
    }

    // setsock - ref: starter code and previous assignments
    int value = 1;
    setsockopt(clientInfo.server_sd, SOL_SOCKET, SO_REUSEADDR, &value, sizeof(value));
    struct sockaddr_in server_addr;
    bzero(&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(CONTROL_CHANNEL);
    server_addr.sin_addr.s_addr = inet_addr(CLIENT_IP);

    // connect
    if (connect(clientInfo.server_sd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("connect");
        exit(-1);
    }

    char buffer[SIZE];
    char buffer_copy[SIZE];

    // getting the client working directory
    getWD(&clientInfo);
    while (1)
    {
        // displaying the client terminal interface
        bzero(buffer, sizeof(buffer));
        printf("ftp> ");

        // getting the user input
        fgets(buffer, sizeof(buffer), stdin);
        buffer[strcspn(buffer, "\n")] = 0; // remove trailing newline char from buffer, fgets does not remove it
        strcpy(buffer_copy, buffer);

        // check if message is empty
        if (strlen(buffer) == 0)
        {
            printf("Message cannot be empty.\n");
            continue;
        }

        // splitting the user input based on spaces
        int userInputLen;
        char **userInput = split_string(buffer, ' ', &userInputLen);

        // calling the clientCommand function to handle the client request
        int commandFlag = clientCommand(userInput, userInputLen, &clientInfo);

        // performing specific task based on the return value from clientCommand
        if (commandFlag == 1)
        {
            // the command was handled on the client side
            // incase of STOR, RETR, or LIST, the commands are relayed to the server
            // from inside of the clientCommand function when PORT is called implicitly
            continue;
        }
        else if (commandFlag == 2)
        {
            // the command was relayed to the server
            size_t buffer_size = sizeof(buffer);
            if (send(clientInfo.server_sd, buffer_copy, buffer_size, 0) < 0)
            {
                perror("send error");
                exit(-1);
            }

            // the response received from the server side with status code
            bzero(clientInfo.server_response, sizeof(clientInfo.server_response));
            if (recv(clientInfo.server_sd, clientInfo.server_response, sizeof(clientInfo.server_response), 0) > 0)
            {
                // displaying the server response
                printf("%s\n", clientInfo.server_response);
            }

            // checking if the server relayed the quit status code 221
            if (strncmp(clientInfo.server_response, "221", 3) == 0)
            {
                // terminating the client side
                printf("Thank you. Exited successfully\n");
                close(clientInfo.server_sd);
                exit(0);
            }
        }
    }
    return 0;
}

// clientCommand handler
int clientCommand(char **userInput, int userInputLen, struct ClientInfo *clientInfo)
{
    // taking the command string
    char *command = userInput[0];
    int retVal;

    // checking the command with the list of available commands
    if (strcmp(command, "LIST") == 0) // listing directories from the server side
    {
        retVal = 1;

        // Set up a new socket from the client side for opening the FTP data connection
        int value = 1;
        int ftp = socket(AF_INET, SOCK_STREAM, 0);
        setsockopt(ftp, SOL_SOCKET, SO_REUSEADDR, &value, sizeof(int));

        // Setting up timeout to prevent the state of the client to the infinitely stuck waiting for server response
        // ref: http://www.ccplusplus.com/2011/09/struct-timeval-in-c.html
        struct timeval tv;
        tv.tv_sec = 5;
        tv.tv_usec = 0;
        setsockopt(ftp, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof tv);

        struct sockaddr_in ftp_addr;
        bzero(&ftp_addr, sizeof(ftp_addr));
        ftp_addr.sin_family = AF_INET;
        ftp_addr.sin_addr.s_addr = inet_addr(CLIENT_IP);

        // repeadtedly checks for empty port for the socket to bind to
        int success = 0;
        do
        {
            base_port += 1;
            ftp_addr.sin_port = htons(base_port);
            success = bind(ftp, (struct sockaddr *)&ftp_addr, sizeof(ftp_addr));
        } while (success < 0);

        // After successful bind, the client side now continues to listen on the ftp socket
        // It acts as the server now for the data connection channel
        if (listen(ftp, 5) < 0)
        {
            perror("listen failed");
            close(ftp);
            return 0;
        }

        // making the PORT command ready to be sent
        char toSend[SIZE] = "PORT ";
        char ip[SIZE] = CLIENT_IP;

        // dividing the port number (16 bit) into two 8 bit numbers - p1 and p2

        int p1 = base_port / 256;
        int p2 = base_port % 256;

        // dividing the client IP (32 bit address) into four 8bit long addresses - h1, h2, h3, h4
        // we initially wanted to repeated integer and modulo division to find h1, h2, h3, h4
        // later we realized that these four numbers were essentially already separated with "."s in the CLIENT_IP.
        // So we replaced the dots with commas to get them in the specific format

        int n = strlen(ip);
        for (int i = 0; i < n; i++)
        {
            if (ip[i] == '.')
            {
                ip[i] = ',';
            }
        }
        strcat(toSend, ip);

        // concatanating p1,p2 to the end of the two send message
        char end[SIZE];
        bzero(end, sizeof(end));
        sprintf(end, ",%d,%d", p1, p2);
        strcat(toSend, end);

        // to send now has - PORT h1,h2,h3,h4,p1,p2
        send(clientInfo->server_sd, toSend, strlen(toSend), 0);

        // receiving the server response - "200 PORT command successful"
        char response[SIZE];
        bzero(response, sizeof(response));
        if (read(clientInfo->server_sd, response, sizeof(response)) <= 0)
        {
            perror("invalid server response");
            close(ftp);
            return 0;
        }
        // displaying the message to the client
        printf("%s\n", response);

        // if the server didn't respond with a 200 means that there was some problem with
        // fulfilling the port command, so the current command is terminated
        if (strncmp(response, "200", 3) != 0)
        {
            // the ftp socket is closed
            close(ftp);
            return 0;
        }

        // if the port command was successful

        // concatanating the original command - STOR LIST or RETR
        char buffer[SIZE] = "";
        for (int i = 0; i < userInputLen; i++)
        {
            if (i > 0)
            {
                strcat(buffer, " ");
            }
            strcat(buffer, userInput[i]);
        }
        // sending the commmand back to the server
        send(clientInfo->server_sd, buffer, strlen(buffer), 0);

        // displaying the server response - "150 File status okay; about to open data connection"
        char servRes2[SIZE];
        bzero(servRes2, sizeof(servRes2));
        recv(clientInfo->server_sd, servRes2, sizeof(servRes2), 0);
        printf("%s\n", servRes2);

        struct sockaddr_in ftp_other_addr;
        int addr_len = sizeof(ftp_other_addr);
        int ftp_other_connection = 0;

        // accepting the server data connection
        if ((ftp_other_connection = accept(ftp, (struct sockaddr *)&ftp_other_addr, (socklen_t *)&addr_len)) < 0)
        {
            perror("server timeout");
            close(ftp);
            return 0;
        }

        // receiving the response of the LIST command on the server side
        char listMsg[SIZE];
        bzero(listMsg, sizeof(listMsg));
        if ((recv(ftp_other_connection, listMsg, sizeof(listMsg), 0)) < 0)
        {
            perror("send file error");
            close(ftp);
            close(ftp_other_connection);
            return 0;
        }
        // displaying the listed directories
        printf("Listed Directories:\n %s\n", listMsg);

        // closing the ftp socket and the data connection
        close(ftp);
        close(ftp_other_connection);
        return 1;
    }
    else if (strcmp(command, "!LIST") == 0) // listing directories in the client side
    {
        retVal = 1;
        if (userInputLen != 1)
        {
            printf("501 Syntax error in parameters or argument\n");
            return 0;
        }

        // ref1: https://pubs.opengroup.org/onlinepubs/7908799/xsh/dirent.h.html
        // ref2: https://www.youtube.com/watch?v=j9yL30R6npk

        DIR *dir;
        struct dirent *ent; // to hold directory entries

        if ((dir = opendir(clientInfo->PWD)) != NULL)
        {
            // reading through each entry in the directory and displaying them
            while ((ent = readdir(dir)) != NULL)
            {
                printf("%s\n", ent->d_name);
            }
            // closing the directory
            closedir(dir);
        }
        else
        {
            // if the directory failed to open
            printf("550 No such file or directory\n");
            return 0;
        }
        return 1;
    }
    else if (strcmp(command, "!PWD") == 0) // printing working directory in the client side
    {
        retVal = 1;
        if (userInputLen != 1)
        {
            printf("501 Syntax error in parameters or argument.\n");
            return 0;
        }
        // printing the current working directory that had been computed using the getWD function in the very beginning
        printf("%s\n", clientInfo->PWD);
        return 1;
    }
    else if (strcmp(command, "!CWD") == 0) // changing working directory on the server side
    {
        retVal = 1;
        if (userInputLen != 2)
        {
            printf("501 Syntax error in parameters or argument.\n");
            return 0;
        }

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
            printf("550 No such file or directory\n");
            return 0;
        }

        // displaying the new working directory of the client
        printf("%s\n", newCWD2);

        // setting the current working directory to the new directory
        bzero(clientInfo->PWD, sizeof(clientInfo->PWD));
        strcpy(clientInfo->PWD, newCWD2);
        return 1;
    }
    else if (strcmp(command, "RETR") == 0) // retrieving file from server side
    {
        retVal = 1;

        // the first part is entirely similar to that of LIST - Creating a socket connection, calling port, accepting a data connection

        // getting complete path of the file
        char path[SIZE];
        strcpy(path, clientInfo->PWD);
        strcat(path, "/");
        strcat(path, userInput[1]);

        // opening file to write into it
        FILE *f;
        f = fopen(path, "wb"); // creates file if it doesn't already exist

        if (f == NULL)
        {
            // if file failed to open for some reason
            return 0;
        }

        int value = 1;
        int ftp = socket(AF_INET, SOCK_STREAM, 0);
        setsockopt(ftp, SOL_SOCKET, SO_REUSEADDR, &value, sizeof(int));

        struct timeval tv;
        tv.tv_sec = 5;
        tv.tv_usec = 0;
        setsockopt(ftp, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof tv);

        struct sockaddr_in ftp_addr;
        bzero(&ftp_addr, sizeof(ftp_addr));
        ftp_addr.sin_family = AF_INET;
        ftp_addr.sin_addr.s_addr = inet_addr(CLIENT_IP);

        int success = 0;
        do
        {
            base_port += 1;
            ftp_addr.sin_port = htons(base_port);
            success = bind(ftp, (struct sockaddr *)&ftp_addr, sizeof(ftp_addr));
        } while (success < 0);

        if (listen(ftp, 5) < 0)
        {
            perror("listen failed");
            close(ftp);
            fclose(f);
            return 0;
        }

        char ip[SIZE] = CLIENT_IP;
        char toSend[SIZE] = "PORT ";

        int p1 = base_port / 256;
        int p2 = base_port % 256;

        int n = strlen(ip);

        for (int i = 0; i < n; i++)
        {
            if (ip[i] == '.')
            {
                ip[i] = ',';
            }
        }

        strcat(toSend, ip);

        char end[SIZE];
        bzero(end, sizeof(end));
        sprintf(end, ",%d,%d", p1, p2);
        strcat(toSend, end);
        send(clientInfo->server_sd, toSend, strlen(toSend), 0);

        char response[SIZE];
        bzero(response, sizeof(response));
        if (read(clientInfo->server_sd, response, sizeof(response)) <= 0)
        {
            perror("invalid server response");
            close(ftp);
            fclose(f);
            return 0;
        }
        printf("%s\n", response);
        if (strncmp(response, "200", 3) != 0)
        {
            close(ftp);
            fclose(f);
            return 0;
        }

        char commandStr[SIZE] = "";
        for (int i = 0; i < userInputLen; i++)
        {
            if (i > 0)
            {
                strcat(commandStr, " ");
            }
            strcat(commandStr, userInput[i]);
        }
        send(clientInfo->server_sd, commandStr, strlen(commandStr), 0);

        char servRes2[SIZE];
        bzero(servRes2, sizeof(servRes2));
        recv(clientInfo->server_sd, servRes2, sizeof(servRes2), 0);
        printf("%s\n", servRes2);

        struct sockaddr_in ftp_other_addr;
        int addr_len = sizeof(ftp_other_addr);
        int ftp_other_connection = 0;
        if ((ftp_other_connection = accept(ftp, (struct sockaddr *)&ftp_other_addr, (socklen_t *)&addr_len)) < 0)
        {
            perror("server timeout");
            close(ftp);
            fclose(f);
            return 0;
        }

        // Receiving the file data from the server side
        char buffer[SIZE];
        bzero(buffer, sizeof(buffer));
        int bytes = 0;
        while ((bytes = recv(ftp_other_connection, buffer, sizeof(buffer), 0)) > 0)
        {
            // writing the file data into the file
            fwrite(buffer, 1, bytes, f);
            bzero(buffer, sizeof(buffer));
        }

        // closing the file and socket connection
        close(ftp);
        fclose(f);
        close(ftp_other_connection);

        // displaying the server side final response - "226 Transfer completed"
        bzero(clientInfo->server_response, sizeof(clientInfo->server_response));
        if (recv(clientInfo->server_sd, clientInfo->server_response, sizeof(clientInfo->server_response), 0) > 0)
        {
            printf("%s\n", clientInfo->server_response);
        }

        return 1;
    }
    else if (strcmp(command, "STOR") == 0) // sending file from client side to server side and storing it in the server's working directory
    {
        retVal = 1;

        // almost same as the RETR function but the file here is opened in the read mode instead
        char path[SIZE];

        strcpy(path, clientInfo->PWD);
        strcat(path, "/");
        strcat(path, userInput[1]);

        FILE *f;
        f = fopen(path, "rb");

        if (f == NULL)
        {
            printf("550 No such file or directory\n");
            return 0;
        }

        int value = 1;
        int ftp = socket(AF_INET, SOCK_STREAM, 0);
        setsockopt(ftp, SOL_SOCKET, SO_REUSEADDR, &value, sizeof(int));

        struct timeval tv;
        tv.tv_sec = 20;
        tv.tv_usec = 0;
        setsockopt(ftp, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof tv);

        struct sockaddr_in ftp_addr;
        bzero(&ftp_addr, sizeof(ftp_addr));
        ftp_addr.sin_family = AF_INET;
        ftp_addr.sin_addr.s_addr = inet_addr(CLIENT_IP);

        int success = 0;
        do
        {
            base_port += 1;
            ftp_addr.sin_port = htons(base_port);
            success = bind(ftp, (struct sockaddr *)&ftp_addr, sizeof(ftp_addr));
        } while (success < 0);

        if (listen(ftp, 5) < 0)
        {
            perror("listen failed");
            close(ftp);
            fclose(f);
            return 0;
        }
        char ip[SIZE] = CLIENT_IP;
        char toSend[SIZE] = "PORT ";

        int p1 = base_port / 256;
        int p2 = base_port % 256;

        int n = strlen(ip);

        for (int i = 0; i < n; i++)
        {
            if (ip[i] == '.')
            {
                ip[i] = ',';
            }
        }

        strcat(toSend, ip);

        char end[SIZE];
        bzero(end, sizeof(end));
        sprintf(end, ",%d,%d", p1, p2);
        strcat(toSend, end);
        send(clientInfo->server_sd, toSend, strlen(toSend), 0);

        char response[SIZE];
        bzero(response, sizeof(response));
        if (read(clientInfo->server_sd, response, sizeof(response)) <= 0)
        {
            perror("invalid server response");
            close(ftp);
            fclose(f);
            return 0;
        }
        printf("%s\n", response);
        if (strncmp(response, "200", 3) != 0)
        {
            return 0;
        }

        char buffer[SIZE] = "";
        for (int i = 0; i < userInputLen; i++)
        {
            if (i > 0)
            {
                strcat(buffer, " ");
            }
            strcat(buffer, userInput[i]);
        }
        send(clientInfo->server_sd, buffer, strlen(buffer), 0);

        char servRes2[SIZE];
        bzero(servRes2, sizeof(servRes2));
        recv(clientInfo->server_sd, servRes2, sizeof(servRes2), 0);
        printf("%s\n", servRes2);

        struct sockaddr_in ftp_other_addr;
        int addr_len = sizeof(ftp_other_addr);
        int ftp_other_connection = 0;
        if ((ftp_other_connection = accept(ftp, (struct sockaddr *)&ftp_other_addr, (socklen_t *)&addr_len)) < 0)
        {
            perror("server timeout");
            close(ftp);
            fclose(f);
            return 0;
        }
        int total_bytes_send = 0;

        char data[SIZE];

        while (fgets(data, SIZE - 1, f) != NULL)
        {
            if (send(ftp_other_connection, data, strlen(data), 0) < 0)
            {
                perror("send file error");
                close(ftp);
                fclose(f);
                close(ftp_other_connection);
                return 0;
            }
            bzero(data, SIZE);
        }

        close(ftp);
        fclose(f);
        close(ftp_other_connection);

        bzero(clientInfo->server_response, sizeof(clientInfo->server_response));
        if (recv(clientInfo->server_sd, clientInfo->server_response, sizeof(clientInfo->server_response), 0) > 0)
        {
            printf("%s\n", clientInfo->server_response);
        }
        return 1;
    }
    else
    {
        // checking for invalid commands on the client side
        if (strncmp(command, "!", 1) == 0)
        {
            printf("202 Command not implemented\n");
            return 1;
        }
        // all other commands - valid or invalid - will be relayed to the server side to deal with
        return 2;
    }
}

// splits the user input - using strtok
// ref: https://www.educative.io/answers/splitting-a-string-using-strtok-in-c

char **split_string(char *str, char delim, int *len)
{
    char **words = malloc(sizeof(char *)); // Allocate memory for a pointer to an array of strings
    int word_count = 0;
    int in_quote = 0; // Flag to check if inside a quote

    char *token = strtok(str, &delim);
    while (token != NULL)
    {
        if (token[0] == '"' && !in_quote)
        { // Check if token starts with a quote and not inside a quote
            in_quote = 1;
            token++;
        }
        if (token[strlen(token) - 1] == '"' && in_quote)
        { // Check if token ends with a quote and inside a quote
            in_quote = 0;
            token[strlen(token) - 1] = '\0';
        }

        if (!in_quote)
        { // Only split tokens outside of quotes
            words = realloc(words, (word_count + 1) * sizeof(char *));
            words[word_count] = malloc(strlen(token) + 1);
            strcpy(words[word_count], token);
            word_count++;
        }
        else
        { // Combine tokens inside quotes
            int last_word = word_count - 1;
            words[last_word] = realloc(words[last_word], strlen(words[last_word]) + strlen(token) + 2);
            strcat(words[last_word], " ");
            strcat(words[last_word], token);
        }

        token = strtok(NULL, &delim);
    }

    words = realloc(words, (word_count + 1) * sizeof(char *));
    words[word_count] = NULL;

    *len = word_count;

    return words;
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
        perror("Unable to retrieve current working directory");
        return;
    }
}
