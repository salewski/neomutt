/**
 * Copyright (C) 1996-2000 Michael R. Elkins.
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 2 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include "mutt.h"
#include "filter.h"
#include "mutt_curses.h"
#include "protos.h"

/** Invokes a command on a pipe and optionally connects its stdin and stdout
 * to the specified handles.
 *
 * @param cmd the command line to invoke using `sh -c`
 * @param in a file stream pointing to stdin for the command process, can be NULL
 * @param out a file stream pointing to stdout for the command process, can be NULL
 * @param err a file stream pointing to stderr for the command process, can be NULL
 * @param fdin if `in` is NULL and fdin is not -1 then fdin will be used as stdin for the command process
 * @param fdout if `out` is NULL and fdout is not -1 then fdout will be used as stdout for the command process
 * @param fderr if `error` is NULL and fderr is not -1 then fderr will be used as stderr for the command process
 *
 * This function provides multiple mechanisms to handle IO sharing for the command process. File streams are prioritized
 * over file descriptors if present.
 *
 * @code{.c}
 *    mutt_create_filter_fd(commandline, NULL, NULL, NULL, -1, -1, -1);
 * @endcode
 *
 * @returns - the pid of the created process or -1 on any error creating pipes or forking.
 * Additionally, in, out, and err will point to FILE* streams representing the processes stdin, stdout, and stderr.
 */
pid_t mutt_create_filter_fd(const char *cmd, FILE **in, FILE **out, FILE **err,
                            int fdin, int fdout, int fderr)
{
  int pin[2], pout[2], perr[2], thepid;
  char columns[11];

  if (in)
  {
    *in = 0;
    if (pipe(pin) == -1)
      return -1;
  }

  if (out)
  {
    *out = 0;
    if (pipe(pout) == -1)
    {
      if (in)
      {
        close(pin[0]);
        close(pin[1]);
      }
      return -1;
    }
  }

  if (err)
  {
    *err = 0;
    if (pipe(perr) == -1)
    {
      if (in)
      {
        close(pin[0]);
        close(pin[1]);
      }
      if (out)
      {
        close(pout[0]);
        close(pout[1]);
      }
      return -1;
    }
  }

  mutt_block_signals_system();

  if ((thepid = fork()) == 0)
  {
    mutt_unblock_signals_system(0);

    if (in)
    {
      close(pin[1]);
      dup2(pin[0], 0);
      close(pin[0]);
    }
    else if (fdin != -1)
    {
      dup2(fdin, 0);
      close(fdin);
    }

    if (out)
    {
      close(pout[0]);
      dup2(pout[1], 1);
      close(pout[1]);
    }
    else if (fdout != -1)
    {
      dup2(fdout, 1);
      close(fdout);
    }

    if (err)
    {
      close(perr[0]);
      dup2(perr[1], 2);
      close(perr[1]);
    }
    else if (fderr != -1)
    {
      dup2(fderr, 2);
      close(fderr);
    }

    if (MuttIndexWindow && (MuttIndexWindow->cols > 0))
    {
      snprintf(columns, sizeof(columns), "%d", MuttIndexWindow->cols);
      mutt_envlist_set("COLUMNS", columns, 1);
    }

    execle(EXECSHELL, "sh", "-c", cmd, NULL, mutt_envlist());
    _exit(127);
  }
  else if (thepid == -1)
  {
    mutt_unblock_signals_system(1);

    if (in)
    {
      close(pin[0]);
      close(pin[1]);
    }

    if (out)
    {
      close(pout[0]);
      close(pout[1]);
    }

    if (err)
    {
      close(perr[0]);
      close(perr[1]);
    }

    return -1;
  }

  if (out)
  {
    close(pout[1]);
    *out = fdopen(pout[0], "r");
  }

  if (in)
  {
    close(pin[0]);
    *in = fdopen(pin[1], "w");
  }

  if (err)
  {
    close(perr[1]);
    *err = fdopen(perr[0], "r");
  }

  return thepid;
}

pid_t mutt_create_filter(const char *s, FILE **in, FILE **out, FILE **err)
{
  return (mutt_create_filter_fd(s, in, out, err, -1, -1, -1));
}

/** Wait for the exit of a process and return its status.
 *
 * @param pid the process id of the process to wait for
 *
 * @returns the exit status of the process identified by pid or -1 in the event of an error.
 */
int mutt_wait_filter(pid_t pid)
{
  int rc;

  waitpid(pid, &rc, 0);
  mutt_unblock_signals_system(1);
  rc = WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;

  return rc;
}
