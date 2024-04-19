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
 * @param[in] fmt The format string used, see man page for strftime
 * @param[out] needs_free Whether the result should be freed
 */
char* get_formatted_time(const char* fmt, bool* needs_free)
{
	struct tm time_br;
	time_t clock;
	int status;
	char* ret;

	*needs_free = true;

	clock = time(NULL);
	if (clock == -1)
	{
		return format_error("!TIME!", errno, needs_free);
	}
	localtime_r(&clock, &time_br);

	ret = malloc(sizeof(char) * MAX_STRFTIME_SIZE);
	status = strftime(ret, MAX_STRFTIME_SIZE, fmt, &time_br);
	if (status == 0)
	{
		free(ret);
		*needs_free = false;
		ret = "!STRFTIME!";
	}
	return ret;
}

/**
 * @brief Returns the hostname of the system
 *
 * @param[in] to_dot Whether to return the hostname up to the first dot of the
 * full hostname
 * @param[out] needs_free Whether the result should be freed
 */
char* get_hostname(bool to_dot, bool* needs_free)
{
	long length;
	int errno_temp;
	char* ret;
	char* dot;

	*needs_free = false;
	errno_temp = errno;

	length = sysconf(_SC_HOST_NAME_MAX);
	if (length == -1)
	{
		if (errno == errno_temp)
		{
			return "!NOHOSTNAMEMAX!";
		}
		format_error("!SYSCONF!", errno, needs_free);
	}

	*needs_free = true;
	ret = malloc(length * sizeof(char));
	if (gethostname(ret, length) == -1)
	{
		// should be impossible
		free(ret);
		return format_error("!GETHOSTNAME!", errno, needs_free);
	}
	if (to_dot)
	{
		dot = strchr(ret, '.');
		if (dot) {
			*dot = 0;
		}
	}

	return ret;
}

/**
 * @brief Gets the basename of stdout
 *
 * @param[out] needs_free Whether the returned string needs to be freed
 */
char* get_tty_basename(bool* needs_free)
{
	int status;
	char* tty;
	char* ret;

	*needs_free = false;

	if (!isatty(STDOUT_FILENO))
	{
		return format_error("!ISATTY!", errno, needs_free);
	}
	tty = ttyname(STDOUT_FILENO);
	if (!tty)
	{
		return format_error("!TTYNAME!", errno, needs_free);
	}
	// On PATH_MAX... man page says to use MAXPATHLEN, but this is
	// 4.2BSD/Solaris only. POSIX (SUSv2) says to use PATH_MAX or pathconf().
	ret = malloc(PATH_MAX * sizeof(char));
	if (!basename_r(tty, ret))
	{
		free(ret);
		return format_error("!BASENAMER!", errno, needs_free);
	}
	return tty;
}

/**
 * @brief Gets the name of the parent process (usually the shell)
 *
 * @param[out] needs_free Whether the returned string needs to be freed
 */
char* get_parent_name(bool* needs_free)
{
	pid_t ppid;
	int ret;
	char* path;

	*needs_free = true;
	path = malloc(PROC_PIDPATHINFO_MAXSIZE * sizeof(char));
	ppid = getppid();
	// This is super platform-specific
#ifdef __APPLE__
	// This was hard to find
	// There is little to no documentation about macOS's libproc, but this
	// should work. See libproc.h for more info.
	ret = proc_pidpath(ppid, path, PROC_PIDPATHINFO_MAXSIZE);
	if (ret <= 0) {
		free(path);
		*needs_free = false;
		return format_error("!PROCPIDPATH!", errno, needs_free);
	}
	return path;
#else
	free(path);
	*needs_free = false;
	return "!NOPROC!";
#endif
}

/**
 * @brief Gets the username
 *
 * @param[out] needs_free Whether the returned string needs to be freed
 */
char* get_username(bool* needs_free)
{
	int bufsz, status;
	char* buf, *username;
	struct passwd pass, *result = NULL;

	*needs_free = false;
	status = errno;
	bufsz = sysconf(_SC_GETPW_R_SIZE_MAX);
	if (bufsz == -1)
	{
		if (status == errno)
			return "!NOGETPWRSIZEMAX!";
		return format_error("!SYSCONF!", errno, needs_free);
	}

	buf = malloc(bufsz);
	status = getpwuid_r(getuid(), &pass, buf, bufsz, &result);
	if (status != 0) {
		free(buf);
		return format_error("!GETPWUIDR!", errno, needs_free);
	} else if(!result) {
		// Not found
		free(buf);
		return "nobody";
	}
	// Technically, since pw_name is the first field of struct passwd we could
	// cast result to a char* and return it, but that is kind of weird.
	// Hopefully the compiler can figure this out.
	*needs_free = true;
	if(!(username = strndup(pass.pw_name, PATH_MAX + 1))) {
		*needs_free = false;
		return "!STRNDUP!";
	}
	return username;
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
 * @param[in] base Should we get the basename of the path
 * @param[out] needs_free Whether the returned string needs to be freed
 */
char* get_pwd_tilde(bool base, bool* needs_free)
{
	// See comment about MAXPATHLEN
	char* home, *match, pwd[PATH_MAX], *pwd_heap, *pwd_cur;
	int status, len;

	if (!getcwd(pwd, PATH_MAX))
		return format_error("!GETCWD!", errno, needs_free);
	status = get_home_dir(&home);
	if (status < 0) {
		*needs_free = status + 2;
		return home;
	}
	match = strnstr(pwd, home, PATH_MAX);
	if (match == pwd) {
		len = strnlen(home, PATH_MAX) - 1;
		*pwd = '~';
		for (pwd_cur = pwd + 1; *(pwd_cur + len); pwd_cur++)
			*pwd_cur = *(pwd_cur + len);
		*pwd_cur = 0;
	}
	if (status == HOME_DIR_ALLOC)
		free(home);
	len = strnlen(pwd, PATH_MAX);
	*needs_free = true;
	pwd_heap = malloc(len);
	if (base && !basename_r(pwd, pwd_heap)) {
		free(pwd_heap);
		return format_error("!BASENAMER!", errno, needs_free);
	} else
		strlcpy(pwd_heap, pwd, PATH_MAX);
	return pwd_heap;
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
			elements[i].str = get_formatted_time("%a %b %d", &elements[i].needs_free);
			break;
		case StrftimeDate: // custom
			elements[i].str = get_formatted_time(prompt[i].arg, &elements[i].needs_free);
			break;
		case HourMinuteSecond24: // bash: %H:%M:%S
			elements[i].str = get_formatted_time("%H:%M:%S", &elements[i].needs_free);
			break;
		case HourMinuteSecond12: // bash: %I:%M:%S
			elements[i].str = get_formatted_time("%I:%M:%S", &elements[i].needs_free);
			break;
		case TimeAmPm: // bash: %I:%M %p
			elements[i].str = get_formatted_time("%I:%M %p", &elements[i].needs_free);
			break;
		case HourMinute24: // bash: %H:%M
			elements[i].str = get_formatted_time("%H:%M", &elements[i].needs_free);
			break;
		case HostnameUpToDot:
			elements[i].str = get_hostname(true, &elements[i].needs_free);
			break;
		case FullHostname:
			elements[i].str = get_hostname(false, &elements[i].needs_free);
			break;
		case TtyBasename:
			elements[i].str = get_tty_basename(&elements[i].needs_free);
			break;
		case ShellName:
			elements[i].str = get_parent_name(&elements[i].needs_free);
			break;
		case Username:
			elements[i].str = get_username(&elements[i].needs_free);
			break;
		case PwdTrunc:
			elements[i].str = get_pwd_tilde(false, &elements[i].needs_free);
			break;
		case PwdTruncBasename:
			elements[i].str = get_pwd_tilde(true, &elements[i].needs_free);
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
