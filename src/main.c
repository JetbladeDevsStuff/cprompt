/* SPDX-License-Identifier: MIT
 * Copyright (c) 2023 Terence Noone
 */

#include <stddef.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/types.h>
#include <pwd.h>
#include <uuid/uuid.h>
#include <sys/errno.h>
#include <limits.h>
#include <libgen.h>
#include <libproc.h>
#include <time.h>
#include "config.h"

#define MAX_STRFTIME_SIZE 50

enum PromptElementType {
	StringLiteral, // Any string literal; arg is char*
	Space, // Basically StringLiteral " "

	Bell, // ASCII bell (07)

	HostnameUpToDot, // The hostname up until the first dot
	FullHostname, // You probably want to use HostnameUpToDot

	//NumJobs, // Number of running jobs

	TtyBasename, // Gets basename of tty, tty0

	ShellName, // Basename of $0, zsh

	WeekMonthDay, // Date, Tue May 26
	StrftimeDate, // Date; arg is a date format string

	HourMinuteSecond24, // Time, 14:32:14
	HourMinuteSecond12, // Time, 11:34:11
	TimeAmPm, // Time, 12:42 PM
	HourMinute24, // Time, 21:11

	Username, // Username, root

	//ShellVersion, // Shell version
	//ShellVersionPatch, // Shell version with patch number
	PwdTrunc, // PWD truncating $HOME to ~, see next comment
	PwdTruncBasename, // Basename of PWD truncating $HOME to ~, see next comment
	// arg (optional) is what $HOME is truncated to instead of ~

	//HistoryNum // The history number of the current command
	//CommandNum // The command number of the current command

	UserPrompt, // If EUID is 0, #, instead a $
	// arg (optional) is an array of the string wanted when [EUID == 0, Else]
};

typedef struct {
	const enum PromptElementType type;
	// This should be const
	void* arg;
} PromptElement;

#include "user_config.h"

struct prompt_string {
	char* str;
	bool needs_free;
};

enum home_dir_ret {
	// Failed to get the home directory and the error string is not allocated
	HOME_DIR_FAILED_MESSAGE_NO_ALLOC = -2,
	// Failed to get the home directory and the error string is allocated
	HOME_DIR_FAILED_MESSAGE_ALLOC = -1,
	// Returned the home directory with no allocation
	HOME_DIR_NO_ALLOC = 0,
	// Returned the home directory and it is allocated
	HOME_DIR_ALLOC = 1,
};

const static int prompt_elements = sizeof(prompt) / sizeof(prompt[0]);

/**
 * Allocates or puts an error into ps
 *
 * @param[out] ps The prompt string to report errors to
 * @param[in] size How many bytes to allocate
 */
void* malloc_or_error(struct prompt_string* ps, size_t size)
{
	void* ptr = malloc(size);
	if (!ptr) {
		ps->needs_free = false;
		ps->str = "!MALLOC!";
	}
	return ptr;
}

/**
 * @brief Formats an error
 *
 * @param[in] err_str The default message to use if strerrorname_np is not available
 * @param[in] err The errno
 * @param[out] used_default Whether the result needs to be freed
 */
char* format_error(char* err_str, int err, bool* needs_free)
{
#if HAVE_STRERRORNAME_NP
	size_t length;
	char* ret;

	*needs_free = true;
	length = 2 + strnlen(strerrorname_np(err), 20); // no error is longer than 20 right?
	ret = malloc(length * sizeof(char));
	if (!ret) {
		*needs_free = false;
		return err_str;
	}
	strlcpy(ret, "!", length);
	strlcat(ret, strerrorname_np(err), length);
	strlcat(ret, "!", length);
	return ret;
#else
	*needs_free = false;
	return err_str;
#endif
}

/**
 * @brief Returns the formatted time
 *
 * @param[out] ps The prompt_string that will be populated
 * @param[in] fmt The format string used, see man page for strftime
 */
void get_formatted_time(struct prompt_string* ps, const char* fmt)
{
	struct tm time_br;
	time_t clock;
	int status;

	ps->needs_free = true;

	clock = time(NULL);
	if (clock == -1)
	{
		ps->str = format_error("!TIME!", errno, &ps->needs_free);
		return;
	}
	localtime_r(&clock, &time_br);

	ps->str = malloc_or_error(ps, sizeof(char) * MAX_STRFTIME_SIZE);
	if (!ps->str) {
		return;
	}
	status = strftime(ps->str, MAX_STRFTIME_SIZE, fmt, &time_br);
	if (status == 0)
	{
		free(ps->str);
		ps->needs_free = false;
		ps->str = "!STRFTIME!";
		return;
	}
	return;
}

/**
 * @brief Returns the hostname of the system
 *
 * @param[out] ps The prompt string to be populated
 * @param[in] to_dot Whether to return the hostname up to the first dot of the
 * full hostname
 */
void get_hostname(struct prompt_string* ps, bool to_dot)
{
	long length;
	int errno_temp;
	char* dot;

	ps->needs_free = false;
	errno_temp = errno;

	length = sysconf(_SC_HOST_NAME_MAX);
	if (length == -1)
	{
		if (errno == errno_temp)
		{
			ps->str = "!NOHOSTNAMEMAX!";
			return;
		}
		ps->str = format_error("!SYSCONF!", errno, &ps->needs_free);
		return;
	}

	ps->needs_free = true;
	ps->str = malloc_or_error(ps, length * sizeof(char));
	if (!ps->str) {
		return;
	}
	if (gethostname(ps->str, length) == -1)
	{
		// should be impossible
		free(ps->str);
		ps->str = format_error("!GETHOSTNAME!", errno, &ps->needs_free);
		return;
	}
	if (to_dot)
	{
		dot = strchr(ps->str, '.');
		if (dot) {
			*dot = 0;
		}
	}

	return;
}

/**
 * @brief Gets the basename of stdout
 *
 * @param[out] ps The prompt string to be populated
 */
void get_tty_basename(struct prompt_string* ps)
{
	int status;
	char* tty;

	ps->needs_free = false;

	if (!isatty(STDOUT_FILENO))
	{
		ps->str = format_error("!ISATTY!", errno, &ps->needs_free);
		return;
	}
	tty = ttyname(STDOUT_FILENO);
	if (!tty)
	{
		ps->str = format_error("!TTYNAME!", errno, &ps->needs_free);
		return;
	}
	// On PATH_MAX... man page says to use MAXPATHLEN, but this is
	// 4.2BSD/Solaris only. POSIX (SUSv2) says to use PATH_MAX or pathconf().
	ps->needs_free = true;
	ps->str = malloc_or_error(ps, PATH_MAX * sizeof(char));
	if (!ps->str) {
		return;
	}
	if (!basename_r(tty, ps->str))
	{
		free(ps->str);
		ps->str = format_error("!BASENAMER!", errno, &ps->needs_free);
	}
	return;
}

/**
 * @brief Gets the name of the parent process (usually the shell)
 *
 * @param[out] ps The prompt string to be populated
 */
void get_parent_name(struct prompt_string* ps)
{
	pid_t ppid;
	int ret;

	ps->needs_free = true;
	ps->str = malloc_or_error(ps, PROC_PIDPATHINFO_MAXSIZE * sizeof(char));
	if (!ps->str) {
		return;
	}
	ppid = getppid();
	// This is super platform-specific
#ifdef __APPLE__
	// This was hard to find
	// There is little to no documentation about macOS's libproc, but this
	// should work. See libproc.h for more info.
	ret = proc_pidpath(ppid, ps->str, PROC_PIDPATHINFO_MAXSIZE);
	if (ret <= 0) {
		free(ps->str);
		ps->str = format_error("!PROCPIDPATH!", errno, &ps->needs_free);
	}
	return;
#else
	free(ps->str);
	ps->needs_free = false;
	ps->str = "!NOPROC!";
	return;
#endif
}

/**
 * @brief Gets the username
 *
 * @param[out] ps The prompt string to be populated
 */
void get_username(struct prompt_string *ps)
{
	int bufsz, status;
	char* buf, *username;
	struct passwd pass, *result = NULL;

	ps->needs_free = false;
	status = errno;
	bufsz = sysconf(_SC_GETPW_R_SIZE_MAX);
	if (bufsz == -1)
	{
		ps->str = status == errno ? "!NOGETPWRSIZEMAX!"
			: format_error("!SYSCONF!", errno, &ps->needs_free);
		return;
	}

	buf = malloc_or_error(ps, bufsz);
	if (!buf) {
		return;
	}
	status = getpwuid_r(getuid(), &pass, buf, bufsz, &result);
	if (status != 0) {
		free(buf);
		ps->str = format_error("!GETPWUIDR!", errno, &ps->needs_free);
		return;
	} else if(!result) {
		// Not found
		free(buf);
		ps->str = "nobody";
		return;
	}
	// Technically, since pw_name is the first field of struct passwd we could
	// cast result to a char* and return it, but that is kind of weird.
	// Hopefully the compiler can figure this out.
	ps->needs_free = true;
	if(!(ps->str = strndup(pass.pw_name, PATH_MAX + 1))) {
		ps->needs_free = false;
		ps->str = "!STRNDUP!";
	}
	free(buf);
	return;
}

/**
 * @brief Gets the home directory of the current user
 *
 * @param[out] ret The returned directory
 * @return One of enum home_dir_ret
 */
int get_home_dir(char** ret)
{
	char* home, *pwbuf;
	int status;
	size_t pwbufsz;
	struct passwd pw, *result;
	bool err_free;

	home = getenv("HOME");
	if (home) {
		*ret = home;
		return HOME_DIR_NO_ALLOC;
	}
	// Try to get it from the passwd database
	status = errno;
	pwbufsz = sysconf(_SC_GETPW_R_SIZE_MAX);
	if (pwbufsz == -1) {
		if (status == errno) {
			*ret = "!NOGETPWRSIZEMAX!";
			return HOME_DIR_NO_ALLOC;
		}
		*ret = format_error("!SYSCONF!", errno, &err_free);
		return err_free - 2;
	}
	pwbuf = malloc(pwbufsz);
	if (!pwbuf) {
		*ret = "!MALLOC!";
		return HOME_DIR_FAILED_MESSAGE_NO_ALLOC;
	}
	status = getpwuid_r(getuid(), &pw, pwbuf, pwbufsz, &result);
	if (status != 0) {
		*ret = format_error("!GETPWUIDR!", errno, &err_free);
		return err_free - 2;
	} else if (!result) {
		*ret = "!USERNOTFOUND!";
		return HOME_DIR_NO_ALLOC;
	}
	if(!(home = strndup(pw.pw_dir, PATH_MAX + 1))) {
		*ret = "!STRNDUP!";
		return HOME_DIR_FAILED_MESSAGE_NO_ALLOC;
	}
	*ret = home;
	return HOME_DIR_ALLOC;
}

/**
 * @brief Gets the current working directory, abreviating $HOME with a tilde
 *
 * @param[out] ps The prompt string to populate
 * @param[in] base Should we get the basename of the path
 */
void get_pwd_tilde(struct prompt_string* ps, bool base)
{
	// See comment about MAXPATHLEN
	char* home, *match, pwd[PATH_MAX];
	int status, len;

	if (!getcwd(pwd, PATH_MAX)) {
		ps->str = format_error("!GETCWD!", errno, &ps->needs_free);
		return;
	}
	status = get_home_dir(&home);
	if (status < 0) {
		ps->needs_free = status + 2;
		ps->str = home;
		return;
	}
	match = strnstr(pwd, home, PATH_MAX);
	if (match == pwd) {
		len = strnlen(home, PATH_MAX) - 1;
		*pwd = '~';
		for (match = pwd + 1; *(match + len); match++)
			*match = *(match + len);
		*match = 0;
	}
	if (status == HOME_DIR_ALLOC)
		free(home);
	len = strnlen(pwd, PATH_MAX) + 1;
	ps->needs_free = true;
	ps->str = malloc_or_error(ps, len);
	if (!ps->str) {
		return;
	}
	if (base && !basename_r(pwd, ps->str)) {
		free(ps->str);
		ps->str = format_error("!BASENAMER!", errno, &ps->needs_free);
		return;
	} else
		strlcpy(ps->str, pwd, PATH_MAX);
	return;
}

/**
 * @brief Makes an array of stringified prompt parts
 *
 * Walks through user-provided `prompt` and turns each part of `prompt` into a
 * corresponding string
 *
 * @param[out] len The amount of pointers
 *
 * @return An array of pointers to strings
 */
struct prompt_string* make_exploded_prompt(size_t* len)
{
	struct prompt_string* elements;

	*len = prompt_elements;
	elements = malloc(prompt_elements * sizeof(struct prompt_string));

	for (int i = 0; i < prompt_elements; ++i)
	{
		switch(prompt[i].type) {
		case StringLiteral:
			elements[i].str = prompt[i].arg;
			elements[i].needs_free = false;
			break;
		case Space:
			elements[i].str = " ";
			elements[i].needs_free = false;
			break;
		case Bell: // for bash compatability with \a
			elements[i].str = "\07";
			elements[i].needs_free = false;
			break;
		case WeekMonthDay: // bash: %a %b %d
			get_formatted_time(&elements[i], "%a %b %d");
			break;
		case StrftimeDate: // custom
			get_formatted_time(&elements[i], prompt[i].arg);
			break;
		case HourMinuteSecond24: // bash: %H:%M:%S
			get_formatted_time(&elements[i], "%H:%M:%S");
			break;
		case HourMinuteSecond12: // bash: %I:%M:%S
			get_formatted_time(&elements[i], "%I:%M:%S");
			break;
		case TimeAmPm: // bash: %I:%M %p
			get_formatted_time(&elements[i], "%I:%M %p");
			break;
		case HourMinute24: // bash: %H:%M
			get_formatted_time(&elements[i], "%H:%M");
			break;
		case HostnameUpToDot:
			get_hostname(&elements[i], true);
			break;
		case FullHostname:
			get_hostname(&elements[i], false);
			break;
		case TtyBasename:
			get_tty_basename(&elements[i]);
			break;
		case ShellName:
			get_parent_name(&elements[i]);
			break;
		case Username:
			get_username(&elements[i]);
			break;
		case PwdTrunc:
			get_pwd_tilde(&elements[i], false);
			break;
		case PwdTruncBasename:
			get_pwd_tilde(&elements[i], true);
			break;
		case UserPrompt:
			elements[i].str = geteuid() == 0 ? "#" : "$";
			elements[i].needs_free = false;
		}
	}

	return elements;
}

/**
 * @brief Frees array made by {make_exploded_prompt}
 *
 * @param exploded_prompt The return value of make_exploded_prompt
 * @param len The len value of make_exploded_prompt
 */
void exploded_prompt_free(struct prompt_string exploded_prompt[], const size_t len)
{
	for (int i = 0; i < len; ++i)
		if (exploded_prompt[i].needs_free)
			free(exploded_prompt[i].str);

	free(exploded_prompt);
}

int main(void)
{
	size_t exploded_length;
	struct prompt_string* exploded_prompt = make_exploded_prompt(&exploded_length);

	for (int i = 0; i < exploded_length; i++) {
		if (i == exploded_length - 1)
			printf("%s\n", exploded_prompt[i].str);
		else
			printf("%s", exploded_prompt[i].str);
	}

	exploded_prompt_free(exploded_prompt, exploded_length);
}
