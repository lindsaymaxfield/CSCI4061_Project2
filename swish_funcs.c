#define _GNU_SOURCE

#include "swish_funcs.h"

#include <assert.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "job_list.h"
#include "string_vector.h"

#define MAX_ARGS 10

int tokenize(char *s, strvec_t *tokens) {
    char *token = strtok(s, " ");    // specify the string to parse for the first call to strtok
    while (token != NULL) {
        if (strvec_add(tokens, token) == -1) {
            fprintf(stderr, "Failed to add token to tokens vector\n");
            return -1;
        }
        token = strtok(NULL, " ");    // call strtok repeatedly until NULL is returned
    }

    return 0;
}

int run_command(strvec_t *tokens) {
    pid_t pid = getpid();
    if (setpgid(pid, pid) == -1) {    // changing child's process group to the child's process ID
        perror("setpgid");
        return -1;
    }

    struct sigaction sac;

    sac.sa_handler = SIG_DFL;
    if (sigfillset(&sac.sa_mask) == -1) {
        perror("sigfillset");
        return -1;
    }

    sac.sa_flags = 0;
    if (sigaction(SIGTTIN, &sac, NULL) == -1 ||
        sigaction(SIGTTOU, &sac, NULL) ==
            -1) {    // set the child’s handlers for SIGTTIN and SIGTTOU back to the default
        perror("sigaction");
        return -1;
    }

    char *args[MAX_ARGS];    // string array to be filled from the tokens vector

    int get_next_token =
        1;        // boolean indicating if the next token should be retrieved from the tokens vector
    int i = 0;    // current index of the tokens vector

    while (
        get_next_token ==
        1) {    // loop that gets the arguments (not redirection operators) from the tokens vector
        char *curr_arg = strvec_get(tokens, i);
        if (curr_arg == NULL) {
            fprintf(stderr, "Failed to get token from tokens vector\n");
            return -1;
        }

        if (strcmp(curr_arg, ">") == 0 || strcmp(curr_arg, "<") == 0 ||
            strcmp(curr_arg, ">>") == 0) {
            get_next_token = 0;
        } else {
            args[i] = curr_arg;
            i++;
        }

        if (i == tokens->length) {
            get_next_token = 0;
        }
    }

    args[i] = NULL;    // adding NULL sentinel

    while (i < tokens->length) {    // loop that handles the redirection operators if applicable
        char *redir_token = strvec_get(tokens, i);
        if (redir_token == NULL) {
            fprintf(stderr, "Failed to get token from tokens vector");
        }
        i++;
        char *file_name = strvec_get(tokens, i);
        if (file_name == NULL) {
            fprintf(stderr, "Failed to get token from tokens vector");
        }
        i++;

        if (strcmp(redir_token, ">") == 0) {    // redirect output
            int out_fd = open(file_name, O_CREAT | O_TRUNC | O_WRONLY,
                              S_IRUSR | S_IWUSR);    // should overwrite file if it already exists
            if (out_fd == -1) {
                perror("Failed to open output file");
                return -1;
            } else if (dup2(out_fd, STDOUT_FILENO) == -1) {
                perror("dup2");
                close(out_fd);
                return -1;
            }
        } else if (strcmp(redir_token, "<") == 0) {    // redirect input
            int in_fd = open(file_name, O_RDONLY);
            if (in_fd == -1) {
                perror("Failed to open input file");
                return -1;
            } else if (dup2(in_fd, STDIN_FILENO) == -1) {
                perror("dup2");
                close(in_fd);
                return -1;
            }
        } else if (strcmp(redir_token, ">>") == 0) {    // redirect and append output
            int out_fd = open(file_name, O_CREAT | O_APPEND | O_WRONLY,
                              S_IRUSR | S_IWUSR);    // should append to file if it already exists
            if (out_fd == -1) {
                perror("Failed to open output file");
                return -1;
            } else if (dup2(out_fd, STDOUT_FILENO) == -1) {
                perror("dup2");
                close(out_fd);
                return -1;
            }
        }
    }

    execvp(args[0], args);
    perror("exec");
    return -1;
}

int resume_job(strvec_t *tokens, job_list_t *jobs, int is_foreground) {
    if (is_foreground) {
        // 2nd token fg call is the index of the job to be moved. Use ASCII to int to parse it
        char *second_token = strvec_get(tokens, 1);
        if (second_token == NULL) {
            fprintf(stderr, "Failed to get token from token vector");
            return -1;
        }
        int index = atoi(second_token);
        job_t *toBeResumed = job_list_get(jobs, index);
        if (toBeResumed == NULL) {
            fprintf(stderr, "Job index out of bounds\n");
            return -1;
        }
        // Send to be resumed to the foreground
        if (tcsetpgrp(STDIN_FILENO, toBeResumed->pid) == -1) {
            perror("tcsetpgrp when resuming stopped process");
            return -1;
        }
        // Send the continue/resume signal
        if (kill(toBeResumed->pid, SIGCONT) == -1) {
            perror("Could not send SIGCONT to resume a process");
            return -1;
        }

        // Repeated code from main() to wait for process to exit
        int status;
        // Waits for child process to terminate
        if (waitpid(toBeResumed->pid, &status, WUNTRACED) == -1) {
            perror("waitpid");
            return -1;
        }

        // Remove jobs that have not stopped (Been moved to foreground or exited)
        if (!WIFSTOPPED(status)) {
            if (job_list_remove(jobs, index) == -1) {
                fprintf(stderr, "Failed to remove job from list");
            }
        }

        pid_t ppid = getpid();
        if (tcsetpgrp(STDIN_FILENO, ppid) == -1) {    // restore the shell process to the foreground
            perror("tcsetpgrp");
            return -1;
        }
    } else if (is_foreground == 0) {
        // 2nd token in bg call is the index of the job to be moved. Use ASCII to int to parse it
        char *second_token = strvec_get(tokens, 1);
        if (second_token == NULL) {
            fprintf(stderr, "Failed to get token from token vector");
            return -1;
        }
        int index = atoi(second_token);
        job_t *toBeResumed = job_list_get(jobs, index);
        if (toBeResumed == NULL) {
            fprintf(stderr, "Job index out of bounds\n");
            return -1;
        }

        toBeResumed->status = BACKGROUND;
        // Send the continue/resume signal
        if (kill(toBeResumed->pid, SIGCONT) == -1) {
            perror("Could not send SIGCONT to resume a process");
            return -1;
        }

    } else {
        return -1;
    }

    return 0;
}

int await_background_job(strvec_t *tokens, job_list_t *jobs) {
    char *second_token = strvec_get(tokens, 1);
    if (second_token == NULL) {
        fprintf(stderr, "Failed to get token from token vector");
        return -1;
    }
    int index = atoi(second_token);
    job_t *toWaitFor = job_list_get(jobs, index);
    if (toWaitFor == NULL) {
        fprintf(stderr, "Failed to get a job from list");
    }
    if (toWaitFor->status == STOPPED) {
        fprintf(stderr, "Job index is for stopped process not background process\n");
        return -1;
    }

    // Repeated code from main() to wait for process to exit
    int status;
    // Waits for child process to terminate
    if (waitpid(toWaitFor->pid, &status, WUNTRACED) == -1) {
        perror("waitpid");
        return -1;
    }

    // Update jobs that have been stopped, remove those which finish
    if (WIFSTOPPED(status)) {
        toWaitFor->status = STOPPED;
    } else {
        if (job_list_remove(jobs, index) == -1) {
            fprintf(stderr, "Failed to remove job from list");
            return -1;
        }
    }

    return 0;
}

int await_all_background_jobs(job_list_t *jobs) {
    int status;
    for (int i = 0; i < jobs->length; i++) {
        job_t *currentJob = job_list_get(jobs, i);
        if (currentJob == NULL) {
            fprintf(stderr, "Failed to get a job from list");
            return -1;
        }
        if (currentJob->status == BACKGROUND) {
            if (waitpid(currentJob->pid, &status, WUNTRACED) == -1) {
                perror("waitpid while looping through bg jobs list");
                return -1;
            }
            if (WIFSTOPPED(status)) {
                currentJob->status = STOPPED;
            }
        }
    }
    job_list_remove_by_status(jobs, BACKGROUND);

    return 0;
}
