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
    // TODO Task 0: Tokenize string s
    // Assume each token is separated by a single space (" ")
    // Use the strtok() function to accomplish this
    // Add each token to the 'tokens' parameter (a string vector)
    // Return 0 on success, -1 on error

    char *token = strtok(s, " ");    // specify the string to parse for the first call to strtok
    while (token != NULL) {
        if (strvec_add(tokens, token) == -1) {
            printf("strvec_add\n");
            return -1;
        }
        token = strtok(NULL, " ");    // call strtok repeatedly until NULL is returned
    }

    return 0;
}

int run_command(strvec_t *tokens) {
    // TODO Task 2: Execute the specified program (token 0) with the
    // specified command-line arguments
    // THIS FUNCTION SHOULD BE CALLED FROM A CHILD OF THE MAIN SHELL PROCESS
    // Hint: Build a string array from the 'tokens' vector and pass this into execvp()
    // Another Hint: You have a guarantee of the longest possible needed array, so you
    // won't have to use malloc.
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

    int error = 0;    // indicates if an error has occurred and the user should still be reprompted

    while (
        get_next_token ==
        1) {    // loop that gets the arguments (not redirection operators) from the tokens vector
        char *curr_arg = strvec_get(tokens, i);
        if (curr_arg == NULL) {
            printf("strvec_get\n");
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
        i++;
        char *file_name = strvec_get(tokens, i);
        i++;

        if (strcmp(redir_token, ">") == 0) {    // redirect output
            int out_fd = open(file_name, O_CREAT | O_TRUNC | O_WRONLY,
                              S_IRUSR | S_IWUSR);    // should overwrite file if it already exists
            if (out_fd == -1) {
                perror("Failed to open output file");
                error = 1;
            } else if (dup2(out_fd, STDOUT_FILENO) == -1) {
                perror("dup2");
                close(out_fd);
                return -1;
            }
        } else if (strcmp(redir_token, "<") == 0) {    // redirect input
            int in_fd = open(file_name, O_RDONLY);
            if (in_fd == -1) {
                perror("Failed to open input file");
                error = 1;
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
                error = 1;
            } else if (dup2(out_fd, STDOUT_FILENO) == -1) {
                perror("dup2");
                close(out_fd);
                return -1;
            }
        }
    }

    if (error == 0) {
        execvp(args[0], args);
        perror("exec");
        return -1;
    }

    // TODO Task 3: Extend this function to perform output redirection before exec()'ing
    // Check for '<' (redirect input), '>' (redirect output), '>>' (redirect and append output)
    // entries inside of 'tokens' (the strvec_find() function will do this for you)
    // Open the necessary file for reading (<), writing (>), or appending (>>)
    // Use dup2() to redirect stdin (<), stdout (> or >>)
    // DO NOT pass redirection operators and file names to exec()'d program
    // E.g., "ls -l > out.txt" should be exec()'d with strings "ls", "-l", NULL

    // TODO Task 4: You need to do two items of setup before exec()'ing
    // 1. Restore the signal handlers for SIGTTOU and SIGTTIN to their defaults.
    // The code in main() within swish.c sets these handlers to the SIG_IGN value.
    // Adapt this code to use sigaction() to set the handlers to the SIG_DFL value.
    // 2. Change the process group of this process (a child of the main shell).
    // Call getpid() to get its process ID then call setpgid() and use this process
    // ID as the value for the new process group ID

    return 0;
}

int resume_job(strvec_t *tokens, job_list_t *jobs, int is_foreground) {
    // TODO Task 5: Implement the ability to resume stopped jobs in the foreground
    // 1. Look up the relevant job information (in a job_t) from the jobs list
    //    using the index supplied by the user (in tokens index 1)
    //    Feel free to use sscanf() or atoi() to convert this string to an int
    // 2. Call tcsetpgrp(STDIN_FILENO, <job_pid>) where job_pid is the job's process ID
    // 3. Send the process the SIGCONT signal with the kill() system call
    // 4. Use the same waitpid() logic as in main -- don't forget WUNTRACED
    // 5. If the job has terminated (not stopped), remove it from the 'jobs' list
    // 6. Call tcsetpgrp(STDIN_FILENO, <shell_pid>). shell_pid is the *current*
    //    process's pid, since we call this function from the main shell process

    if (is_foreground) {
        // 2nd token fg call is the index of the job to be moved. Use ASCII to int to parse that
        // token
        int index = atoi(strvec_get(tokens, 1));
        job_t *toBeResumed = job_list_get(jobs, index);
        if (toBeResumed == NULL) {
            fprintf(stderr, "Job index out of bounds\n");
            strvec_clear(tokens);
            return -1;
        }
        // Send to be resumed to the foreground
        if (tcsetpgrp(STDIN_FILENO, toBeResumed->pid) == -1) {
            perror("tcsetpgrp when resuming stopped process");
            strvec_clear(tokens);
            job_list_free(jobs);
            return -1;
        }
        // Send the continue/resume signal
        if (kill(toBeResumed->pid, SIGCONT) == -1) {
            perror("Could not send SIGCONT to resume a process");
            strvec_clear(tokens);
            job_list_free(jobs);
            return -1;
        }

        // Repeated code from main() to wait for process to exit
        int status;
        // waits for child process to terminate
        if (waitpid(toBeResumed->pid, &status, WUNTRACED) == -1) {
            perror("waitpid");
            strvec_clear(tokens);
            job_list_free(jobs);
            return -1;
        }

        // remove jobs that have not stopped (Been moved to foreground or exited)
        if (!WIFSTOPPED(status)) {
            job_list_remove(jobs, index);
        }

        pid_t ppid = getpid();
        if (tcsetpgrp(STDIN_FILENO, ppid) == -1) {    // restore the shell process to the foreground
            perror("tcsetpgrp");
            strvec_clear(tokens);
            job_list_free(jobs);
            return -1;
        }
    } else if (is_foreground == 0) {
        // 2nd token in bg call is the index of the job to be moved. Use ASCII to int to parse it
        int index = atoi(strvec_get(tokens, 1));
        job_t *toBeResumed = job_list_get(jobs, index);
        if (toBeResumed == NULL) {
            fprintf(stderr, "Job index out of bounds\n");
            strvec_clear(tokens);
            return -1;
        }

        toBeResumed->status = BACKGROUND;
        // Send the continue/resume signal
        if (kill(toBeResumed->pid, SIGCONT) == -1) {
            perror("Could not send SIGCONT to resume a process");
            strvec_clear(tokens);
            job_list_free(jobs);
            return -1;
        }

    } else {
        return -1;
    }
    // TODO Task 6: Implement the ability to resume stopped jobs in the background.
    // This really just means omitting some of the steps used to resume a job in the foreground:
    // 1. DO NOT call tcsetpgrp() to manipulate foreground/background terminal process group
    // 2. DO NOT call waitpid() to wait on the job
    // 3. Make sure to modify the 'status' field of the relevant job list entry to BACKGROUND
    //    (as it was STOPPED before this)

    return 0;
}

int await_background_job(strvec_t *tokens, job_list_t *jobs) {
    // TODO Task 6: Wait for a specific job to stop or terminate
    // 1. Look up the relevant job information (in a job_t) from the jobs list
    //    using the index supplied by the user (in tokens index 1)
    // 2. Make sure the job's status is BACKGROUND (no sense waiting for a stopped job)
    // 3. Use waitpid() to wait for the job to terminate, as you have in resume_job() and main().
    // 4. If the process terminates (is not stopped by a signal) remove it from the jobs list

    int index = atoi(strvec_get(tokens, 1));
    job_t *toWaitFor = job_list_get(jobs, index);

    if (toWaitFor->status == STOPPED) {
        fprintf(stderr, "Job index is for stopped process not background process\n");
        return -1;
    }

    // Repeated code from main() to wait for process to exit
    int status;
    // waits for child process to terminate
    if (waitpid(toWaitFor->pid, &status, WUNTRACED) == -1) {
        perror("waitpid");
        strvec_clear(tokens);
        job_list_free(jobs);
        return -1;
    }

    // update jobs that have been stopped, remove those which finish
    if (WIFSTOPPED(status)) {
        toWaitFor->status = STOPPED;
    } else {
        job_list_remove(jobs, index);
    }

    return 0;
}

int await_all_background_jobs(job_list_t *jobs) {
    // TODO Task 6: Wait for all background jobs to stop or terminate
    // 1. Iterate through the jobs list, ignoring any stopped jobs
    // 2. For a background job, call waitpid() with WUNTRACED.
    // 3. If the job has stopped (check with WIFSTOPPED), change its
    //    status to STOPPED. If the job has terminated, do nothing until the
    //    next step (don't attempt to remove it while iterating through the list).
    // 4. Remove all background jobs (which have all just terminated) from jobs list.
    //    Use the job_list_remove_by_status() function.

    int status;
    for (int i = 0; i < jobs->length; i++) {
        job_t *currentJob = job_list_get(jobs, i);
        if (currentJob->status == BACKGROUND) {
            if (waitpid(currentJob->pid, &status, WUNTRACED) == -1) {
                perror("waitpid while looping through bg jobs list");
                job_list_free(jobs);
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
