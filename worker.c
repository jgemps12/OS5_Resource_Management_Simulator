/* Jesse Gempel
 * 4/29/2025
 * Professor Mark Hauschild
 * CMP SCI 4760-001
*/


// The user.c file works with CHILD processes.
// It decides whether a child should request a resource, release a resource, or terminate.



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
#define LONG_BUFFER_SIZE sizeof(long long int)


// Starting a memory segment for a log file to validate ftok() function.
#define SHMKEY3 42071
#define LOGFILE_BUFFER_SIZE 105


// Permissions for memory queue.
#define PERMISSIONS 0644


// Enumerates options that the child should do.
enum ResourceTask {
   REQUEST,
   RELEASE,
   TERMINATE_PROCESS
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
int getAllowedTerminationTimeSeconds(int);
long long int timeToCheckForTerminations(int, long int);
void sendMessageToOSS();
void receiveMessageFromOSS();
void slowDownProgram();




int main(int argc, char** argv) {
   srand(time(NULL));
  
   // Creates two shared memory identifiers (and one for a log file).
   int secondsShmid = shmget(SHMKEY1, INT_BUFFER_SIZE, 0777);
   long int nanoShmid = shmget(SHMKEY2, LONG_BUFFER_SIZE, 0777);
   int logfileShmid = shmget(SHMKEY3, LOGFILE_BUFFER_SIZE, 0777);


   // Attaches the system time and log file into shared memory.
   int *sharedSeconds = (int *)shmat(secondsShmid, 0, 0);
   long long int *sharedNanoseconds = (long long int *)shmat(nanoShmid, 0, 0);
   logfileFP = (char *)shmat(logfileShmid, 0, 0);


   // Used for constant system time updated from shared memory.
   int systemClockSeconds = *sharedSeconds;
   long long int systemClockNano = *sharedNanoseconds;
   long long int systemNanoOnly = 0;

   // Used for termination time calculations. Will not be updated.
   int initialSystemClockSecs = *sharedSeconds;
   long int initialSystemClockNano = *sharedNanoseconds;        


   // Determines the earliest system time that a child process can terminate.
   int allowedTerminationTimeSeconds = getAllowedTerminationTimeSeconds(initialSystemClockSecs);
   long int allowedTerminationTimeNano = *sharedNanoseconds;

   // Checks if process should terminate every 250 ms.
   long long int termCheckTime = *sharedNanoseconds;
   bool processCanTerminate = false;

   // Initialization of message operations.
   int probabilityValue = 0;
   int processSelection = -1;
   int resourceType = -1;
   initializeMessageQueue();

   

   do {
      enum ResourceTask processSelection;

      // Compare # of seconds before and after shared memory is re-read.
      int secondsBeforeMemRead = systemClockSeconds;                                              // Before read.
      systemClockSeconds = *sharedSeconds;                                                        // During read.
      int secondsAfterMemRead = systemClockSeconds;                                               // After read.

      systemClockNano = *sharedNanoseconds;


      systemNanoOnly = (long long int) (systemClockSeconds * 1000000000LL) + systemClockNano;
      
      
      // Slow down program to prevent race conditions between Process Table and printf() message times (for oss.c and user.c, respectively).
      int i;
      for (i = 0; i < 20000000; i++) {
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
	 case TERMINATE_PROCESS:
	  
	    // Termination only allowed if at least 1 second of runtime passes. Until then, release a random resource.
	    if ((systemClockSeconds > allowedTerminationTimeSeconds) || 
		  ((systemClockSeconds == allowedTerminationTimeSeconds) && (systemClockNano >= allowedTerminationTimeNano))) {
         
	       processCanTerminate = true;
	      
               if (systemNanoOnly >= termCheckTime) {
                  sendBuffer.selection == TERMINATE_PROCESS;	 
               }
               
	       else {
	          processSelection = RELEASE;
                  sendBuffer.selection = RELEASE;
	       }
	    }
	    else {
	       processSelection = RELEASE;
	       sendBuffer.selection = RELEASE;

	       sendBuffer.processID = getpid();
               sendBuffer.resourceType = resourceType;
   
               sendMessageToOSS();

	    }

	    break;
      }

    
      if (systemNanoOnly >= termCheckTime) {
	 if (processCanTerminate == true) {
            processSelection = TERMINATE_PROCESS;
	    sendBuffer.selection = TERMINATE_PROCESS;
	   
            sendBuffer.processID = getpid();
            sendBuffer.resourceType = resourceType;

            sendMessageToOSS();

	    break;
         }
	 else {
            termCheckTime = timeToCheckForTerminations(systemClockSeconds, systemClockNano);
         }
      }

      receiveMessageFromOSS();
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


int getAllowedTerminationTimeSeconds (int systemClockSeconds) {
   return systemClockSeconds + 1;
}


// The point in simulated system time that unlocks a child's ability to terminate.
long long int timeToCheckForTerminations (int systemClockSeconds, long int systemClockNano) {
   long int oneBillionNS = 1000000000;
   long int quarterSecond = 250000000;
   
   long long int terminationCheckTime = (systemClockSeconds * oneBillionNS) + systemClockNano + quarterSecond;
 
   long long int trueTerminationTime = terminationCheckTime - (terminationCheckTime % quarterSecond);
   
   return trueTerminationTime;
}


// Randomly decides option 1, 2, or 3 based on probability.
ResourceTask determineProcessSelection (int probabilityValue) {
   enum ResourceTask selection;                            

 
   if (probabilityValue >= 1 && probabilityValue <= 800) {
      selection = REQUEST;
   }

   else if (probabilityValue >= 801 && probabilityValue <= 980) {  
      selection = RELEASE;
   }

   else if (probabilityValue >= 981 && probabilityValue <= 1000) {
      selection = TERMINATE_PROCESS;
   }

   return selection;
} 


// msgsnd() operations.
void sendMessageToOSS() {   
   if (msgsnd(messageQueueID, &sendBuffer, sizeof(messageBuffer) - sizeof(long int), IPC_NOWAIT) == -1) {
      printf("ERROR in user.c: Problem with msgsnd() function.\n");

      exit(-1);
   }
}


// Nonblocking msgrcv() operations.
void receiveMessageFromOSS() {
   if (msgrcv(messageQueueID, &receiveBuffer, sizeof(messageBuffer), getpid(), IPC_NOWAIT) == -1) {
      printf("ERROR in worker.c: Problem with msgrcv() function.\n");

      exit(-1);
   }
}

void slowDownProgram() {
   int i;
   for (i = 0; i < 10000000; i++) {
      //  Do nothing.
   }
}
