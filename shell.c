#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#define BUF_SIZE 8
#define FAC_MODE 0777

typedef struct ParseConfT {
  char *cmd;
  char sep, token;
  int command_mode, token_count;
} ParseConfT;

typedef struct PipeConfT {
  int used, cmd_num, total_num, prev0, prev1;
} PipeConfT;

int execCommand(char *cmd, PipeConfT *piping, int *jobs, int* jc);
char** parse(int *count, ParseConfT *parser);
int prompt(int *jobs, int* jc);
void catchEndedJobs(int *jobs, int *jc);
void triggerJobsTable(int tpid, int status, int j, int *jobs, int *jc);
int gotOpStxErr(char **cmds, int n);
int escapeExec(char *msg, int ret_code, char **argv, int argc);

int main(){
  int buf = BUF_SIZE;
  int jc = 0, exited = 0;
  int* jobs = malloc(buf * sizeof(int));
  while (!exited){
    if (jc == buf - 2){
      buf *= 2;
      jobs = realloc(jobs, buf * sizeof(int));
    }
    exited = prompt(jobs, &jc);
  }

  free(jobs);
  return 0;
}

int prompt(int *jobs, int* jc){
  int bufs = BUF_SIZE;
  char *input = malloc(bufs*sizeof(char));
  int c = 0, i = 0;
  printf(">");
  while ((c = getchar()) != '\n' && c != EOF){
    if (i == bufs - 2){
         bufs *= 2;
         input = realloc(input, sizeof(char)*bufs);
    }
    input[i++] = c;
  }
  input[i] = '\0';
  if (ferror(stdin)){
    perror(NULL);
    exit(1);
  }

  ParseConfT parser;
  parser.cmd = input;
  parser.sep = '|';
  parser.token = '|';
  parser.command_mode = 0;
  parser.token_count = 0;

  int exited = 0, last_jc = 0, awaits = 0, count = 0;
  char **cmds = parse(&count, &parser);

  if ( parser.token_count && gotOpStxErr(cmds, count) ){
      free(input);
      return escapeExec("wrong usage of '|'", 0, cmds, count);
  }

  ParseConfT parser2;
  parser2.sep = ';';
  parser2.token = ';';
  parser2.command_mode = 0;
  parser2.token_count = 0;

  PipeConfT piping;
  piping.used = parser.token_count;
  piping.cmd_num = 0;
  piping.total_num = count;
  piping.prev0 = 0;
  piping.prev1 = 0;
  int m_count = 0, k;
  for (i = 0; i < count; i++){
      parser2.cmd = cmds[i];
      char **multi = parse(&m_count, &parser2);
      int stx_err = gotOpStxErr(multi, m_count);
      for (k = 0; k < m_count; k++){
        last_jc = *jc;
        piping.cmd_num = i;
        piping.used = k == 0 ? parser.token_count : 0;
        exited = execCommand(stx_err ? "false" : multi[k], &piping, jobs, jc);
        catchEndedJobs(jobs, jc);
        if (piping.used && last_jc == *jc) awaits++;
        free(multi[k]);
      }
      piping.used = parser.token_count;
      free(cmds[i]);
      free(multi);
  }

  int tpid, status, j;
  for (i = 0; i < awaits; i++){
      tpid = waitpid(-1, &status, 0);
      if (tpid > 0)
        for (j = 0; j < *jc; j++)
          if (jobs[j] == tpid){
            triggerJobsTable(tpid, status, j, jobs, jc);
            i--; break;
          }
  }

  if (piping.used){
    if (piping.prev0) close(piping.prev0);
    if (piping.prev1) close(piping.prev1);
  }
  free(input);
  free(cmds);
  return exited;
}

int gotOpStxErr(char **cmds, int n){
  int i, k, len, empty;
  for (i = 0; i < n; i++){
    empty = 1;
    if ( (len = strlen(cmds[i])) == 0) return 1;
    for (k = 0; k < len; k++)
        if (cmds[i][k] != ' '){
          empty = 0;
          break;
        }
    if (empty) return 1;
  }
  return 0;
}

void remAllOccurs(char* haystack, char needle){
  char *p;
  while ( (p = strstr( haystack, "\"" )) != NULL ){
    memmove(p, p + 1, haystack + strlen( haystack ) - 1 - p);
    haystack[strlen( haystack ) - 1] = '\0';
  }
}

char ** parse(int *cnt, ParseConfT *parser){
  int len = strlen(parser->cmd), buf = BUF_SIZE, start = 0, count = 0;
  int i = 0, k = 0, quoted = 0, lastc = parser->sep;
  char **cmds = malloc(sizeof(char *) * BUF_SIZE);

  for (i = 0, k = parser->command_mode ? 1 : 0; i < len; i++, k++){
    if (count == buf - 2){
      buf *= 2;
      cmds = realloc(cmds, buf*sizeof(char *));
    }
    if (!quoted && parser->command_mode
          && lastc == parser->sep && parser->cmd[i] == parser->sep){
      start++; k--;
      continue;
    }
    if (!quoted && parser->cmd[i] == parser->sep && k > 0){
      cmds[count] = strndup(parser->cmd + start, k - 1);
      if (parser->command_mode) remAllOccurs(cmds[count], '"');
      //if (parser->command_mode) printf("%s\n", cmds[count]);
      start = i + 1; k = 0; count++;
    }
    if (!quoted && parser->cmd[i] == parser->token)
      parser->token_count++;

    if (quoted && parser->cmd[i] == '"' && i < len - 1 && parser->cmd[i+1] == ' ') quoted = 0;
    else if (parser->cmd[i] == '"') quoted = 1;
    lastc = parser->cmd[i];
  }
  cmds[count] = strndup(parser->cmd + start, k);
  if (parser->command_mode) remAllOccurs(cmds[count], '"');
  //if (parser->command_mode) printf("%s\n", cmds[count]);
  if (strcmp(cmds[count], "") == 0) free(cmds[count--]);
  cmds[++count] = NULL;
  *cnt = count;
  return cmds;
}

int escapeExec(char *msg, int ret_code, char **argv, int argc){
  int i;
  if (msg) printf("*** ERROR: %s\n", msg);
  for (i = 0; i < argc; i++) free(argv[i]);
  free(argv);
  return ret_code;
}

void triggerJobsTable(int tpid, int status, int j, int *jobs, int *jc){
  int h;
  printf("[%d] was completed with status %d\n", tpid, WEXITSTATUS(status));
  for (h = j + 1; h < *jc; h++) jobs[h - 1] = jobs[h];
  (*jc)--;
}

void catchEndedJobs(int *jobs, int *jc){
  int j, tpid, status;
  for (j = 0; j < *jc; j++){
    tpid = waitpid(jobs[j], &status, WNOHANG);
    if (tpid > 0) triggerJobsTable(tpid, status, j, jobs, jc);
  }
}

int execCommand(char *cmd, PipeConfT *piping, int *jobs, int* jc){
  ParseConfT parser;
  parser.sep = ' ';
  parser.token = '&';
  parser.command_mode = 1;
  parser.token_count = 0;
  parser.cmd = cmd;
  int argc = 0;
  char **argv = parse(&argc, &parser);

  if ( argc == 0 ){
    free(argv);
    return 0;
  }

  if (parser.token_count > 1 || (parser.token_count == 1 && argc == 1))
    return escapeExec("wrong usage of '&'", 0, argv, argc);
  if (parser.token_count == 1){
      if (strcmp(argv[argc - 1], "&") == 0){
        free(argv[argc - 1]); argv[argc - 1] = NULL;
      } else if (argv[argc - 1][strlen(argv[argc - 1]) - 1] == '&')
        argv[argc - 1][strlen(argv[argc - 1]) - 1] = '\0';
      else return escapeExec("wrong usage of '&'", 0, argv, argc);
  }

  int redir = -1, redir_used = 0;
  if (argc >= 3) {
    if (strcmp(argv[argc - 2], "<") == 0) redir = 0;
    else if (strcmp(argv[argc - 2], ">") == 0) redir = 1;
    else if (strcmp(argv[argc - 2], ">>") == 0) redir = 2;

    if (redir > -1){
      redir_used = 1;
      free(argv[argc - 2]);
      argv[argc - 2] = NULL;
    }
  }

  if (strcmp(argv[0], "qq") == 0) return escapeExec(NULL, 1, argv, argc);

  if ( strcmp(argv[0], "cd") == 0 ){
  	if ( argc == 2 && argv[1] && strcmp(argv[1], "~") == 0 )
  		  argv[1] = getenv("HOME");

    if ( argc < 2 || !argv[1] || chdir(argv[1]) < 0 )
      return escapeExec(" cd failed", 0, argv, argc);
    else if (!parser.token_count) printf("Changed directory to %s\n", argv[1]);
    return escapeExec(NULL, 0, argv, argc);
  }

  int fd;
  if (redir_used){
    if (redir > 0)
      fd = open(argv[argc-1], O_CREAT | O_WRONLY | (redir == 1 ? O_TRUNC : O_APPEND), FAC_MODE);
    else fd = open(argv[argc-1], O_RDONLY);
    if (fd == -1) return escapeExec("cannot open file", 0, argv, argc);
  }

  int pid, status, pp[2] = {0, 0};
  if (piping->used) pipe(pp);

  switch ( (pid = fork()) ){
    case -1:
      return escapeExec(" fork failed", 1, argv, argc);
    case 0:
      if (piping->used && piping->cmd_num){
        dup2(piping->prev0, 0);
        close(piping->prev0);
        close(piping->prev1);
      }
      if (piping->used && piping->cmd_num != (piping->total_num - 1)){
        dup2(pp[1], 1);
      }
      if ( parser.token_count == 1 ){
        close(1);
        close(2);
      }
      if (piping->used){
        close(pp[1]);
        close(pp[0]);
      }

      if (redir_used){
        if (redir > 0) dup2(fd, 1);
        else dup2(fd, 0);
        close(fd);
      }
      execvp(*argv, argv);
      if ( !piping->used ) printf("*** ERROR: exec failed\n");
      execlp("false", "false", NULL);
      exit(1);

    default:
      if (piping->used){
        if (piping->prev0) close(piping->prev0);
        if (piping->prev1) close(piping->prev1);
        piping->prev0 = pp[0];
        piping->prev1 = pp[1];
      }
      if (redir_used) close(fd);
      if ( parser.token_count == 1 ){
        printf("[%d] started running\n", pid);
        jobs[(*jc)++] = pid;
      } else if (!piping->used) waitpid(pid, &status, 0);

  }

  return escapeExec(NULL, 0, argv, argc);
}
