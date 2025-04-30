#include "functions.h"
#include <stdio.h>
#include <errno.h>


// Create a logfile for process output.
void initializeLogfile() {
   logOutputFP = fopen(logfile, "w");

   if (logOutputFP == NULL) {
      printf("ERROR in oss.c: cannot create a log file named '%s'", logfile);

      exit(-1);
   }
}


// Create message queue for interprocess communication.
void initializeMessageQueue() {
   if ((key = ftok(logfile, 1)) == -1) {
      printf("ERROR in oss.c: problem with ftok() function.\n");
      printf("Cannot access a key for message queue initialization.\n\n");

      exit(-1);
   }

   if ((messageQueueID = msgget(key, PERMISSIONS | IPC_CREAT)) == -1) {
      printf("ERROR in oss.c: problem with msgget() function.\n");
      printf("Cannot acquire a message queue ID for initialization.\n\n");

      exit(-1);
   }
   printf("Message queue is now set up.\n\n");
}


void initializeFeedbackQueue(MultiLevelQueue *queue) {
   queue->front = -1;
   queue->rear = -1;
}


bool isQueueEmpty(MultiLevelQueue *queue) {
   bool frontNeverInitialized = (queue->front == -1);
   bool allElementsDequeued = (queue->front  >  queue->rear);

   if (frontNeverInitialized == true || allElementsDequeued == true) {
      return true;
   }

   return false;
}


// Add process to back of queue.
void enqueue(MultiLevelQueue *queue, pid_t pid) {
   if (queue->rear >= MAX_SIZE - 1) {
      printf("ERROR in oss.c: enqueue() function failed. Queue overflow occurred.\n");

      periodicallyTerminateProgram(-1);
   }

   slowDownProgram();

   if (isQueueEmpty(queue) == true) {
      queue->front = 0;
      queue->rear = 0;
   }
   else {
      queue->rear++;
   }

   queue->processEntries[queue->rear] = pid;
}

pid_t dequeue(MultiLevelQueue *queue) {
   if (isQueueEmpty(queue) == true) {
      printf("ERROR in oss.c: dequeue() function failed. Queue underflow occurred.\n");


      periodicallyTerminateProgram(-1);
   }

   slowDownProgram();

   pid_t processID = queue->processEntries[queue->front];

   queue->front++;

   if (queue->front > queue->rear) {
      initializeFeedbackQueue(queue);
   }


   return processID;
}

// Return value at front of queue WITHOUT removing it.
pid_t peekQueue(MultiLevelQueue *queue) {
   if (isQueueEmpty(queue) == false) {
      int front = queue->front;
      int frontValue = queue->processEntries[front];

      return frontValue;
   }

   return -1;
}

void printAllFeedbackQueues(MultiLevelQueue queue[]) {
   int i;
   for (i = 0; i < QUEUE_COUNT; i++) {
      printf("Queue %d: ", i);
      fprintf(logOutputFP, "Queue %d: ", i);

      printOneQueue(&queue[i]);
   }
   printf("\n");
   fprintf(logOutputFP, "\n");
}
void printOneQueue(MultiLevelQueue *queue) {
   if (isQueueEmpty(queue) == true) {
      printf("(empty)\n");
      fprintf(logOutputFP, "(empty)\n");

      return;
   }

   int i;
   for (i = queue->front; i <= queue->rear; i++) {
      printf("%d ", queue->processEntries[i]);
      fprintf(logOutputFP, "%d ", queue->processEntries[i]);
   }

   printf("\n");
   fprintf(logOutputFP, "\n");
}


// For the allocation and request matrices, place zeros in every element to initialize them.
void initializeMatrix(int matrix[]) {
   int i;
   for (i = 0; i < 100; i++) {
      matrix[i] = 0;
   }
}


// Displays error messages based on 'optarg' arguments.
void checkForOptargEntryError(int value, char getoptArgument[]) {
   if ((value <= 0 || value > 10)  && (strcmp(getoptArgument, "-n [proc]") == 0)) {
      printf("ERROR in oss.c: User must enter an integer between 1 and 10 for argument %s.\n\n", getoptArgument);

      exit(-1);
   }
   if ((value <= 0 || value > 1000) && (strcmp(getoptArgument, "-i [intervalInMSToLaunchChildren]") == 0)) {
      printf("ERROR in oss.c: User must enter an integer between 1 and 1000 for argument %s.\n\n", getoptArgument);

      exit(-1);
   }
   if ((strcmp(getoptArgument, "-n [proc]") != 0) || (strcmp(getoptArgument, "-i [intervalInMSToLaunchChildren]") != 0)) {
      if (value <= 0) {
         printf("ERROR in oss.c: User must enter a positive integer for argument %s.\n\n", getoptArgument);

         exit(-1);
      }
   }
}


// Displays error message if # of simlataneous processes exceeds the total process count.
void checkForSimulExceedsProcError(int simulProcesses, int totalProcesses) {
   if (simulProcesses > totalProcesses) {
      printf("ERROR in oss.c: The -s [simul] value '%d' cannot be greater than the -n [proc] value '%d'.\n\n", simulProcesses, totalProcesses);

      exit(-1);
   }
}

// Adjust system time's seconds and nanoseconds.
long long int incrementClock(int *seconds, long long int *nanoseconds, int increment) {
   (*nanoseconds) += increment;

   if (*nanoseconds > oneBillionNanoseconds) {
       *nanoseconds = 0;
       (*seconds)++;
   }
   else if (*nanoseconds < 0) {
      *nanoseconds += oneBillionNanoseconds;
      (*seconds)--;
   }

   systemNanoOnly = convertSystemTimeToNanosecondsOnly(seconds, nanoseconds);
   return increment;
}


// TOTAL nanoseconds used to determine when to launch the next process.
long long int convertSystemTimeToNanosecondsOnly(int *seconds, long long int *nanoseconds) {
   long long int nanosecondsWithoutSecs = *nanoseconds;
   int i;
   for (i = 1; i <= (*seconds); i++) {
      nanosecondsWithoutSecs += oneBillionNanoseconds;
   }

   return nanosecondsWithoutSecs;
}
    

// Only deals with system nanoseconds to determine next launch time. 
/*long long int determineNextLaunchNanoseconds (long long int maxTimeBetweenProcesses, long long int currentNanoTime) {
   long long int waitTime = (rand() % (maxTimeBetweenProcesses + 1));

   return waitTime + currentNanoTime;
}
*/

// Only deals with system nanoseconds to determine next launch time.
// System seconds is implicitly dealt with in main().
long long int determineNextLaunchNanoseconds (int intervalMS, long long int oldLaunchTime) {
    long long int nanoConversion = (long long int)intervalMS * 1000000;
    long long int newLaunchTime = oldLaunchTime + nanoConversion;

    return newLaunchTime;
}
/*
// Generates a value between 1 and 5 seconds.
long long int determineEventWaitTime(int secondsWaitTimeMax, int millisecondsWaitTimeMax, long long int systemClockTime) {
   int secondsToWait = rand() % secondsWaitTimeMax;
   int millisecondsToWait = rand() % millisecondsWaitTimeMax; 

   if (secondsToWait == secondsWaitTimeMax) {
      millisecondsToWait = 0;
   }

   long long int nanoToWait = (secondsToWait * oneBillionNanoseconds) + (millisecondsToWait * 1000000);
   long long int timeToUnblock = nanoToWait + systemClockTime; 
   

   return timeToUnblock;
}

// Return the correct time quantum based on queue level.
int determineTimeQuantum(int queueLevel) {
   long int timeQuantum;

   // HIGH PRIORITY
   if (queueLevel == 0) {
      timeQuantum = 10 * oneMillionNanoseconds;
   }
   // MEDIUM PRIORITY
   else if (queueLevel == 1) {
      timeQuantum = 20 * oneMillionNanoseconds;
   }
   // LOW PRIORITY
   else if (queueLevel == 2) {
      timeQuantum = 40 * oneMillionNanoseconds; 
   }

   return timeQuantum;
}
*/
// Attempts to prevent race conditions from occurring during message transfers,
void slowDownProgram() {
   int i;
   for (i = 0; i < 200000; i++) {
      // Do nothing.
   }
}


// These three functions below manage and delete entries in the PCB table.
int addToProcessTable(pid_t pid) {
   int i;
   for (i = 0; i < 20; i++) {
      if (processTable[i].occupied == 0) {
         processTable[i].occupied = 1;
         processTable[i].processID = pid;
         processTable[i].startSeconds = systemClockSeconds;
         processTable[i].startNanoseconds = systemClockNano;

	 int j;
	 for (j = 0; j < 5; j++) {
	    processTable[i].allocated[j] = 0;
	    processTable[i].request[j] = 0;
         }

	 processTable[i].blocked = 0;


	 if (systemClockNano == oneBillionNanoseconds) {
            processTable[i].startSeconds++;
            processTable[i].startNanoseconds = 0;
         }

         return i;
      }
   }
   // If table is full, return this value to print an error in main() function.
   return -1;
}

int findIndexInProcessTable(pid_t pid) {
   int i;
   for (i = 0; i < 20; i++) {
      if (processTable[i].processID == pid) {
         return i;
      }
   }

   return -1;
}

/*
void addServiceTimeToProcessTable(int i) {
   processTable[i].serviceTimeNanoseconds += receiveBuffer.quantumData;

   if (processTable[i].serviceTimeNanoseconds >= oneBillionNanoseconds) {	   
      processTable[i].serviceTimeSeconds++;
      processTable[i].serviceTimeNanoseconds -= oneBillionNanoseconds;
   }
}
*/
/*
void addWaitTimeToProcessTable(long long int waitTime, int i) {
   processTable[i].eventWaitNanoseconds = waitTime;

   while (processTable[i].eventWaitNanoseconds >= oneBillionNanoseconds) {
      processTable[i].eventWaitSeconds++;
      processTable[i].eventWaitNanoseconds -= oneBillionNanoseconds;
   }
}
*/
/*
void possiblyUnblockChild(MultiLevelQueue *queue) {
   long long int eventWaitNanoOnly;
   long int dequeuedChild;
   int i;

   for (i = 0; i < 20; i++) {
      eventWaitNanoOnly = (processTable[i].eventWaitSeconds * oneBillionNanoseconds) + processTable[i].eventWaitNanoseconds;
      
      if (systemNanoOnly >= eventWaitNanoOnly && eventWaitNanoOnly != 0) {
	 processTable[i].eventWaitSeconds = 0;
	 processTable[i].eventWaitNanoseconds = 0;
         processTable[i].blocked = 0;

         while (dequeuedChild != processTable[i].processID) {
//	    dequeuedChild = dequeue(&queue[3]);
      	    
	    if (dequeuedChild == processTable[i].processID) {
	       break;
	    }
	 //   enqueue(&queue[3], dequeuedChild);
	 }
	 //enqueue(&queue[0], processTable[i].processID);
      }  
   }
}   
*/
void removeFromProcessTable(pid_t pid) {
   int i;
   for (i = 0; i < 20; i++) {
      if (processTable[i].processID == pid) {
         processTable[i].occupied = 0;
         processTable[i].processID = 0;
         processTable[i].startSeconds = 0;
         processTable[i].startNanoseconds = 0;
         
	 int j;
	 for (j = 0; j < 5; j++) {
            processTable[i].allocated[j] = 0;
            processTable[i].request[j] = 0;
         }

         processTable[i].blocked = 0;

         break;
      }
   }
}


// Output goes to console.
void printProcessTable() {
   printf("OSS: Outputting process table:\n");
   printf("\nOSS PID: %d  SysClockS: %d  SysClockNano: %lld\n", getpid(), systemClockSeconds, systemClockNano);
   printf("Process Table:\n");
   printf("Entry\t Occupied\t PID\t\t StartS\t StartN\t\t Allocated\t Request\t Blocked\n");

   int i, j;


   for (i = 0; i < 20; i++) {
      // Prints first 3 columns (Entry, Occupied, PID).
      printf("%d\t %d\t\t %d\t", i, processTable[i].occupied, processTable[i].processID);
      if (processTable[i].processID == 0) {
         printf("\t");
      }

      // Prints columns 4 and 5 (StartS, StartN).
      printf(" %d\t %ld\t", processTable[i].startSeconds, processTable[i].startNanoseconds);
      if (processTable[i].startNanoseconds < 1000000) {
         printf("\t");
      }

      // Prints column 6 (Allocated).
      for (j = 0; j < 5; j++) {
         printf(" %d", processTable[i].allocated[j]);
      }
      printf("\t");

      // Prints column 7 (Request).
      for (j = 0; j < 5; j++) {
         printf(" %d", processTable[i].request[j]);
      }
      printf("\t");


      // Prints column 8 (Blocked--the final column).
      printf(" %d\n", processTable[i].blocked);
   }

   printf("\n\n");

   printProcessTableToLogfile();
}


void printProcessTableToLogfile() {
   fprintf(logOutputFP, "OSS: Outputting process table:\n");
   fprintf(logOutputFP, "\nOSS PID: %d  SysClockS: %d  SysClockNano: %lld\n", getpid(), systemClockSeconds, systemClockNano);
   fprintf(logOutputFP, "Process Table:\n");
   fprintf(logOutputFP, "Entry\t Occupied\t PID\t\t StartS\t StartN\t\t Allocated\t Request\t Blocked\n");

   int i, j;


   for (i = 0; i < 20; i++) {
      // Prints first 3 columns (Entry, Occupied, PID).
      fprintf(logOutputFP, "%d\t %d\t\t %d\t", i, processTable[i].occupied, processTable[i].processID);
      if (processTable[i].processID == 0) {
         fprintf(logOutputFP, "\t");
      }

      // Prints columns 4 and 5 (StartS, StartN).
      fprintf(logOutputFP, " %d\t %ld\t", processTable[i].startSeconds, processTable[i].startNanoseconds);
      if (processTable[i].startNanoseconds < 1000000) {
         fprintf(logOutputFP, "\t");
      }

      // Prints column 6 (Allocated).
      for (j = 0; j < 5; j++) {
         fprintf(logOutputFP, " %d", processTable[i].allocated[j]);
      }
      fprintf(logOutputFP, "\t");

      // Prints column 7 (Request).
      for (j = 0; j < 5; j++) {
         fprintf(logOutputFP, " %d", processTable[i].request[j]);
      }
      fprintf(logOutputFP, "\t");


      // Prints column 8 (Blocked--the final column).
      fprintf(logOutputFP, " %d\n", processTable[i].blocked);
   }

   fprintf(logOutputFP, "\n\n");
}




// Request matrix represents that a child process is requesting a specific resource type.
void updateRequestMatrix(int row, int column, int *matrix, ResourceTask option) {
   int location;

   if (option == REQUEST) {
      location = (5 * row) + column;

      matrix[location] = 1;
      processTable[row].request[column] = 1;
   }
   else if (option == RELEASE) {
   
   
   }
   else if (option == TERMINATE_PROCESS) {
      int i;
      location = 5 * row;
      
      for (i = 0; i < 5; i++) {
         matrix[location] = 0;
	 location++;
      }
   }
}

void updateAllocationMatrix(int row, int column, int *matrix, ResourceTask option) {
   int location;

   if (option == REQUEST) {
      location = (5 * row) + column;

      matrix[location]++;
      processTable[row].allocated[column]++;
   }
   else if (option == RELEASE) {
      



   }
   else if (option == TERMINATE_PROCESS) {
      int i;
      location = 5 * row;

      for (i = 0; i < 5; i++) {
         matrix[location] = 0;
         location++;
      }


   }
}

void updateAllocationVector(int element, int *allocationVector, ResourceTask option) {
   if (option == REQUEST) {	
      allocationVector[element]--;
   }
   else if (option == RELEASE) {


   }
   else if (option == TERMINATE_PROCESS) {


   }
}

void updateAllocationVector(int row, int *allocationMatrix, int *allocationVector, ResourceTask option) {
   int i; 
   int location; 

   if (option == TERMINATE_PROCESS) {
      location = 5 * row;

      for (i = 0; i < 5; i++) {
         allocationVector[i] += allocationMatrix[location];
	 location++;
      }
   }
}
void printResourceTable(int matrix[]) {
   printf("OSS: Outputting resource table:\n");
   printf("\nOSS PID: %d  SysClockS: %d  SysClockNano: %lld\n", getpid(), systemClockSeconds, systemClockNano);
   printf("Resource table:\n");
   printf("\t R0\t R1\t R2\t R3\t R4\n");
   
   int i, j, k;

   for (i = 0; i < 20; i++) {
      printf("P%d\t ", i);

      for (j = 0; j < 5; j++) {
         k = (5 * i) + j;
	 
	 printf("%d\t ", matrix[k]);
      }
      printf("\n");
   }
   printf("\n\n");

   printResourceTableToLogfile(matrix);
}

void printResourceTableToLogfile(int matrix[]) {
   fprintf(logOutputFP, "OSS: Outputting resource table:\n");
   fprintf(logOutputFP, "\nOSS PID: %d  SysClockS: %d  SysClockNano: %lld\n", getpid(), systemClockSeconds, systemClockNano);
   fprintf(logOutputFP, "Resource table:\n");
   fprintf(logOutputFP, "\t R0\t R1\t R2\t R3\t R4\n");

   int i, j, k;

   for (i = 0; i < 20; i++) {
      fprintf(logOutputFP, "P%d\t ", i);

      for (j = 0; j < 5; j++) {
         k = (5 * i) + j;

         fprintf(logOutputFP, "%d\t ", matrix[k]);
      }
      fprintf(logOutputFP, "\n");
   }
   fprintf(logOutputFP, "\n\n");
}


// Perform msgsnd() operations.
void sendMessageToUSER() {
   if (msgsnd(messageQueueID, &sendBuffer, sizeof(messageBuffer) - sizeof(long int), 0) == -1) {
      
      printf("ERROR in oss.c: Problem with msgsnd() function.\n");
      printf("Cannot send message to user.c.\n\n");

      periodicallyTerminateProgram(-1);
   }
}

// Perform nonblocking msgrcv() operations.
void receiveMessageFromUSER(int i) {
   if (msgrcv(messageQueueID, &receiveBuffer, sizeof(messageBuffer), 0, IPC_NOWAIT) == -1) {
      if (errno == ENOMSG) {
         // Keep running oss.c
      }
      else {
         printf("ERROR in oss.c: Problem with msgrcv() function.\n");
         
         periodicallyTerminateProgram(-1);
      }
   }
}


// Displays a help message if user enters './oss -h'.
void printHelpMessage() {
   printf("\n\n\nThis program displays information about child and parent processes, including:\n");
   printf("\t1.) A Process Control Block (PCB) table with child process entry information.\n");
   printf("\t2.) Messages on the console and a logfile related to process scheduling.\n");


   printf("\n\nTo execute this program, type './oss', then type in any combination of options:\n\n\n");
   printf("Option:                       What to enter after option:               Default values (if argu-      Description:\n");
   printf("                                                                         ment is not entered):\n");
   printf("  -h                           > nothing.                                 > (not applicable)           > Displays this help menu.\n"); 
   printf("  -n [proc]                    > an integer between 1 and 10.             > defaults to 1.             > Runs a total # of processes.\n");
   printf("  -s [simul]                   > an integer smaller than '-n [proc]'.     > defaults to 1.             > Runs a max # of processes simultaneously.\n");
   printf("  -i [intervalInMS             > an integer between 1 and 1000.           > defaults to 500.           > Runs a new process every [interval\n");
   printf("       ToLaunchChildren]                                                                                  inMSToLaunchChildren] milliseconds.\n");
   printf("  -f [logfile]                 > a file's basename.                       > defaults to 'logfile'      > Stores output relating to parent and\n");
   printf("                                                                                                          child processes.\n\n"); 

   printf("For example, typing './oss -n 6 -s 4 -i 600 -f storage' will run:\n");
   printf("\t1.) a total of 6 processes.\n");
   printf("\t2.) a maximum of 4 processes simultaneously.\n");
   printf("\t3.) a new process every 600 milliseconds.\n");
   printf("\t4.) while storing message statuses inside a file called 'storage.txt'.\n\n\n");

   exit(EXIT_SUCCESS);
}


void printStatistics () {
   deadlockDetectionTermPercentage = ((double)processesTerminatedByDeadlock / ((double)processesTerminatedByDeadlock + (double)processesTerminatedGracefully)) * 100;

   printf("********************PROGRAM SUMMARY********************\n\n");
   printf("Requests granted immediately:\t\t %d\n", requestsGrantedImmediately);
   printf("Requests granted after waiting:\t\t %d\n", requestsGrantedAfterWaiting);
   printf("Deadlock detection terminations:\t %d\n", processesTerminatedByDeadlock); 
   printf("Graceful terminations:\t\t\t %d\n", processesTerminatedGracefully);
   printf("Deadlock termination percentage:\t %.2f%%\n", deadlockDetectionTermPercentage);
   printf("# of deadlock detection operations:\t %d\n\n", deadlockDetectionAlgCount);
   printf("*******************************************************\n\n");
}

// Clean memory segments and message queue.
void detachAndClearSharedMemory () {
   shmdt(secondsShared);
   shmdt(nanosecondsShared);
   shmdt(logfileFP);
   shmctl(secondsShmid, IPC_RMID, NULL);
   shmctl(nanoShmid, IPC_RMID, NULL);
   shmctl(logfileShmid, IPC_RMID, NULL);
}

void removeMessageQueue() {
   if (msgctl(messageQueueID, IPC_RMID, NULL) == -1) {
      printf("ERROR in oss.c: problem with msgctl() function.\n");
      printf("Cannot delete or remove message queue.\n\n");

      exit(-1);
   }
}


// Gracefully terminates program after a function error or use of CTRL + C.
void periodicallyTerminateProgram(int signal) {
   printf("\n\n\nNow terminating all child processes...\n");

   int i;
   for (i = 0; i < 20; i++) {
      if (processTable[i].occupied == 1) {
         pid_t processID = processTable[i].processID;

         if (processID > 0) {
            kill(processTable[i].processID, SIGTERM);

            printf("Signal SIGTERM was sent to PID %d\n", processID);
         }
      }
   }
   printf("Child process termination complete.\n\n");

   
   // Shared memory operations.
   printf("Now freeing shared memory...\n");
   detachAndClearSharedMemory();
   printf("Shared memory detachment and deletion complete.\n\n");
   

   // Queue removal operations.
   printf("Now deleting the message queue...\n");
   removeMessageQueue();
   printf("Message queue removal and deletion complete.\n\n");
 

   // Graceful termination.
   printf("Now exiting program...\n\n");

   exit(0);
}




