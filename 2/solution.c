#include "parser.h"

#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>

static void shut_down_error_messages()
{
	int devnull = open("/dev/null", O_WRONLY);
	if (devnull > 0)
	{
		dup2(devnull, STDERR_FILENO);
		close(devnull);
	}
}

static bool command_existence(const char *cmd)
{
	if (strchr(cmd, '/') != NULL) return access(cmd, X_OK) == 0;

	const char *cmd_path = getenv("PATH");
	if (PATH == NULL) return false;

	bool access_to_path = false;
	char *path_copy = strdup(path);
	char *dir = strtok(path_copy, ":");

	while (dir != NULL)
	{
		char full_path[4096];
		snprintf(full_path, 4096, "%s/%s", dir, cmd);

		if (access(full_path, X_OK) == 0)
		{
			found = true;
			break;
		}

		dir = strtok(NULL, ":");
	}

	free(path_copy);

	return access_to_path;
}

static int execute_command(struct command *cmd)
{
	if (!command_existence(cmd->exe)) return 1;

}

static void execute_command_line(const struct command_line *line)
{
	assert(line != NULL);

	const struct expr *e = line->head;

	if (e->type == EXPR_TYPE_COMMAND && strcmp(e->cmd.exe, "exit") == 0 && e->next == NULL)
	{
		int exit_code = 0;
		if (e->cmd.arg_count > 0)
			exit_code = atoi(e->cmd.args[0]);
		exit(exit_code);
	}
	else if (e->type == EXPR_TYPE_COMMAND && strcmp(e->cmd.exe, "cd") == 0 && e->next == NULL &&
	 line->out_type == OUTPUT_TYPE_STDOUT)
	{
		const char *path = ".";
		if (cmd->arg_count > 0)
			path = cmd->args[0];

		if (chdir(path) == 0) {
			return 0;
		}
		return 1;
	}
	else if (is_pipe(line))
	{
		return execute_piped_command(line);
	}
	else if (line->out_type != OUTPUT_TYPE_STDOUT)
	{
		/* Создаём новый процесс */
		pid_t pid = fork();

		if (pid == -1) return 1;

		/* Обработка команды дочерним процессом */
		if (pid == 0)
		{
			shut_down_error_messages()

			int fd;
			if (line->out_type == OUTPUT_TYPE_FILE_NEW)
			{
				fd = open(line->out_file, O_WRONLY | O_CREAT | O_TRUNC, 0666);
			}
			else
			{
				fd = open(line->out_file, O_WRONLY | O_CREAT | O_APPEND, 0666);
			}

			if (fd < 0) exit(1);

			dup2(fd, STDOUT_FILENO);
			close(fd);

			return execute_command;
		}
		else
		{
			int ret_status;
			wait(pid, &ret_status, 0);
			return WEXITSTATUS(ret_status);
		}
	}

}

int main(void)
{
	const size_t buf_size = 1024;
	char buf[buf_size];
	int rc;
	struct parser *p = parser_new();
	while ((rc = read(STDIN_FILENO, buf, buf_size)) > 0) {
		parser_feed(p, buf, rc);
		struct command_line *line = NULL;
		while (true) {
			enum parser_error err = parser_pop_next(p, &line);
			if (err == PARSER_ERR_NONE && line == NULL)
				break;
			if (err != PARSER_ERR_NONE) {
				printf("Error: %d\n", (int)err);
				continue;
			}
			execute_command_line(line);
			command_line_delete(line);
		}
	}
	parser_delete(p);
	return 0;
}
