/********************************************************
 * PROGRAM: Shell			                            *
 * CLASS: CISC 361-011                                  *
 * AUTHORS:                                             *
 *    Alex Sederquest | alexsed@udel.edu | 702414270    *
 *    Ben Segal | bensegal@udel.edu | 702425559         *
 ********************************************************/

#include <signal.h> 
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <limits.h>
#include <unistd.h>
#include <stdlib.h>
#include <pwd.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <glob.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <pthread.h>
#include <fcntl.h>
#include "lists.h"

// CONSTANTS
#define MAX_CMD 128
#define BUFFERSIZE 512
#define BUILT_IN_COMMAND_COUNT 14

// HELPER FUNCTIONS
char **parseBuffer(char buffer[], char **commandList);
void executeBuiltInFunctions(char **commandList, char **envp, struct pathelement *pathList, char **argv);
int shouldRunAsBackground(char **commandList);
void runExecutable(char **commandList, char **envp, struct pathelement *pathList, char **argv);
void *watchUserCallback(void *arg);
int isBuiltIn(char *command); 
void runBuiltIn(char **commandList, struct pathelement *pathList, char **envp);
void sigHandler(int signal);
void childHandler(int signal);
void alarmHandler(int);
char *getExternalPath(char **commandList, struct pathelement *pathList);


// REDIRECTION
int getRedirectionType(char **commandList);
void removeAfterRedirect(char **commandList);
char *getRedirectionDest(char **commandList);
int handleRedirection(int redirectionType, char *destFile);


// PIPES
int getPipeType(char **commandList);
char **splitPipe(char **commandList, int beforeOrAfter);
int handlePipes(char **commandList, char **envp, struct pathelement *pathList, char **argv);
void freePipeArrays(char **beforePipe, char **afterPipe);


// BUILT IN COMMAND FUNCTIONS
void noClobber();
void watchMail(char **commandList);
void watchUser(char **commandList);
char *which(char *command, struct pathelement *pathList);
char *where(char *command, struct pathelement *pathList);
void list (char *dir);
void printenv(char **envp);
void printWorkingDirectory();
void prompt(char *commandList[]);
int exitProgram();
void printPid();
void changeDirectory(char **commandList);
void printEnvironment(char **commandList, char **envp); 
int setEnvironment(char **commandList, char **envp, struct pathelement *pathList);
void killIt(char **commandList);


// CONVIENIENCE FUNCTIONS
void printShell();
void listHandler(char **commandList);
void whichHandler(char **commandList, struct pathelement *pathList);
void whereHandler(char **commandList, struct pathelement *pathList);
void freeAll(struct pathelement *pathList, char *cwd);
void freePath(struct pathelement *pathList);
void handleInvalidArguments(char *arg);
void freeAndExit(struct pathelement *pathList, char **commandList);