// 
// tsh - A tiny shell program with job control
// 
// <Michael Brughelli (mibr7750), Stephen Rowell (rowells)>
//

using namespace std;

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <string>

#include "globals.h"
#include "jobs.h"
#include "helper-routines.h"

//
// Needed global variable definitions
//

static char prompt[] = "tsh> ";
int verbose = 0;

//
// You need to implement the functions eval, builtin_cmd, do_bgfg,
// waitfg, sigchld_handler, sigstp_handler, sigint_handler
//
// The code below provides the "prototypes" for those functions
// so that earlier code can refer to them. You need to fill in the
// function bodies below.
// 

void eval(char *cmdline);
int builtin_cmd(char **argv);
void do_bgfg(char **argv);
void waitfg(pid_t pid);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);

//
// main - The shell's main routine 
//
int main(int argc, char **argv) 
{
  int emit_prompt = 1; // emit prompt (default)

  //
  // Redirect stderr to stdout (so that driver will get all output
  // on the pipe connected to stdout)
  //
  dup2(1, 2);

  /* Parse the command line */
  char c;
  while ((c = getopt(argc, argv, "hvp")) != EOF) {
    switch (c) {
    case 'h':             // print help message
      usage();
      break;
    case 'v':             // emit additional diagnostic info
      verbose = 1;
      break;
    case 'p':             // don't print a prompt
      emit_prompt = 0;  // handy for automatic testing
      break;
    default:
      usage();
    }
  }

  //
  // Install the signal handlers
  //

  //
  // These are the ones you will need to implement
  //
  Signal(SIGINT,  sigint_handler);   // ctrl-c
  Signal(SIGTSTP, sigtstp_handler);  // ctrl-z
  Signal(SIGCHLD, sigchld_handler);  // Terminated or stopped child

  //
  // This one provides a clean way to kill the shell
  //
  Signal(SIGQUIT, sigquit_handler); 

  //
  // Initialize the job list
  //
  initjobs(jobs);

  //
  // Execute the shell's read/eval loop
  //
  for(;;) {
    //
    // Read command line
    //
    if (emit_prompt) {
      printf("%s", prompt);
      fflush(stdout);
    }

    char cmdline[MAXLINE];

    if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin)) {
      app_error("fgets error");
    }
    //
    // End of file? (did user type ctrl-d?)
    //
    if (feof(stdin)) {
      fflush(stdout);
      exit(0);
    }

    //
    // Evaluate command line
    //
    eval(cmdline);
    fflush(stdout);
    fflush(stdout);
  } 

  exit(0); //control never reaches here
}
  
/////////////////////////////////////////////////////////////////////////////
//
// eval - Evaluate the command line that the user has just typed in
// 
// If the user has requested a built-in command (quit, jobs, bg or fg)
// then execute it immediately. Otherwise, fork a child process and
// run the job in the context of the child. If the job is running in
// the foreground, wait for it to terminate and then return.  Note:
// each child process must have a unique process group ID so that our
// background children don't receive SIGINT (SIGTSTP) from the kernel
// when we type ctrl-c (ctrl-z) at the keyboard.
//
void eval(char *cmdline) 
{
  /* Parse command line */
  //
  // The 'argv' vector is filled in by the parseline
  // routine below. It provides the arguments needed
  // for the execve() routine, which you'll need to
  // use below to launch a process.
  //
  char *argv[MAXARGS];  //Argument list
  pid_t PID;            //process id
  sigset_t mask;        //block signals

  
  // The 'bg' variable is TRUE if the job should run
  // in background mode or FALSE if it should run in FG
  //
  
  
  int bg = parseline(cmdline, argv); 
  if(bg == -1)
  {
	  return;
  }
  	
  sigemptyset(&mask);
  sigaddset(&mask, SIGCHLD);
  sigaddset(&mask, SIGINT);
  sigaddset(&mask, SIGTSTP);	 
  	  
  if(!builtin_cmd(argv)) {
                sigprocmask(SIG_BLOCK, &mask, NULL);
                /* Child process. PID = 0. */
                if((PID = fork()) == 0) {
                        sigprocmask(SIG_UNBLOCK, &mask, NULL);
                        setpgid(0, 0);
                        if(execv(argv[0], argv) < 0) {
                                printf("Invalid command: %s \n", argv[0]);
                                exit(0);
                        }
                }
 
                /* Parent process. PID = PID of child.
                 * If a foreground job, addjob and wait for completion. Otherwise, addjob and output jobs list. */
                if(!bg)
                {
                        if(addjob(jobs, PID, FG, cmdline)) {
                                sigprocmask(SIG_UNBLOCK, &mask, NULL);
                                waitfg(PID);
                        }
                }
                else
                {
                        if(addjob(jobs, PID, BG, cmdline)) {
                                sigprocmask(SIG_UNBLOCK, &mask, NULL);
                                printf("[%d] (%d) %s", pid2jid(PID), PID, cmdline);
                        }
                }
        }
        return;
}


/////////////////////////////////////////////////////////////////////////////
//
// builtin_cmd - If the user has typed a built-in command then execute
// it immediately. The command name would be in argv[0] and
// is a C string. We've cast this to a C++ string type to simplify
// string comparisons; however, the do_bgfg routine will need 
// to use the argv array as well to look for a job number.
//
int builtin_cmd(char **argv) 
{
	if (strcmp(argv[0], "quit") == 0) {
                exit(0);
        }
        else if (strcmp(argv[0], "jobs") == 0) {
                listjobs(jobs);
                return 1;
        }
        else if (strcmp(argv[0], "bg") == 0 || strcmp(argv[0], "fg") == 0) {
                do_bgfg(argv);
                return 1;
        }
        else if (strcmp(argv[0], "ls") == 0) {
                argv[0] = "/bin/ls";
                return 0;
        }
        else if (strcmp(argv[0], "ps") == 0) {
                argv[0] = "/bin/ps";
                return 0;
        }
        else
        {
                return 0;     /* not a builtin command */
        }
  //string cmd(argv[0]);
  //return 0;     /* not a builtin command */
}

/////////////////////////////////////////////////////////////////////////////
//
// do_bgfg - Execute the builtin bg and fg commands
//
void do_bgfg(char **argv) 
{
	struct job_t *job = NULL;
        pid_t pid;
        int jid;
 
        if(argv[1] == NULL) {
                printf("No argument found! \n");
                return;
        }
 
        /* The following if-else statements fetch the job to be worked on. */
        if(argv[1][0] == '%') {
                /* Argument begins with '%', so it's a JID. */
                jid = atoi(&argv[1][1]);
                job = getjobjid(jobs, jid);
 
                if(job == NULL) {
                        printf("Job with JID %d does not currently exist. \n", jid);
                        return;
                }
        }
        else if(isdigit(argv[1][0])) {
                /* Argument begins with a digit, so it's a PID. */
                pid = atoi(argv[1]);
                job = getjobpid(jobs, pid);
 
                if(job == NULL) {
                        printf("Job with PID %d does not currently exist. \n", pid);
                        return;
                }
        }
        else
        {
                printf("Invalid argument! \n");
                return;
        }
 
        /* Set pid and jid to that of the job to be worked on. */
        pid = job->pid;
        jid = job->jid;
 
        /* The following if-else statements perform actions on the job based on the command entered. */
        if(strcmp("bg", argv[0]) == 0) {
                /* Command entered was BG. If job's state is stopped, continue in the background. */
                if(job->state == ST) {
                        if(kill(-pid, SIGCONT) < 0) {
                                printf("An error has occurred in kill. \n");
                        }
                        job->state = BG;
                        printf("[%d] (%d) %s", jid, pid, job->cmdline);
                }
                else
                {
                        printf("This job is already running in the background. \n");
                        return;
                }
        }
        else
        {
                if(kill(-pid, SIGCONT) < 0) {
                        printf("An error has occurred in kill. \n");
                }
                job->state = FG;
                waitfg(pid);
        }
        return;
}

/////////////////////////////////////////////////////////////////////////////
//
// waitfg - Block until process pid is no longer the foreground process
//
void waitfg(pid_t pid)
{
	struct job_t *fgjob = getjobpid(jobs, pid);
 
        /* Job no longer exists in current job list. */
        if(getjobpid(jobs, pid) == NULL)
        { return; }
 
        while(fgjob->state == FG && fgjob->pid == pid)
        { sleep(1); }
 
        return;
}

/////////////////////////////////////////////////////////////////////////////
//
// Signal handlers
//


/////////////////////////////////////////////////////////////////////////////
//
// sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
//     a child job terminates (becomes a zombie), or stops because it
//     received a SIGSTOP or SIGTSTP signal. The handler reaps all
//     available zombie children, but doesn't wait for any other
//     currently running children to terminate.  
//
void sigchld_handler(int sig) 
{
	int pid, jid, status;
        struct job_t *fgjob = NULL;
 
        while((pid = waitpid(-1, &status, WNOHANG|WUNTRACED)) > 0) {
                jid = pid2jid(pid);
                if(WIFSTOPPED(status)) {
                        printf("Job [%d] (%d) has been stopped. \n", jid, pid);
                        fgjob = getjobpid(jobs, pid);
                        fgjob->state = ST;
                }
                else if(deletejob(jobs, pid)) {
                        if(WIFSIGNALED(status)) {
                                printf("Job [%d] (%d) has been terminated. \n", jid, pid);
                        }
                }
        }
        return; 
}

/////////////////////////////////////////////////////////////////////////////
//
// sigint_handler - The kernel sends a SIGINT to the shell whenver the
//    user types ctrl-c at the keyboard.  Catch it and send it along
//    to the foreground job.  
//
void sigint_handler(int sig) 
{
	pid_t pid = 0;
 
        if((pid = fgpid(jobs)) > 0) {
                if(kill(-pid, sig) < 0) {
                        printf("An error has occurred in kill. \n");
                }
        }
        return;
}


/////////////////////////////////////////////////////////////////////////////
//
// sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
//     the user types ctrl-z at the keyboard. Catch it and suspend the
//     foreground job by sending it a SIGTSTP.  
//
void sigtstp_handler(int sig) 
{
	pid_t pid = 0;
 
        if((pid = fgpid(jobs)) > 0) {
                if(kill(-pid, sig) < 0) {
                        printf("An error has occurred in kill. \n");
                }
        }
        return;
}

/*********************
 * End signal handlers
 *********************/




