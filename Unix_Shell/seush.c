#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <ctype.h>

const char error_msg[] = "An error has occurred\n";

static char **path_dirs = NULL;
static int path_count = 0;

void execute(char *cmd, int should_wait);
void print_error();
void init_path();
void free_path();
static void set_path(char **dirs, int ndirs);
void split_parallel_commands(char *line);
void execute(char *cmd_str, int wait_falg);
void parse_and_execute(char *line);

int main(int argc, char *argv[]){
    init_path();
    FILE *input = stdin;
    int interactive = 1;

    if (argc > 2) {
        print_error();
        free_path();
        exit(1);
    }
    if (argc == 2) {
        interactive = 0;
        input = fopen(argv[1], "r");
        if (input == NULL) {
            print_error();
            free_path();
            exit(1);
        }
    }

    char *line = NULL;
    size_t len = 0;
    ssize_t nread;

    while (1) {
        if (interactive) {
            printf("seush> ");
            fflush(stdout);
        }

        nread = getline(&line, &len, input);
        if (nread == -1) {
            free(line);
            free_path();
            exit(0);
        }

        parse_and_execute(line);
    }

    free(line);
    free_path();
    return 0;
}

void print_error() {
    write(STDERR_FILENO, error_msg, strlen(error_msg));
}

void init_path() {
    path_dirs = malloc(sizeof(char*));
    if (path_dirs)
        path_dirs[0] = strdup("/bin");
    path_count = 1;
}

void free_path() {
    if (path_dirs) {
        for (int i = 0; i < path_count; i++)
            free(path_dirs[i]);
        free(path_dirs);
        path_dirs = NULL;
    }
}

int find_executable(const char *cmd, char *fullpath, int size) {
    if (strchr(cmd, '/')) {
        if (access(cmd, X_OK) == 0) {
            strncpy(fullpath, cmd, size);
            fullpath[size - 1] = '\0';
            return 1;
        }
        return 0;
    }
    for (int i = 0; i < path_count; i++) {
        snprintf(fullpath, size, "%s/%s", path_dirs[i], cmd);
        if (access(fullpath, X_OK) == 0)
            return 1;
    }
    return 0;
}

void split_parallel_commands(char *line) {
    char *cmd[64];
    int cmd_count = 0;
    char *p = line;
    while (*p && cmd_count < 64) {
        char *start = p;
        while (*p && *p != '&') p++;
        char saved = *p;
        *p = '\0';
        if (p > start)
            cmd[cmd_count++] = start;
        if (saved == '&')
            p++;
    }
    if (cmd_count == 0) return;

    if (cmd_count == 1) {
        execute(cmd[0], 1);
        return;
    }

    for (int s = 0; s < cmd_count; s++)
        execute(cmd[s], 0);

    while (waitpid(-1, NULL, 0) > 0);
}

static void set_path(char **dirs, int ndirs) {
    for (int i = 0; i < path_count; i++)
        free(path_dirs[i]);
    path_count = ndirs;
    path_dirs = realloc(path_dirs, sizeof(char*) * (ndirs > 0 ? ndirs : 1));
    for (int i = 0; i < ndirs; i++)
        path_dirs[i] = strdup(dirs[i]);
}

void execute(char *cmd_str, int wait_falg) {
    char processed[1024];
    int i,j;
    for(i=0,j=0;cmd_str[i];i++){
        if(cmd_str[i]=='>'){
            if(j>0 && processed[j-1]!=' ' && processed[j-1]!='\t')
                processed[j++]=' ';
            processed[j++]='>';
            if(cmd_str[i+1] && cmd_str[i+1]!=' ' && cmd_str[i+1]!='\t')
                processed[j++]=' ';
        } 
        else {
            processed[j++]=cmd_str[i];
        }
    }
    processed[j]='\0';

    char *tokens[128];
    int ntokens = 0;
    char *p = strtok(processed, " \t");
    while (p && ntokens < 128) {
        tokens[ntokens++] = p;
        p = strtok(NULL, " \t");
    }
    if (ntokens == 0) return;

    char *argv[128];
    int argc = 0;
    char *redirect_file = NULL;
    int error = 0;

    for (int i = 0; i < ntokens && !error; i++) {
        if (strcmp(tokens[i], ">") == 0) {
            if (redirect_file) { print_error(); error = 1; break; }
            i++;
            if (i >= ntokens) { print_error(); error = 1; break; }
            redirect_file = tokens[i];
        } else {
            if (redirect_file) { print_error(); error = 1; break; }
            argv[argc++] = tokens[i];
        }
    }
    if (error) return;
    if (argc == 0) {
        if (redirect_file) print_error();
        return;
    }

    argv[argc] = NULL;

    if (strcmp(argv[0], "exit") == 0) {
        if (argc != 1 || redirect_file) print_error();
        else { free_path(); exit(0); }
        return;
    }
    if (strcmp(argv[0], "cd") == 0) {
        if (argc != 2 || redirect_file) print_error();
        else if (chdir(argv[1]) != 0) print_error();
        return;
    }
    if (strcmp(argv[0], "path") == 0) {
        if (redirect_file) { print_error(); return; }
        set_path(argc > 1 ? &argv[1] : NULL, argc - 1);
        return;
    }

    char fullpath[1024];
    if (!find_executable(argv[0], fullpath, sizeof(fullpath))) {
        print_error();
        return;
    }

    pid_t pid = fork();
    if (pid == 0) {
        if (redirect_file) {
            int fd = open(redirect_file, O_WRONLY | O_CREAT | O_TRUNC, 0666);
            if (fd < 0) { print_error(); exit(1); }
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            close(fd);
        }
        execv(fullpath, argv);
        print_error();
        exit(1);
    } else if (pid < 0) {
        print_error();
        return;
    }

    if (wait_falg) {
        waitpid(pid, NULL, 0);
        return;
    }
}

void parse_and_execute(char *line) {
    line[strcspn(line, "\n")] = '\0';//结尾去除换行

    for (char *p = line; *p; p++) {
        if (*p != ' ' && *p != '\t') { 
            split_parallel_commands(line); 
            return; 
        }
    }
}

