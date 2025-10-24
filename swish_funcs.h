// Author: John Kolb <jhkolb@umn.edu>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SWISH_FUNCS_H
#define SWISH_FUNCS_H

#include "job_list.h"
#include "string_vector.h"

/**
 * @brief Divide a string into substrings separated by a single space (" ")
 *
 * @details A input string s gets converted into a vector of strings (strvec_t*)
 * which can be indexed and manipulated with the functions in string_vector.h
 *
 * @param s Input string to be tokenized (character pointer)
 * @param tokens Pointer to the output String Vector (strvec_t*)
 *
 * @return 0 on success, -1 on failure
 */
int tokenize(char *s, strvec_t *tokens);

/**
 * @brief Runs a user-specified command with file redirection and signal handling
 *
 * @details This function should only be called in the CHILD process of a shell
 * It takes in the arguments from strvec_t* tokens and attempts to run an execvp()
 * syscall to perform the command
 *
 * Adds features for file I/O redirection with "<", ">", and ">>" tokens
 * Ensures SIGTTIN and SITTOU signals are NOT ignored
 *
 * @param tokens String vector that contains the name of the executable, followed by arguments to
 * the command
 *
 * @return Returns 1 on failure. Does not return on success (completes execvp() and then terminates
 * the calling proccess)
 */
int run_command(strvec_t *tokens);

/**
 * @brief Resumes a stopped proccess in either the background (bg) or foreground (fg)
 *
 * @details Used to implement fg [index] and bg [index] commands. Sends a SIGCONT to the desired
 * process. If is_foreground = 1, then the child process gets set as the foreground process. If
 * is_foreground = 0, the child process is run the background
 *
 * @param tokens String Vector of command line arguments (should be either 'fg [index]' or 'bg
 * [index]' where [index] is an integer index to the job list)
 * @param jobs list of currently stopped or background jobs, [index] should be a valid index for
 * this list
 * @param is_foreground 0 if the command was a bg command, 1 if it was a fg command
 *
 * @return 0 on success, -1 on error
 */
int resume_job(strvec_t *tokens, job_list_t *jobs, int is_foreground);

/**
 * @brief Block the calling shell process until a specific background (not stopped) job
 * stops running (either is stopped or exits)
 *
 * @details Uses waitpid() with WUNTRACED flag to wait for a specific job from the background.
 * Ignores stopped proccesses waitpid() will return if the request job either exited or is stopped
 * with the SIGINT signal. The job is removed from the job list if it has exited. The job is kept in
 * the list, but its status is updated to "STOPPED" if it was stopped by SIGINT
 *
 * @param tokens String Vector containing command line arguments, should be either "wait-for
 * [index]", where [index] is a valid integer index to the jobs list
 * @param jobs List of jobs currently stopped or running in the background
 *
 * @return 0 on success, -1 on failure
 */
int await_background_job(strvec_t *tokens, job_list_t *jobs);

/**
 * @brief Block the calling shell process until all background (not stopped) jobs
 * stop running (either are stopped or exit)
 *
 * @details Loops through the entire job list, ignores STOPPED jobs, and uses waitpid() to wait for
 * background jobs to either exit or be stopped by a signal
 *
 * @param jobs List of jobs currently stopped or running in the background
 *
 * @return 0 on success, -1 on failure
 */
int await_all_background_jobs(job_list_t *jobs);

#endif    // SWISH_FUNCS_H
