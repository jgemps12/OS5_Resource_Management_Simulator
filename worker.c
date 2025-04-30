/* Jesse Gempel
 * 3/18/2025
 * Professor Mark Hauschild
 * CMP SCI 4760-001
*/


// The user.c file works with CHILD processes.
// It prints out child and parent process IDs, as well as child process and termination times, to the user for each iteration.


#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>


// Starting a memory segment for system clock seconds.
#define SHMKEY1 42069
#define INT_BUFFER_SIZE sizeof(int)


// Starting a memory segment for system clock nanoseconds.
#define SHMKEY2 42070
#define LONG_BUFFER_SIZE sizeof(long int)


// Starting a memory segment for a log file to validate ftok() function.
#define SHMKEY3 42071
#define LOGFILE_BUFFER_SIZE 105


// Permissions for memory queue.
#define PERMISSIONS 0644


// Enumerates options that the child should do.
enum ResourceTask {
   REQUEST,
   RELEASE,
   TERMINATE_PROGRAM
};


// Holds message queue information.
typedef struct messageBuffer {
   long int processID;
   int resourceType;
   ResourceTask selection;
} messageBuffer;


// Initializes information for message buffer.
messageBuffer receiveBuffer;
messageBuffer sendBuffer;
int messageQueueID;
key_t key;


// Logfile pointer.
char *logfileFP = NULL;


// Function prototypes.
void initializeMessageQueue();
ResourceTask determineProcessSelection(int);
int determineResourceType();
void sendMessageToOSS();
void receiveMessageFromOSS();
void slowDownProgram();


int main(int argc, char** argv) {
  
   // Creates two shared memory identifiers (and one for a log file).
   int secondsShmid = shmget(SHMKEY1, INT_BUFFER_SIZE, 0777);
   long int nanoShmid = shmget(SHMKEY2, LONG_BUFFER_SIZE, 0777);
   int logfileShmid = shmget(SHMKEY3, LOGFILE_BUFFER_SIZE, 0777);


   // Attaches the system time and log file into shared memory.
   int *sharedSeconds = (int *)shmat(secondsShmid, 0, 0);
   long int *sharedNanoseconds = (long int *)shmat(nanoShmid, 0, 0);
   logfileFP = (char *)shmat(logfileShmid, 0, 0);


   // Used for constant system time updated from shared memory.
   int systemClockSeconds = *sharedSeconds;
   long int systemClockNano = *sharedNanoseconds;


   // Used for termination time calculations. Will not be updated.
   int initialSystemClockSecs = *sharedSeconds;
   long int initialSystemClockNano = *sharedNanoseconds;        
   
   
   // processSelection uses numbers 1-3 to determine child process's outcome.
   // probabilityValue randomly chooses between 1 and 1000 to determine processSelection value.
   srand(time(NULL) ^ getpid());

   int probabilityValue = 0;
   int processSelection = -1;
  // int timeQuantumFraction = 0;


   int resourceType = -1;
   initializeMessageQueue();

   
   do {
      enum ResourceTask processSelection;
      // Receives a message from the parent.
   //   receiveMessageFromOSS();


      // Compare # of seconds before and after shared memory is re-read.
      int secondsBeforeMemRead = systemClockSeconds;                                              // Before read.
      systemClockSeconds = *sharedSeconds;                                                        // During read.
      int secondsAfterMemRead = systemClockSeconds;                                               // After read.

      systemClockNano = *sharedNanoseconds;


      // Slow down program to prevent race conditions between Process Table and printf() message times (for oss.c and user.c, respectively).
      int i;
      for (i = 0; i < 7000000; i++) {
         //  Do nothing.
      }

      probabilityValue = (rand() % 1000) + 1;
      resourceType = rand() % 5;
      processSelection = determineProcessSelection(probabilityValue);
  
      
      switch (processSelection) {       
	 // If child decides to REQUEST a resource.
	 case REQUEST:
            sendBuffer.selection = REQUEST;
            sendBuffer.processID = getpid();
	   

            sendBuffer.resourceType = resourceType;
           // printf("resourceType: %d\n", resourceType);

            sendMessageToOSS();

	    break;
	 

	 // If child decides to RELEASE a resource.
	 case RELEASE:
	    sendBuffer.selection = RELEASE;
            sendBuffer.processID = getpid();
          
	    sendBuffer.resourceType = resourceType;

            sendMessageToOSS();

            break;


	 // If child decides to TERMINATE a program.
	 case TERMINATE_PROGRAM:
            sendBuffer.selection = TERMINATE_PROGRAM;
	    sendBuffer.processID = getpid();
     
	    sendBuffer.resourceType = 0;

            sendMessageToOSS();
	    
	    break;
      }

      receiveMessageFromOSS();

      if (processSelection == TERMINATE_PROGRAM) {
         break;
      }


      //receiveMessageFromOSS();
   }
   while (1);
  

   // Detach shared memory.
   shmdt(sharedSeconds);
   shmdt(sharedNanoseconds);
   shmdt(logfileFP);


   return EXIT_SUCCESS;

}


// Attempts to set up a message queue.
void initializeMessageQueue() {
   if ((key = ftok(logfileFP, 1)) == -1) {
      printf("ERROR in user.c: problem with ftok() function.\n");
      printf("Cannot access a key for message queue initialization.\n\n");

      exit(-1);
   }

   if ((messageQueueID = msgget(key, PERMISSIONS)) == -1) {
      printf("ERROR in user.c: problem with msgget() function.\n");
      printf("Cannot acquire a message queue ID for initialization.\n\n");

      exit(-1);
   }
}


// Randomly decides option 1, 2, or 3 based on probability.
ResourceTask determineProcessSelection (int probabilityValue) {
   enum ResourceTask selection;                            

   // Process runs full time quantum.
   if (probabilityValue >= 1 && probabilityValue <= 800) {
      selection = REQUEST;
   }

   // Process runs part of quantum, but gets blocked.
   else if (probabilityValue >= 801 && probabilityValue <= 995) {  
      selection = RELEASE;
   }

   // Process runs part of quantum, but gets terminated.
   else if (probabilityValue >= 996 && probabilityValue <= 1000) {
      selection = TERMINATE_PROGRAM;
   }

   //printf("probabilityValue: %d\n", probabilityValue); 
   return selection;
}


// Randomly decides resource types R0, R1, R2, R3, or R4.
/*int determineResourceType () {
  */ 

// msgsnd() operations.
void sendMessageToOSS() {
//   slowDownProgram();
   
   if (msgsnd(messageQueueID, &sendBuffer, sizeof(messageBuffer) - sizeof(long int), 0) == -1) {
      printf("ERROR in user.c: Problem with msgsnd() function.\n");
      printf("Cannot send message to oss.c.\n\n");

      exit(-1);
   }
}


// Nonblocking msgrcv() operations.
void receiveMessageFromOSS() {
   if (msgrcv(messageQueueID, &receiveBuffer, sizeof(messageBuffer), getpid(), 0) == -1) {
     // if (errno == ENOMSG) {
  //       slowDownProgram();
	 // Continue running worker.c
     // }
     // else {
         printf("ERROR in worker.c: Problem with msgrcv() function.\n");

         exit(-1);
     // }
   }
}

void slowDownProgram() {
   int i;
   for (i = 0; i < 10000000; i++) {
      //  Do nothing.
   }
}
