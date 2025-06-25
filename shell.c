#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <linux/limits.h> 
#include <readline/readline.h>
#include <readline/history.h>

#define TOK_DELIM " \t\r\n"
#define PATH_MAX  4096   
#define PIPE_DELIM "|"

char **tokenize(char *line);
int execute(char **args);
int shell_exit(char **args);
void startup_banner(); 
int pipeIt(char *line);
int chainIt(char *line);
void process_logical_chain(char *line);
int should_run(int prev_status, char *op);

void loop() {
    char *line;
    char **args;
    int status = 1;

    do {
        char cwd[PATH_MAX];
        getcwd(cwd, sizeof(cwd));

        char prompt[PATH_MAX + 50];
        snprintf(prompt, sizeof(prompt), "\033[1;36mHy-shell\033[0m:\033[1;34m~%s\033[0m$ ", cwd);

        line = readline(prompt);

        if (!line) {
            printf("\n");
            break; 
        }

        if(strchr(line, '|') != NULL) {
            pipeIt(line);
            free(line);
            continue;
        }

        if(strstr(line, "&&") || strstr(line, "||") || strchr(line, ';')) {
            chainIt(line);
            free(line);
            continue;
        }

        if (*line) {
            add_history(line);
            args = tokenize(line); // convert to tokens
            status = execute(args); // execute the comm
            free(args);
        }

        free(line);

    } while (status);
}

int chainIt(char *line) {
    char *segment = strtok(line, ";");
    while (segment != NULL) {
        process_logical_chain(segment);  
        segment = strtok(NULL, ";");
    }
    return 1;
}

void process_logical_chain(char *line) {
    char *commands[100];
    char *operators[100];
    int cmd_count = 0, op_count = 0;

    char *p = line;
    while (*p) {
        while (*p == ' ') p++;

        char *op_ptr = strstr(p, "&&");
        char *or_ptr = strstr(p, "||");
        char *next_op = NULL;
        char op[3] = "";

        if (op_ptr && (!or_ptr || op_ptr < or_ptr)) {
            next_op = op_ptr;
            strcpy(op, "&&");
        } else if (or_ptr) {
            next_op = or_ptr;
            strcpy(op, "||");
        }

        if (next_op) {
            *next_op = '\0';  
            commands[cmd_count++] = strdup(p);
            operators[op_count++] = strdup(op);
            p = next_op + 2;
        } else {
            commands[cmd_count++] = strdup(p);
            break;
        }
    }

    int status = 0;
    for (int i = 0; i < cmd_count; i++) {
        char **args = tokenize(commands[i]);
        if (i == 0 || should_run(status, operators[i - 1])) {
            status = execute(args);
        }
        free(args);
        free(commands[i]);
        if (i > 0) free(operators[i - 1]);
    }
}

int should_run(int prev_status, char *op) {
    if (strcmp(op, "&&") == 0) return prev_status == 0;
    if (strcmp(op, "||") == 0) return prev_status != 0;
    return 1;
}



int pipeIt(char *line) {
    int i = 0;
    int pipes = 1;
    int bufferSize = 1024;
    char **comms = malloc(sizeof(char*) * bufferSize);
    char *comm;
    int position = 0;

    if (!comms) {
        perror("Allocation error!");
        exit(EXIT_FAILURE);
    }

    while (line[i]) {
        if (line[i] == '|') pipes++;
        i++;
    }

    comm = strtok(line, PIPE_DELIM);
    while (comm != NULL) {
        comms[position++] = comm;
        comm = strtok(NULL, PIPE_DELIM);
    }
    comms[position] = NULL;

    int fd[pipes - 1][2];
    for (int i = 0; i < pipes - 1; i++) {
        if (pipe(fd[i]) < 0) {
            perror("pipe");
            exit(EXIT_FAILURE);
        }
    }

    for (int i = 0; i < pipes; i++) {
        char **args = tokenize(comms[i]);
        pid_t pid = fork();

        if (pid < 0) {
            perror("fork");
            exit(EXIT_FAILURE);
        }

        if (pid == 0) {  
            if (i > 0) {
                dup2(fd[i - 1][0], STDIN_FILENO);
            }

            if (i < pipes - 1) {
                dup2(fd[i][1], STDOUT_FILENO);
            }

            for (int j = 0; j < pipes - 1; j++) {
                close(fd[j][0]);
                close(fd[j][1]);
            }

            execvp(args[0], args);
            perror("execvp failed");
            exit(EXIT_FAILURE);
        }

        free(args);  
    }

    for (int i = 0; i < pipes - 1; i++) {
        close(fd[i][0]);
        close(fd[i][1]);
    }

    for (int i = 0; i < pipes; i++) {
        wait(NULL);
    }

    free(comms);
    return 1;
}


char **tokenize(char *line) {
    int bufferSize = 64;
    char **tokens = malloc(sizeof(char*) * bufferSize);
    char *token;
    int position = 0;

    if (!tokens) {
        perror("Allocation error!");
        exit(EXIT_FAILURE);
    }

    token = strtok(line, TOK_DELIM);
    while (token != NULL) {
        tokens[position] = token;
        position++;

        if (position >= bufferSize) {
            bufferSize += 64;
            tokens = realloc(tokens, sizeof(char*) * bufferSize);
            if (!tokens) {
                perror("Allocation error!");
                exit(EXIT_FAILURE);
            }
        }

        token = strtok(NULL, TOK_DELIM);
    }
    tokens[position] = NULL;
    return tokens;
}

int is_command_available(const char *cmd) {
    char check_cmd[256];
    snprintf(check_cmd, sizeof(check_cmd), "command -v %s > /dev/null 2>&1", cmd);
    return system(check_cmd) == 0;
}


int execute(char **args) {
    if (args[0] == NULL) return 1;

    if (strcmp(args[0], "exit") == 0) return shell_exit(args);

    if (strcmp(args[0], "cd") == 0) {
        if (args[1] == NULL) {
            fprintf(stderr, "hyzen: expected argument to \"cd\"\n");
        } else {
            if (chdir(args[1]) != 0) perror("hyzen");
        }
        return 1;
    }

    if (strcmp(args[0], "help") == 0) {
        printf("HYZEN Shell — available commands:\n");
        printf("  cd <dir>        Change directory\n");
        printf("  help            Show this help message\n");
        printf("  exit            Exit the shell\n");
        printf("  echo <text>     Print text to terminal\n");
        printf("  hystat <app>    Show app's resource usage (PID, CPU, MEM, etc)\n");
        printf("  All other Linux commands via execvp()\n");
        return 1;
    }

    if (strcmp(args[0], "echo") == 0) {
        for (int i = 1; args[i] != NULL; i++) {
            printf("%s", args[i]);
            if (args[i + 1] != NULL) printf(" ");
        }
        printf("\n");
        return 1;
    }

    if (strcmp(args[0], "hystat") == 0) {
        if (args[1] == NULL) {
            fprintf(stderr, "hyzen: expected argument to \"hystat\"\n");
        } else {
            if (!is_command_available(args[1])) {
                fprintf(stderr, "hyzen: '%s' is not installed or not in PATH.\n", args[1]);
            } else {
                char command[256];
                snprintf(command, sizeof(command),
                         "ps -C %s -o pid,pcpu,pmem,etime,thcount", args[1]);
                system(command);
            }
        }
        return 1;
    }

    pid_t cpid;
    int status;

    cpid = fork();
    if (cpid < 0) {
        perror("Fork Failed!");
        exit(EXIT_FAILURE);
    } else if (cpid == 0) {
        if (execvp(args[0], args) < 0) {
            perror("hyzen");
            exit(EXIT_FAILURE);
        }
    } else {
        waitpid(cpid, &status, WUNTRACED);
    }

    return 1;
}



int shell_exit(char **args) {
    return 0;
}

void startup_banner() {
    printf("\033[1;36m");
    printf("=========================================\n");
    printf("           Welcome to HYZEN-Shell        \n");
    printf("         Your shell is now starting      \n");
    printf("=========================================\n");
    printf("\033[0m");
    sleep(1);
}

int main() {
    system("clear");
    startup_banner();
    loop();
    return EXIT_SUCCESS;
}

