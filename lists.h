#include <stdio.h>
#include <utmpx.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

/********************************************************
 * PROGRAM: Shell			                                  *
 * CLASS: CISC 361-011                                  *
 * AUTHORS:                                             *
 *    Alex Sederquest | alexsed@udel.edu | 702414270    *
 *    Ben Segal | bensegal@udel.edu | 702425559         *
 ********************************************************/

// getPath definitions
char *path; // I made this global so I can free it from sh.c

/* function prototype.  It returns a pointer to a linked list for the path
   elements. */
struct pathelement *getPath();

struct pathelement {
  char *element;			/* a dir in the path */
  struct pathelement *next;		/* pointer to next node */
};


// End getPath definitions
// watchUser definitions
// Struct definition
struct user {
    int isLoggedOn; // 1 if logged on, 0 if not logged on
    char *username; // The user to be watched, or unwatched
    struct user *next; // Pointer to the next userNode
}*userHead;


// Typical linked list functions
struct user *addUser(char *username);
struct user *findUser(char *username);
struct user *removeUser(char *usernameToRemove);
void freeUsers(struct user *list);
void printUsers();

// End watchUser definitions
// watchMail definitions
// Struct definition
struct mail {
    char *pathToFile;
    pthread_t thread;
    struct mail *next;
}*mailHead;

// Typical linked list functions
struct mail *addMail(char *pathToFile, pthread_t threadID);
void printMail();
struct mail *removeMail(char *fileName);
void freeAllMail(struct mail *list);
struct mail *findMail(char *fileName);