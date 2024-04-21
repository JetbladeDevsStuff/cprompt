# SPDX-License-Identifier: MIT
# Copyright (c) 2024 Terence Noone

AC_PREREQ([2.71])
AC_INIT([cprompt], [0.1], [me@techtricity.net], [cprompt], [https://techtricity.net/cprompt])
AM_INIT_AUTOMAKE([foreign -Wall -Werror])
AC_CONFIG_SRCDIR([src/main.c])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([Makefile src/Makefile])
AC_CONFIG_LINKS([user_config.h:user_config.h])

# Checks for programs.
AC_PROG_CC

# Checks for libraries.

# Checks for header files.
AC_CHECK_HEADERS([unistd.h])

# Checks for typedefs, structures, and compiler characteristics.

# Checks for library functions.
AC_LANG([C])

AC_CHECK_FUNCS([strerrorname_np])

AC_MSG_NOTICE([=== CONFIGURE BY EDITING user_config.h ===])

AC_OUTPUT
