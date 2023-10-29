// Author: Paulo Batitay
// A shell with three built-in commands (cd, path, exit)
// Other programs if not built-in are fetched from /bin or user specified path
// Forks and pipes were implemented to run each command requested by the user

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>
#include <ctype.h>

// shell modes
#define INTERACTIVE_MODE      0      // user writes commands into shell
#define BATCH_MODE            1      // user specifies files with commands

// child messages to parent processes
#define MESSAGE_TYPE_CD       1       // cd-specific messages via pipe
#define MESSAGE_TYPE_PATH     2       // path-specific messages via pipe

// child exit types
#define CHILD_EXIT_REQUEST    20 
#define CHILD_SYSTEM_ERROR    21 

// global definitions
#define MAX_PARALLEL_COMMANDS 100   // max amount possible to execute parallel commands (&)

// global vars
const char ERROR_MESSAGE[30] = "An error has occurred\n";
char *INIT_SHELL_PATH = "/bin";

// ---------------------
// Function Declarations
// ---------------------
// helper function declarations
void check_shell_mode(int args, char *shell_mode);
void open_batch_file(FILE **fp, char *file);
void modify_path(char *buffer, char ***shell_path, int *num_path);
void remove_lead_and_trailing_whitespaces(char **buffer);
void check_child_message_and_close_pipes(int command_count, int pipes[][2], char **buffer, char ***shell_path, int *num_path);
char check_builtin_cmd(char *buffer);
void run_builtin_cmd(char **buffer, int child_to_parent[2], char ***shell_path, int *num_path);
void run_extern_cmd(char **buffer, char ***shell_path, int *num_path);

// processes function declarations
void cmd_process(char **buffer, int *count, int pipes[][2], pid_t children[], char ***shell_path, int *num_path, FILE **fp);
void cmd_parse_processes(char **buffer, char ***shell_path, int *num_path, FILE **fp);

// deallocator function declarations
void deallocate_string(char **buffer);
void deallocate_shell_path(char ***shell_path, int *num_path);
void attempt_close_batch(FILE **fp);

// exit function declarations
void wait_and_check_child_exit_status(int command_count, pid_t children[], char** buffer, char ***shell_path, int *num_path, FILE **fp);
void system_error(const char *message);
void shell_error(const char *message);
void shell_system_error(const char* message);

int main(int argc, char *argv[])
{
    // shell mode flag: INTERACTIVE OR BATCH
    char shell_mode;
    check_shell_mode(argc, &shell_mode);

    // attempt to open batch file if shell is in batch mode
    FILE *fp = NULL;
    if (shell_mode == BATCH_MODE) { open_batch_file(&fp, argv[1]); }

    // set initial shell path
    int num_path = 0;
    char **shell_path = NULL;
    modify_path(INIT_SHELL_PATH, &shell_path, &num_path);

    // dynamnic command buffer init
    char *command_buffer = NULL;
    size_t len_buffer = 0;

    while (1)
    {
        // read command line from file or stin
        if (shell_mode == BATCH_MODE)
        {
            if (getline(&command_buffer, &len_buffer, fp) == -1) 
            { 
                attempt_close_batch(&fp);
                deallocate_string(&command_buffer);
                deallocate_shell_path(&shell_path, &num_path);
                break; 
            }
        }
        else
        {
            printf("wish> ");
            if (getline(&command_buffer, &len_buffer, stdin) == -1) { system_error(ERROR_MESSAGE); }
        }

        // remove leading spaces and trailing newline
        remove_lead_and_trailing_whitespaces(&command_buffer);

        // check for ampersand operator and decide on which type of process to run
        if (command_buffer && command_buffer[0] != '\0' && command_buffer[0] != '\r' && command_buffer[0] != '\n')
        {
            if (strstr(command_buffer, "&"))
            {
                // there are several commands found
                cmd_parse_processes(&command_buffer, &shell_path, &num_path, &fp);
            }
            else
            {
                // only single command is needed to be ran
                int pipe[1][2];                 // one pair of file descriptors
                pid_t child[1];                 // one child needed only for a single command
                int count = 0;                  // run one single command
                cmd_process(&command_buffer, &count, pipe, child, &shell_path, &num_path, &fp);
                wait_and_check_child_exit_status(count, child, &command_buffer, &shell_path, &num_path, &fp);
                check_child_message_and_close_pipes(count, pipe, &command_buffer, &shell_path, &num_path);
            }
        }

        len_buffer = 0;
        deallocate_string(&command_buffer);
    }

    return 0;
}

// ---------------------
// Function Definitions
// ---------------------
// checks shell mode given the number of arguments given in program launch
void check_shell_mode(int args, char *shell_mode)
{
    // argument check for shell mode
    if (args < 2) 
    {
        *shell_mode = INTERACTIVE_MODE;
    } else if (args < 3) 
    {
        *shell_mode = BATCH_MODE;
    } else 
    {
        system_error(ERROR_MESSAGE);
    }
}

// attempts to open batch file
void open_batch_file(FILE **fp, char *file)
{
    if ((*fp = fopen(file, "r")) == NULL)
    {
        system_error(ERROR_MESSAGE);
    }
}

// modify the program path and readjust the list if needed
void modify_path(char *buffer, char ***shell_path, int *num_path)
{
    char *token = strtok(buffer, " ");
    if (token != NULL) { token = strtok(NULL, " "); }
    if (*shell_path == NULL && token == NULL)
    {
        // shell path has not yet been created and we just want to put first index in
        (*num_path)++;
        *shell_path = (char **)realloc(*shell_path, (*num_path)*sizeof(char *));
        if (*shell_path == NULL)
        {
            return;
        }
        else
        {
            (*shell_path)[(*num_path) - 1] = strdup(buffer);
        }
    }
    else if (token == NULL || token[0] == '\0' || token[0] == '\n')
    {
        // handle command is "path" w/ no arguments
        free((*shell_path)[0]);
        (*shell_path)[0] = strdup("");
        for (int i = 1; i < *num_path; i++)
        {
            free((*shell_path)[i]);
            (*shell_path)[i] = NULL;
        }
        *num_path = 1;
    }
    else
    {
        // handle path with multiple arguments
        int path_counter = 0;
        while (token != NULL)
        {
            if (path_counter < *num_path)
            {
                // overwrite the current path; first index
                free((*shell_path)[path_counter]);
                (*shell_path)[path_counter] = NULL;
                (*shell_path)[path_counter++] = strdup(token);
            }
            else
            {
                // else if need to add more paths, add num_paths and increase size of shell path
                *shell_path = (char **)realloc(*shell_path, (*num_path + 1) * sizeof(char *));
                (*shell_path)[path_counter] = strdup(token);
                path_counter++;
                (*num_path)++;
            }
            token = strtok(NULL, " ");
        }

        // if there are extra paths that are not used, deallocate them
        for (int i = path_counter; i < *num_path; i++)
        {
            free((*shell_path)[i]);
            (*shell_path)[i] = NULL;
        }
        *num_path = path_counter;
    }
}

// removes all leading and trailing whitespaces
void remove_lead_and_trailing_whitespaces(char **buffer)
{
    if (!buffer || !*buffer || !**buffer) {
        return;
    }

    // make copy
    char *str = *buffer;

    // move pointer until first non-whitespace character
    char *buffer_copy = str;
    while (*buffer_copy && isspace((unsigned char)*buffer_copy)) 
    {
        buffer_copy++;
    }

    // shift the string after dealing with its leading whitespaces
    if (buffer_copy != str) 
    {
        size_t length = strlen(buffer_copy) + 1;
        memmove(str, buffer_copy, length);
    }

    // remove trailing whitespaces
    char *end = str + strlen(str) - 1;
    while (end >= str && isspace((unsigned char)*end)) 
    {
        *end-- = '\0';
    }
}

// awaits messages (if any) from the child process. This usually means a request 
// from the child for parent to change directories or modify the shell path
void check_child_message_and_close_pipes(int command_count, int pipes[][2], char **buffer, char ***shell_path, int *num_path) 
{
    for (int i = 0; i < command_count; i++)
    {
        // check if children wrote to parent
        char message_type;
        char new_dir[512];
        ssize_t bytesRead = read(pipes[i][0], &message_type, sizeof(char));
        if (bytesRead > 0) 
        {
            switch (message_type) 
            {
                case MESSAGE_TYPE_CD:
                    // read the directory and change to it
                    read(pipes[i][0], new_dir, sizeof(new_dir));
                    chdir(new_dir);
                    break;
                case MESSAGE_TYPE_PATH:
                    // handle path modification
                    modify_path(*buffer, shell_path, num_path);
                    break;
            }
        }
        close(pipes[i][0]);  // close read end of parent pipe
    }
}

// checks if the program is built-in or not
char check_builtin_cmd(char *buffer)
{
    if ((strcmp(buffer, "exit") == 0) ||
        (strcmp(buffer, "cd") == 0) ||
        (strcmp(buffer, "path") == 0))
        {
            return 1;
        }
        else
        {
            return 0;
        }
}

// attempt to run built in programs
void run_builtin_cmd(char **buffer, int child_to_parent[2], char ***shell_path, int *num_path)
{
    char *buffer_copy = (char *)malloc(strlen(*buffer) + 1);
    strcpy(buffer_copy, *buffer);
    char *token = strtok(buffer_copy, " ");
    if (strcmp(token, "exit") == 0 && strcmp(*buffer, token) == 0)
    {
        deallocate_string(&buffer_copy);
        deallocate_shell_path(shell_path, num_path);
        deallocate_string(buffer);
        _exit(CHILD_EXIT_REQUEST);
    }
    else if (strcmp(token, "cd") == 0)
    {
        token = strtok(NULL, " ");
        if (token && strtok(NULL, " ") == NULL)
        {
            if (chdir(token) != 0)
            {
                shell_error(ERROR_MESSAGE);
            }
            else
            {
                char message_type = MESSAGE_TYPE_CD;
                if (write(child_to_parent[1], &message_type, sizeof(char)) == -1)
                {
                    deallocate_string(&buffer_copy);
                    deallocate_shell_path(shell_path, num_path);
                    deallocate_string(buffer);
                    shell_system_error(ERROR_MESSAGE);
                }
                if (write(child_to_parent[1], token, strlen(token) + 1) == -1)
                {
                    deallocate_string(&buffer_copy);
                    deallocate_shell_path(shell_path, num_path);
                    deallocate_string(buffer);
                    shell_system_error(ERROR_MESSAGE);
                }
            }
        }
        else
        {
            shell_error(ERROR_MESSAGE);
        }
    }
    else if (strcmp(token, "path") == 0)
    {
        // send flag to parent to modify path
        char message_type = MESSAGE_TYPE_PATH;
        if (write(child_to_parent[1], &message_type, sizeof(char)) == -1)
        {
            deallocate_string(&buffer_copy);
            deallocate_string(buffer);
            deallocate_shell_path(shell_path, num_path);
            shell_system_error(ERROR_MESSAGE);
        }
    }
    else
    {
        shell_error(ERROR_MESSAGE);
    }
    deallocate_string(&buffer_copy);
}

// attempt to run built-in commands
void run_extern_cmd(char **buffer, char ***shell_path, int *num_path)
{
    char *args[512];
    int i = 0;

    // search for the redirection operator
    char *redirect_char = strchr(*buffer, '>');
    char *filename = NULL;

    if (redirect_char) 
    {
        // split command and filename
        *redirect_char = '\0';
        filename = redirect_char + 1;

        while (*filename && isspace((unsigned char)*filename)) 
        {
            filename++;
        }

        // check if filename is empty after '>'
        if (!*filename) 
        {
            shell_error(ERROR_MESSAGE);
            deallocate_string(buffer);
            deallocate_shell_path(shell_path, num_path);
            _exit(0);
        }
        
        // check if there are spaces after the filename, indicating multiple files
        if (strchr(filename, ' ')) 
        {
            shell_error(ERROR_MESSAGE);
            deallocate_string(buffer);
            deallocate_shell_path(shell_path, num_path);
            _exit(0);
        }

        char *filename_final = strdup(filename);
        remove_lead_and_trailing_whitespaces(&filename_final);
        
        int fd = open(filename_final, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd == -1) 
        {
            deallocate_string(buffer);
            deallocate_shell_path(shell_path, num_path);
            deallocate_string(&filename_final);
            shell_system_error(ERROR_MESSAGE);
        }

        // rout stdout to file
        if (dup2(fd, STDOUT_FILENO) == -1) 
        {
            deallocate_string(buffer);
            deallocate_shell_path(shell_path, num_path);
            close(fd);
            deallocate_string(&filename_final);
            shell_system_error(ERROR_MESSAGE);
        }
        close(fd);
        deallocate_string(&filename_final);
    }

    // continue getting command args
    char *token = strtok(*buffer, " ");
    while (token) 
    {
        args[i++] = token;
        token = strtok(NULL, " ");
    }
    args[i] = NULL;  // NULL terminate the args array

    char executable_path[512];
    int command_found = 0;

    // make valid program path string given shell path and program name
    for(i = 0; i < *num_path; i++) 
    {
        snprintf(executable_path, sizeof(executable_path), "%s/%s", (*shell_path)[i], args[0]);
        if(access(executable_path, X_OK) == 0) 
        {
            command_found = 1;
            break;
        }
    }

    if (!command_found) 
    {
        shell_error(ERROR_MESSAGE);
        deallocate_string(buffer);
        deallocate_shell_path(shell_path, num_path);
        _exit(0); 
    }

    /* IMPORTANT: child's copy of buffer and shell_path will not be deallocated manually if execv runs */
    /* Valgrind may this as a "still reachable" memory leak even if the OS will cleanly reclaim the child's memory */
    /* May need to create Valgrind suppresion file for this case */
    execv(executable_path, args);

    // if execv returns, there was an error
    deallocate_string(buffer);
    deallocate_shell_path(shell_path, num_path);
    shell_system_error(ERROR_MESSAGE);
}

// run a single process
void cmd_process(char **buffer, int *count, int pipes[][2], pid_t children[], char ***shell_path, int *num_path, FILE **fp)
{
    // open a pipe
    if (pipe(pipes[*count]) == -1)
    {
        attempt_close_batch(fp);
        deallocate_string(buffer);
        deallocate_shell_path(shell_path, num_path);
        system_error(ERROR_MESSAGE);
    }

    // attempt fork
    pid_t child = fork();
    if (child == -1)
    {
        attempt_close_batch(fp);
        deallocate_string(buffer);
        deallocate_shell_path(shell_path, num_path);
        system_error(ERROR_MESSAGE);
    }

    if (child == 0)
    {
        // close child's copy of file stream
        attempt_close_batch(fp);

        // close reading end pipe of child
        close(pipes[*count][0]);

        // make copy of buffer for safe tokenization
        char* buffer_copy = (char *)malloc(strlen(*buffer) + 1);
        strcpy(buffer_copy, *buffer);
        char *program_name = strtok(buffer_copy, " ");

        // check which command-running method to do
        // deallocate buffer copy until after checking program name
        if (check_builtin_cmd(program_name))
        {
            deallocate_string(&buffer_copy);    // deallocate copy here because we exit immediately if "EXIT" cmd
            run_builtin_cmd(buffer, pipes[*count], shell_path, num_path);
        }
        else
        {
            deallocate_string(&buffer_copy);    // deallocate copy here because execv replaces the child process
            run_extern_cmd(buffer, shell_path, num_path);
        }

        deallocate_string(buffer);
        deallocate_shell_path(shell_path, num_path);
        close(pipes[*count][1]);    // close write end pipe of child
        _exit(0);
    }
    else
    {
        close(pipes[*count][1]);    // close write end pipe of parent
        children[*count] = child;   // update child process list
        (*count)++;     // increment amount of command seen/executed
    }
}

// parse multiple processes and attempts to run them
void cmd_parse_processes(char **buffer, char ***shell_path, int *num_path, FILE **fp)
{
    char* token;
    int command_count = 0;
    int pipes[MAX_PARALLEL_COMMANDS][2];
    pid_t children[MAX_PARALLEL_COMMANDS];

    // we already know that the buffer only has commands with '&' due to if else in "int main()"
    token = strtok(*buffer, "&");
    while (token != NULL)
    {
        remove_lead_and_trailing_whitespaces(&token);
        cmd_process(&token, &command_count, pipes, children, shell_path, num_path, fp);
        token = strtok(NULL, "&");
    }

    wait_and_check_child_exit_status(command_count, children, buffer, shell_path, num_path, fp);
    check_child_message_and_close_pipes(command_count, pipes, buffer, shell_path, num_path);
}

// deallocates and nullifies a string
void deallocate_string(char **buffer)
{
    free(*buffer);
    *buffer = NULL;
}

// deallocates the shell path/added start for maybe use with modify path?
void deallocate_shell_path(char ***shell_path, int *num_path)
{
    for (int i = 0; i < *num_path; i++)
    {
        free((*shell_path)[i]);
        (*shell_path)[i] = NULL;
    }
    free(*shell_path);
    *shell_path = NULL;
    *num_path = 0;
}

// attempts to close batch file if opened
void attempt_close_batch(FILE **fp)
{
    if (*fp != NULL)
    {
        fclose(*fp);
    }
}

// check child exit status and cleanly deallocate if child requested to exit or serious system error occured in child
void wait_and_check_child_exit_status(int command_count, pid_t children[], char **buffer, char ***shell_path, int *num_path, FILE **fp)
{
    for (int i = 0; i < command_count; i++)
    {
        // wait for each child and get their statuses
        int child_status;
        waitpid(children[i], &child_status, 0);

        // terminate whole program if serious system errors such as pipe/allocation error happened in child
        // exit gracefully if exit command is requested
        if (WIFEXITED(child_status))
        {
            if (WEXITSTATUS(child_status) == CHILD_SYSTEM_ERROR)
            {
                attempt_close_batch(fp);
                deallocate_string(buffer);
                deallocate_shell_path(shell_path, num_path);
                exit(1);
            }
            else if (WEXITSTATUS(child_status) == CHILD_EXIT_REQUEST)
            {
                attempt_close_batch(fp);
                deallocate_string(buffer);
                deallocate_shell_path(shell_path, num_path);
                exit(0);
            }
        }
    }
    return;
}

// displays shell system error message and exits with 1 status in child
// used for allocating, fork, piping, file methods failure in child process
void shell_system_error(const char* message)
{
    write(STDERR_FILENO, message, strlen(message)); 
    _exit(CHILD_SYSTEM_ERROR);
}

// main process system error for allocating, fork, piping, file methods failure in main process
void system_error(const char *message)
{
    write(STDERR_FILENO, message, strlen(message)); 
    exit(1);
}

// displays shell error message but does not exit a process
void shell_error(const char *message)
{
    write(STDERR_FILENO, message, strlen(message)); 
}