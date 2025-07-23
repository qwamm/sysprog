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
#include <limits.h>
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

static void execute_exit(const struct command *cmd)
{
	int exit_code = 0;
	if (cmd->arg_count > 0)
		exit_code = atoi(cmd->args[0]);
	exit(exit_code);
}

static int execute_cd(const struct command *cmd)
{
	const char *path = ".";
        if (cmd->arg_count > 0)
		path = cmd->args[0];

        if (chdir(path) == 0) {
                        return 0;
        }
        return 1;
}

static bool command_existence(const char *cmd)
{
	if (strchr(cmd, '/') != NULL) return access(cmd, X_OK) == 0;

	const char *cmd_path = getenv("PATH");
	if (cmd_path == NULL) return false;

	bool access_to_path = false;
	char *path_copy = strdup(cmd_path);
	char *dir = strtok(path_copy, ":");


	while (dir != NULL)
	{
		char full_path[4096];
		snprintf(full_path, 4096, "%s/%s", dir, cmd);

		if (access(full_path, X_OK) == 0)
		{
			access_to_path = true;
			break;
		}

		dir = strtok(NULL, ":");
	}

	free(path_copy);

	return access_to_path;
}

static bool is_pipeline(const struct command_line *line)
{
	assert(line != NULL);

	struct expr *cur_expr = line->head;

	while (cur_expr != NULL && cur_expr->next != NULL)
	{
		if (cur_expr->next->type == EXPR_TYPE_PIPE)
		{
			return true;
		}
		cur_expr = cur_expr->next;
	}

	return false;
}

static int execute_command(const struct command *cmd)
{
	if (strcmp(cmd->exe, "exit") == 0)
	{
		execute_exit(cmd);
	}
	else if (strcmp(cmd->exe, "cd") == 0)
	{
		return execute_cd(cmd);
	}
	else if (!command_existence(cmd->exe))
	{
		return 1;
	}

	pid_t pid = fork();

	if (pid < 0) return 1;

	if (pid == 0)
	{
		shut_down_error_messages();

		//Выделение памяти размера, соответствующего кол-ву аргументов + NULL и имя команды
		char **args = malloc((cmd->arg_count + 2)*sizeof(char*));
		if (args == NULL) exit(1);

		args[0] = cmd->exe;

		for (uint32_t i = 1; i < cmd->arg_count + 1; i++)
		{
			args[i] = cmd->args[i-1];
		}

		args[cmd->arg_count + 1] = NULL;

		execvp(cmd->exe, args);
		free(args);
		exit(1);
	}
	else
	{
		int ret_status;
		waitpid(pid, &ret_status, 0);
		return WEXITSTATUS(ret_status);
	}

}

static int execute_command_line(const struct command_line *line)
{
	assert(line != NULL);

	const struct expr *e = line->head;

	if (e->type == EXPR_TYPE_COMMAND && strcmp(e->cmd.exe, "exit") == 0 && e->next == NULL)
	{
		execute_exit(&e->cmd);
	}
	else if (e->type == EXPR_TYPE_COMMAND && strcmp(e->cmd.exe, "cd") == 0 && e->next == NULL &&
	 line->out_type == OUTPUT_TYPE_STDOUT)
	{
		return execute_command(&e->cmd);
	}
	//else if (is_pipeline(line))
	//{
	//	return execute_piped_command(line);
	//}
	else if (line->out_type != OUTPUT_TYPE_STDOUT)
	{
		/* Создаём новый процесс */
		pid_t pid = fork();

		if (pid == -1) return 1;

		/* Обработка команды дочерним процессом */
		if (pid == 0)
		{
			shut_down_error_messages();

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

			int ret_status = execute_command(&e->cmd);

			exit(ret_status);
		}
		else
		{
			int ret_status;
			waitpid(pid, &ret_status, 0);
			return WEXITSTATUS(ret_status);
		}
	}

	return execute_command(&e->cmd);
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
