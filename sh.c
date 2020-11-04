#include "sh.h"

/********************************************************
 * PROGRAM: Shell			                            *
 * CLASS: CISC 361-011                                  *
 * AUTHORS:                                             *
 *    Alex Sederquest | alexsed@udel.edu | 702414270    *
 *    Ben Segal | bensegal@udel.edu | 702425559         *
 ********************************************************/

// Globals
char *prefix = NULL;
char *last_dir = NULL;
int timeout = 0;
int isChildDone = 0;
int pid;
int threadExists = 0;
pthread_t watchUserID;
pthread_mutex_t mutexLock;
int hasNoClobber = 0;

void sigHandler(int signal) {
  if (signal == SIGINT) {
    // For ctrl+c
    printf(" Interrupt\n");
    printShell();
    fflush(stdout);
  }
}

int main(int argc, char **argv, char **envp)
{
    last_dir = getcwd(NULL, 0);
    // Signal setup
    signal(SIGCHLD, childHandler);
    sigignore(SIGTSTP); // Ignore control z
    sigignore(SIGTERM); // Ignore control z and control c
    signal(SIGINT, sigHandler); // more control c handler
    char buffer[BUFFERSIZE], **commandList;
    struct pathelement *pathList = getPath();
    commandList = calloc(MAX_CMD, sizeof(char *));

    printf("Welcome to sssh\nThe shell so bad it will make you mad\n");

    // Main loop for shell
    while (1)
    {
        printShell();
        if (fgets(buffer, BUFFERSIZE, stdin) == NULL)
        { 
            // Ignore ctrl+d / EOF
            printf("^D\nUse \"exit\" to leave shell.\n");
            continue; // Continue just ignores the rest of the loop and continues to the next iteration
        }

        // Handle \n appended by fgets()
        buffer[strlen(buffer) - 1] = '\0';
        if (strlen(buffer) < 1)
            continue;
        commandList = parseBuffer(buffer, commandList);
        // Execute whatever command was entered by the user
        executeBuiltInFunctions(commandList, envp, pathList, argv);
    }
}

/**
 * parseBuffer, gets the string produced by fgets and tokenizes it via strtok. This handles allocating memory for the commandList
 *              be it the commands entered by the user or the glob paths found by the glob(3) library function. This function
 *              returns the command list and of course, it must be reset and freed in the sh function. If globbing is detected
 *              and no patterns are found, an error message is displayed but the command preceding the glob will still go through.
 * 
 * Args: A string, An array of strings
 * Return: An array of strings
 */
char **parseBuffer(char buffer[], char **commandList)
{
    int csource;
    char *token;
    glob_t paths;
    token = strtok(buffer, " ");
    for (int i = 0; token != NULL;)
    {
        if ((strstr(token, "*") != NULL) || (strstr(token, "?") != NULL))
        {
            csource = glob(token, 0, NULL, &paths);
            if (csource == 0)
            {
                for (char **p = paths.gl_pathv; *p != NULL; p++)
                {
                    commandList[i] = (char *)malloc((int)strlen(*p) + 1);
                    strcpy(commandList[i], *p);
                    i++;
                }
                globfree(&paths);
            }
            else
            {
                errno = ENOENT;
                perror("glob");
            }
        }
        else
        {
            commandList[i] = (char *)malloc((int)strlen(token) + 1);
            strcpy(commandList[i], token);
            i++;
        }
        token = strtok(NULL, " ");
    }
    return commandList;
}

/**
 * executeBuiltInFunctions, this is the function that is ultimately responsible for running every other function in this file at some point. 
 *            First if a pipe was entered as a command, the handlePipes function is called to use its special logic for
 *            properly running a piped command. Next, if no pipe was found, runCommand is called to run normal commands that
 *            don't use pipes. This will be either an external or built-in command.
 * 
 * Args: Three arrays of strings, a struct
 * Return: Nothing
 */
void executeBuiltInFunctions(char **commandList, char **envp, struct pathelement *pathList, char **argv)
{
    if (!handlePipes(commandList, envp, pathList, argv))
        runExecutable(commandList, envp, pathList, argv);
    for (int i = 0; commandList[i] != NULL; i++)
    {
        free(commandList[i]);
        commandList[i] = NULL;
    }
}

/**
 * shouldRunAsBackground, searches through the commandList array tokenized in the main loop and returns
 *                        1 if the & (ampersand) character appears at the end of the array as an indicator
 *                        that a process should be run in the background. 0 otherwise.
 * 
 *                        IMPORTANT: The return for this function acts as an index in which to remove the '&'
 *                                   character, as well as to evaluate to true if non-zero.
 * 
 * Args: An array of strings
 * Return: An integer
 */
int shouldRunAsBackground(char **commandList)
{
    int shouldRun = 0;
    for (int i = 0; commandList[i] != NULL; i++)
        if (!strcmp(commandList[i], "&") && commandList[i + 1] == NULL)
        {                 
            // Make sure & is the last thing in commandList
            shouldRun = i;
            break;
        }
    return shouldRun;
}

/**
 * runExecutable, First checks if the command to run is a built-in command. If it is, the built-in id run and this function returns
 *                before any forking takes place. If it is not a built-in the getExternalPath function along with other functions for
 *                checking fore redirection are called. THe path for the external command is returned, either sn absolute, ie. contains
 *                a "./" or "../", or "/", or something of the like, or the which function returns the path to the command if it is not
 *                an absolute or relative path. Then fork() is called and execve is called rirght after witht the appropriate commandList
 *                for either a normal command or one involving redirection. The parent handles a couple of things. FIrst if a command takes
 *                longer than the time interval specified in the command line argument, the process is aborted and the child is killed. If
 *                the command finishes in time, no problem. There is also good use of waitpid here as well as a callback such that zombies
 *                are avoided.
 * 
 * Args: Three lists of strings, a struct
 * Return: Nothing
 */
void runExecutable(char **commandList, char **envp, struct pathelement *pathList, char **argv)
{
    if (isBuiltIn(commandList[0]))
    {
        // Built-in command check
        printf("Executing built-in: %s\n", commandList[0]);
        runBuiltIn(commandList, pathList, envp);
        return;
    }
    int result, abortProcess = 0, status = 0, redirectionType = getRedirectionType(commandList);
    int shouldRunInBg = shouldRunAsBackground(commandList);
    char *externalPath = getExternalPath(commandList, pathList);
    if (shouldRunInBg)
        commandList[shouldRunInBg] = '\0';
    if (externalPath != NULL)
    {
        printf("Executing: %s\n", externalPath);
        // Child
        if ((pid = fork()) < 0)
        { 
            // fork(), execve() and waitpid()
            perror("fork error");
        }
        else if (pid == 0)
        {
            if (redirectionType)
            {
                abortProcess = handleRedirection(redirectionType, getRedirectionDest(commandList));
                removeAfterRedirect(commandList);
            }
            if (abortProcess)
                kill(pid, SIGTERM);
            execve(externalPath, commandList, envp);
            perror("execve problem: ");
        }
        // Parent
        if (shouldRunInBg)
        {
            // background process stuff
            printShell();
            if ((result = waitpid(pid, &status, WNOHANG)) == -1 && errno != ECHILD)
                perror("waitpid error");
        }
        else
        {
            free(externalPath);
            signal(SIGALRM, alarmHandler);
            signal(SIGCHLD, childHandler);
            alarm(30);
            pause();
            if (timeout)
            {
                if ((result = waitpid(pid, &status, WNOHANG)) == -1 && errno != ECHILD)
                {
                    perror("waitpid error");
                }
                else if (result == 0)
                {
                    printf("!!! taking too long to execute this command !!!\n");
                    kill(pid, SIGINT);
                }
            }
        }
        if (WEXITSTATUS(status) != 0)
            printf("exit code of child: %d\n", WEXITSTATUS(status));
    }
}

/**
 * childHandler, callback function for signal SIGCHLD, the reason waitpid uses WNOHANG is so that 
 *               waitpid cannot block. The reason waitpid is in a loop is because if there are multiple
 *               zombies, they all get reaped.
 * 
 * Args: An integer
 * Return: Nothing
 */
void childHandler(int signal)
{
    isChildDone = 1;
    while (waitpid((pid_t)(-1), 0, WNOHANG) > 0);
}

/**
 * alarmHandler, updates the global variable timeout when called by signal(2)
 *               in the runExecutable function.
 * 
 * Args: An integer
 * Return: Nothing
 */
void alarmHandler(int signal)
{
    timeout = 1;
}

/**
 * getExternalPath, either returns the command if it is an absolute path ie. contains
 *                  a / ./ or ../ or something of the like, or returns the path to an 
 *                  executable found by the which function.
 * 
 * Args: A list of strings, a struct
 * Return: A string
 */
char *getExternalPath(char **commandList, struct pathelement *pathList)
{
    char *externalPath;
    char temp[BUFFERSIZE];
    struct stat file;

    if (strstr(commandList[0], "./") || strstr(commandList[0], "../") || strstr(commandList[0], "/"))
    {
        if (stat(commandList[0], &file) == 0)
        {
            if (S_ISDIR(file.st_mode))
            {
                errno = EISDIR;
                printf("shell: %s: %s\n", commandList[0], strerror(errno));
                return NULL;
            }
            if (file.st_mode & S_IXUSR && file.st_mode & S_IXGRP && file.st_mode & S_IXOTH)
            {
                strcpy(temp, commandList[0]);
                externalPath = (char *)malloc(sizeof(temp));
                strcpy(externalPath, temp);
            }
            else
            {
                errno = EACCES;
                printf("%s: %s\n", commandList[0], strerror(errno));
                return NULL;
            }
        }
        else
        {
            perror("stat error");
            return NULL;
        }
    }
    else
    {
        externalPath = which(commandList[0], pathList);
    }
    return externalPath;
}

/**
 * watchMailCallback, this function is a callback which is called when pthread_create is used when using the watchmail
 *                    built-in command. When a file specified by the user in the mailList linked list increases in size,
 *                    a beep noise is played and a message is printed out to notify the user that there is new mail in 
 *                    file that got larger. This is accomplished through an infinite loop that sleeps for 1 second each
 *                    iteration and uses stat(2) to check for file size increases among the files in the mailList data
 *                    structure (linked list).
 * 
 * Args: Args that are given by pthread_create
 * Return: Nothing
 */
void *watchMailCallback(void *callbackArgs)
{
    char *fileName = (char *)callbackArgs;
    struct stat st;
    if (stat(fileName, &st) != 0)
    {
        perror("stat problem");
        pthread_exit(NULL);
    }
    long prevFileSize = (long)st.st_size;
    struct timeval tv;
    time_t currentTime;
    while (1)
    {
        if (gettimeofday(&tv, NULL) != 0)
        {
            perror("Get current time problem");
            pthread_exit(NULL);
        }
        currentTime = tv.tv_sec;
        if (stat(fileName, &st) != 0)
        {
            perror("stat problem");
            pthread_exit(NULL);
        }
        if ((long)st.st_size > prevFileSize)
        {
            printf("\a\nYou've Got Mail in %s at %s", fileName, ctime(&currentTime));
            prevFileSize = (long)st.st_size;
        }
        sleep(1);
    }
}

/**
 * watchUserCallback, loops infinitely on a sleep timer of 20 seconds. Finds the user on the machine and tracks their logins.
 *                  The data shared the global users linked list is protected by a mutex_lock and unlock.
 * 
 * Args: Anything, whatever the watchUser built-in supplies
 * Return: Nothing
 */
void *watchUserCallback(void *callbackArgs)
{
    struct utmpx *up;
    while (1)
    {
        setutxent();
        while ((up = getutxent()))
        {
            if (up->ut_type == USER_PROCESS)
            {
                pthread_mutex_lock(&mutexLock);
                struct user *someUser = userHead;
                while (someUser)
                {
                    if (strcmp(someUser->username, up->ut_user) == 0)
                    {
                        someUser->isLoggedOn = 1;
                        printf("\n%s has logged on %s from %s\n", up->ut_user, up->ut_line, up->ut_host);
                    }
                    else
                    {
                        printf("\n%s, no such user\n", someUser->username);
                    }
                    someUser = someUser->next;
                }
                pthread_mutex_unlock(&mutexLock);
            }
        }
        printUsers();
        sleep(5);
    }
    return NULL;
}

/**
 * isBuiltIn, determines whether the command given is apart of the built in commands list. Returns 1 if true, 0 otherwise.
 * 
 * Args: A string
 * Return: An integer
 */
int isBuiltIn(char *command)
{
    const char *builtInCommands[] = {"exit", "which", "where", "cd", "pwd", "list", "pid", "kill", "prompt",
                                     "printenv", "setenv", "watchuser", "watchmail", "noclobber"};
    int inList = 0;
    for (int i = 0; i < BUILT_IN_COMMAND_COUNT; i++)
    {
        if (strcmp(command, builtInCommands[i]) == 0)
        {
            inList = 1;
            break;
        }
    }
    return inList;
}

/**
 * runBuiltIn, runs the built in commands, if a command like list is called with multiple arguments, its handler function is called, 
 *             calling the command for each argument given. This function also handles exiting the program and freeing/getting the 
 *             old and new linked lists for the PATH environment variable if need be.
 * 
 * Args: Two arrays of strings, a string, a struct
 * Return: Nothing
 */
void runBuiltIn(char **commandList, struct pathelement *pathList, char **envp)
{
    int shouldExit = 0, pathChanged = 0;
    if (strcmp(commandList[0], "exit") == 0)
    {
        shouldExit = exitProgram(); 
    }
    else if (strcmp(commandList[0], "which") == 0)
    {
        whichHandler(commandList, pathList);
    }
    else if (strcmp(commandList[0], "where") == 0)
    {
        whereHandler(commandList, pathList);
    }
    else if (strcmp(commandList[0], "cd") == 0)
    {
        changeDirectory(commandList);
    }
    else if (strcmp(commandList[0], "pwd") == 0)
    {
        printWorkingDirectory();
    }
    else if (strcmp(commandList[0], "list") == 0)
    {
        listHandler(commandList);
    }
    else if (strcmp(commandList[0], "pid") == 0)
    {
        printPid();
    }
    else if (strcmp(commandList[0], "kill") == 0)
    {
        killIt(commandList);
    }
    else if (strcmp(commandList[0], "prompt") == 0)
    {
        prompt(commandList);
    }
    else if (strcmp(commandList[0], "printenv") == 0)
    {
        printEnvironment(commandList, envp);
    }
    else if (strcmp(commandList[0], "setenv") == 0)
    {
        pathChanged = setEnvironment(commandList, envp, pathList);
    }
    else if (strcmp(commandList[0], "watchuser") == 0)
    {
        watchUser(commandList);
    }
    else if (strcmp(commandList[0], "watchmail") == 0)
    {
        watchMail(commandList);
    }
    else if (strcmp(commandList[0], "noclobber") == 0)
    {
        noClobber();
    }
    if (pathChanged)
    {
        freePath(pathList);
        pathList = getPath();
    }
    if (shouldExit)
        freeAndExit(pathList, commandList);
}

/**
 * getRedirectionType, traverses the commandList array and searches for a redirection type. If found it will return one of ">",
 *                     ">&", ">", ">>&", ">". If none of these are found, NULL will be returned.
 * 
 *                     IMPORTANT: This function returns 1 of 6 integers 0-5
 *                                -0: No redirection found
 *                                -1: ">"   found
 *                                -2: ">>"  found
 *                                -3: "<"   found 
 *                                -4: ">>&" found
 *                                -5: ">&"  found
 * 
 * Args: An array of strings
 * Return: An integer
 */
int getRedirectionType(char **commandList)
{
    int redirectionType = 0;
    for (int i = 0; commandList[i] != NULL; i++)
    {
        if (!strcmp(commandList[i], ">"))
            redirectionType = 1;
        if (!strcmp(commandList[i], ">>"))
            redirectionType = 2;
        if (!strcmp(commandList[i], "<"))
            redirectionType = 3;
        if (!strcmp(commandList[i], ">>&"))
            redirectionType = 4;
        if (!strcmp(commandList[i], ">&"))
            redirectionType = 5;
    }
    return redirectionType;
}

/**
 * removeAfterRedirect, this function removes all garbage in the command list including and after the redirection symbol because execve
 *                      only cares about what comes before the redirection symbol. All the extra stuff after will cause problems in 
 *                      exec if not removed.
 * 
 * Args: An array of strings
 * Return: Nothing
 */
void removeAfterRedirect(char **commandList)
{
    int commandCount = 0;
    while (1)
    {
        if (strstr(commandList[commandCount], "<") || strstr(commandList[commandCount], ">"))
            break;
        commandCount++;
    }
    while (commandList[commandCount] != NULL)
    {
        commandList[commandCount] = NULL;
        commandCount++;
    }
}

/**
 * getRedirectionDest, gets the string input by th user directly after the redirection symbol. This string is taken to be a filename
 *                     in which the contents of a command will be redirected to. So ls > temp.txt would dump the contents of ls into
 *                     temp.txt. This function gets that temp.txt filename.
 * 
 * Args: An array of strings
 * Return: A string
 */
char *getRedirectionDest(char **commandList)
{
    char *redirectionDest;
    for (int i = 0; commandList[i] != NULL; i++)
        if (strstr(commandList[i], "<") || strstr(commandList[i], ">"))
            if (commandList[i + 1] != NULL)
            {
                redirectionDest = (char *)malloc(strlen(commandList[i + 1]) + 1);
                strcpy(redirectionDest, commandList[i + 1]);
                return redirectionDest;
            }
    return NULL;
}

/**
 * handleRedirection, please refer to the documentation above the getRedirectionType function to know more about what the redirection
 *                    parameter is. This function also returns an integer, 0 if the redirection can go through, or 1 if noclobber is
 *                    turned on and an issue overwriting could arise. 
 * 
 * Args: An integer
 * Return: Nothing
 */
int handleRedirection(int redirectionType, char *destFile)
{
    int fileDescriptor;
    int abort = 0;
    int wrx = 0666;
    struct stat buffer;
    int fileExists = (stat(destFile, &buffer) == 0) ? 1 : 0;
    if (redirectionType == 0)
    {
        abort = 0;
    }
    else if (redirectionType == 1)
    {
        if (hasNoClobber && fileExists)
        {
            printf("noclobber is on, cannot overwrite %s, aborting...\n", destFile);
            abort = 1;
        }
        else
        {
            fileDescriptor = open(destFile, O_WRONLY | O_CREAT | O_TRUNC, wrx);
            close(1);
            dup(fileDescriptor);
            close(fileDescriptor);
        }
    }
    else if (redirectionType == 2)
    {
        if (hasNoClobber && fileExists)
        {
            printf("noclobber is on, cannot overwrite %s, aborting...\n", destFile);
            abort = 1;
        }
        else
        {
            fileDescriptor = open(destFile, O_WRONLY | O_CREAT | O_APPEND, wrx);
            close(1);
            dup(fileDescriptor);
            close(fileDescriptor);
        }
    }
    else if (redirectionType == 3)
    {
        if (!fileExists)
        {
            printf("%s: %s", destFile, strerror(ENOENT));
            abort = 1;
        }
        else
        {
            fileDescriptor = open(destFile, O_RDONLY);
            close(0);
            dup(fileDescriptor);
            close(fileDescriptor);
        }
    }
    else if (redirectionType == 4)
    {
        if (hasNoClobber && fileExists)
        {
            printf("noclobber is on, cannot overwrite %s, aborting...\n", destFile);
            abort = 1;
        }
        else
        {
            fileDescriptor = open(destFile, O_WRONLY | O_CREAT | O_APPEND, wrx);
            close(1); 
            dup(fileDescriptor);
            close(2);
            dup(fileDescriptor);
            close(fileDescriptor);
        }
    }
    else if (redirectionType == 5)
    {
        if (hasNoClobber && fileExists)
        {
            printf("noclobber is on, cannot overwrite %s, aborting...\n", destFile);
            abort = 1;
        }
        else
        {
            fileDescriptor = open(destFile, O_WRONLY | O_CREAT | O_TRUNC, wrx);
            close(1);
            dup(fileDescriptor);
            close(2);
            dup(fileDescriptor);
            close(fileDescriptor);
        }
    }
    free(destFile);
    return abort;
}

/**
 * getPipeType, checks whether or not there is a pipe in the commandList. Returns an integer based on which type
 *              of pipe was entered by the user.
 *                  
 *              IMPORTANT: this function returns integers 0-2
 *                         -0: No pipe found
 *                         -1: "|"  found
 *                         -2: "|&" found
 * 
 * Args: An array of strings
 * Return: An integer
 */
int getPipeType(char **commandList)
{
    int hasPipe = 0;
    for (int i = 0; commandList[i] != NULL; i++)
    {
        if (!strcmp(commandList[i], "|"))
            hasPipe = 1;
        if (!strcmp(commandList[i], "|&"))
            hasPipe = 2;
    }
    return hasPipe;
}

/**
 * getPipeIndex, small helper function for getting the index in the commandList at which the pipe exists.
 *               Returns -1 if there is no pipe.
 * 
 * Args: An array of strings
 * Return: An integer
 */
int getPipeIndex(char **commandList)
{
    for (int i = 0; commandList[i] != NULL; i++)
        if (strstr(commandList[i], "|"))
            return i;
    return -1;
}

/**
 * splitPipe, This function splits the command list before or after where the pipe lies. If the beforeOrAfter parameter
 *            is non-zero, this function returns the commandList before the pipe, if it is zero, this function returns
 *            the commandList after the pipe.
 * 
 * Args: An array of strings, an integer
 * Return: An array of strings
 */
char **splitPipe(char **commandList, int beforeOrAfter)
{
    char **pipeList = calloc(MAX_CMD, sizeof(char *));
    int pipeIndex = getPipeIndex(commandList);
    if (beforeOrAfter)
    {
        for (int i = 0; i < pipeIndex; i++)
        {
            pipeList[i] = (char *)malloc((int)strlen(commandList[i]) + 1);
            strcpy(pipeList[i], commandList[i]);
        }
    }
    else
    {
        int saveIndex = 0;
        for (int i = pipeIndex + 1; commandList[i] != NULL; i++)
        {
            pipeList[saveIndex] = (char *)malloc((int)strlen(commandList[i]) + 1);
            strcpy(pipeList[saveIndex], commandList[i]);
            saveIndex++;
        }
    }
    return pipeList;
}

/**
 * handlePipes, handles the logic for piping. If the pipeType function returns 0, that means there is no pipe and this function
 *              does nothing. If the pipe exists, then pipe(2) is called to set a file descriptor array and the file descriptors
 *              for stdin, stdout, and stderr are opened/closed appropriately. There are two calls the the runExecutable function
 *              here, the first call executes the command given before the pipe, stdout and stderr are returned to the terminal 
 *              and then the second call to runExecutable is made. This time, runExecutable runs the command that comes after the
 *              pipe.  
 * 
 * Args: Three arrays of strings, A struct
 * Return: An integer
 */
int handlePipes(char **commandList, char **envp, struct pathelement *pathList, char **argv)
{
    int fileDescriptor, before = 1, after = 0, wasPiped = 0, pipeType = getPipeType(commandList), pipeFileDescriptor[2];
    char **beforePipe = splitPipe(commandList, before), **afterPipe = splitPipe(commandList, after);
    if (pipeType)
    {
        if (pipe(pipeFileDescriptor) != 0)
        {
            perror("pipe");
        }

        close(0); // Close stdin
        dup(pipeFileDescriptor[0]);
        close(pipeFileDescriptor[0]);

        if (pipeType == 2)
        {
            close(2);
            dup(pipeFileDescriptor[1]);
        }

        close(1); // Close stdout
        dup(pipeFileDescriptor[1]);
        close(pipeFileDescriptor[1]);

        runExecutable(beforePipe, envp, pathList, argv); 

        fileDescriptor = open("/dev/tty", O_WRONLY);
        close(1);
        dup(fileDescriptor);
        close(fileDescriptor);

        fileDescriptor = open("/dev/tty", O_WRONLY);
        close(2);
        dup(fileDescriptor);
        close(fileDescriptor);

        runExecutable(afterPipe, envp, pathList, argv);

        fileDescriptor = open("/dev/tty", O_RDONLY);
        close(0);
        dup(fileDescriptor);
        close(fileDescriptor);

        wasPiped = 1;
    }
    freePipeArrays(beforePipe, afterPipe);
    return wasPiped;
}

/**
 * freePipeArrays, frees the two arrays allocated for piping
 * 
 * Args: Two arrays of strings
 * Return: Nothing
 */
void freePipeArrays(char **beforePipe, char **afterPipe)
{
    for (int i = 0; afterPipe[i] != NULL; i++)
    {
        free(afterPipe[i]);
    }
    free(afterPipe);
    for (int i = 0; beforePipe[i] != NULL; i++)
    {
        free(beforePipe[i]);
    }
    free(beforePipe);
}

/**
 * noClobber, for managing the hasNoClobber global variable. When noclobber is entered as a command, the hasNoClobber global will
 *            be updated to turn it on or off. It is off(0) by default, so typing noclobber would turnit on, typing it again would
 *            turn it off.
 * 
 * Args: Nothing
 * Return: Nothing
 */
void noClobber()
{
    if (hasNoClobber == 0)
    {
        hasNoClobber = 1;
        printf("noclobber is now on\n");
    }
    else
    {
        hasNoClobber = 0;
        printf("noclobber is now off\n");
    }
}

/**
 * watchMail, if no arguments are given, an error message is printed. With one argument given, this function spawns 
 *            a thread that calls the watchMailCallback function which watches the given file in an infinite loop for
 *            updates to the file, read more about that function in the description for it. If two arguments are given,
 *            if the second argument is the word "off", then the thread is killed and all of its memory is collected.
 * 
 * Args: An array of strings
 * Return: Nothing
 */
void watchMail(char **commandList)
{
    char *fileName = strdup(commandList[1]);
    if (commandList[1] == NULL)
    {
        errno = ENOENT;
        perror("watchmail");
    }
    else if (commandList[2] != NULL)
    {
        if (strcmp("off", commandList[2]) == 0)
        {
            struct mail *toRemove = findMail(commandList[1]);
            free(fileName);
            if (toRemove == NULL)
            {
                printf("Cannot unwatch %s, not in mail list.\n", commandList[1]);
                return;
            }
            pthread_cancel(toRemove->thread);
            pthread_join(toRemove->thread, NULL);
            removeMail(toRemove->pathToFile);
        }
        else
        {
            errno = EINVAL;
            perror("watchmail");
        }
    }
    else
    {
        struct stat buffer;
        pthread_t mailID;
        int fileExists;
        if ((fileExists = stat(commandList[1], &buffer)) != 0)
        {
            errno = ENOENT;
            perror("stat");
            return;
        }
        pthread_create(&mailID, NULL, watchMailCallback, (void *)fileName);
        addMail(fileName, mailID);
    }
}

/**
 * watchUser, if no argumemnts are given, print an error message. If one argument is given, add the user given as 
 *            the argument to the global users list. There is an optional second argument for untracking a user. 
 *            by typing: watchuser USER off, that user will be removed from the user list.
 * 
 * Args: An array of strings
 * Return: Nothing
 */
void watchUser(char **commandList)
{
    char *username = strdup(commandList[1]);
    if (commandList[1] == NULL)
    {
        errno = EINVAL;
        printf("Enter a username to track: %s\n", strerror(errno));
    }
    else if (commandList[2] != NULL)
    {
        if (strcmp("off", commandList[2]) == 0)
        {
            struct user *toRemove = findUser(username);
            free(username);
            if (toRemove == NULL)
            {
                printf("Cannot remove, no such user\n");
                return;
            }
            pthread_mutex_lock(&mutexLock);
            removeUser(toRemove->username);
            pthread_mutex_unlock(&mutexLock);
        }
        else
        {
            errno = EINVAL;
            perror("watchuser");
        }
    }
    else
    {
        pthread_mutex_lock(&mutexLock);
        addUser(username);
        pthread_mutex_unlock(&mutexLock);
        if (!threadExists)
        {
            threadExists = 1;
            if (pthread_create(&watchUserID, NULL, watchUserCallback, NULL) != 0)
            {
                perror("pthread_create problem");
            }
        }
    }
}

/**
 * killIt, when given just a pid, sends SIGTERM to it to politely kill that process.
 *         if given a signal number (ie. kill -9 1234), sends that signal to the 
 *         process.
 * 
 * Args: A list of strings
 * Return: Nothing
 */
void killIt(char **commandList)
{
    int pid;
    if (commandList[1] == NULL)
    {
        fprintf(stderr, "%s", " kill: Specify at least one argument\n");
    }
    else if (commandList[2] != NULL)
    {
        int signal;
        commandList[1][0] = ' ';
        if ((signal = atoi(commandList[1])) == 0)
        {
            errno = EINVAL;
            perror(" kill, bad signal");
        }
        else if ((pid = atoi(commandList[2])) == 0)
        {
            errno = EINVAL;
            perror(" kill, no pid");
        }
        else
        {
            if ((kill(pid, signal)) != 0)
            {
                errno = ESRCH;
                perror(" kill");
            }
        }
    }
    else
    {
        if ((pid = atoi(commandList[1])) == 0)
        {
            errno = EINVAL;
            perror(" kill");
        }
        else
        {
            if (kill(pid, SIGTERM) != 0)
            {
                errno = ESRCH;
                perror(" kill");
            }
        }
    }
}

/**
 * setEnvironment, when called with no arguments, prints out all the environment
 *                 variables like printEnvironment does. When called with one
 *                 argument, sets it as an empty environment variable. When called
 *                 with two arguments, sets the first equal to the second. More than
 *                 two arguments should result in an error message. This will also 
 *                 handle two special cases: One if the HOME variable is changed,
 *                 Two if the PATH variable is changed.
 * 
 * Args: Two lists of strings
 * Return: An integer
 */
int setEnvironment(char **commandList, char **envp, struct pathelement *pathList)
{
    int pathChanged = 0;
    if (commandList[3] != NULL)
    {
        fprintf(stderr, "%s", " setenv: Too many arguments.\n");
    }
    else if (commandList[1] == NULL)
    {
        for (int i = 0; envp[i] != NULL; i++)
        {
            printf(" \n%s", envp[i]);
        }
    }
    else if (commandList[2] != NULL)
    {
        if (strcmp("PATH", commandList[1]) == 0)
        {
            setenv("PATH", commandList[2], 1);
            pathChanged = 1;
        }
        else
        {
            setenv(commandList[1], commandList[2], 1);
        }
    }
    else
    {
        if (strcmp(commandList[1], "PATH") == 0)
        {
            setenv("PATH", "", 1);
            pathChanged = 1;
        }
        else
        {
            setenv(commandList[1], "", 1);
        }
    }
    return pathChanged ? 2 : 0;
}

/**
 * printEnvironment, when given no arguments, prints all of the enviornment variables.
 *           When given one argument, calls getenv(3). Two or more arguments
 *           are not accepted and will invoke an error message.
 * 
 * Args: A string, An array of strings
 * Return: Nothing
 */
void printEnvironment(char **commandList, char **envp)
{
    if (commandList[2] != NULL)
    {
        fprintf(stderr, "%s", " printenv: Too many arguments\n");
    }
    else if (commandList[1] == NULL)
    {
        for (int i = 0; envp[i] != NULL; i++)
        {
            printf("\n%s", envp[i]);
        }
    }
    else
    {
        if (getenv(commandList[1]) != NULL)
            printf(" %s\n", getenv(commandList[1]));
        else
            fprintf(stderr, "%s", " Error: environment variable not found\n");
    }
}

/**
 * changeDirectory, with no arguments, changes the cwd to the home directory. 
 *                  with "-" as an argument, changes directory to the one 
 *                  previously in. Otherwise change to the directory given as
 *                  the argument.
 * 
 * Args: A list of strings
 * Return: Nothing
 */
void changeDirectory(char **commandList)
{
    int success;
    if (commandList[1] == NULL) {
        // cd with nothing passed in
        free(last_dir);
        last_dir = getcwd(NULL, 0);
        success = chdir(getenv("HOME"));
    }
    else if (strcmp(commandList[1], "-") == 0) {
        // cd to previous dir
        success = chdir(last_dir);
        free(last_dir);
        last_dir = getcwd(NULL, 0);
    } else {
        // normal path in cd
        free(last_dir);
        last_dir = getcwd(NULL, 0);
        success = chdir(commandList[1]);
    }
    if (success >= 0) {
        printf("Directory change successful\n");
    } else {
        printf("Directory change failed\n");
    }
}

/**
 * printPid, prints the pid of the shell.
 * 
 * Args: Nothing
 * Return: Nothing
 */
void printPid()
{
    int pid = getpid();
    printf(" pid: %d\n", pid);
}

/**
 * exitProgram, returns 1 if called.
 * 
 * Args: Nothing
 * Return: Nothing
 */
int exitProgram()
{
    return 1;\
}

/**
 * printWorkingDirectory, just prints the absolute path to the current working
 *                        directory, if only all of these functions were that
 *                        simple.
 * 
 * Args: Nothing
 * Return: Nothing
 */
void printWorkingDirectory()
{
    char *ptr = getcwd(NULL, 0);
    printf(" %s\n", ptr);
    free(ptr);
}

/**
 * prompt, puts a new prefix string at the beginning of the shell. If this
 *         function is called with no argument, it prompts the user to enter
 *         a string with which to prefix the shell with.
 * 
 * Args: A string
 * Return: Nothing
 */
void prompt(char **commandList)
{
    if (commandList[1] != NULL)
    {
        if (prefix != NULL)
            free(prefix);
        prefix = (char *)malloc(sizeof(commandList) + 1);
        strcpy(prefix, "");
        for (int i = 1; commandList[i] != NULL; i++)
        {
            strcat(prefix, commandList[i]);
            strcat(prefix, " ");
        }
    }
    else
    {
        char tempBuffer[BUFFERSIZE];
        printf(" input prompt prefix: ");
        fgets(tempBuffer, BUFFERSIZE, stdin);
        tempBuffer[strlen(tempBuffer) - 1] = '\0';
        if (prefix != NULL)
            free(prefix);
        prefix = (char *)malloc(strlen(tempBuffer) + 1);
        strcpy(prefix, tempBuffer);
    }
}

/**
 * which, locates commands. Returns the location of the command given as the argument.
 *                          If this function is called, don't forget to free the returned
 *                          string at some point.
 * 
 * Args: A string, A list of strings
 * Return: A string
 */
char *which(char *command, struct pathelement *pathlist)
{
    char temp[BUFFERSIZE];
    DIR *dp;
    struct dirent *dirp;
    while (pathlist)
    {
        if ((dp = opendir(pathlist->element)) == NULL)
        {
            perror("opendir");
            exit(errno);
        }
        while ((dirp = readdir(dp)) != NULL)
        {
            if (strcmp(dirp->d_name, command) == 0)
            {
                if (access(pathlist->element, X_OK) == 0)
                {
                    strcpy(temp, pathlist->element);
                    strcat(temp, "/");
                    strcat(temp, dirp->d_name);
                    char *executablePath = (char *)malloc(strlen(temp) + 1);
                    strcpy(executablePath, temp);
                    closedir(dp);
                    return executablePath;
                }
            }
        }
        closedir(dp);
        pathlist = pathlist->next;
    }
    printf(" %s: Command not found.\n", command);
    return NULL;
}

/**
 * where, returns all instances of the command in path. This is the same code as 
 *        the which function except the loop doesn't stop when one
 *        file is found, rather all files containing the command string will be
 *        returned assuming they're executables.
 * 
 * Args: A string, A list of strings
 * Return: A string
 */
char *where(char *command, struct pathelement *pathlist)
{
    char temp[BUFFERSIZE] = "";
    DIR *dp;
    struct dirent *dirp;
    while (pathlist)
    {
        if ((dp = opendir(pathlist->element)) == NULL)
        {
            perror(" Error opening");
            return NULL;
        }
        while ((dirp = readdir(dp)) != NULL)
        {
            if (strcmp(dirp->d_name, command) == 0)
            {
                if (access(pathlist->element, X_OK) == 0)
                {
                    strcat(temp, pathlist->element);
                    strcat(temp, "/");
                    strcat(temp, dirp->d_name);
                    strcat(temp, "\n\n");
                }
            }
        }
        closedir(dp);
        pathlist = pathlist->next;
    }
    if (temp == NULL)
        return NULL;
    char *path = (char *)malloc(sizeof(temp));
    strcpy(path, temp);
    return path;
}

/**
 * list, acts as the ls command, with no arguments, lists all the files in the
 *       current working directory, with arguments lists the files contained in
 *       the arguments given (directories).
 * 
 * Args: A string (directory name)
 * Return: Nothing
 */
void list(char *dir)
{
    DIR *dp;
    struct dirent *dirp;
    if (strcmp(dir, "") == 0)
    {
        char *cwd = getcwd(NULL, 0);
        if ((dp = opendir(cwd)) == NULL)
        {
            errno = ENOENT;
            perror("No cwd: ");
        }
        while ((dirp = readdir(dp)) != NULL)
            printf(" %s\n", dirp->d_name);
        free(cwd);
        free(dp);
    }
    else
    {
        if ((dp = opendir(dir)) == NULL)
        {
            errno = ENOENT;
            printf(" list: cannot access %s: %s\n", dir, strerror(errno));
            return;
        }
        printf(" %s:\n", dir);
        while ((dirp = readdir(dp)) != NULL)
        {
            printf("    %s\n", dirp->d_name);
        }
        closedir(dp);
    }
}

/**
 * printShell, prints the cwd in the form [path]>
 * 
 * Args: Nothing 
 * Return: Nothing
 */
void printShell()
{
    char *ptr = getcwd(NULL, 0);
    if (prefix != NULL)
        printf("%s [%s]>", prefix, ptr);
    else
        printf("[%s]>", ptr);
    free(ptr);
}

/**
 * listHandler, handles the logic for the list function. Checks if called
 *              with no arguments, or with arguments and calls list accordingly.
 * 
 * Args: An array of strings
 * Return: Nothing
 */
void listHandler(char **commandList)
{
    if (commandList[1] == NULL)
    {
        list("");
    }
    else
    {
        for (int i = 1; commandList[i] != NULL; i++)
            list(commandList[i]);
    }
}

/**
 * whichHandler, Handles multiples args being sent to which
 * 
 * Args: Two lists of strings
 * Return: Nothing
 */
void whichHandler(char **commandList, struct pathelement *pathList)
{
    char *pathToCmd;
    for (int i = 1; commandList[i] != NULL; i++)
    {
        pathToCmd = which(commandList[i], pathList);
        if (pathToCmd != NULL)
        {
            printf(" %s\n", pathToCmd);
            free(pathToCmd);
        }
    }
}

/**
 * whereHandler, Handles multiples args being sent to where
 * 
 * Args: Two lists of strings
 * Return: Nothing
 */
void whereHandler(char **commandList, struct pathelement *pathList)
{
    char *paths;
    for (int i = 1; commandList[i] != NULL; i++)
    {
        paths = where(commandList[i], pathList);
        if (paths != NULL)
            printf("%s", paths);
        else
            printf(" %s: command not found\n", commandList[i]);
        free(paths);
    }
}

/**
 * freePath, if the path environment variable is changed vis setenv, 
 *           the linked list data structure holding the path elements
 *           is freed using this function.
 * 
 * Args: A structure
 * Return: Nothing
 */
void freePath(struct pathelement *pathList)
{
    struct pathelement *temp;
    while (pathList != NULL)
    {
        temp = pathList;
        pathList = pathList->next;
        free(temp);
    }
    free(path);
}

/**
 * freeAndExit, this function gets called when exit is typed to exit. Frees all of the things that are still taking
 *              up space and exits the program.
 * 
 * Args: A struct, An array of strings
 * Return: Nothing
 */
void freeAndExit(struct pathelement *pathList, char **commandList)
{
    if (prefix)
        free(prefix);
    freePath(pathList);
    free(last_dir);
    freeUsers(userHead);
    pthread_cancel(watchUserID);
    pthread_join(watchUserID, NULL);
    freeAllMail(mailHead);
    free(commandList[0]);
    free(commandList);
    exit(0);
}