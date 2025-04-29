/* Jesse Gempel
 * 4/15/2025
 * Professor Mark Hauschild
 * CMP SCI 4760-001
*/


// The oss.c file works with PARENT processes.
// It launches a specific number of user processes with user input gathered from the 'getopt()' switch statement. 
// This time, process launches will be scheduled by utilizing the Multilevel Feedback Queue scheduling method.


#include "functions.h"

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


// A process table holds information about each child process.
struct PCB processTable[20];


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

long int systemClockIncrement = oneMillionNanoseconds;


// For determining the child process order in which messages are to be sent.
int currentChildIndex = 0;



int main(int argc, char** argv) {
   int opt;
   strcpy(logfileFP, logfile);

   // If user does not input the arguments corresponding to variables below, assign default values.
   int proc = 1;
   int simul = 1;
   int intervalInMSToLaunchChildren = 500;

   // Determines a random time frame between process launches.
   long long int nextLaunchTimeNano = determineNextLaunchNanoseconds(intervalInMSToLaunchChildren, systemNanoOnly);
   long long int currentLaunchTimeNano = 0;
   
   // Copies the default log file name into shared memory.
   strcpy(logfileFP, "logfile.txt");

   // User option for entering a logfile name.
   char procName[] = "-n [proc]";
   char simulName[] = "-s [simul]";
   char intervalName[] = "-i [intervalInMSToLaunchChildren]";
   char logfileName[] = "-f [logfile]";

   
   while ((opt = getopt(argc, argv, "hn:s:i:f:")) != -1) {
      switch (opt) {
         case 'h':
            printHelpMessage();

	    break;

         case 'n':
            proc = atoi(optarg);
	    checkForOptargEntryError(proc, procName);

            break;

         case 's':
            simul = atoi(optarg);
            checkForOptargEntryError(simul, simulName);
	    checkForSimulExceedsProcError(simul, proc);

            break;

	 case 'i':
	    intervalInMSToLaunchChildren = atoi(optarg);
	    checkForOptargEntryError(intervalInMSToLaunchChildren, intervalName);

	    nextLaunchTimeNano = determineNextLaunchNanoseconds(intervalInMSToLaunchChildren, currentLaunchTimeNano);
            currentLaunchTimeNano = nextLaunchTimeNano;

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
   int currentChild = 0;
   int nextChild = 0;
   int blocked = 0;
   
 
   // Initializes shared memory segments.
   *secondsShared = 0;
   *nanosecondsShared = 0;

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


   // Resources allocated to each process.
   int allocationMatrix[100];
   initializeMatrix(allocationMatrix);

   // Resources requested by each process.
   int requestMatrix[100];
   initializeMatrix(requestMatrix);

   // # of TOTAL resources available for each resource type.
   int resourceVector[5] = {10, 10, 10, 10, 10};

   // # of resources available to allocate for each resource type.
   int allocationVector[5] = {10, 10, 10, 10, 10};
 

   while (processesFinished == false) {

     // printf("while (processesFinished == false\n");
 
     // printf("if (childrenActive < simul && totalChildrenLaunched < proc)\n");
      //printf("childrenActive: %d\t simul: %d\t totalChildrenLaunched: %d\t proc: %d\n", childrenActive, simul, totalChildrenLaunched, proc);
      // If children are still available to launch simultaneously.
      if (childrenActive < simul && totalChildrenLaunched < proc) {
         // printf("BEFORE while (1)\n");
    
	  pid_t processID;
           

         // Keeps incrementing system clock until a process is ready to launch.
         while (1) {
//           printf("After WHILE...\n");
           gettimeofday(&realCurrentTime, NULL);        
	  
	   // Determine when program should stop launching processes.
           realSeconds = realCurrentTime.tv_sec - realStartTime.tv_sec;
           realMicroseconds = realCurrentTime.tv_usec - realStartTime.tv_usec;	   
	

	   if (/*realSeconds >= 3 || */totalChildrenLaunched >= 100) {
	      break;
	   }
	   
	   if (realMicroseconds < 0) {
              realSeconds--;
	      realMicroseconds += 1000000;
	   }

           if (systemClockNano == 0 || systemClockNano == halfBillionNanoseconds) { 		   
	      printProcessTable();
	      printResourceTable(allocationMatrix);
           }

	   // If no processes are ready, increment the clock by 1 ms.
	   incrementClock(&systemClockSeconds, &systemClockNano, systemClockIncrement);


           // System time in shared memory constantly updates in loop.
           *secondsShared = systemClockSeconds;
           *nanosecondsShared = systemClockNano;

	   currentLaunchTimeNano = nextLaunchTimeNano;

           
	   // Launches a child based on [maxTimeBetweenNewProcsNS]. 
	   if (systemNanoOnly >= nextLaunchTimeNano) {
	      
	      printf("(IN IF STATEMENT): systemNanoOnly: %lld\t nextLaunchTimeNano: %lld\n", systemNanoOnly, nextLaunchTimeNano);

	      processID = fork();

              nextLaunchTimeNano = determineNextLaunchNanoseconds(intervalInMSToLaunchChildren, currentLaunchTimeNano);
	      	       
	       // Makes sure system time updates EXACTLY to when a child launches, without rounding to the next 100 ms.
	    // long int exactLaunchTime = currentLaunchTimeNano - systemNanoOnly;
	     //  incrementClock(&systemClockSeconds, &systemClockNano, systemClockIncrement);
              
	       break;
	    }   
         }

	 // Work with child process. 
         if (processID == 0 /* && realSeconds < 3*/) {
            *secondsShared = systemClockSeconds;
            *nanosecondsShared = systemClockNano;

	    // Run child processes.
	    execl("./worker", "worker.c", NULL);

            printf("ERROR in oss.c: the execl() function has failed. Terminating program.\n\n");
            exit(-1);
         }

         // Work with parent process. Send a message to a running child process.
	 if (processID > 0 /* && realSeconds < 3*/) {
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

	    printf("++OSS: Generating process with PID %d at time %d:%lld\n\n", processID, systemClockSeconds, systemClockNano);   
	    fprintf(logOutputFP, "++OSS: Generating process with PID %d at time %d:%lld\n\n", processID, systemClockSeconds, systemClockNano);
        

	    // If for-loop below already ran, reinitialize to prevent segmentation faults. 
	    //nextChild = 0;
            //receiveBuffer.processID = 0;
	   // receiveBuffer.resourceType = 0;
	    printf("receiveBuffer.resourceType: %d\n", receiveBuffer.resourceType);
	    printf("totalChildrenActive: %d\n", totalChildrenLaunched);
	    printProcessTable();
	 }
      }
     

      // For-loop acts as a Round-Robin scheduling mechanism.
      for (nextChild = 0; nextChild < childrenActive; nextChild++) {  
      	 processTable[currentChild].request[receiveBuffer.resourceType] = 0;
       
	 // Print process table every half second of simulated system time. 
	 if (systemClockNano == 0 || systemClockNano == halfBillionNanoseconds) {
   	    printProcessTable();
	    printResourceTable(allocationMatrix);
         }   
       
	 if (processTable[nextChild].occupied == 1 && processTable[nextChild].blocked == 0) {	    

	    // Slow down program to prevent race conditions between times in Process Table and those analyzed in user.c.
	    // Also prevents multiple empty Process Tables from printing towards the program's end.
	    int i; 
            for (i = 0; i < 20000000; i++) {
               // Do nothing.
            }  
           
            // Another buffer stores info about what the parent receives from a child.
            receiveBuffer.processID = processTable[nextChild].processID;
//            sendBuffer.selection = receiveBuffer.selection;
//	    sendBuffer.resourceType = receiveBuffer.resourceType;

	    // Parent process receives a message from a child process. Output printed to a logfile.
	    receiveMessageFromUSER(nextChild);
          

	    // Increment clock based on a child's scheduled time.
	    incrementClock(&systemClockSeconds, &systemClockNano, systemClockIncrement);
	          
	    currentChild = nextChild - 1;
	    if (nextChild == 0) {
	       currentChild = childrenActive - 1;
	    }
	  
	    printf("currentChild: %d\t nextChild: %d\t childrenActive: %d\n", currentChild, nextChild, childrenActive);	    
	    printf("receiveBuffer.processID: %ld\n", receiveBuffer.processID);
	    // Prints resource type that a process has requested.
	    if (receiveBuffer.selection == REQUEST && receiveBuffer.resourceType >= 0 && currentChild >= 0) {
//	       currentChild = findIndexInProcessTable(receiveBuffer.processID);


	       printf("OSS: Detected Process P%d (PID %ld) requesting R%d ", currentChild, receiveBuffer.processID, receiveBuffer.resourceType);
	       printf("at time %d:%lld\n",  systemClockSeconds, systemClockNano);
	       fprintf(logOutputFP, "OSS: Detected Process P%d (PID %ld) requesting R%d ", currentChild, receiveBuffer.processID, receiveBuffer.resourceType);
               fprintf(logOutputFP, "at time %d:%lld\n",  systemClockSeconds, systemClockNano);
	       
	       
	       updateRequestMatrix(currentChild, receiveBuffer.resourceType, requestMatrix);
	       updateAllocationMatrix(currentChild, receiveBuffer.resourceType, allocationMatrix);
               updateAllocationVector(receiveBuffer.resourceType, allocationVector);

	       int j;
	       printf("\n\nresourceVector: ");
	       for (j = 0; j < 5; j++) {
	          printf("%d ", resourceVector[j]);	
               }
	       printf("\nallocationVector: ");
               for (j = 0; j < 5; j++) {
                  printf("%d ", allocationVector[j]);
               }
               printf("\n");
	       printProcessTable();

	   
	   
	    }
	       /*
            // If user.c passes back a partial time quantum, send blocked process to BLOCKED queue.
            if (sendBuffer.quantumData != receiveBuffer.quantumData && receiveBuffer.quantumData > 0) {
               
	       processTable[nextChild].blocked = 1;
               printProcessTable();


	       continue;
	    }
*/

	    // If the user process sends back a negative number for a time quantum, end child process.
            if (receiveBuffer.selection == TERMINATE_PROGRAM && childrenActive > 0) {
               pid_t pid;

               removeFromProcessTable(sendBuffer.processID);

               printf("---OSS: Process P%d (PID %ld) terminated---\n\n", currentChild, receiveBuffer.processID);
               fprintf(logOutputFP, "---OSS: Process P%d (PID %ld) terminated.---\n\n", currentChild, receiveBuffer.processID);

               receiveBuffer.selection = REQUEST;

               childrenActive--;	       
               

	       if (totalChildrenLaunched != proc) {
	          continue;
	       }
	       else {
                  processesFinished = true;

	          break;
	       }
            }

 
	    //if (totalChildrenLaunched != proc && childrenActive > 0) {
	       // A buffer stores information about what will be sent to a child.
               sendBuffer.processID = processTable[nextChild].processID;
               sendBuffer.selection = receiveBuffer.selection;
               sendBuffer.resourceType = receiveBuffer.resourceType;


               // Parent process sends a message to a child process. Output printed to a logfile.
               sendMessageToUSER();

              // printf("OSS: Sending message to Worker #%d PID %ld at time %d:%lld\n", nextChild, sendBuffer.processID, systemClockSeconds, systemClockNano);
               //fprintf(logOutputFP, "OSS: Sending message to Worker #%d PID %ld at time %d:%lld\n", nextChild, sendBuffer.processID, systemClockSeconds, systemClockNano);
               fflush(logOutputFP);
         //   }

	 } 
	    
      
	 // If the limit of simultaneous children has been reached, but more still need to be launched, wait for them to terminate.
         if (childrenActive == simul && totalChildrenLaunched < proc) {
            int status;
            pid_t pid;
         
	   
            while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
   	       removeFromProcessTable(pid);
	       //childrenActive--;  
	    } 
	 }


         // If all available children have launched, but not all of them finished, wait for them to terminate.
         if (childrenActive > 0 && totalChildrenLaunched == proc) {
            int status;
            pid_t pid;

            while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
               removeFromProcessTable(pid);
	       //childrenActive--;
            }
         }

	 // If no more children are running and the maximum # of total children have been launched, end loop/program.
	 if (childrenActive == 0 && totalChildrenLaunched == proc) {
            int status;
	    pid_t pid;

            while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
               removeFromProcessTable(pid);
               //childrenActive--;
            }

	    
            processesFinished = true;

            break;
         }

	 // Keep restarting the loop until it needs to work with a new process.
	 if ((nextChild == childrenActive - 1) && systemNanoOnly < nextLaunchTimeNano) {
            nextChild = -1;
	 }
      } 
   }
   //printAllFeedbackQueues(queue);
   printProcessTable();
   printResourceTable(allocationMatrix);
   
   fclose(logOutputFP);
   periodicallyTerminateProgram(EXIT_SUCCESS);

   return EXIT_SUCCESS;
}
