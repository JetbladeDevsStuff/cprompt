/* SPDX-License-Identifier: MIT
 * Copyright (c) 2024 Terence Noone
 */

/* User configuration file for cprompt
 *
 * Insert the configuration you want into this file: it will be included at
 * build time
 *
 * Provides a basic configuration by default: to reset to this default run
 *     $ git restore user_config.h
 * This basic configuration mimics the Gentoo Linux default bash prompt for
 * non-root users.
 */

/* FINAL PROMPT
 *
 * This is the structure where your prompt will be defined
 * List all the sections in order
 */
static const PromptElement prompt[] = {
	{ StringLiteral, "\033[1;32m" },
	{ Username, NULL },
	{ StringLiteral, "@" },
	{ HostnameUpToDot, NULL },
	{ StringLiteral, "\033[1;34m" },
	{ Space, NULL },
	{ PwdTrunc, NULL },
	{ Space, NULL },
	{ UserPrompt, NULL },
	{ StringLiteral, "\033[0m" },
	{ Space, NULL },
};

