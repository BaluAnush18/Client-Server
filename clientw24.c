/*
Authors:

110126149: Balu Anush Anthu Kumar
110126196: Vismitha Pulakkayaiah Yohanan

Advanced System Programming - 2
*/

#include <stdio.h> // Include Standard Input Output header file for I/O operations
#include <stdlib.h> // Include Standard Library for memory allocation, process control, etc.
#include <string.h> // Include String operations header file for string manipulation functions
#include <unistd.h> // Include POSIX operating system API for UNIX standard function definitions
#include <arpa/inet.h> // Include definitions for internet operations (e.g., IP addresses conversion)
#include <fcntl.h> // Include File Control options for file handling operations
#include <time.h> // Include Time functions for manipulating and formatting time
#include <signal.h> // Include Signal handling functionalities

#define O_BINARY 0 // Define O_BINARY as 0 for compatibility (relevant in Windows for file mode)

#define SERVER_IP "127.0.0.1" // Define server's IP address as localhost
#define SERVER_PORT 8082      // Define the port number on which the server will listen
#define BUFFER_SIZE 10000     // Define buffer size for data transfer
#define MAX_PATH_LENGTH 4096  // Define maximum path length for file paths
#define MAX_ARGS 10           // Define maximum number of arguments in commands

FILE *fp; // Declare a file pointer to be used globally
// Helper function to check if the filename is safe
int is_safe_filename(char *filename) {
    // Add any other checks as necessary
    if (strchr(filename, '') != NULL || strchr(filename, ';') != NULL) {
        return 0; // Unsafe characters found
    }
    return 1; // Safe
}

void unzip_tar_file(char *tar_filename) {
    printf("unzipping file...\n");

    if (!is_safe_filename(tar_filename)) {
        fprintf(stderr, "Unsafe filename detected. Aborting.\n");
        exit(1);
    }

    char command[MAX_PATH_LENGTH + 50]; // Extra space for command format
    int status = snprintf(command, sizeof(command), "tar -xzf '%s' -C .", tar_filename);

    if (status < 0 || status >= sizeof(command)) {
        fprintf(stderr, "Failed to prepare command. Aborting.\n");
        exit(1);
    }

    status = system(command);
    if (status == -1) {
        perror("Error executing tar command");
        exit(1);
    }

    printf("File unzipped...\n");
}

void receive_file(int socketfd, int unzipProcess) {
    int pid = fork(); // Forks the current process to create a child process
    if (pid == -1) { // Checks if fork failed
        perror("fork"); // Prints the error related to fork failure
        exit(EXIT_FAILURE); // Exits the program indicating failure
    }

    if (pid == 0) { // Checks if this is the child process (fork returns 0 to the child process)
        char buffer[BUFFER_SIZE]; // Creates a buffer to store the data received from the socket
        const char *tarName = "temp.tar.gz"; // Sets the name of the file to be created

        // Opens/creates a file for writing only. If the file does not exist, it will be created with user read/write permissions. S_IRUSR sets the permission for the file owner to read the file. S_IWUSR sets the permission for the file owner to write to the file. 
        int fd = open(tarName, O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR); 
        if (fd == -1) { // Checks if opening/creating the file failed
            perror("open"); // Prints the error related to file open/create failure
            exit(EXIT_FAILURE); // Exits the program indicating failure
        }

        ssize_t bytes_received = 0; // Initializes a variable to store the number of bytes received
        long int total_bytes_received = 0; // Initializes a variable to keep the running total of bytes received
        // Receives data from the socket until there's nothing left to read
        while ((bytes_received = recv(socketfd, buffer, BUFFER_SIZE, 0)) > 0) { 
            if (write(fd, buffer, bytes_received) != bytes_received) { // Writes the received data to file and checks for errors
                perror("write"); // Prints the error related to write failure
                close(fd); // Closes the file descriptor
                exit(EXIT_FAILURE); // Exits the program indicating failure
            }
            total_bytes_received += bytes_received; // Adds the number of bytes received to the total
        }

        if (bytes_received == -1) { // Checks if there was an error receiving data
            perror("recv"); // Prints the error related to recv failure
            close(fd); // Closes the file descriptor
            exit(EXIT_FAILURE); // Exits the program indicating failure
        }

        // Prints a message based on whether any bytes were received
        printf(total_bytes_received > 0 ? "File received successfully.\n" : "No files found.\n");
        printf("Total file received %ld bytes.\n", total_bytes_received); // Prints the total bytes received

        close(fd); // Closes the file descriptor
        exit(EXIT_SUCCESS); // Exits the child process successfully
    } else { // This block is executed by the parent process
        printf("File reception initiated...\n"); // Indicates that file reception has started
        if (unzipProcess == 1) {
            // This would attempt to unzip potentially before the file is fully received.
            // Consider reworking logic for when unzip should occur.
            sleep(5); // Consider removing or reworking this delay
            unzip_tar_file("temp.tar.gz");
    }
}
}

int main() {
    int client_socket; // Declare variable to hold the client socket descriptor
    struct sockaddr_in server_addr; // Declare a structure to hold the server's address information
    char buffer[BUFFER_SIZE]; // Declare a buffer for storing received data
    ssize_t bytes_received; // Declare a variable to store the number of bytes received from the server
    ssize_t read_bytes; // Variable to store the number of bytes to read (unused in this segment)
    int file_fd; // File descriptor for the file being received (unused in this segment)

    // Create a socket with AF_INET (IPv4), SOCK_STREAM (TCP), and protocol 0 (IP)
    client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket == -1) { // Check if socket creation failed
        perror("Error: Failed to create socket"); // Print the error message
        exit(1); // Exit the program with a status code of 1
    }

    // Initialize the server address structure with zeros
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET; // Set the family to IPv4
    // Convert and set the server IP address from string to binary form
    server_addr.sin_port = htons(SERVER_PORT); // Convert and set the server port number (host to network short)
    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) == -1) { // Convert and check if IP conversion failed
        perror("inet_pton"); // Print the error message
        exit(EXIT_FAILURE); // Exit the program indicating failure
    }

    // Attempt to connect to the server using the created socket and server address
    if (connect(client_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("Error: Failed to connect to server"); // Print the error message if connection failed
        exit(1); // Exit the program with a status code of 1
    }

    // Print confirmation of successful connection
    printf("\nConnected to server: %s:%d\n", SERVER_IP, SERVER_PORT);
    char command[1000], server_reply[2000]; // Declare arrays for storing commands and server replies
    char *args[MAX_ARGS]; // Declare an array of pointers for command arguments
    int num_args; // Declare a variable for counting the number of arguments

    // Loop indefinitely to send and receive commands
    while (1) {
        printf("clientw24: "); // Prompt the user for a command
        fgets(command, 1000, stdin); // Read the command from standard input
        char temp[1000]; // Declare a temporary array to hold the command
        sprintf(temp, "%s", command); // Copy the command into the temporary array
        // Tokenize the command string into arguments
        num_args = 0; // Initialize the argument count to 0
        args[num_args] = strtok(temp, " \n"); // Get the first token/argument
        // Loop through the command string to extract all arguments
        while (args[num_args] != NULL && num_args < MAX_ARGS - 1) {
            num_args++; // Increment the argument count
            args[num_args] = strtok(NULL, " \n"); // Get the next token/argument
        }

        int command_valid_flag = 0; // Initialize a flag to check if a valid command has been entered
int file_flag = 0; // Initialize a flag to check if the operation involves dealing with files
int unzip = 0; //Initialize a flag to check the unzip file function

// Check if the first argument (command) is "w24fn" which  stand for "find file by name"
if (strcmp(args[0], "w24fn") == 0) {
    file_flag = 0; // Explicitly setting file_flag to 0, indicating no file transfer is expected
    printf("File Search Operation Invoked\n"); // Notify that the findfiles command has been invoked
    if (num_args != 2) { // Check if the correct number of arguments is provided
        fprintf(stderr, "Usage: %s filename\n", args[0]); // If not, print the correct usage
    } else {
        command_valid_flag = 1; // If correct, set the command valid flag
    }
}

// Check if the first argument (command) is "w24fz" which stands "find files by size"
else if (strcmp(args[0], "w24fz") == 0) {
    printf("Find Files By Size Invoked\n"); // Notify that the get files by size command has been invoked
    command_valid_flag = 1; // Initially assume the command is valid
    if (num_args < 3 || num_args > 4) { // Check for correct number of arguments
        printf("Usage: %s size1 size2\n", args[0]); // If incorrect, print the correct usage
        command_valid_flag = 0; // And mark command as invalid
    } else {
        long size1 = atol(args[1]); // Convert first size argument from string to long
        long size2 = atol(args[2]); // Convert second size argument from string to long
        if (size1 < 0 || size2 < 0 || size1 > size2) { // Check if sizes are valid
            printf("Invalid size parameters\n"); // If not, notify the user
            command_valid_flag = 0; // And mark command as invalid
        }
    }
    if (command_valid_flag)
            {
                file_flag = 1;
                if (strcmp(args[num_args - 1], "-u") == 0)
                {
                    unzip = 1;
                }
            }
}

// Check if the first argument (command) is "w24fdb" which  stand for "find files by date before"
else if (strcmp(args[0], "w24fdb") == 0) {
    printf("Function for Files Before given Date Invoked\n"); // Notify that the find files by date command has been invoked
    command_valid_flag = 1; // Initially assume the command is valid
    if (num_args != 2 && num_args != 3) { // Check for the correct number of arguments
        printf("Usage: %s date\n", args[0]); // If incorrect, print the correct usage
        command_valid_flag = 0; // And mark command as invalid
    } else {
        struct tm date_tm; // Declare a struct to hold the date
        memset(&date_tm, 0, sizeof(date_tm)); // Initialize the struct
        // Convert the date string to a tm struct
        if (strptime(args[1], "%Y-%m-%d", &date_tm) == NULL) {
            printf("Invalid date format: YYYY-MM-DD\n"); // If conversion fails, notify the user
            command_valid_flag = 0; // And mark command as invalid
        }
    }
    if (command_valid_flag) {
        file_flag = 1;
        unzip = 1;
    }
}

// Handling w24fda command (files created on or after the specified date)
else if (strcmp(args[0], "w24fda") == 0) {
    printf("Function for Files After given Date \n"); // Print that the 'w24fda' command has been invoked
    command_valid_flag = 1; // Assume the command is valid initially
    if (num_args != 2 && num_args != 3) { // Check if the number of arguments is correct
        printf("Usage: %s date\n", args[0]); // If not, display the correct usage
        command_valid_flag = 0; // Mark the command as invalid
    } else {
        struct tm date_tm; // Create a struct to hold the parsed date
        memset(&date_tm, 0, sizeof(date_tm)); // Initialize the struct with zeros
        // Try to parse the date argument into the struct
        if (strptime(args[1], "%Y-%m-%d", &date_tm) == NULL) {
            printf("Invalid date format: YYYY-MM-DD\n"); // If parsing fails, notify the user
            command_valid_flag = 0; // Mark the command as invalid
        }
    }
    if (command_valid_flag) {
        file_flag = 1;
        unzip = 1;
    }    
}

// Handling w24ft command (get files with specific extensions)
else if (strcmp(args[0], "w24ft") == 0) {
    if (num_args < 1 || num_args > 4) { // Check if the number of arguments is within expected range
        printf("Usage: %s extension1 [extension2 ... extension3]\n", args[0]); // If not, display the correct usage
        // Note: No command_valid_flag is set here, which  indicate missing functionality or an oversight
    }
    else
            {
            	printf("Tar mentioned Extension Files Function Invoked\n"); // Print that the 'w24ft' command has been invoked
                command_valid_flag = 1;
                file_flag = 1;
                unzip = 1;
            }
}

// Handling dirlist command (list directory contents with options)
else if (strcmp(args[0], "dirlist") == 0 && (strcmp(args[1], "-a") == 0 || strcmp(args[1], "-t") == 0)) {
    write(client_socket, command, strlen(command)); // Send the 'dirlist' command to the server

    // Prepare to receive the server's response
    memset(server_reply, 0, sizeof(server_reply)); // Initialize the reply buffer
    if (recv(client_socket, server_reply, 2000, 0) < 0) { // Attempt to receive the response
        printf("Receiving from server failed. Error\n"); // If receiving fails, notify the user
    } else {
        printf("%s\n", server_reply); // If successful, print the server's response
    }
}

// If the command entered is 'quitc', the client application will prepare to exit.
else if (strcmp(args[0], "quitc") == 0) {
    printf("Exiting...\n"); // Print an exit message to the user.
    break; // Break out of the while loop, leading to the termination of the program.
}
// If none of the recognized commands match, print an error message.
else {
    printf("Bad Request! Command not supported by server\n");
}

// If a valid command has been identified,
if (command_valid_flag) {
    // Send the command to the server.
    write(client_socket, command, strlen(command));

    // If the command involves receiving a file,
    if (file_flag) {
        printf("Receiving file...\n");
        receive_file(client_socket, unzip); // Invoke the function to handle file reception.
    } else {
        // If the command expects a message from the server,
        memset(server_reply, 0, sizeof(server_reply)); // Clear the server reply buffer.
        // Attempt to receive the server's response.
        if (recv(client_socket, server_reply, 2000, 0) < 0) {
            printf("Receiving from server failed. Error\n"); // Print an error message if receiving fails.
            break; // Break from the while loop, indicating a potential issue with the connection or server.
        }
        printf("Server reply: %s\n", server_reply); // Print the received server reply.
        memset(server_reply, 0, sizeof(server_reply)); // Clear the buffer again after printing.
    }
}
}
}


