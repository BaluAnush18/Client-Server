/*
Authors:

110126149: Balu Anush Anthu Kumar
110126196: Vismitha Pulakkayaiah Yohanan

Advanced System Programming - 2
*/

// Standard libraries for I/O, memory allocation, and string operations
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
// UNIX standard library for miscellaneous symbolic constants and types
#include <unistd.h>
// Libraries for socket programming
#include <sys/socket.h> // Main sockets library
#include <arpa/inet.h> // For internet operations, e.g., converting addresses
#include <netinet/in.h> // For internet domain address structures
// Additional libraries for process control, directory reading, file status, time manipulation, and file control
#include <sys/wait.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>
#include <fcntl.h>
#include <stdbool.h> // For using boolean types

#define SERVER_IP "127.0.0.1" // IP address of the server
#define SERVER_PORT 8083      // Port number of the mirror server
#define BUFFER_SIZE 1024      // Buffer size for receiving data from clients
#define MAX_PATH_LENGTH 4096
#define MAX_COMMAND_LENGTH 10000
#define MAX_ARGS 10
#define MAX_CMD_LEN 2048
#define MAX_CLIENTS 100
// Global variable declarations
FILE *fp; // File pointer for file operations
char fileBuffer[1024] = {0}; // Buffer for file data, initialized to zeros

void crequest(int client_socket);
int send_file(int socketFd, const char *fp); // Correct the return type to match the definition

// Define a structure for storing directory information
typedef struct {
    char name[MAX_PATH_LENGTH]; // Name of the directory
    time_t creation_time; // Creation time of the directory
} dir_info_t;

// Function to send detailed information about a file over a network socket
void send_file_info(int socket, const char *path, const char *filename, const struct stat *file_stat) {
    // Buffer for constructing the message to be sent
    char message[1024];
    
    // Prepare a message with detailed file information
    // Includes the file's path, name, size, creation time, and permissions
    int message_length = snprintf(message, sizeof(message),
                                  "\nFile Path:%s\nFilename: %s\nFile Size: %ld\nCreate At: %s\nPermissions: %o\n",
                                  path, filename, file_stat->st_size, ctime(&file_stat->st_mtime), file_stat->st_mode & 0777);

    // Send the prepared message to the specified socket
    // Check if the sending fails
    if (send(socket, message, message_length, 0) < 0) {
        perror("send failed"); // Print an error message to stderr
        exit(EXIT_FAILURE); // Terminate the program with a failure status
    }
}

// Recursively searches for a file in the given directory and subdirectories.
void find_and_send_file(int client_socket, const char *filename, const char *directory, int *found) {
    // Open the directory specified by 'directory' parameter.
    DIR *dir = opendir(directory);
    if (!dir) {
        // If opening directory fails, print error message and exit function.
        perror("Failed to open directory");
        return;
    }

    struct dirent *dp;
    char buffer[BUFFER_SIZE];
    struct stat st;

    // Iterate over each entry in the directory.
    while ((dp = readdir(dir)) != NULL && !*found) {
        // Skip over '.' and '..' entries to avoid infinite loops.
        if (strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0) {
            continue;
        }

        // Construct full path for current entry.
        snprintf(buffer, sizeof(buffer), "%s/%s", directory, dp->d_name);

        if (dp->d_type == DT_DIR) {
            // If entry is a directory, search it recursively for the file.
            find_and_send_file(client_socket, filename, buffer, found);
        } else if (dp->d_type == DT_REG && strcmp(dp->d_name, filename) == 0) {
            // If entry is a regular file and names match, retrieve file details.
            if (stat(buffer, &st) == 0) {
                // Send file information through the socket.
                send_file_info(client_socket, directory, filename, &st);
                *found = 1; // Indicate that the file has been found.
            }
        }
    }

    // Close the directory.
    closedir(dir);
}

void findfile(int client_socket, char *filename, char *path, int *found) {
    // Calls the 'find_and_send_file' function to attempt to locate and send the file.
    find_and_send_file(client_socket, filename, path, found);

    // If the file was not found ('found' flag is false), send a "file not found" message to the client.
    if (!*found) {
        char message[] = "File not found\n";
        // Send the "file not found" message to the client through the socket.
        send(client_socket, message, sizeof(message) - 1, 0);
    }
}

void sgetfiles(const char *dir_path, const char *tar_path, const char *size1, const char *size2, int client_socket) {
    // Creates a unique temporary file path to store the list of files meeting the criteria.
    char temp_file_list_path[] = "/tmp/filelistXXXXXX";
    // Creates the temporary file and returns a file descriptor to it.
    int temp_fd = mkstemp(temp_file_list_path);
    if (temp_fd == -1) {
        // If creating the temporary file fails, sends an error message to the client.
        perror("Failed to create temporary file");
        send(client_socket, "Server error: could not generate file list.\n", 45, 0);
        return;
    }

    // Prepares a command string to find files within specified size range and outputs to the temporary file.
    char find_cmd[MAX_CMD_LEN];
    int res = snprintf(find_cmd, MAX_CMD_LEN, "find %s -type f -size +%s -size -%s > %s",
                       dir_path, size1, size2, temp_file_list_path);
    // Checks if the command string was truncated or if there was an error creating it.
    if (res >= MAX_CMD_LEN || res < 0) {
        fprintf(stderr, "Error constructing find command.\n");
        // Closes and deletes the temporary file before exiting.
        close(temp_fd);
        unlink(temp_file_list_path);
        return;
    }

    // Executes the 'find' command. If it fails (non-zero return value), sends a "No file found" message.
    int find_result = system(find_cmd);
    if (find_result != 0) {
        const char* message = "No file found\n";
        send(client_socket, message, strlen(message), 0);
        // Closes and deletes the temporary file before exiting.
        close(temp_fd);
        unlink(temp_file_list_path);
        return;
    }

    // Prepares a command string to create a tar archive of the files listed in the temporary file.
    char tar_cmd[MAX_CMD_LEN];
    snprintf(tar_cmd, MAX_CMD_LEN, "tar -czf %s -T %s", tar_path, temp_file_list_path);
    // Executes the 'tar' command. If it fails, sends an error message.
    int tar_result = system(tar_cmd);
    if (tar_result != 0) {
        fprintf(stderr, "Failed to create tar archive.\n");
        const char* message = "Error creating file archive\n";
        send(client_socket, message, strlen(message), 0);
    } else {
        // If the tar archive is successfully created, sends it to the client.
        send_file(client_socket, tar_path);
    }

    // Closes the temporary file and deletes it to clean up.
    close(temp_fd);
    unlink(temp_file_list_path);
}

// Defines a function to search for files modified before a specified date and send a tar archive of those files.
void dgetfiles_before(const char *dir_path, const char *tar_path, const char *date, int client_socket) {
    // Buffer to store the command to find files modified before a specific date.
    char find_cmd[MAX_CMD_LEN];
    // Buffer to store the command to archive the found files.
    char tar_cmd[MAX_CMD_LEN];

    // Fills 'find_cmd' with the command to find files in 'dir_path' modified before 'date'.
    snprintf(find_cmd, MAX_CMD_LEN, "find %s -type f ! -newermt '%s'", dir_path, date);

    // Executes the 'find' command. If files are found (exit status 0), it proceeds; otherwise, it sends a message.
    if (system(find_cmd) == 0) {
        // Constructs the command to create a tar archive of the found files, storing the archive in 'tar_path'.
        snprintf(tar_cmd, MAX_CMD_LEN, "%s | xargs tar -czf %s", find_cmd, tar_path);
        // Executes the command to create the tar archive.
        system(tar_cmd);
        // Sends the tar archive file to the client over the socket.
        send_file(client_socket, tar_path);
    } else {
        // If no files matching the criteria are found, sends a "No file found" message to the client.
        const char* message = "No file found\n";
        send(client_socket, message, strlen(message), 0);
    }
}

void dgetfiles_after(const char *dir_path, const char *tar_path, const char *date, int client_socket) {
    char find_cmd[MAX_CMD_LEN]; // Buffer to hold the find command
    char tar_cmd[MAX_CMD_LEN]; // Buffer to hold the tar command

    // Prepare the find command to search for files newer than a specific date
    snprintf(find_cmd, MAX_CMD_LEN, "find %s -type f -newermt '%s'", dir_path, date);

    // Execute the find command using system() call
    // system() returns 0 if the command executed successfully
    if (system(find_cmd) == 0) {
        // If the find command succeeds, prepare the tar command to archive the found files
        snprintf(tar_cmd, MAX_CMD_LEN, "%s | xargs tar -czf %s", find_cmd, tar_path);
        // Execute the tar command using system() call
        system(tar_cmd);
        // Send the tar file to the client
        send_file(client_socket, tar_path);
    } else {
        // If no files were found (or if the find command failed for any other reason),
        // send a "No file found" message to the client
        const char* message = "No file found\n";
        send(client_socket, message, strlen(message), 0);
    }
}

// Function to compare two directory information structures based on their creation time.
// This function is designed to be used with qsort().
int compare_dir_info(const void *a, const void *b) {
    // Cast the void pointers to dir_info_t pointers to access the directory information.
    dir_info_t *dirA = (dir_info_t *)a;
    dir_info_t *dirB = (dir_info_t *)b;

    // Compare the creation times of the two directories.
    // If dirA's creation time is greater than dirB's, a positive value is returned.
    // If dirA's creation time is less than dirB's, a negative value is returned.
    // If both have the same creation time, zero is returned.
    // This comparison logic ensures that an array of dir_info_t structures can be sorted in ascending order of creation time.
    return (dirA->creation_time > dirB->creation_time) - (dirA->creation_time < dirB->creation_time);
}

void list_subdirectories_by_time(int client_socket) {
    DIR *d; // Directory stream
    struct dirent *dir; // Pointer for directory entry
    struct stat st; // Used to get information about the file
    char *homeDir = getenv("HOME"); // Get the path to the home directory
    dir_info_t directories[MAX_CLIENTS]; // Array to store directory info, adjust size as needed
    int count = 0; // Counter for directories found

    // Open the home directory
    d = opendir(homeDir);
    if (d) { // Check if directory is successfully opened
        // Read each entry in the directory
        while ((dir = readdir(d)) != NULL && count < MAX_CLIENTS) {
            // Check if the entry is a directory and not '.' or '..'
            if (dir->d_type == DT_DIR && strcmp(dir->d_name, ".") != 0 && strcmp(dir->d_name, "..") != 0) {
                char fullPath[MAX_PATH_LENGTH];
                // Construct full path of the directory
                snprintf(fullPath, sizeof(fullPath), "%s/%s", homeDir, dir->d_name);
                // Get file status
                if (stat(fullPath, &st) == 0) {
                    // Store the directory name and creation time if stat call is successful
                    strcpy(directories[count].name, dir->d_name);
                    directories[count].creation_time = st.st_ctime; // Store creation time
                    count++; // Increment counter
                }
            }
        }
        closedir(d); // Close directory stream

        // Sort the directories array based on creation time using qsort
        qsort(directories, count, sizeof(dir_info_t), compare_dir_info);

        // Concatenate directory names and creation times into a single string
        char sortedDirectories[1024 * 10] = ""; // Buffer to store sorted directory names, adjust size as needed
        char timeBuff[80]; // Buffer for formatted time string
        for (int i = 0; i < count; i++) {
            // Format creation time of directory
            strftime(timeBuff, sizeof(timeBuff), "%Y-%m-%d %H:%M:%S", localtime(&directories[i].creation_time));
            strcat(sortedDirectories, directories[i].name); // Append directory name
            strcat(sortedDirectories, " - "); // Append separator
            strcat(sortedDirectories, timeBuff); // Append formatted time
            strcat(sortedDirectories, "\n"); // Append newline
        }

        // Send the sorted list to the client
        send(client_socket, sortedDirectories, strlen(sortedDirectories), 0);
    } else {
        // Send error message to client if home directory cannot be opened
        char *errorMsg = "Failed to open home directory.\n";
        send(client_socket, errorMsg, strlen(errorMsg), 0);
    }
}

// This function checks if the provided filename has an extension that matches any of the extensions in the given list.
bool has_valid_extension(const char *filename, const char **extensions, int num_extensions) {
    // Iterate over each extension in the list
    for (int i = 0; i < num_extensions; i++) {
        const char *extension = extensions[i]; // Current extension to check against
        size_t filename_len = strlen(filename); // Length of the filename
        size_t extension_len = strlen(extension); // Length of the current extension

        // Ensure the filename is longer than the extension to avoid out-of-bounds errors
        if (filename_len > extension_len) {
            // Check if the end of the filename matches the extension
            // This is done by comparing the substring of the filename that should match the extension
            if (strcmp(filename + filename_len - extension_len, extension) == 0) {
                return true; // If a match is found, return true
            }
        }
    }
    return false; // If no matching extension is found after checking all, return false
}

void search_and_add_files_to_temp(const char *root_path, const char **extensions, int num_extensions, FILE *tempFile) {
    DIR *dir = opendir(root_path); // Attempt to open the directory specified by root_path
    if (!dir) {
        perror("Failed to open directory"); // If opening the directory fails, print an error message
        return; // Exit the function
    }

    struct dirent *dir_entry; // Structure to hold information about each directory entry
    char file_path[MAX_PATH_LENGTH]; // Buffer to hold the full path of each file

    while ((dir_entry = readdir(dir)) != NULL) { // Read each entry in the directory
        // Skip "." and ".." entries to avoid infinite recursion
        if (strcmp(dir_entry->d_name, ".") == 0 || strcmp(dir_entry->d_name, "..") == 0) continue;

        // Construct the full path of the current entry
        snprintf(file_path, sizeof(file_path), "%s/%s", root_path, dir_entry->d_name);

        if (dir_entry->d_type == DT_DIR) {
            // If the entry is a directory, call the function recursively to handle subdirectories
            search_and_add_files_to_temp(file_path, extensions, num_extensions, tempFile);
        } else if (dir_entry->d_type == DT_REG) {
            // If the entry is a regular file, check if it matches any of the specified extensions
            if (has_valid_extension(dir_entry->d_name, extensions, num_extensions)) {
                // If the file matches, write its full path to the temporary file
                fprintf(tempFile, "%s\n", file_path);
            }
        }
    }

    closedir(dir); // Close the directory after processing all entries
}

void generate_tar_gz_from_files_with_extensions(const char *tar_filename, const char **extensions, int num_extensions) {
    // Create a temporary file to hold the list of file paths. This file will be used to specify which files should be included in the tar archive.
    char tempFileTemplate[] = "/tmp/tempfilelistXXXXXX";
    int tempFd = mkstemp(tempFileTemplate); // Creates a unique temporary file and opens it. The XXXXXX will be replaced with a unique string.
    if (tempFd < 0) {
        perror("Unable to create temporary file"); // If creating the temporary file fails, print an error message.
        return; // Exit the function if unable to create the temporary file.
    }

    FILE *tempFile = fdopen(tempFd, "w"); // Open the file descriptor returned by mkstemp as a FILE* to allow writing with standard I/O functions.
    if (tempFile == NULL) {
        perror("Failed to open temporary file"); // If opening the file fails, print an error message.
        close(tempFd); // Close the file descriptor to clean up resources.
        return; // Exit the function if unable to open the file.
    }

    // Search for files that match the given extensions and add their paths to the temporary file.
    // The search starts from the user's HOME directory.
    const char *root_path = getenv("HOME"); 
    search_and_add_files_to_temp(root_path, extensions, num_extensions, tempFile);

    fclose(tempFile); // Close the FILE* to ensure all data is written to disk.

    // Construct the tar command that uses the list of file paths stored in the temporary file to create a tar.gz archive.
    char command[MAX_COMMAND_LENGTH];
    snprintf(command, sizeof(command), "tar -czf %s -T %s", tar_filename, tempFileTemplate); // -T option tells tar to read the list of files from the specified file.
    
    int result = system(command); // Execute the tar command.
    if (result != 0) {
        fprintf(stderr, "Failed to create tar.gz archive\n"); // If the tar command fails, print an error message.
    } else {
        printf("Archive created successfully\n"); // If the tar command succeeds, print a success message.
    }

    unlink(tempFileTemplate); // Delete the temporary file to clean up.
}

// Comparison function for use with sorting routines like qsort. It compares two strings.
int cmpstr(const void *a, const void *b) {
    // Cast the void pointers to pointers to pointers to char. This is necessary because
    // this function is meant to work with an array of strings (char*), and qsort
    // passes pointers to the elements of the array to the comparison function. In this
    // case, the elements of the array are themselves pointers.
    const char **ia = (const char **)a;
    const char **ib = (const char **)b;

    // Use strcmp to compare the strings pointed to by ia and ib. strcmp returns:
    //   < 0 if the first string is less than the second,
    //   0 if they are equal, and
    //   > 0 if the first string is greater than the second.
    // The return value of strcmp is directly returned by cmpstr, making cmpstr
    // suitable for use with qsort to sort an array of strings in lexicographical order.
    return strcmp(*ia, *ib);
}


// list_subdirectories function
void list_subdirectories(int client_socket) {
    DIR *d;
    struct dirent *dir;
    char *homeDir = getenv("HOME");
    d = opendir(homeDir);
    if (d) {
        char *directories[2000]; // Adjust size based on expected number of directories
        int count = 0;

        while ((dir = readdir(d)) != NULL) {
            if (dir->d_type == DT_DIR && strcmp(dir->d_name, ".") != 0 && strcmp(dir->d_name, "..") != 0) {
                directories[count] = strdup(dir->d_name); // Duplicate and store directory name
                count++;
            }
        }
        closedir(d);

        // Sort the directories array
        qsort(directories, count, sizeof(char *), cmpstr);

        // Concatenate sorted directory names into a single string
        char sortedDirectories[1024 * 10] = ""; // Adjust size as needed
        for (int i = 0; i < count; i++) {
            strcat(sortedDirectories, directories[i]);
            strcat(sortedDirectories, "\n");
            free(directories[i]); // Free the duplicated string
        }

        // Send the sorted list to the client
        send(client_socket, sortedDirectories, strlen(sortedDirectories), 0);
    } else {
        char *errorMsg = "Failed to open home directory.\n";
        send(client_socket, errorMsg, strlen(errorMsg), 0);
    }
}

// Sends a file over a socket
int send_file(int socketFd, const char *fp) {
    pid_t pid = fork();
    if (pid == -1) {
        perror("Failed to fork");
        return -1; // Return error indicator
    }

    if (pid == 0) {
        // Child process: responsible for sending the file
        int filefd = open(fp, O_RDONLY);
        if (filefd < 0) {
            perror("Failed to open file");
            exit(EXIT_FAILURE);
        }

        char buffer[BUFFER_SIZE];
        ssize_t bytesRead;
        while ((bytesRead = read(filefd, buffer, sizeof(buffer))) > 0) {
            if (send(socketFd, buffer, bytesRead, 0) < 0) {
                perror("Failed to send data");
                close(filefd);
                exit(EXIT_FAILURE);
            }
        }

        if (bytesRead < 0) {
            perror("Failed to read file");
        }

        close(filefd);
        close(socketFd); // Ensure the socket is closed in the child process
        fprintf(stderr, "File %s sent successfully to client\n", fp);
        exit(EXIT_SUCCESS); // Terminate the child process successfully
    } else {
        // Parent process waits for the child to complete
        int status;
        waitpid(pid, &status, 0);
        if (status != 0) {
            fprintf(stderr, "Failed to send file %s to client\n", fp);
            return -1; // Indicate failure
        }
        return 0; // Success indicator
    }
}

void crequest(int client_socket)
{
    char buffer[BUFFER_SIZE];
    int file_fd;

    char client_message[2000];
    char command[1000], server_reply[2000];
    char *args[MAX_ARGS];
    int num_args;
    while (1)
    {
        // Receive message from client
        int read_size = recv(client_socket, client_message, 2000, 0);
        if (read_size == 0)
        {
            printf("Client disconnected\n");
            break;
        }
        // Add null terminator to message
        client_message[read_size] = '\0';
        printf("\nClient message: %s", client_message);
        char temp[1000];
        sprintf(temp, "%s", client_message);
        // call commands
        // break raw command
        num_args = 0;
        args[num_args] = strtok(temp, " \n");
        int file_transfer = 0;
        while (args[num_args] != NULL && num_args < MAX_ARGS - 1)
        {
            num_args++;
            args[num_args] = strtok(NULL, " \n");
        }

        // commands
        int command_success_flag = 0;
        char message[1000];
        if (strcmp(args[0], "w24fn") == 0)
        {
            printf("Find Files Function Invoked\n");
            file_transfer = 0;
            char *filename = args[1];
            char *path = getenv("HOME");
            int found = 0;
            findfile(client_socket, filename, path, &found);
            if (found == 0)
            {
                printf("File not found\n");
                memset(message, 0, sizeof(message));
                sprintf(message, "File not found\n");
                message[strlen(message)] = '\0';
                if (send(client_socket, message, strlen(message), 0) < 0)
                {
                    perror("send failed");
                    exit(EXIT_FAILURE);
                }
                memset(message, 0, sizeof(message));
            }
        }        
        else if (strcmp(args[0], "w24fz") == 0)
	{
    		printf("File Search Function Invoked\n");
    		char size1_str[20];
	    	char size2_str[20];
	    	long size1 = atol(args[1]);
	    	long size2 = atol(args[2]);

  	    	// Convert size1 and size2 from long to string for the command
	    	snprintf(size1_str, sizeof(size1_str), "%ldc", size1); // 'c' specifies bytes in find command
	    	snprintf(size2_str, sizeof(size2_str), "%ldc", size2);

	    	char home_dir[1024];
	    	snprintf(home_dir, sizeof(home_dir), "%s", getenv("HOME"));

	    	// Call sgetfiles with the converted string sizes and the client socket
	    	// The sgetfiles function itself will handle file sending or sending "No file found"
	    	sgetfiles(home_dir, "temp.tar.gz", size1_str, size2_str, client_socket);
	}

        else if (strcmp(args[0], "w24ft") == 0)
        {
            printf("Generate TAR Files Function Invoked\n");
            char **extensions = malloc((num_args - 1) * sizeof(char *));
            for (int i = 1; i < num_args; i++)
            {
                extensions[i - 1] = args[i];
            }
            int num_extensions = num_args - 1;

            if (num_args == 4)
            {
                num_extensions--;
            }

            char tar_filename[] = "temp.tar.gz";
            generate_tar_gz_from_files_with_extensions(tar_filename, extensions, num_extensions);
            file_transfer = 1;
            if ((file_fd = open("temp.tar.gz", O_RDONLY)) == -1)
            {
                perror("open");
                exit(EXIT_FAILURE);
            }
       	     ssize_t read_bytes;
             while ((read_bytes = read(file_fd, buffer, BUFFER_SIZE)) > 0)
             {
                 if (send(client_socket, buffer, read_bytes, 0) == -1)
                 {
                     perror("send");
                     exit(EXIT_FAILURE);
                 }
             }
             close(file_fd);
        }
        
        else if (strcmp(args[0], "w24fdb") == 0) {
	    if (num_args != 2) {
	        printf("Usage: w24fdb date\n");
	    } else {
	    	printf("Search Before Date Function Invoked\n");
	    	char home_dir[1024];
            	snprintf(home_dir, sizeof(home_dir), "%s", getenv("HOME"));
        	dgetfiles_before(home_dir, "temp.tar.gz", args[1], client_socket);
    		}
	} 
	else if (strcmp(args[0], "w24fda") == 0) {
    	if (num_args != 2) {
        	printf("Usage: w24fda date\n");
    	} else {
    		printf("Search After Date Function Invoked\n");
        	char home_dir[1024];
        	snprintf(home_dir, sizeof(home_dir), "%s", getenv("HOME"));
        	dgetfiles_after(home_dir, "temp.tar.gz", args[1], client_socket);
    		}
	}

	else if (strcmp(args[0], "dirlist") == 0 && strcmp(args[1], "-a") == 0) {
		printf("List Directories By File Name Invoked\n");
    		list_subdirectories(client_socket);
	}

	else if (strcmp(args[0], "dirlist") == 0 && strcmp(args[1], "-t") == 0) {
		printf("List Directories By File Date Invoked\n");
    		list_subdirectories_by_time(client_socket);
	}

        else if (strcmp(args[0], "quitc") == 0)
        {
            printf("Client requested to exit\n");
            break;
        }
        
        if (file_transfer)
        {
            printf("Transfering file to client\n");
            send_file(client_socket, "temp.tar.gz");
        }
    }
    close(client_socket);
}
int main()
{
    int mirror_socket, client_socket, opt = 1;
    struct sockaddr_in server_addr, client_addr;
    char buffer[BUFFER_SIZE];
    int bytes_received, addrlen = sizeof(client_addr);
    int clients_count = 0;

    // Create a socket
    mirror_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (mirror_socket == -1)
    {
        perror("Error: Failed to create mirror socket");
        exit(1);
    }

    // Configure server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(SERVER_IP);
    server_addr.sin_port = htons(SERVER_PORT);

    // Set socket options to reuse address
    if (setsockopt(mirror_socket, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)))
    {
        perror("Error: Failed to set socket options");
        exit(1);
    }

    // Bind the socket to the server address
    if (bind(mirror_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1)
    {
        perror("Error: Failed to bind mirror socket to server address");
        exit(1);
    }

    // Listen for incoming client connections
    if (listen(mirror_socket, 4) == -1)
    {
        perror("Error: Failed to listen for incoming connections");
        exit(1);
    }

    printf("Mirror server listening on %s:%d\n", SERVER_IP, SERVER_PORT);

    // Accept client connections and mirror incoming data to standard output
    while (1)
    {
        // Accept incoming client connections
        client_socket = accept(mirror_socket, (struct sockaddr *)&client_addr, (socklen_t *)&addrlen);
        if (client_socket == -1)
        {
            perror("Error: Failed to accept incoming client connection");
            continue;
        }

        printf("Mirror server accepted incoming client connection from %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        pid_t pid = fork();
        if (pid == -1)
        {
            close(client_socket);
            perror("Error: Coult not fork to handle client");
            continue;
        }
        else if (pid == 0)
        {
            // Child process
            printf("Client %d handled by the mirror\n", clients_count + 1);
            close(mirror_socket); // Close unused server socket
            crequest(client_socket);
            exit(EXIT_SUCCESS);
        }
        else
        {
            clients_count++; // Increment count of clients.
            printf("No. of Clients handled: %d\n", clients_count);
            // Parent process
            close(client_socket); // Close unused client socket
        }
    }

    // Close the mirror server socket
    close(mirror_socket);

    return 0;
}
