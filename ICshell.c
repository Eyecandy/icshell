#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <sys/resource.h>
#include <fcntl.h>


#define TOK_DELIMTER " \t\r\n\a"
#define MAX_ARG 10
#define EXIT "exit"
#define BACKGROUND "bg"
#define FOREGROUND "fg"
#define JOBS "jobs"
#define ECHO "echo"

pid_t shell_proccessgroupID;
int shell_terminal;
int shell_is_interactive;

/* A process is a single process.  */
typedef struct process
{

  struct process *next;
  char *line;
  int index;
  pid_t pid;                  
  char state;                 
} process;

process * head = NULL;
int count = 1;
int sz;


void addToJob(pid_t pid,int status,char *name) {
  process *process_toInsert = (process*) malloc(sizeof(process));
  process_toInsert->line = name;
  process_toInsert->pid = pid;
  process_toInsert->state = status;
  process* p = head;
  char indexExist = 'f';
  while(p!=NULL) {
  	if (p->index == count) {
  		indexExist = 't';
  	}
  	p = p->next;

  }
  if (indexExist=='f') {
  	process_toInsert->index = count;
  }
  else if (sz == 1) {
  	process_toInsert->index = sz;
  }
  else {
  	process_toInsert->index = sz+1;
  }
  process_toInsert->next = head;
  head = process_toInsert;
  count++;
  sz++;
}

void remove_process(pid_t pid) {
  process *p;
  p = head;
  if (p != NULL  && p->pid == pid) {
    head = head->next;
    count = p->index;
    sz--;
    free(p);
    return;
  }
  
  int remove = -1;
  while (p!=NULL){
    if (p->next->pid == pid) {
      remove = 0;
      break;
    }
    p = p->next;
  }
  if (remove==0) {
  process *before = p;
  process *current = p->next;
  count = current->index;
  process *after = current->next;
  before->next = after;
  sz--;
  free(current);
  }
}

int findBGPid(pid_t pid) {
  process *p;
  p = head;
  while (p!=NULL){
    if (p->pid == pid && p->state == 'b') {
      return p->index;
    }
    p = p->next;
  }
  return -1;
}

void loop_process() {
  process *p;
  p = head;

  while (p!=NULL){
    printf("[%d]  ",p->index);
    if (p->state == 'b') {
      printf("running   ");
    }
    else {
      printf("suspended   ");
    }
    printf("%s %d\n",p->line, p->pid);
    p = p->next;
  }
}

int exit_status;
void exit_process()
{
   
    int wstatz;
    union wait wstat;
    pid_t pid;
      while (1) {
      pid = wait3 (&wstatz, WNOHANG, (struct rusage *)NULL );

      if (pid == 0) {
        
        return;
      }
      else if (pid == -1) {
        return;
      }
      else {
        
        int index= findBGPid(pid); 

        if (index != -1) {
           printf ("\n[%d] %d, Done!, exit code:%d\n",index,pid, wstat.w_retcode);
           remove_process(pid);
        }
        else {
        	//exit_status = wstat.w_retcode;
        }
        tcsetpgrp (shell_terminal, shell_proccessgroupID);
      }
   }
}


void foreground(char *cmd_arg[]) {
  
  if (cmd_arg[1] != NULL && cmd_arg[1][0] == '%') {
    char *index  = strtok(cmd_arg[1],"%");
    int index_int =  atoi(index);
    process *p = head;
    pid_t pid= -1;

    while (p!=NULL){
      if (index_int == p->index) {
        p->state ='f';
        pid = p->pid;
        break;
      }
      p = p->next;
    }
    //check that what was given is a valid index
    if (pid!=-1) {
      int status;
      printf("%d continued %s\n",p->pid,p->line);

      tcsetpgrp (shell_terminal, pid);
      kill(pid,SIGCONT);
      waitpid(pid, &status, WUNTRACED);
       if (WIFSTOPPED(status)) {
        printf("\n%d suspended\n",pid);
      }
      //if exited normally
      else if (WIFEXITED(status)) {
        exit_status = WEXITSTATUS(status);
        remove_process(pid);
      }
      //if signal killed process
      else if (WIFSIGNALED(status)) {
        exit_status = WTERMSIG(status);
        remove_process(pid);
      }
      tcsetpgrp (shell_terminal, shell_proccessgroupID);
    }
  }
}

void background(char *cmd_arg[]) {

  if (cmd_arg[1] != NULL && cmd_arg[1][0] == '%') {
    char *index  = strtok(cmd_arg[1],"%");
    int index_int =  atoi(index);
    process *p = head;
    pid_t pid= -1;

    while (p!=NULL){
      if (index_int == p->index) {
        p->state ='b';
        pid = p->pid;
      }
      p = p->next;
    }
    //check that what was given is a valid index
    if (pid!=-1) {
      kill(pid,SIGCONT);
    }
  }
}

void jobs(char *cmd_arg[]) {
  loop_process();
}

void echo(char *cmd_arg[]) {
  printf("exit status: %d\n", exit_status);
}

void launch_process(int is_background,int pgid) {
    //get caling pid
   int pid = getpid ();
   if (pgid == 0) {
     pgid = pid;
   }
   //set pid in process grp
   setpgid (pid, pgid);
 
  //if it is a foreground process
  if (is_background==1) {
    tcsetpgrp (shell_terminal, pgid);
  }
  signal (SIGINT, SIG_DFL);
  signal (SIGTSTP, SIG_DFL);
  signal (SIGQUIT, SIG_DFL);
  signal (SIGTTIN, SIG_DFL);
  signal (SIGTTOU, SIG_DFL);
  signal(SIGCHLD,exit_process);
}

void checkIfRedirection(char **cmd_arg) {
  int index = 1;
  while (cmd_arg[index] != NULL) {
    //if there is a > and there exist string for output file
    if (strcmp(cmd_arg[index],">") == 0 && cmd_arg[index+1]!=NULL) {
      int file = open(cmd_arg[index+1], O_CREAT|O_WRONLY, S_IRWXU);
      dup2(file, 1);
      cmd_arg[index] = NULL;
      close(file);
      break;
    }
    if (strcmp(cmd_arg[index],"<") == 0&& cmd_arg[index+1] != NULL) {
      int file = open(cmd_arg[index+1], O_RDONLY, 0);
      dup2(file,0);
      cmd_arg[index] = NULL;
      close(file);
      break;
    }
    index ++;
  }
}

void exec_external(char **cmd_arg, int is_background,char *line)
{
  pid_t pid;
  int status;
  pid = fork();

  //child
  if (pid == 0) {
    //set signals for children and sets pid in fg if foreground process
  	launch_process(is_background,pid);
    //checks if we want to redirect output
    checkIfRedirection(cmd_arg);
    
    if (execvp(cmd_arg[0], cmd_arg) == -1) {
      perror("Exec error");
      exit_status = 127;
      exit(127);
    }
    exit(0);
  } 
  //error , less 0
  else if (pid < 0) {
    perror("Exec error");
  } 
  //is parent and background
  if (is_background == 0) {
    int cp = count;
    addToJob(pid,'b',line);
    printf("[%d] %d\n",cp,pid );
  }
  //is parent and foreground
  else {
    //set program in foreground, change status in jobs list, wait for it (blocking)
    tcsetpgrp (shell_terminal, pid);
    addToJob(pid,'f',line);
    waitpid(pid, &status, WUNTRACED);
    //check if ctrl z, and print
    if (WIFSTOPPED(status)) {
      	printf("\n%d suspended %s\n",pid,line);
      }
    //if exited normally
    else if (WIFEXITED(status)) {
        exit_status = WEXITSTATUS(status);
        remove_process(pid);
      }
    //if signal killed process
    else if (WIFSIGNALED(status)) {
        exit_status = WTERMSIG(status);
        remove_process(pid);
    }
    tcsetpgrp (shell_terminal, shell_proccessgroupID);
  }
}

int internal_cmd(char * cmd_arg[]) {
  char * cmd = cmd_arg[0];
 
  int isInternal = -1;
  if (strcmp(cmd,FOREGROUND) ==0) {
    foreground(cmd_arg);
    isInternal=0;
  }
  else if (strcmp(cmd,BACKGROUND) ==0) {
    background(cmd_arg);
    isInternal = 0;
  }
  
  else if (strcmp(cmd,JOBS) ==0) {
    jobs(cmd_arg);
    isInternal = 0;
  }
  
  else if (strcmp(cmd,ECHO)==0 && cmd_arg[1] != NULL && strcmp(cmd_arg[1], "$?")==0) {
    
    echo(cmd_arg);
    isInternal = 0;
  }
  return isInternal;
}

void parseInput(char* inputLine, char** token, int * is_background) {
  int argCount = 0;
  char* stk;
  stk = strtok(inputLine,TOK_DELIMTER);

  while (stk != NULL && argCount <= MAX_ARG ) {
    token[argCount] = stk;
    stk = strtok(NULL, TOK_DELIMTER);
    argCount++;
  }
  token[argCount] = NULL;
  if (argCount > 0 && token[argCount-1][0] == '&') {
    token[argCount-1] = NULL;
    *is_background = 0;
  }
  else {
    *is_background = 1;
  }
}

char *waitForInput()
{ 
  char *inputLine = NULL;
  size_t bufferSize = 0; 
  int len = getline(&inputLine, &bufferSize, stdin);
  if ( len == -1){
      printf("ERROR: %s\n", strerror(errno));
      exit(EXIT_SUCCESS);
  }

  return inputLine;
}

void init_shell ()
{

  shell_terminal = STDIN_FILENO;
  //if file description refers to terminal
  shell_is_interactive = isatty (shell_terminal);

  if (shell_is_interactive)
    {
      //move shell to foreground
      while (tcgetpgrp (shell_terminal) != (shell_proccessgroupID = getpgrp ()))
        kill (- shell_proccessgroupID, SIGTTIN);
      
      signal (SIGINT, SIG_IGN);
      signal (SIGQUIT, SIG_IGN);
      signal (SIGTSTP, SIG_IGN);
      signal (SIGTTIN, SIG_IGN);
      signal (SIGTTOU, SIG_IGN);
      signal(SIGCHLD,exit_process);
     
      shell_proccessgroupID = getpid ();
      if (setpgid (shell_proccessgroupID, shell_proccessgroupID) < 0)
        {
          perror ("Shell could not be placed in it's own process group");
          exit (1);
        }
      tcsetpgrp (shell_terminal, shell_proccessgroupID);
    }
}

int main(int argc, char **argv)
{

  init_shell();
  char *token[10];
  int is_background;
  while(1) {
    printf("icsh> ");
    
    char *inputLine = waitForInput();
    char *dest = (char*) malloc(sizeof(char)*100);
    strcpy(dest,inputLine);
    parseInput(inputLine, token, &is_background);
    int len = strlen(dest);
    dest[len-1] = '\0';
    
    if (token[0] == NULL) {
        continue;
    }

    if (strcmp(EXIT,token[0]) ==0) {
      printf("exit code: 0\n");
        free(inputLine);
        break;
    }

    int isInternal = internal_cmd(token);
    if (isInternal == 0) {
      continue;
    }
    else {
      exec_external(token,is_background,dest);
    }
    
  }
  
  return 0;
}