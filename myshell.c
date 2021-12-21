#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/wait.h>

void my_SIGCHLD_handler(int signum);
int register_SIGCHLD_handler();
int execBackground(char** arglist, int index);
int execRedirOutput(char** arglist, int index);
int execPiping(char** arglist, int index);
int execCommand(char** arglist);
void printError(int err);


int prepare(void)
{
    int err;
    if (signal(SIGINT,SIG_IGN) == SIG_ERR) /* Setting SIGINT to be ignored, so shell wont terminate upon SIGINT */
    {
        err = errno;
        printError(err);
        return -1;
    }

    if(register_SIGCHLD_handler() == -1) /* Registring the handler for SIGCHLD */
    {
        err = errno;
        printError(err);
        exit(1);
    }
    return 0;
}

/* The handler for SIGCHLD */
void my_SIGCHLD_handler(int signum)
{
    int err, status;
    if (signum == SIGCHLD)
    {
        /* Will terminate zombie processes(given -1). WNOHANG allows code to continue running, and only terminate zombies,
           while other running processes wont be affected */
        if (waitpid(-1, &status, WNOHANG) == -1)
        {
            err = errno;
            if ((err != ECHILD) && (err != EINTR))
            {
                printError(err);
                exit(1);
            }
        }
    }
}

/* Setting a handler for SIGCHLD */
int register_SIGCHLD_handler() 
{
    struct sigaction zombiekiller;
    memset(&zombiekiller, 0, sizeof(zombiekiller));
    zombiekiller.sa_handler = my_SIGCHLD_handler;
    zombiekiller.sa_flags = SA_RESTART;
    return sigaction(SIGCHLD, &zombiekiller,NULL);
}


int process_arglist(int count, char** arglist)
{
    int err;
    if (signal(SIGINT,SIG_IGN) == SIG_ERR) /* Setting SIGINT to be ignored, so shell wont terminate upon SIGINT */
    {
        err = errno;
        printError(err);
        return 0;
    }

    int done = 0, index = 0, returnCode = 1;
    while ((arglist[index] != NULL) && (done == 0)) /* Scan arglist for either |,>,&. If found, after execution, loop exits. */
    {
        if (strcmp(arglist[index],"|") == 0)
        {
            returnCode = execPiping(arglist,index);
            done = 1;
            break;
        }
        if (strcmp(arglist[index],">") == 0)
        {
            returnCode = execRedirOutput(arglist,index);
            done = 1;
            break;
        }
        if (strcmp(arglist[index],"&") == 0)
        {
            returnCode = execBackground(arglist,index);
            done = 1;
            break;
        }
        index++;
    }

    /* Case: No |,>,& found. Regular command will be executed */
    if (done == 0)
    {
        returnCode = execCommand(arglist);
    }
    
    return returnCode;
}



/** Executes command in background (&). index is the index of & in arglist */
int execBackground(char** arglist, int index)
{
    
    int pid, status,err;
    arglist[index] = NULL;

    pid = fork();
    if (pid == -1) /* Fork failed */
    {
        err = errno;
        printError(err);
        return 0;
    }
    if (pid == 0) /*Fork success */
    {
        /*Child*/
        if (signal(SIGINT,SIG_IGN) == SIG_ERR) /* Setting SIGINT to be ignored, so background processes wont terminate upon SIGINT */
        {
            err = errno;
            printError(err);
            exit(1);
        }

        if(execvp(arglist[0], arglist) == -1)
        {
            err = errno;
            printError(err);
            exit(1);
        }
    }
    else
    {
        /*Father*/
        if (waitpid(pid, &status, WNOHANG) == -1) /* Waiting WNOHANG - so code will continue executing while child runs in background */
        {
            err = errno;
            if ((err != ECHILD) && (err != EINTR))
            {
                printError(err);
                return 0;
            }
        }
    }

    return 1;
}

/** Executes command, redirects output to given file (>). index is the index of > in arglist */
int execRedirOutput(char** arglist, int index)
{

    int pid, status, fd_redir, err;
    char* path;

    path = arglist[index+1]; /* Getting filepath string */
    arglist[index] = NULL; /* Setting the > string to NULL, effectively ending the arglist on index*/

    fd_redir = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0777); /** Opening the file, with all permissions. If doesnt exist, create.  */
    if (fd_redir == -1)
    {
        err = errno;
        printError(err);
        exit(1);
    }

    pid = fork();
    if (pid == -1) /* Fork failed */
    {
        err = errno;
        printError(err);
        return 0;
    }
    if (pid == 0) /* Fork success */
    {
        /* Child */
        if (dup2(fd_redir, 1) == -1) /* Redirecting stdout to file */
        {
            err = errno;
            printError(err);
            exit(1);
        }

        if (close(fd_redir) == -1) /* Can close file descriptor since stdout alredy points there */
        {
            err = errno;
            printError(err);
            exit(1);
        }

        if (signal(SIGINT,SIG_DFL) == SIG_ERR) /*Setting SIGINT to DFL, so foreground will terminate upon SIGINT*/
        {
            err = errno;
            printError(err);
            exit(1);
        }

        if(execvp(arglist[0], arglist) == -1)
        {
            err = errno;
            printError(err);
            exit(1);
        }
        
    }
    else
    {
        /* Father */
        if (close(fd_redir) == -1) /* Can close file descriptor, father does not use it */
        {
            err = errno;
            printError(err);
            return 0;
        }

        if (waitpid(pid, &status, 0) == -1)
        {
            err = errno;
            if ((err != ECHILD) && (err != EINTR))
            {
                printError(err);
                return 0;
            }
        }
    }

    return 1;
}

/** Executing two commands, with single piping (|). index is the index of | in arglist */
int execPiping(char** arglist, int index)
{
    int pid, pid2,status;
    int fd[2];
    char **arglist2;
    int err;

    arglist[index] = NULL; /* Setting the | string to NULL, effectively ending the arglist on index*/
    arglist2 = &(arglist[index + 1]); /* Pointing to index of second command */

    if (pipe(fd) == -1) /* Creating pipe, with file descriptors in array fd */
    {
        err = errno;
        printError(err);
        return 0;

    }

    pid = fork();
    if (pid == -1) /* Fork failed */
    {
        err = errno;
        printError(err);
        return 0;
    }

    if (pid == 0)
    {
        /* Child 1 */
        if (dup2(fd[1], 1) == -1) /* Redirecting stdout to write end of the pipe */
        {
            err = errno;
            printError(err);
            exit(1);
        }


        if(close(fd[0]) == -1) /* Closing read end of the pipe, not needed for this child */
        {
            err = errno;
            printError(err);
            exit(1);
        }

        if (signal(SIGINT,SIG_DFL) == SIG_ERR) /*Setting SIGINT to DFL, so foreground will terminate upon SIGINT*/
        {
            err = errno;
            printError(err);
            exit(1);
        }

        if (execvp(arglist[0], arglist) == -1)
        {
            err = errno;
            printError(err);
            exit(1);
        }

    }
    else
    {
        /* Father, pid = process id of child 1*/
        if (close(fd[1]) == -1) /* Closing write end of the pipe, not needed for tather or child 2 */
        {
            err = errno;
            printError(err);
            return 0;
        }

        pid2 = fork();
        if (pid == -1)
        {
            err = errno;
            printError(err);
            return 0;
        }

        if (pid2 == 0)
        {
            /* Child 2*/
            
            if(dup2(fd[0], 0) == -1) /* Redirecting read end of the pipe to stdin */
            {
                err = errno;
                printError(err);
                exit(1);
            }

            if (signal(SIGINT,SIG_DFL) == SIG_ERR) /*Setting SIGINT to DFL, so foreground will terminate upon SIGINT*/
            {
                err = errno;
                printError(err);
                exit(1);
            }

            if (execvp(arglist2[0], arglist2) == -1)
            {
                err = errno;
                printError(err);
                exit(1);
            }
        }
        else
        {
            /*Father, pid2 = process id of child 2*/
            if (close(fd[0]) == -1) /* Closing read end of the pipe for father */
            {
                err = errno;
                printError(err);
                return 0;
            } 

            if (waitpid(pid, &status, 0) == -1) /* Waiting for child 1 (1st end of pipe) to finish */
            {
                err = errno;
                if ((err != ECHILD) && (err != EINTR))
                {
                    printError(err);
                    return 0;
                }
            } 
            
            if (waitpid(pid2, &status, 0) == -1) /* Waiting for child 2 (2nd end of pipe) to finish */
            {
                err = errno;
                if ((err != ECHILD) && (err != EINTR))
                {
                    printError(err);
                    return 0;
                }
            }
            

        }
    }

    return 1;
}

/** Executing a "normal" command: Foreground, no output redirection, no piping */
int execCommand(char** arglist)
{
    int pid,status,err;

    pid = fork();
    if (pid == -1) /*Fork failed*/
    {
        err = errno;
        printError(err);
        return 0;
    }
    if (pid == 0) /*Fork success, 0 returned for child*/
    {
        /*Child*/
        if (signal(SIGINT,SIG_DFL) == SIG_ERR) /*Setting SIGINT to DFL, so foreground will terminate upon SIGINT*/
        {
            err = errno;
            printError(err);
            exit(1);
        }
        
        if (execvp(arglist[0], arglist) == -1)
        {
            err = errno;
            printError(err);
            exit(1);
        }
    }
    else
    {
        /* Father; pid = child's process id */
        if (waitpid(pid, &status, 0) == -1) //Waiting for child
        {
            err = errno;
            if ((err != ECHILD) && (err != EINTR))
            {
                printError(err);
                return 0;
            }
        }
        
    }

    return 1;
}



/** Helper method, prints to stderror the recieved errno */
void printError(int err)
{
    perror(strerror(err));
}


int finalize(void)
{
    return 0;
}


