#include "functions.h"
#include <sys/wait.h>
#include <sys/ipc.h>
#include <stdio.h>
#include <errno.h>
#include <math.h>



/************************************************PROGRAM INITIALIZATION************************************************/

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


// A wait queue holds processes that are waiting for an unavailable resource type.
void initializeFeedbackQueue(MultiLevelQueue *queue) {
   queue->front = -1;
   queue->rear = -1;
}


// For the allocation and request matrices, place zeros in every element to initialize them.
void initializeMatrix(int matrix[]) {
   int i;
   for (i = 0; i < 100; i++) {
      matrix[i] = 0;
   }
}



/**********************************************RESOURCE QUEUE OPERATIONS***********************************************/

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


// If process ID is in the queue, it will prevent duplicate queue entries.
bool searchQueue(MultiLevelQueue *queue, int processID) {
   int i;
	
   // If queue is empty.
   if (isQueueEmpty(queue) == true) {
      return false;
   }
   
   // If process ID is found in the queue.
   for (i = queue->front; i <= queue->rear; i++) {
      if (queue->processEntries[i] == processID) {
         return true;
      }
   }

   // If process ID is NOT in the queue.
   return false;
}


// Removes from ANY part of the queue (not just the first in line).
void removeFromQueue(MultiLevelQueue *queue, int processID) {
   bool foundID = false;
   int i;

   // If queue is empty, do nothing.	
   if (queue->front == -1 || queue->rear == -1) {
      return;
   }
   
   // Attempts to find process ID. If found, shift entries to the left.
   for (i = queue->front; i <= queue->rear; i++) {
      if (queue->processEntries[i] == processID) {
         foundID = true;
      }
      if (foundID == true && i < queue->rear) {
         queue->processEntries[i] = queue->processEntries[i + 1];
      }
   }

   // If found, reduce rear index and make queue empty if found process ID is the only element in the queue.
   if (foundID == true) {
      queue->rear--;

      if (queue->rear < queue->front) {
         queue->front = -1;
	 queue->rear = -1;
      }
   }
}


void printAllResourceQueues(MultiLevelQueue queue[]) {
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



/************************************************USER INPUT VALIDATION*************************************************/

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



/***********************************************SYSTEM CLOCK OPERATIONS************************************************/

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
// System seconds is implicitly dealt with in main().
long long int determineNextLaunchNanoseconds (int intervalMS, long long int oldLaunchTime) {
    long long int nanoConversion = (long long int)intervalMS * 1000000;
    long long int newLaunchTime = oldLaunchTime + nanoConversion;

    return newLaunchTime;
}


// OSS grants, denies, or releases resources at a random time between 0 and B nanoseconds.
long int determineBoundB (long int B) {

   long int nextDecisionTime = rand() % B;

   return nextDecisionTime;
}


// Attempts to prevent race conditions from occurring during message transfers,
void slowDownProgram() {
   int i;
   for (i = 0; i < 200000; i++) {
      // Do nothing.
   }
}



/**********************************************PROCESS TABLE OPERATIONS************************************************/

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


// Determine the row that a process ID is located in.
int findIndexInProcessTable(pid_t pid) {
   int i;
   for (i = 0; i < 20; i++) {
      if (processTable[i].processID == pid) {
         return i;
      }
   }

   return -1;
}


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



/**********************************************MATRIX/VECTOR OPERATIONS************************************************/

// Request matrix represents that a child process is requesting a specific resource type.
void updateRequestMatrix(int row, int column, int *matrix, ResourceTask option) {
   int location;

   if (option == REQUEST) {
      location = (5 * row) + column;

      matrix[location] = 1;
      processTable[row].request[column] = 1;
   }

   else if (option == RELEASE || option == TERMINATE_PROCESS) {
      int i;
      location = 5 * row;

      
      for (i = 0; i < 5; i++) {
         matrix[location] = 0;
	 processTable[row].request[column] = 0;
         location++;
	 row++;
      }
   }
}


// Allocation matrix represents how many resources a child holds.
void updateAllocationMatrix(int row, int column, int *matrix, ResourceTask option) {
   int location;

   if (option == REQUEST) {
      location = (5 * row) + column;

      matrix[location]++;
      processTable[row].allocated[column]++;
   }

   else if (option == RELEASE) {
      location = (5 * row) + column;

      matrix[location]--;
      processTable[row].allocated[column]--;
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


// Allocation vector represents how many resources are remaining in the system.
void updateAllocationVector(int element, int *allocationVector, ResourceTask option) {
   if (option == REQUEST) {	
      allocationVector[element]--;
   }
   else if (option == RELEASE) {
      allocationVector[element]++;  
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


// If one process holds all 10 of a specific resource type, automatically release one of those 10.
void releaseOneResource(int *requestMatrix, int *allocationMatrix, int *allocationVector, MultiLevelQueue *queue) {
   int i;

   for (i = 0; i < 100; i++) {
      if (allocationMatrix[i] == 10) {

	 double row = floor(i / 5);
	 int child = (int) row;

         int resource = i % 5;
         long int processID = processTable[child].processID;


	 printf("OSS: Process P%d (PID %ld) is RELEASING R%d ", child, processID, resource);
         printf("at time %d:%lld.\n",  systemClockSeconds, systemClockNano);
         fprintf(logOutputFP, "OSS: Process P%d (PID %ld) is RELEASING R%d ", child, processID, resource);
         fprintf(logOutputFP, "at time %d:%lld.\n",  systemClockSeconds, systemClockNano);

       
         updateRequestMatrix(child, resource, requestMatrix, RELEASE);
         updateAllocationMatrix(child, resource, allocationMatrix, RELEASE);
	 updateAllocationVector(resource, allocationVector, RELEASE);

         
	 processTable[child].blocked = 0;

         removeFromQueue(&queue[resource], processID);


	 break;
      }
   }
}


bool runDeadlockAlgorithm(int *requestMatrix, int *allocationMatrix, int *allocationVector, int processes, int resources, MultiLevelQueue *queue) {
   printf("OSS: Running deadlock detection algorithm at time %d:%lld.\n", systemClockSeconds, systemClockNano);
   fprintf(logOutputFP, "OSS: Running deadlock detection algorithm at time %d:%lld.\n", systemClockSeconds, systemClockNano);


   int work[resources];
   bool finish[processes];
   int deadlockedPIDs[processes];
   int deadlockedProcesses = 0;
   int i, j, p;
   int location;

   // Initialize 'work' vector.
   for (i = 0; i < resources; i++) {
       work[i] = allocationVector[i];
   } 

   // Initialize 'finish' vector. No processes can finish at first.
   for (i = 0; i < processes; i++) {
       finish[i] = false;
   }

   // Check if all processes are blocked.
   bool allProcessesBlocked = true;
   int blockedCount = 0;

   for (p = 0; p < processes; p++) {
      if (processTable[p].occupied == 1 && processTable[p].blocked == 1) {
         blockedCount++;

	 if (processesCanBeFulfilled(requestMatrix, allocationVector, p, resources) == true) {
            allProcessesBlocked = false;

	    break;
	 }
      }
   }

   printf("\tProcesses deadlocked:\t");
   fprintf(logOutputFP, "\tProcesses deadlocked:\t");

   // If ALL processes are blocked, then there is a deadlock. Terminate a process.
   if (blockedCount > 0 && allProcessesBlocked == true) {
      int deadlockedPID = -1;

      // Find a blocked process to kill.
      for (p = 0; p < processes; p++) {
         if (processTable[p].occupied == 1 && processTable[p].blocked == 1) {
            deadlockedPID = p;

	    break;
	 }
      }

 
      if (deadlockedPID != -1) {
         int processID = processTable[deadlockedPID].processID;
	 int location = deadlockedPID * resources;
         
	 printf("\n");
         fprintf(logOutputFP, "\n");
         printf("\tOSS: terminating P%d (PID %d) to remove deadlock.\n", deadlockedPID, processID);
         fprintf(logOutputFP, "\tOSS: terminating P%d (PID %d) to remove deadlock.\n", deadlockedPID, processID);
     
         kill(processID, SIGTERM);
	 removeFromProcessTable(processID);

	 for (i = 0; i < resources; i++) {
            processTable[i].blocked = 0;
	 }

	 removeFromQueue(&queue[resources], processID);

	 return true;
      }
   }

   // Determine any processes that can finish.
   for (p = 0; p < processes; p++) {
      if (finish[p] == true) {
         continue;
      }

      // If all resource requests can be granted, then the processes can finish.
      if (canRequestBeGranted(requestMatrix, work, p, resources) == true) {
         finish[p] = true;
     
//         printf("finish[%d]: %d ", p, finish[p]); 

         // Allocated resources become released.
	 location = p * resources;

         for (j = 0; j < resources; j++) {
            work[j] += allocationMatrix[location];
            location++;
         }

         // Check from beginning once again.
         p = -1;     
      }
   } 

   //printf("\tProcesses deadlocked:\t");
  // fprintf(logOutputFP, "\tProcesses deadlocked:\t");
   

   // A deadlock exists if a process is not finished. If deadlock exists, return true.
   for (p = 0; p < processes; p++) { 
     if (finish[p] == false && processTable[p].occupied == 1) {
	 printf("P%d\t", p);
         fprintf(logOutputFP, "P%d\t", p);
         deadlockedPIDs[deadlockedProcesses++] = p;
      }
   }


   if (deadlockedProcesses > 0) {
      i = 0;
      int status;
      int processToKill = deadlockedPIDs[0];
      int processID = processTable[processToKill].processID;
      int location = i * resources;

      printf("\n");
      fprintf(logOutputFP, "\n");
      printf("\tOSS: terminating P%d (PID %d) to remove deadlock.\n", processToKill, processID);
      fprintf(logOutputFP, "\tOSS: terminating P%d (PID %d) to remove deadlock.\n", processToKill, processID);

      kill(processID, SIGTERM);
     // printf("Post-deadlock check: Process %d is %s\n", processToKill, (processTable[processToKill].occupied == 0) ? "terminated" : "alive");

     // waitpid(processID, &status, 0);
     
      removeFromProcessTable(processID);
 //     childrenActive--;
    //  printf("Post-deadlock check: Process %d is %s\n", processToKill, (processTable[processToKill].occupied == 0) ? "terminated" : "alive");    

      for (i = 0; i < resources; i++) {
         requestMatrix[location] = 0;
         allocationVector[i] += allocationMatrix[location];
	 allocationMatrix[location] = 0;

	 location++;

      }
      for (i = 0; i < processes; i++) {
         processTable[i].blocked = 0;
      }
      removeFromQueue(&queue[resources], processID);

      
      return true;
   }


   // If no deadlock exists.
   printf("NONE.\n\n");
   fprintf(logOutputFP, "\tNONE.\n\n");

   return false;
}


// Checks if there exists an unblocked process.
bool processesCanBeFulfilled(int *requestMatrix, int *allocationVector, int index, int resources) {
   int location = index * resources;
   int i;

   for (i = 0; i < resources; i++) {
      if (requestMatrix[location + i] > allocationVector[i]) {
         return false;
      }
   }

   return true;
}


// Determines whether a resource type request can be granted for a child.
bool canRequestBeGranted(int *requestMatrix, int *allocationVector, int processNumber, int resources) {
   int location = processNumber * resources;
   int i;

   for (i = 0; i < resources; i++) {
      if (requestMatrix[location] > allocationVector[i]) {
	 return false;
      }

      location++;
   }

   return true;
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


void printChildTerminationMessage(int *allocationMatrix, int child, long int processID) {
   int i;
   int j = 5 * child;
   bool isAllocationMatrixRowEmpty = true;

   printf("---OSS: Process P%d (PID %ld) TERMINATED.---\n", child, processID);
   printf("\tResources released: ");
   fprintf(logOutputFP, "---OSS: Process P%d (PID %ld) TERMINATED.---\n", child, processID);
   fprintf(logOutputFP, "\tResources released: ");


   // Determine whether to print resource type quantities or 'NONE' for released resources.
   for (i = 0; i < 5; i++) {
      if (allocationMatrix[j] > 0) {
         isAllocationMatrixRowEmpty = false;

         break;
      }
      j++;
   }

   j = 5 * child;

   // If no resources were released before termination.
   if (isAllocationMatrixRowEmpty == true) {
      printf("NONE\n");
      fprintf(logOutputFP, "NONE\n");
   }

   // If at least one resource type was released before termination.
   for (i = 0; i < 5; i++) {
      if (allocationMatrix[j] > 0) {
         printf("P%d: %d    ", i, allocationMatrix[j]);
         fprintf(logOutputFP, "P%d: %d    ", i, allocationMatrix[j]);
      }
      j++;
   }
   printf("\n");
   fprintf(logOutputFP, "\n");

}



/********************************************MESSAGE PASSING OPERATIONS************************************************/

// Perform msgsnd() operations.
void sendMessageToUSER() {
   if (msgsnd(messageQueueID, &sendBuffer, sizeof(messageBuffer) - sizeof(long int), IPC_NOWAIT) == -1) {
      if (errno == ENOMSG) {
         // Keep running oss.c.
      }
      else {
         printf("ERROR in oss.c: Problem with msgsnd() function.\n");

         periodicallyTerminateProgram(-1);
      }
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



/***************************************************USER GUIDANCE******************************************************/

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



/*************************************************PROGRAM TERMINATION**************************************************/

void printStatistics () {
   totalRequestsGranted = requestsGrantedImmediately + requestsGrantedAfterWaiting;
   deadlockDetectionTermPercentage = ((double)processesTerminatedByDeadlock / ((double)processesTerminatedByDeadlock + (double)processesTerminatedGracefully)) * 100;

   printf("********************PROGRAM SUMMARY********************\n\n");
   printf("Total granted requests:\t\t\t %d\n", totalRequestsGranted);
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

   printStatistics();


   exit(0);
}




