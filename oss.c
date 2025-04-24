/* Jesse Gempel
 * 4/15/2025
 * Professor Mark Hauschild
 * CMP SCI 4760-001
*/


// The oss.c file works with PARENT processes.
// It launches a specific number of user processes with user input gathered from the 'getopt()' switch statement. 
// This time, process launches will be scheduled by utilizing the Multilevel Feedback Queue scheduling method.


#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <signal.h>
#include <time.h>


// Starting a memory segment for system clock seconds.
#define SHMKEY1 42069
#define INT_BUFFER_SIZE sizeof(int)

// Starting a memory segment for system clock nanoseconds.
#define SHMKEY2 42070
#define LONG_BUFFER_SIZE sizeof(long int)

// Starting a memory segment for a log file.
#define SHMKEY3 42071
#define LOGFILE_BUFFER_SIZE 105

// Permissions for memory queue.
#define PERMISSIONS 0644

// Starting a multilevel feedback queue.
#define MAX_SIZE 1000
#define QUEUE_COUNT 4


// Initializes a log file (and a file pointer) to store message queue information.
char logfile[105] = "logfile.txt";
char suffix[] = ".txt";
FILE *logOutputFP = NULL;


// Creates and attaches two shared memory identifiers (plus one for a log file).
int secondsShmid = shmget(SHMKEY1, INT_BUFFER_SIZE, 0777 | IPC_CREAT);
int *secondsShared = (int *)shmat(secondsShmid, 0, 0);

long int nanoShmid = shmget(SHMKEY2, LONG_BUFFER_SIZE, 0777 | IPC_CREAT);
long int *nanosecondsShared = (long int *)shmat(nanoShmid, 0, 0);

int logfileShmid = shmget(SHMKEY3, LOGFILE_BUFFER_SIZE, 0777 | IPC_CREAT);
char *logfileFP = (char *)shmat(logfileShmid, 0, 0);


// Sets up for different queues for multilevel feedback scheduling.
typedef struct MultiLevelQueue {
   int processEntries[MAX_SIZE];
   int front;
   int rear;
} MultiLevelQueue;

// A process table holds information about each child process.
struct PCB {
   int occupied;                            // Is the entry in the process table empty (0) or full (1)?
   pid_t processID;                         // Child's process ID.
   int startSeconds;                        // Time when a process FORKED (in seconds).
   long int startNanoseconds;               // Time when a process FORKED (in nanoseconds).
   int serviceTimeSeconds;                  // Total time when a process was SCHEDULED (in seconds).
   long int serviceTimeNanoseconds;         // Total time when a process was SCHEDULED (in nanoseconds).
   int eventWaitSeconds;                    // Time when a process becomes UNBLOCKED (in seconds).
   long long int eventWaitNanoseconds;	    // Time when a process becomes UNBLOCKED (in nanoseconds).
   int blocked;                             // Is the process blocked (1) or unblocked (0)?
};
struct PCB processTable[20];


// Holds message queue information.
typedef struct messageBuffer {
   long int messageType;
   char stringData[100];
   long int quantumData;
} messageBuffer;


// Initializes information for message buffer.
messageBuffer sendBuffer;
messageBuffer receiveBuffer;
int messageQueueID;
key_t key;


// Places nanosecond values into variables for easier code readability.
long int oneMillionNanoseconds = 1000000;
long int halfBillionNanoseconds = 500000000;
long int oneBillionNanoseconds = 1000000000;
long int oneQuarterSecond = 250000000;
long int hundredMS = 100000000;

// Initialization of simulated system time. Increments whenever a scheduling event occurs.
int systemClockSeconds = 0;
long long int systemClockNano = 0;
long long int systemNanoOnly = 0;                                

int lastTablePrintSeconds = 0;
long int lastTablePrintNano = 0;

long int systemClockIncrement = hundredMS;


// For determining the child process order in which messages are to be sent.
int currentChildIndex = 0;


/*************************FUNCTION PROTOTYPES********************************/

//For initialization.
void initializeLogfile();
void initializeMessageQueue();
void initializeFeedbackQueue(MultiLevelQueue *);

// For feedback queue.
bool isQueueEmpty(MultiLevelQueue *);
void enqueue(MultiLevelQueue *, pid_t);
pid_t dequeue(MultiLevelQueue *);
pid_t peekQueue(MultiLevelQueue *);
void printAllFeedbackQueues(MultiLevelQueue *);
void printOneQueue(MultiLevelQueue *);

// For system clock/time operations.
long long int incrementClock(int *, long long int *, int);
long long int convertSystemTimeToNanosecondsOnly (int *, long long int *); 
long long int determineNextLaunchNanoseconds(long long int, long long int);
int determineDispatchTime();
long long int determineEventWaitTime(int, int, long long int);
int determineTimeQuantum(int);
void slowDownProgram();

// For process table operations.
int addToProcessTable(pid_t);
int findIndexInProcessTable(pid_t);
void addServiceTimeToProcessTable(int);
void addWaitTimeToProcessTable(long long int, int);
void possiblyUnblockChild(MultiLevelQueue *);
void removeFromProcessTable(pid_t);
void printProcessTable();
void printProcessTableToLogfile();

// For message passing operations.
void sendMessageToUSER();
void receiveMessageFromUSER(int);

// For guiding the user.
void printHelpMessage();

// For terminating the program.
void detachAndClearSharedMemory();
void removeMessageQueue();
void periodicallyTerminateProgram(int);



int main(int argc, char** argv) {
   int opt;
   strcpy(logfileFP, logfile);

   // Hardcoded values (that used to be user options).
   int proc = 1;
   int simul = 1;

   // Determines a random time frame between process launches.
   long long int maxTimeBetweenNewProcsNS = halfBillionNanoseconds;
   long long int nextLaunchTimeNano = determineNextLaunchNanoseconds(maxTimeBetweenNewProcsNS, systemNanoOnly);
   long long int currentLaunchTimeNano = 0;
   
   // Default time that process spends in dispatch. New values randomly generated.
   int dispatchTime = 1000;
   int maxWaitTimeSeconds = 5;
   int maxWaitTimeMS = 1000;

   // Copies the default log file name into shared memory.
   strcpy(logfileFP, "logfile.txt");

   // User option for entering a logfile name.
   char logfileName[] = "-f [logfile]";

   
   while ((opt = getopt(argc, argv, "hf:")) != -1) {
      switch (opt) {
         case 'h':
            printHelpMessage();

	    break;

	 case 'f':
	    char basename[100];

	    // Gathers filename input.
	    strncpy(basename, optarg, sizeof(basename) - 1);
	    basename[sizeof(basename) - 1] = '\0';

	    // Adds .txt suffix to user-inputted basename.
	    strcat(basename, suffix);
	    strcpy(logfile, basename);                    

	    // Copies user-inputted filename into shared memory.
	    strcpy(logfileFP, logfile);

	    break;

         default:
	    printf("ERROR in oss.c: Arguments are invalid or you forgot to input a value for them.\n");
	    printf("Please type './oss -h' for help.\n\n");

            exit(-1);

 	    break;
      }
   }


   bool processesFinished = false;                                  // Determines whether the program should end.
   int childrenActive = 0;                                          // # of children running simultaneously (not to be confused with 'proc').
   int totalChildrenLaunched = 0;                                   // # of children launched so far (not to be confused with 'simul').  
   int plannedTerminations = 0;
   int nextChild = 0;
   int childWithServiceTime = -1;
   int iterationBeforeBreak = 0;
   int queueLevel = 0;
   int blocked = 0;
   
   pid_t processDispatchedNext;

   // Initializes shared memory segments.
   *secondsShared = 0;
   *nanosecondsShared = 0;

   // Initializes each queue for multilevel feedback scheduling.
   MultiLevelQueue queue[QUEUE_COUNT];
   int i;
   for (i = 0; i < QUEUE_COUNT; i++) {
      initializeFeedbackQueue(&queue[i]);
   }

   // Track real-life time to determine when to stop launching processes.
   long int realSeconds;
   long int realMicroseconds;
   struct timeval realStartTime;
   struct timeval realCurrentTime;
   gettimeofday(&realStartTime, NULL);

   // Creates .txt file and message queue to store message update information from oss.c (this file).
   initializeLogfile();
   initializeMessageQueue();

   // Signal handler for terminating program after 60 real-life seconds.
   signal(SIGALRM, periodicallyTerminateProgram);
   signal(SIGINT, periodicallyTerminateProgram);
   signal(SIGTERM, periodicallyTerminateProgram);
   alarm(60);



   while (processesFinished == false) {
      possiblyUnblockChild(queue);

      // If children are still available to launch simultaneously.
      if (childrenActive < simul && totalChildrenLaunched < proc) {
         pid_t processID;
              

         // Keeps incrementing system clock until a process is ready to launch.
         while (1) {
           gettimeofday(&realCurrentTime, NULL);        
	  
	   // Determine when program should stop launching processes.
           realSeconds = realCurrentTime.tv_sec - realStartTime.tv_sec;
           realMicroseconds = realCurrentTime.tv_usec - realStartTime.tv_usec;	   
	
	   if (realSeconds >= 3 || totalChildrenLaunched >= 100) {
	      break;
	   }
	   
           long long int lastTablePrintout = (lastTablePrintSeconds * oneBillionNanoseconds) + lastTablePrintNano;

	   if (realMicroseconds < 0) {
              realSeconds--;
	      realMicroseconds += 1000000;
	   }

	   if (realSeconds >= 3 || totalChildrenLaunched >= 100) {
	      break;
	   }

           if (systemNanoOnly - lastTablePrintout >= halfBillionNanoseconds) { 		   
              printAllFeedbackQueues(queue);
	      printProcessTable();

	      lastTablePrintSeconds = systemClockSeconds;
	      lastTablePrintNano = systemClockNano;
           }

	   // If no processes are ready, increment the clock 100 ms.
	   systemClockIncrement = incrementClock(&systemClockSeconds, &systemClockNano, hundredMS); 
  

           // System time in shared memory constantly updates in loop.
           *secondsShared = systemClockSeconds;
           *nanosecondsShared = systemClockNano;

	   currentLaunchTimeNano = nextLaunchTimeNano;


           // Launches a child based on [maxTimeBetweenNewProcsNS]. 
	   if (systemNanoOnly >= nextLaunchTimeNano) {
	       processID = fork();

               nextLaunchTimeNano = determineNextLaunchNanoseconds(maxTimeBetweenNewProcsNS, systemNanoOnly);
	      	       
	       // Makes sure system time updates EXACTLY to when a child launches, without rounding to the next 100 ms.
	       long int exactLaunchTime = currentLaunchTimeNano - systemNanoOnly;
	       systemClockIncrement = incrementClock(&systemClockSeconds, &systemClockNano, exactLaunchTime);
              
	       break;
	    }   
         }

	 // Work with child process. 
         if (processID == 0 && realSeconds < 3) {
            *secondsShared = systemClockSeconds;
            *nanosecondsShared = systemClockNano;

	    // Run child processes.
	    execl("./worker", "worker.c", NULL);

            printf("ERROR in oss.c: the execl() function has failed. Terminating program.\n\n");
            exit(-1);
         }

         // Work with parent process. Send a message to a running child process.
	 if (processID > 0 && realSeconds < 3) {
            
            // Always start new processes in the high-priority queue.
            queueLevel = 0; 
	
	    processDispatchedNext = processID;
	   
	    childrenActive++;
	    totalChildrenLaunched++;
	  
            // Updates real time for ceasing process generation.
	    gettimeofday(&realCurrentTime, NULL);
            realSeconds = realCurrentTime.tv_sec - realStartTime.tv_sec;
            realMicroseconds = realCurrentTime.tv_usec - realStartTime.tv_usec;


            // PCB operations. Adding a process to the Process Table and queue.
	    if (addToProcessTable(processID) == -1) {
               printf("ERROR in oss.c: Process Control Block (PCB) table is full.\n");
               printf("Cannot add PID %d\n", processID);
            }

	    printf("++OSS: Generating process with PID %d and putting it in queue 0 ", processID);
            printf("at time %d:%lld\n\n", systemClockSeconds, systemClockNano);
            fprintf(logOutputFP, "++OSS: Generating process with PID %d and putting it in queue 0 ", processID);
            fprintf(logOutputFP, "at time %d:%lld\n\n", systemClockSeconds, systemClockNano);

            enqueue(&queue[queueLevel], processID);
          
	    nextChild = findIndexInProcessTable(processDispatchedNext);
         }
      }

      bool queueEmpty = true;
     

      // While-loop determines which child needs to be scheduled.
      while (systemNanoOnly < nextLaunchTimeNano || totalChildrenLaunched == proc || childrenActive == simul) {
	 
	 // Print process table every half second of simulated system time. 
         long long int lastPrintoutTime = (lastTablePrintSeconds * oneBillionNanoseconds) + lastTablePrintNano;
            
	 if (systemNanoOnly - lastPrintoutTime >= halfBillionNanoseconds) {	    
            printAllFeedbackQueues(queue);
	    printProcessTable();

            lastTablePrintSeconds = systemClockSeconds;
            lastTablePrintNano = systemClockNano;
         }   
         
	 if (processTable[nextChild].occupied == 1 && processTable[nextChild].blocked == 0) {	    

            // A buffer stores information about what will be sent to a child.
            sendBuffer.messageType = processTable[nextChild].processID;
	       
	    if (queueEmpty == false) {
	       sendBuffer.messageType = processDispatchedNext;
            }
  
            
            // Store info to be sent to child for the correct process.
            if (isQueueEmpty(&queue[queueLevel]) == false) {
               processDispatchedNext = peekQueue(&queue[queueLevel]);
               sendBuffer.messageType = processDispatchedNext;

               nextChild = findIndexInProcessTable(processDispatchedNext);
            }


	    // 10, 20, or 40 ms time quantum sent to child.
	    sendBuffer.quantumData = determineTimeQuantum(queueLevel);
	    snprintf(sendBuffer.stringData, sizeof(sendBuffer.stringData), "Message sent to child %d again. Child is still running.", nextChild);


	    // Parent process sends a message to a child process. Output printed to a logfile.
	    sendMessageToUSER();
  

	    // Determine a random overhead time occurring after a process has just launched.
	    dispatchTime = determineDispatchTime();
   	    systemClockIncrement = incrementClock(&systemClockSeconds, &systemClockNano, dispatchTime);
       
            if (processDispatchedNext >= 0) {
	
               processDispatchedNext = sendBuffer.messageType;
            
               printf("OSS: Dispatching process with PID %ld from queue %d ", sendBuffer.messageType, queueLevel); 
	       printf("at time %d:%lld\n", systemClockSeconds, systemClockNano);
	       printf("OSS: Total time spent in dispatch was %d nanoseconds\n", dispatchTime);
	       fprintf(logOutputFP, "OSS: Dispatching process with PID %ld from queue %d ", sendBuffer.messageType, queueLevel);
	       fprintf(logOutputFP, "at time %d:%lld\n", systemClockSeconds, systemClockNano);
               fprintf(logOutputFP, "OSS: Total time spent in dispatch was %d nanoseconds\n", dispatchTime);
               fflush(logOutputFP);
  

	       // Slow down program to prevent race conditions between times in Process Table and those analyzed in user.c.
	       // Also prevents multiple empty Process Tables from printing towards the program's end.
	       int i; 
               for (i = 0; i < 5000000; i++) {
                  // Do nothing.
               }  
           

               // Another buffer stores info about what the parent receives from a child.
               receiveBuffer.messageType = sendBuffer.messageType;
	       receiveBuffer.quantumData = determineTimeQuantum(queueLevel);


	       // Parent process receives a message from a child process. Output printed to a logfile.
	       receiveMessageFromUSER(nextChild);
          

	       // Increment clock based on a child's scheduled time.
               int absoluteValueQuantData = abs(receiveBuffer.quantumData);
	       systemClockIncrement = incrementClock(&systemClockSeconds, &systemClockNano, absoluteValueQuantData);	   
	    
	       
	       // Prints duration of time that a process was scheduled.
	       printf("OSS: Receiving that process with PID %ld ran for %ld nanoseconds\n", sendBuffer.messageType, receiveBuffer.quantumData);
               fprintf(logOutputFP, "OSS: Receiving that process with PID %ld ran for %ld nanoseconds\n", receiveBuffer.messageType, receiveBuffer.quantumData);
  
	   
               // If user.c passes back a partial time quantum, send blocked process to BLOCKED queue.
               if (sendBuffer.quantumData != receiveBuffer.quantumData && receiveBuffer.quantumData > 0) {
                  printf("**OSS: Did not use its entire time quantum**\n");
                  fprintf(logOutputFP, "**OSS: Did not use its entire time quantum**\n");
                  
	          dequeue(&queue[queueLevel]);
	          slowDownProgram();
		  enqueue(&queue[3], receiveBuffer.messageType);

                  printf("OSS: Putting process with PID %ld into blocked queue.\n", receiveBuffer.messageType);
                  fprintf(logOutputFP, "OSS: Putting process with PID %ld into blocked queue.\n", receiveBuffer.messageType);

		  
	          processTable[nextChild].blocked = 1;

	          // Add accumulating schedule time info to the process table.
                  childWithServiceTime = findIndexInProcessTable(sendBuffer.messageType);
                  addServiceTimeToProcessTable(childWithServiceTime);
	       
		  // Determine when to unblock the process.
                  long long int unblockTime = determineEventWaitTime(maxWaitTimeSeconds, maxWaitTimeMS, systemNanoOnly);	   
                  addWaitTimeToProcessTable(unblockTime, nextChild);

		  // Find child to schedule next.
	          processDispatchedNext = peekQueue(&queue[queueLevel]);
                  nextChild = findIndexInProcessTable(processDispatchedNext);

	          if (processDispatchedNext < 0) {
                     break;
                  }

	          continue;
               }


	       // If the user process sends back a negative number for a time quantum, end child process.
               if (sendBuffer.quantumData != receiveBuffer.quantumData && receiveBuffer.quantumData < 0) {
                  pid_t pid;

		  printf("**OSS: Did not use its entire time quantum**\n");
                  fprintf(logOutputFP, "**OSS: Did not use its entire time quantum**\n");

                  removeFromProcessTable(sendBuffer.messageType);

                  printf("---OSS: User #%d PID %ld is planning to terminate.---\n\n", nextChild, sendBuffer.messageType);
                  fprintf(logOutputFP, "---OSS: User #%d PID %ld is planning to terminate.---\n\n", nextChild, sendBuffer.messageType);

		  // Delete process from whatever queue it was in before terminating.
  	          if (isQueueEmpty(&queue[queueLevel]) == false) {
	             printAllFeedbackQueues(queue);
	             dequeue(&queue[queueLevel]);

		     printAllFeedbackQueues(queue);
                  }

                  plannedTerminations++;
             
                  // Find child to schedule next.
	          processDispatchedNext = peekQueue(&queue[queueLevel]);
                  nextChild = findIndexInProcessTable(processDispatchedNext);

                  // If there are no running processes, break loop so that program can eventually generate more.
	          if (processDispatchedNext < 0) {
	             break;
	          }
	   
                  continue;
               }
         
	       // Remove process from current queue.
	       if (isQueueEmpty(&queue[queueLevel]) == false) {
	          dequeue(&queue[queueLevel]);  
	       }
	       
	       // Move process to a lower priority queue.   
               if (queueLevel < 2) {
                  enqueue(&queue[++queueLevel], processDispatchedNext);
                  printAllFeedbackQueues(queue);
               }

	       // After process runs in low priority queue, place in the back of it.
	       else if (queueLevel == 2) {
		  slowDownProgram();
                  enqueue(&queue[queueLevel], processDispatchedNext);	 
               }

	       // Add accumulating schedule time info to the process table.
	       childWithServiceTime = findIndexInProcessTable(sendBuffer.messageType);
	       addServiceTimeToProcessTable(childWithServiceTime);
	    }
	 } 
	    
         if (iterationBeforeBreak == nextChild - 1) {
               iterationBeforeBreak = 0;
         }
   
	 if (totalChildrenLaunched == plannedTerminations) {
            proc = plannedTerminations;
         }
	 
         // If no more children are running and the maximum # of total children have been launched, end loop/program.
         if (childrenActive == 0 && totalChildrenLaunched == proc) {
            processesFinished = true;

            break;
         }


	    // If the limit of simultaneous children has been reached, but more still need to be launched, wait for them to terminate.
         if (childrenActive == simul && totalChildrenLaunched < proc) {
            int status;
            pid_t pid;
           
            while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
               removeFromProcessTable(pid);
	       childrenActive--;
	       
	       nextLaunchTimeNano = determineNextLaunchNanoseconds(maxTimeBetweenNewProcsNS, systemNanoOnly);

               break;
	    }
	 }


         // If all available children have launched, but not all of them finished, wait for them to terminate.
         if (childrenActive > 0 && totalChildrenLaunched == proc) {
            int status;
            pid_t pid;

            while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
               removeFromProcessTable(pid);
	       childrenActive--;
            }
         }

         	 
	 // Break if it is time to launch a new process.
         if (systemNanoOnly >= nextLaunchTimeNano && totalChildrenLaunched < proc) {
            break;
         }

	 // Break if a process needs to terminate.
         if (plannedTerminations == totalChildrenLaunched) {
            break;
         }
         
         // Break if it is time to terminate the program.
         if (childrenActive == 0 && totalChildrenLaunched == proc) {
	    queueEmpty = true;    
            break;
	 }
      } 
   }
   printAllFeedbackQueues(queue);
   printProcessTable();
   
   fclose(logOutputFP);
   periodicallyTerminateProgram(EXIT_SUCCESS);

   return EXIT_SUCCESS;
}


// Create a logfile for process output.
void initializeLogfile() {
   logOutputFP = fopen(logfile, "w");

   if (logOutputFP == NULL) {
      printf("ERROR in oss.c: cannot create a log file named '%s'", logfile);

      exit(-1);
   }
}


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

// Remove process from back of queue.
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
long long int determineNextLaunchNanoseconds (long long int maxTimeBetweenProcesses, long long int currentNanoTime) {
   long long int waitTime = (rand() % (maxTimeBetweenProcesses + 1));

   return waitTime + currentNanoTime;
}

      
// Generates a random dispatch time between 1000 and 10000 nanoseconds (for overhead).
int determineDispatchTime () {
   int maxNanoseconds = 10000;
   int minNanoseconds = 1000;
   int timeSpentInDispatch = rand() % (maxNanoseconds - minNanoseconds + 1) + 1000;

   return timeSpentInDispatch;
}


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
	 processTable[i].serviceTimeSeconds = 0;
	 processTable[i].serviceTimeNanoseconds = 0;
	 processTable[i].eventWaitSeconds = 0;
	 processTable[i].eventWaitNanoseconds = 0;
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

void addServiceTimeToProcessTable(int i) {
   processTable[i].serviceTimeNanoseconds += receiveBuffer.quantumData;

   if (processTable[i].serviceTimeNanoseconds >= oneBillionNanoseconds) {	   
      processTable[i].serviceTimeSeconds++;
      processTable[i].serviceTimeNanoseconds -= oneBillionNanoseconds;
   }
}


void addWaitTimeToProcessTable(long long int waitTime, int i) {
   processTable[i].eventWaitNanoseconds = waitTime;

   while (processTable[i].eventWaitNanoseconds >= oneBillionNanoseconds) {
      processTable[i].eventWaitSeconds++;
      processTable[i].eventWaitNanoseconds -= oneBillionNanoseconds;
   }
}

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
	    dequeuedChild = dequeue(&queue[3]);
      	    
	    if (dequeuedChild == processTable[i].processID) {
	       break;
	    }
	    enqueue(&queue[3], dequeuedChild);
	 }
	 enqueue(&queue[0], processTable[i].processID);
      }  
   }
}   

void removeFromProcessTable(pid_t pid) {
   int i;
   for (i = 0; i < 20; i++) {
      if (processTable[i].processID == pid) {
         processTable[i].occupied = 0;
         processTable[i].processID = 0;
         processTable[i].startSeconds = 0;
         processTable[i].startNanoseconds = 0;
         processTable[i].serviceTimeSeconds = 0;
         processTable[i].serviceTimeNanoseconds = 0;
         processTable[i].eventWaitSeconds = 0;
         processTable[i].eventWaitNanoseconds = 0;
         processTable[i].blocked = 0;

         break;
      }
   }
}


// Output goes to console.
void printProcessTable() {
   printf("\nOSS PID: %d  SysClockS: %d  SysClockNano: %lld\n", getpid(), systemClockSeconds, systemClockNano);
   printf("Process Table:\n");
   printf("Entry\t Occupied\t PID\t\t StartS\t StartN\t\t ServiceS\t ServiceN\t EventWaitS\t EventWaitN\t Blocked\n");

   int i;

  
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
      
      // Prints columns 6 and 7 (ServiceS, ServiceN).
      printf(" %d\t\t %ld\t", processTable[i].serviceTimeSeconds, processTable[i].serviceTimeNanoseconds);
      if (processTable[i].serviceTimeNanoseconds < 1000000) {
         printf("\t");
      }

      // Prints columns 8 and 9 (EventWaitS, EventWaitN).
      printf(" %d\t\t %lld\t", processTable[i].eventWaitSeconds, processTable[i].eventWaitNanoseconds);
      if (processTable[i].eventWaitNanoseconds < 1000000) {
         printf("\t");
      }

      // Prints column 10 (Blocked--the final column).
      printf(" %d\n", processTable[i].blocked);
   }

   printf("\n");

   printProcessTableToLogfile();
}
void printProcessTableToLogfile() {
   fprintf(logOutputFP, "OSS: Outputting process table:\n");
   fprintf(logOutputFP, "\nOSS PID: %d  SysClockS: %d  SysClockNano: %lld\n", getpid(), systemClockSeconds, systemClockNano);
   fprintf(logOutputFP, "Process Table:\n");
   fprintf(logOutputFP, "Entry\t Occupied\t PID\t\t StartS\t StartN\t\t ServiceS\t ServiceN\t EventWaitS\t EventWaitN\t Blocked\n");

   int i;


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

      // Prints columns 6 and 7 (ServiceS, ServiceN).
      fprintf(logOutputFP, " %d\t\t %ld\t", processTable[i].serviceTimeSeconds, processTable[i].serviceTimeNanoseconds);
      if (processTable[i].serviceTimeNanoseconds < 1000000) {
         fprintf(logOutputFP, "\t");
      }

      // Prints columns 8 and 9 (EventWaitS, EventWaitN).
      fprintf(logOutputFP, " %d\t\t %lld\t", processTable[i].eventWaitSeconds, processTable[i].eventWaitNanoseconds);
      if (processTable[i].eventWaitNanoseconds < 1000000) {
         fprintf(logOutputFP, "\t");
      }

      // Prints column 10 (Blocked--the final column).
      fprintf(logOutputFP, " %d\n", processTable[i].blocked);
   }

   fprintf(logOutputFP, "\n");
}


void sendMessageToUSER() {
   if (msgsnd(messageQueueID, &sendBuffer, sizeof(messageBuffer) - sizeof(long int), 0) == -1) {
      printf("ERROR in oss.c: Problem with msgsnd() function.\n");
      printf("Cannot send message to user.c.\n\n");

      periodicallyTerminateProgram(-1);
   }
}


void receiveMessageFromUSER(int i) {
   if (msgrcv(messageQueueID, &receiveBuffer, sizeof(messageBuffer), processTable[i].processID, 0) == -1) {
      printf("ERROR in oss.c: Problem with msgrcv() function.\n");
      printf("Cannot receive message from user.c.\n\n");

      periodicallyTerminateProgram(-1);
   }
}


// Displays a help message if user enters './oss -h'.
void printHelpMessage() {
   printf("\n\n\nThis program displays information about child and parent processes, including:\n");
   printf("\t1.) A Process Control Block (PCB) table with child process entry information.\n");
   printf("\t2.) Messages on the console and a logfile related to process scheduling.\n\n");


   printf("\n\nTo execute this program, type './oss', then type in any combination of options:\n\n\n");
   printf("Option:                       What to enter after option:               Default values (if argu-      Description:\n");
   printf("                                                                         ment is not entered):\n\n");
   printf("  -h                           > nothing.                                 > (not applicable)           > Displays this help menu.\n"); 
   printf("  -f [logfile]                 > a file's basename.                       > defaults to 'logfile'      > Stores output relating to parent and\n");
   printf("                                                                                                          child processes.\n\n\n"); 

   printf("For example, typing './oss -f 'storage' will run the program:\n");
   printf("\t1.) while storing message statuses inside a file called 'storage.txt'.\n\n\n");

   exit(0);
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
   printf("Now terminating all child processes...\n\n");

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
   printf("\nChild process termination complete.\n");

   
   // Shared memory operations.
   printf("Now freeing shared memory...\n");
   detachAndClearSharedMemory();
   printf("\nShared memory detachment and deletion complete.\n");
   

   // Queue removal operations.
   printf("Now deleting the message queue...\n");
   removeMessageQueue();
   printf("\nMessage queue removal and deletion complete.\n");
 

   // Graceful termination.
   printf("Now exiting program...\n\n");

   exit(0);
}



