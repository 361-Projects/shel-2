#include "lists.h"

/********************************************************
 * PROGRAM: Shell			                            *
 * CLASS: CISC 361-011                                  *
 * AUTHORS:                                             *
 *    Alex Sederquest | alexsed@udel.edu | 702414270    *
 *    Ben Segal | bensegal@udel.edu | 702425559         *
 ********************************************************/

// getPath List functions
struct pathelement *getPath()
{
  /* path is a copy of the PATH and p is a temp pointer */
  char *p;

  /* tmp is a temp point used to create a linked list and pathlist is a
     pointer to the head of the list */
  struct pathelement *tmp, *pathlist = NULL;

  p = getenv("PATH");	/* get a pointer to the PATH env var.
			   make a copy of it, since strtok modifies the
			   string that it is working with... */
  path = malloc((strlen(p)+1)*sizeof(char));	/* use malloc(3) */
  strncpy(path, p, strlen(p));
  path[strlen(p)] = '\0';

  p = strtok(path, ":"); 	/* PATH is : delimited */
  do				/* loop through the PATH */
  {				/* to build a linked list of dirs */
    if ( !pathlist )		/* create head of list */
    {
      tmp = calloc(1, sizeof(struct pathelement));
      pathlist = tmp;
    }
    else			/* add on next element */
    {
      tmp->next = calloc(1, sizeof(struct pathelement));
      tmp = tmp->next;
    }
    tmp->element = p;	
    tmp->next = NULL;
  } while ((p = strtok(NULL, ":")));

  return pathlist;
}

/**
 * addUser, this function appends a user to the end of the user list. No inserting in order here.
 *          The added user is returned from this function.
 * 
 * Args: A struct, a String
 * Return: A struct
 */
struct user *addUser(char *username) {
    struct user **tracker = &userHead, *newNode;
    while (*tracker) // Traverse the list until end
        tracker = &(*tracker)->next;
    newNode = (struct user*)malloc(sizeof(struct user));
    newNode->username = username;
    newNode->next = *tracker; // NULL
    *tracker = newNode; // Insert newNode at end
    return *tracker;
}

/**
 * findUser, traverses the linked list to find the given user by username, pretty straightforward.
 *           Return NULL if not found.
 * Args: A struct, A string
 * Return: A struct
 */
struct user *findUser(char *username) {
    struct user **tracker = &userHead;
    while (*tracker) {
        if (strcmp(username, (*(tracker))->username) == 0)
            return *tracker;
        tracker = &(*tracker)->next;
    }
    return NULL; // returns NULL if not found
}


/**
 * removeUser, removes a user from the list based on the username given. Also returns the removed user
 *             in case it is needed for other operations.
 * 
 * Args: A struct, A string
 * Return: A struct
 */
struct user *removeUser(char *usernameToRemove) {
    struct user **tracker = &userHead, *temp = userHead;
    while (temp) {
        if (strcmp(usernameToRemove, temp->username) == 0) {
            *tracker = temp->next;
            free(temp->username); // Clean up strdup
            free(temp);
            return *tracker;
        }
        tracker = &(*tracker)->next;
        temp = temp->next;
    }
    return NULL; // If the user to delete does not exist
}


/**
 * freeUsers, frees the entire users linked list data structure.
 * 
 * Args: A struct
 * Return: Nothing
 */
void freeUsers(struct user *list) {
    struct user *temp;
    while(list) {
        temp = list;
        list = list->next;
        free(temp->username);
        free(temp);
    }
}


/**
 * printMail, prints the list of users in the linked list.
 * 
 * Args: Nothing
 * Return: Nothing
 */
void printUsers() {
    struct user *n = userHead;
    while (n) {
        printf("\nNAME: %s\n", n->username);
        n = n->next;
    }
}

/**
 * addMail, makes a new mail struct and mallocs space for it. Sets its path and threadID attributes.
 *          This linked list will always just append new mails to the end of the list.
 * 
 * Args: A string, A pthread_t
 * Return: A struct
 */
struct mail *addMail(char *pathToFile, pthread_t threadID) {
    struct mail **tracker = &mailHead, *newMail;
    while(*tracker)
        tracker = &(*tracker)->next;
    newMail = (struct mail*)malloc(sizeof(struct mail));
    newMail->pathToFile = pathToFile;
    newMail->thread = threadID;
    newMail->next = *tracker; // NULL
    *tracker = newMail;
    return *tracker;
}


/**
 * printMail, prints the names and ids of all the mail in the list.
 * 
 * Args: Nothing
 * Return: Nothing
 */
void printMail() {
    struct mail *list = mailHead;
    while (list != NULL) {
        printf("\nFileName: --> %s, ThreadID: --> %ld\n", list->pathToFile, list->thread);
        list = list->next;
    }
}


/**
 * removeMail, removes the mail from the list based on the filename given.
 * 
 * Args: A string
 * Return: A struct
 */
struct mail *removeMail(char *fileName) {
    struct mail **tracker = &mailHead, *temp = mailHead;
    while(temp) {
        if (strcmp(temp->pathToFile, fileName) == 0) {
            *tracker = temp->next;
            free(temp->pathToFile);
            free(temp);
            return *tracker;
        }
        tracker = &(*tracker)->next;
        temp = temp->next;
    }
    return NULL;
}


/**
 * findMail, finds the piece of mail based on the string entered as the filename (or path). 
 *           It then returns the mail struct associated with that filename.
 * 
 * Args: A string
 * Return: Nothing
 */
struct mail *findMail(char *fileName) {
    struct mail **tracker = &mailHead;
    while(*tracker) 
        if (strcmp((*(tracker))->pathToFile, fileName) == 0)
            return *tracker;
    return NULL; // If the mail wasn't found
}


/**
 * freeAllMail, frees all the threads and or structs in the mailList.
 * 
 * Args: A struct
 * Return: Nothing
 */
void freeAllMail(struct mail *list) {
    struct mail *temp;
    while(list) {
        temp = list;
        list = list->next;
        free(temp->pathToFile);
        pthread_cancel(temp->thread);
        pthread_join(temp->thread, NULL);
        free(temp);
    }
}