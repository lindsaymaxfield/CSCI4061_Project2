#define _GNU_SOURCE

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "job_list.h"
#include "string_vector.h"
#include "swish_funcs.h"

#define CMD_LEN 512
#define PROMPT "@> "

int main(int argc, char **argv) {
    struct sigaction sac;
    sac.sa_handler = SIG_IGN;
    if (sigfillset(&sac.sa_mask) == -1) {
        perror("sigfillset");
        return 1;
    }
    sac.sa_flags = 0;
    if (sigaction(SIGTTIN, &sac, NULL) == -1 || sigaction(SIGTTOU, &sac, NULL) == -1) {
        perror("sigaction");
        return 1;
    }

    strvec_t tokens;
    strvec_init(&tokens);
    job_list_t jobs;
    job_list_init(&jobs);
    char cmd[CMD_LEN];

    printf("%s", PROMPT);
    while (fgets(cmd, CMD_LEN, stdin) != NULL) {
        // Need to remove trailing '\n' from cmd. There are fancier ways.
        int i = 0;
        while (cmd[i] != '\n') {
            i++;
        }
        cmd[i] = '\0';

        if (tokenize(cmd, &tokens) != 0) {
            printf("Failed to parse command\n");
            strvec_clear(&tokens);
            job_list_free(&jobs);
            return 1;
        }
        if (tokens.length == 0) {
            printf("%s", PROMPT);
            continue;
        }
        const char *first_token = strvec_get(&tokens, 0);

        // Get the shell's current working directory
        if (strcmp(first_token, "pwd") == 0) {
            char dir_name[CMD_LEN];
            if (getcwd(dir_name, CMD_LEN) == NULL) {
                perror("getcwd");
                strvec_clear(&tokens);
                job_list_free(&jobs);
                return 1;
            }

            printf("%s\n", dir_name);
        }

        // Change the shell's current working directory
        else if (strcmp(first_token, "cd") == 0) {
            char *home_dir = getenv("HOME");
            if (home_dir == NULL) {
                printf("Failed to get home directory\n");
            }
            char *directory = strvec_get(&tokens, 1);
            if (directory == NULL) {
                if (chdir(home_dir) == -1) {
                    perror("chdir");
                }
            } else {
                if (chdir(directory) == -1) {
                    perror("chdir");
                }
            }
        }

        else if (strcmp(first_token, "exit") == 0) {
            strvec_clear(&tokens);
            break;
        }

        // Print out current list of pending jobs
        else if (strcmp(first_token, "jobs") == 0) {
            int i = 0;
            job_t *current = jobs.head;
            while (current != NULL) {
                char *status_desc;
                if (current->status == BACKGROUND) {
                    status_desc = "background";
                } else {
                    status_desc = "stopped";
                }
                printf("%d: %s (%s)\n", i, current->name, status_desc);
                i++;
                current = current->next;
            }
        }

        // Move stopped job into foreground
        else if (strcmp(first_token, "fg") == 0) {
            if (resume_job(&tokens, &jobs, 1) == -1) {
                printf("Failed to resume job in foreground\n");
            }
        }

        // Move stopped job into background
        else if (strcmp(first_token, "bg") == 0) {
            if (resume_job(&tokens, &jobs, 0) == -1) {
                printf("Failed to resume job in background\n");
            }
        }

        // Wait for a specific job identified by its index in job list
        else if (strcmp(first_token, "wait-for") == 0) {
            if (await_background_job(&tokens, &jobs) == -1) {
                printf("Failed to wait for background job\n");
            }
        }

        // Wait for all background jobs
        else if (strcmp(first_token, "wait-all") == 0) {
            if (await_all_background_jobs(&jobs) == -1) {
                printf("Failed to wait for all background jobs\n");
            }
        }

        else {
            const char *last_token = strvec_get(&tokens, tokens.length - 1);
            if (strcmp(last_token, "&") == 0) {
                strvec_take(&tokens, tokens.length - 1);
                pid_t pid = fork();
                if (pid < 0) {    // an error occurred
                    perror("fork");
                    strvec_clear(&tokens);
                    job_list_free(&jobs);
                    return 1;
                } else if (pid == 0) {    // child process
                    run_command(&tokens);
                    strvec_clear(&tokens);
                    job_list_free(&jobs);
                    exit(1);
                } else {    // parent process
                    job_list_add(&jobs, pid, first_token, BACKGROUND);
                }

            } else {
                int status;
                pid_t pid = fork();
                if (pid < 0) {    // an error occurred
                    perror("fork");
                    strvec_clear(&tokens);
                    job_list_free(&jobs);
                    return 1;
                } else if (pid == 0) {    // child process
                    run_command(&tokens);
                    strvec_clear(&tokens);
                    job_list_free(&jobs);
                    exit(1);
                } else {    // parent process
                    // put the child's process group in the foreground
                    if (tcsetpgrp(STDIN_FILENO, pid) == -1) {
                        perror("tcsetpgrp");
                        strvec_clear(&tokens);
                        job_list_free(&jobs);
                        return 1;
                    }
                    // waits for child process to terminate
                    if (waitpid(pid, &status, WUNTRACED) == -1) {
                        perror("waitpid");
                        strvec_clear(&tokens);
                        job_list_free(&jobs);
                        return 1;
                    }
                    if (WIFSTOPPED(status)) {
                        if (job_list_add(&jobs, pid, first_token, STOPPED) == -1) {
                            printf("Failed to add to job list\n");
                            strvec_clear(&tokens);
                            job_list_free(&jobs);
                            return 1;
                        }
                    }

                    pid_t ppid = getpid();
                    // restore the shell process to the foreground
                    if (tcsetpgrp(STDIN_FILENO, ppid) == -1) {
                        perror("tcsetpgrp");
                        strvec_clear(&tokens);
                        job_list_free(&jobs);
                        return 1;
                    }
                }
            }
        }

        strvec_clear(&tokens);
        printf("%s", PROMPT);
    }

    job_list_free(&jobs);
    return 0;
}
