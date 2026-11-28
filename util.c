/*
 * util.c - small helpers used by several modules
 *
 *   read_whole_file()   a file into memory, always with a 0 byte behind it
 *   run_program()       an external program (OSFMount), waits for it
 *
 * SPDX-License-Identifier: MIT (same terms as cc1541, see LICENSE)
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L   /* posix_spawnp */
#endif

#include "cit.h"
#ifdef _WIN32
#include <windows.h>
#else
#include <spawn.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
extern char **environ;
#endif

/* The whole file; *data has size + 1 bytes, the last one 0, so text can be
   read with the str functions without running past the end. 0 = ok */
int
read_whole_file(const char *path, unsigned char **data, long *size)
{
    *data = NULL;
    *size = 0;
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return -1;
    }
    long l = ftell(f);
    if (l < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return -1;
    }
    unsigned char *d = malloc((size_t)l + 1);
    if (d == NULL) {
        fprintf(stderr, "ERROR: Memory allocation error\n");
        exit(-1);
    }
    if (l > 0 && fread(d, 1, (size_t)l, f) != (size_t)l) {
        fclose(f);
        free(d);
        return -1;
    }
    fclose(f);
    d[l] = 0;
    *data = d;
    *size = l;
    return 0;
}

/* runs prog with args (NULL-terminated) and waits; quiet sends its output
   to nowhere. 0 = ok, 1 = it failed, -1 = it could not be started */
int
run_program(const char *prog, const char *const args[], bool quiet)
{
#ifdef _WIN32
    char cmd[8192];
    int n = snprintf(cmd, sizeof cmd, "\"%s\"", prog);
    for (int i = 0; args[i] != NULL && n < (int)sizeof cmd; i++) {
        n += snprintf(cmd + n, sizeof cmd - n, " \"%s\"", args[i]);
    }
    if (n >= (int)sizeof cmd) {
        return -1;
    }
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    HANDLE nul = INVALID_HANDLE_VALUE;
    if (quiet) {
        SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };
        nul = CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, NULL);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        si.hStdOutput = nul;
        si.hStdError = nul;
    }
    BOOL ok = CreateProcessA(NULL, cmd, NULL, NULL, quiet, quiet ? CREATE_NO_WINDOW : 0, NULL, NULL, &si, &pi);
    if (nul != INVALID_HANDLE_VALUE) {
        CloseHandle(nul);
    }
    if (!ok) {
        return -1;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return code == 0 ? 0 : 1;
#else
    const char *argv[64];
    int n = 0;
    argv[n++] = prog;
    for (int i = 0; args[i] != NULL && n < 63; i++) {
        argv[n++] = args[i];
    }
    argv[n] = NULL;
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    if (quiet) {
        posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
        posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
    }
    pid_t pid;
    int r = posix_spawnp(&pid, prog, &fa, NULL, (char *const *)argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    if (r != 0) {
        return -1;
    }
    int status;
    if (waitpid(pid, &status, 0) < 0) {
        return 1;
    }
    if (WIFEXITED(status) && WEXITSTATUS(status) == 127) {
        return -1;
    }
    return (WIFEXITED(status) && WEXITSTATUS(status) == 0) ? 0 : 1;
#endif
}
