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
   
   // Default time that process spends in dispatch. New values randomly generated.
   int dispatchTime = 1000;
   int maxWaitTimeSeconds = 5;
   int maxWaitTimeMS = 1000;

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

     // printf("while (processesFinished == false\n");
 
     // printf("if (childrenActive < simul && totalChildrenLaunched < proc)\n");
      //printf("childrenActive: %d\t simul: %d\t totalChildrenLaunched: %d\t proc: %d\n", childrenActive, simul, totalChildrenLaunched, proc);
      // If children are still available to launch simultaneously.
      if (childrenActive < simul && totalChildrenLaunched < proc) {
        
// 	  printf("BEFORE while (1)\n");
    
	  pid_t processID;
           

         // Keeps incrementing system clock until a process is ready to launch.
         while (1) {
           gettimeofday(&realCurrentTime, NULL);        
	  
	   // Determine when program should stop launching processes.
           realSeconds = realCurrentTime.tv_sec - realStartTime.tv_sec;
           realMicroseconds = realCurrentTime.tv_usec - realStartTime.tv_usec;	   
	

	   if (/*realSeconds >= 3 || */totalChildrenLaunched >= 100) {
	      break;
	   }
	   
           long long int lastTablePrintout = (lastTablePrintSeconds * oneBillionNanoseconds) + lastTablePrintNano;

	   if (realMicroseconds < 0) {
              realSeconds--;
	      realMicroseconds += 1000000;
	   }

           if (systemNanoOnly - lastTablePrintout >= halfBillionNanoseconds) { 		   
	      printProcessTable();

	      lastTablePrintSeconds = systemClockSeconds;
	      lastTablePrintNano = systemClockNano;
//	      printProcessTable();
           }

	   // If no processes are ready, increment the clock 1 ms.
	   incrementClock(&systemClockSeconds, &systemClockNano, systemClockIncrement);
 //          printf("from while(1) --> systemClockSeconds: %d\t systemClockNano: %lld\n", systemClockSeconds, systemClockNano); 
  
  

           // System time in shared memory constantly updates in loop.
           *secondsShared = systemClockSeconds;
           *nanosecondsShared = systemClockNano;

	   currentLaunchTimeNano = nextLaunchTimeNano;


	   printf("systemNanoOnly: %lld\t nextLaunchTimeNano: %lld\t\t totalChildrenLaunched: %d\t childrenActive: %d\n", systemNanoOnly, nextLaunchTimeNano, totalChildrenLaunched, childrenActive);
           
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

	    nextChild = findIndexInProcessTable(processDispatchedNext);
         }
      }
     

      // While-loop determines which child needs to be scheduled.
      while (systemNanoOnly < nextLaunchTimeNano || totalChildrenLaunched == proc || childrenActive == simul) {
	 
	 // Print process table every half second of simulated system time. 
         long long int lastPrintoutTime = (lastTablePrintSeconds * oneBillionNanoseconds) + lastTablePrintNano;
            
	 if (systemClockNano == 0 || systemClockNano == halfBillionNanoseconds/*(systemNanoOnly - lastPrintoutTime >= halfBillionNanoseconds)*/) {

   	    printProcessTable();

            lastTablePrintSeconds = systemClockSeconds;
            lastTablePrintNano = systemClockNano;
         }   
         
	 if (processTable[nextChild].occupied == 1 && processTable[nextChild].blocked == 0) {	    

            // A buffer stores information about what will be sent to a child.
            sendBuffer.messageType = processTable[nextChild].processID;
	   
	    sendBuffer.messageType = processDispatchedNext;
            

	    // 10, 20, or 40 ms time quantum sent to child.
	    sendBuffer.quantumData = 40 * oneMillionNanoseconds;
	    snprintf(sendBuffer.stringData, sizeof(sendBuffer.stringData), "Message sent to child %d again. Child is still running.", nextChild);


	    // Parent process sends a message to a child process. Output printed to a logfile.
	    sendMessageToUSER();
  

	    // Determine a random overhead time occurring after a process has just launched.
	    dispatchTime = determineDispatchTime();
   //   systemClockIncrement = incrementClock(&systemClockSeconds, &systemClockNano, dispatchTime);
       
            if (processDispatchedNext >= 0) {
	
               processDispatchedNext = sendBuffer.messageType;
            
               printf("OSS: Dispatching process with PID %ld from queue 0 ", sendBuffer.messageType); 
	       printf("at time %d:%lld\n", systemClockSeconds, systemClockNano);
	 //    printf("OSS: Total time spent in dispatch was %d nanoseconds\n", dispatchTime);
	       fprintf(logOutputFP, "OSS: Dispatching process with PID %ld from queue 0 ", sendBuffer.messageType);
	       fprintf(logOutputFP, "at time %d:%lld\n", systemClockSeconds, systemClockNano);
     //        fprintf(logOutputFP, "OSS: Total time spent in dispatch was %d nanoseconds\n", dispatchTime);
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
	       printf("YEAHH, increment that clock\n");
	       incrementClock(&systemClockSeconds, &systemClockNano, systemClockIncrement);
	       
	       
	       // Prints duration of time that a process was scheduled.
	       printf("OSS: Receiving that process with PID %ld ran for %ld nanoseconds\n", sendBuffer.messageType, receiveBuffer.quantumData);
               fprintf(logOutputFP, "OSS: Receiving that process with PID %ld ran for %ld nanoseconds\n", receiveBuffer.messageType, receiveBuffer.quantumData);
  
	   
               // If user.c passes back a partial time quantum, send blocked process to BLOCKED queue.
               if (sendBuffer.quantumData != receiveBuffer.quantumData && receiveBuffer.quantumData > 0) {
                  printf("**OSS: Did not use its entire time quantum**\n");
                  fprintf(logOutputFP, "**OSS: Did not use its entire time quantum**\n");
                  
                  printf("OSS: Putting process with PID %ld into blocked queue.\n", receiveBuffer.messageType);
                  fprintf(logOutputFP, "OSS: Putting process with PID %ld into blocked queue.\n", receiveBuffer.messageType);

		  
	          processTable[nextChild].blocked = 1;
                  printProcessTable();
	          // Add accumulating schedule time info to the process table.
                  childWithServiceTime = findIndexInProcessTable(sendBuffer.messageType);
                  addServiceTimeToProcessTable(childWithServiceTime);
	       
		  // Determine when to unblock the process.
                  long long int unblockTime = determineEventWaitTime(maxWaitTimeSeconds, maxWaitTimeMS, systemNanoOnly);	   
                  addWaitTimeToProcessTable(unblockTime, nextChild);

		  // Find child to schedule next.
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

//		  printf("\n\n\ntotalChildrenLaunched: %d\t plannedTerminations: %d\n", totalChildrenLaunched, plannedTerminations);
		
                  plannedTerminations++;
                  printf("\n\n\ntotalChildrenLaunched: %d\t plannedTerminations: %d\n", totalChildrenLaunched, plannedTerminations);


                  // Find child to schedule next.
                  nextChild = findIndexInProcessTable(processDispatchedNext);

                  // If there are no running processes, break loop so that program can eventually generate more.
	          if (processDispatchedNext < 0) {
	             break;
	          }
	   
                  continue;
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
          //printf("systemClockSeconds: %d\t systemClockNano: %lld\n", systemClockSeconds, systemClockNano);

 //         proc = plannedTerminations;
            //printf("processDispatchedNext: %d\n", processDispatchedNext);  
	    
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
          
	    //printf("totalChildrenLaunched: %d\t plannedTerminations: %d\n", totalChildrenLaunched, plannedTerminations);

            while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
               printf("pid: %d\n", pid);
	       printf("waitpid()\n");
   	       removeFromProcessTable(pid);
	       childrenActive--;
	       
	      // nextLaunchTimeNano = determineNextLaunchNanoseconds(intervalInMSToLaunchChildren, systemNanoOnly);
  //              printf("childrenActive: %d\n", childrenActive);

               
	    }
	   // printf("realSeconds: %ld\n", realSeconds);
	    break;
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
            break;
	 }
      } 
   }
   //printAllFeedbackQueues(queue);
   printProcessTable();
   
   fclose(logOutputFP);
   periodicallyTerminateProgram(EXIT_SUCCESS);

   return EXIT_SUCCESS;
}
