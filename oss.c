// The oss.c file works with PARENT processes.
// It launches a specific number of user processes with user input gathered from the 'getopt()' switch statement. 
// This time, processes will acquire and release resources while occassionally undergoing deadlocks.
// Those deadlocks will be detected and recovered to ensure continuing operation of this system.


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
#define LONG_BUFFER_SIZE sizeof(long long int)

// Starting a memory segment for a log file.
#define SHMKEY3 42071
#define LOGFILE_BUFFER_SIZE 105

// Permissions for memory queue.
#define PERMISSIONS 0644

// Starting a multilevel feedback queue.
#define MAX_SIZE 1000
#define QUEUE_COUNT 5
#define RESOURCE_COUNT 5

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
int minChildIndex = 0;
int maxChildIndex = 0;

// Counter and average variables that keep track of statistics. These statistics will be printed at end of program.
int totalRequestsGranted = 0;
int requestsGrantedImmediately = 0;
int requestsGrantedAfterWaiting = 0;
int processesTerminatedByDeadlock = 0;
int processesTerminatedGracefully = 0;
double deadlockDetectionTermPercentage = 0.0;
int deadlockDetectionAlgCount = 0;


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
    bool deadlock = false;															// Did deadlock algorithm find a deadlock.
    int childrenActive = 0;                                          // # of children running simultaneously (not to be confused with 'proc').
    int totalChildrenLaunched = 0;                                   // # of children launched so far (not to be confused with 'simul').  
    int nextChild = 0;
    int blocked = 0;
    int numberOfBlockedChildren = 0;
    long int boundB = 50 * oneMillionNanoseconds;                     // Maximum # of nanoseconds between process requests/releases.	    
    int halfSecondTablePrintouts = 0;											 // Run deadlock algorithm after 2 printouts (or 1 sec).
    int lastPrintedRequests = 0;												    // Prevents duplicate resource table printouts.

    // Initializes shared memory segments.
    *secondsShared = 0;
    *nanosecondsShared = 0;
    
    // Initializes each queue for multilevel feedback scheduling.
    MultiLevelQueue resourceQueue[QUEUE_COUNT];
    int i;
    for (i = 0; i < QUEUE_COUNT; i++) {
        initializeFeedbackQueue(&resourceQueue[i]);
    }
    
    // Creates .txt file and message queue to store message update information from oss.c (this file).
    initializeLogfile();
    initializeMessageQueue();
    
    // Signal handler for terminating program after 60 real-life seconds.
    signal(SIGALRM, periodicallyTerminateProgram);
    signal(SIGINT, periodicallyTerminateProgram);
    signal(SIGTERM, periodicallyTerminateProgram);
    
    // **ALLOCATION MATRIX**  --> Resources allocated to each process.
    int allocationMatrix[100];
    initializeMatrix(allocationMatrix);
    
    // **REQUEST MATRIX**     --> Resources requested by each process.
    int requestMatrix[100];
    initializeMatrix(requestMatrix);
    
    // **RESOURCE VECTOR**    --> # of TOTAL resources available for each resource type.
    int resourceVector[5] = {10, 10, 10, 10, 10};
    
    // **ALLOCATION VECTOR**  --> # of resources available to allocate for each resource type.
    int allocationVector[5] = {10, 10, 10, 10, 10};
    
    while (processesFinished == false) {
        // If children are still available to launch simultaneously.
        if (childrenActive < simul && totalChildrenLaunched < proc) {
            pid_t processID;
             
            // Keeps incrementing system clock until a process is ready to launch.
            while (1) {
                totalRequestsGranted = requestsGrantedImmediately + requestsGrantedAfterWaiting;
                numberOfBlockedChildren = 0;
                          
                if (totalChildrenLaunched == proc) {
                    break;
                }
                
                // Prints process table every half second of simulated system time.
                long long int lastPrintoutTime = (lastTablePrintSeconds * oneBillionNanoseconds) + lastTablePrintNano;
                long long int actualPrintoutDifference = systemNanoOnly - lastPrintoutTime;
                
                if (actualPrintoutDifference >= halfBillionNanoseconds) {
                    printProcessTable();
                
                    lastTablePrintSeconds = systemClockSeconds;
                    lastTablePrintNano = systemClockNano;
                    halfSecondTablePrintouts++;
                }
                
                // Prints resource table every 20 granted requests).
                if (totalRequestsGranted > 0 && totalRequestsGranted % 20 == 0 && totalRequestsGranted != lastPrintedRequests) {
                    printResourceTable(allocationMatrix);
						
						  lastPrintedRequests = totalRequestsGranted;	
                }
                
                // Run deadlock detection algorithm after 1 second of simulated system time.
                if (halfSecondTablePrintouts == 2) {
                    deadlock = runDeadlockAlgorithm(requestMatrix, allocationMatrix, allocationVector, childrenActive, RESOURCE_COUNT, resourceQueue);
                     
                    if (deadlock == true) {
                        processesTerminatedByDeadlock++;
                        childrenActive--;
                        continue;
                    }		
                    
                    deadlockDetectionAlgCount++;
                    halfSecondTablePrintouts = 0;
                }
                
                // If no processes are ready, increment the clock by 1 ms.
                incrementClock(&systemClockSeconds, &systemClockNano, systemClockIncrement);
                
                // System time in shared memory constantly updates in loop.
                *secondsShared = systemClockSeconds;
                *nanosecondsShared = systemClockNano;
                
                currentLaunchTimeNano = nextLaunchTimeNano;
                
                // Launches a child based on [maxTimeBetweenNewProcsNS]. 
                if (systemNanoOnly >= nextLaunchTimeNano) {
                    processID = fork();
                    nextLaunchTimeNano = determineNextLaunchNanoseconds(intervalInMSToLaunchChildren, currentLaunchTimeNano);
                    
                    break;
                }   
            }
        
            // Work with and runs child processes. 
            if (processID == 0) {
                execl("./worker", "worker.c", NULL);
                
                printf("ERROR in oss.c: the execl() function has failed. Terminating program.\n\n");
                exit(-1);
            }
        
            // Work with parent process. Send a message to a running child process.
            if (processID > 0) {
                childrenActive++;
                totalChildrenLaunched++;
                 
                // PCB operations. Adding a process to the Process Table and queue.
                if (addToProcessTable(processID) == -1) {
                    printf("ERROR in oss.c: Process Control Block (PCB) table is full.\n");
                    printf("Cannot add PID %d\n", processID);
                } 

			       printEventMessage(GENERATE_PROCESS, processID, -1, -1, false);
			   }
        }
    
        // For-loop acts as a Round-Robin scheduling mechanism.  
        for (nextChild = minChildIndex; nextChild <= maxChildIndex; nextChild++) {
            minChildIndex = findMinimumLoopIndex();
            maxChildIndex = findMaximumLoopIndex();
            
            // printf("currentChild: %d\n", currentChild);
            totalRequestsGranted = requestsGrantedImmediately + requestsGrantedAfterWaiting;
            
            *secondsShared = systemClockSeconds;
            *nanosecondsShared = systemClockNano;
            
            
            // Print process table every half second of simulated system time. 
            long long int lastPrintoutTime = (lastTablePrintSeconds * oneBillionNanoseconds) + lastTablePrintNano;
            long long int actualPrintoutDifference = systemNanoOnly - lastPrintoutTime;
            
            if (actualPrintoutDifference >= halfBillionNanoseconds) {
                printProcessTable();
                
                lastTablePrintSeconds = systemClockSeconds;
                lastTablePrintNano = systemClockNano;
                halfSecondTablePrintouts++;
            }
           
				// Prints resource table every 20 granted requests).
            if (totalRequestsGranted > 0 && totalRequestsGranted % 20 == 0 && totalRequestsGranted != lastPrintedRequests) {
                printResourceTable(allocationMatrix);

                lastPrintedRequests = totalRequestsGranted;

            }


            // Run deadlock detection algorithm after second of simulated system time. 
            if (halfSecondTablePrintouts == 2) {
                deadlock = runDeadlockAlgorithm(requestMatrix, allocationMatrix, allocationVector, childrenActive, RESOURCE_COUNT, resourceQueue);
               
                if (deadlock == true) {
                    processesTerminatedByDeadlock++;
                    childrenActive--;     
                    continue;
                }
                
                deadlockDetectionAlgCount++;
                halfSecondTablePrintouts = 0;
            }
            
            // Increment based on 0 to B bound.
            incrementClock(&systemClockSeconds, &systemClockNano, determineBoundB(boundB));
            
            if (processTable[nextChild].blocked == 1) {
                continue;
            }
         
            if (processTable[nextChild].blocked == 1) {
                numberOfBlockedChildren++;
                
                if (numberOfBlockedChildren == childrenActive) {
                    numberOfBlockedChildren = 0;
                    break;
                }
            }
            
            if (processTable[nextChild].occupied == 0) {
                nextChild++;
            }
            
            if (processTable[nextChild].occupied == 1 && processTable[nextChild].blocked == 0) {	    
                // Slow down program to prevent race conditions between times in Process Table and those analyzed in user.c.
                // Also prevents multiple empty Process Tables from printing towards the program's end.
                int i; 
                for (i = 0; i < 100000000; i++) {
                    // Do nothing.
                }  
                
                // Parent process receives a message from a child process. Output printed to a logfile.
                receiveMessageFromUSER(nextChild);
            
                // Another buffer stores info about what the parent receives from a child.
                receiveBuffer.processID = processTable[nextChild].processID;
                
                // Prints resource type that a process has requested.
                if (receiveBuffer.selection == REQUEST && receiveBuffer.resourceType >= 0 && nextChild >= 0) {
                    updateRequestMatrix(nextChild, receiveBuffer.resourceType, requestMatrix, REQUEST);
                    
                    int vectorIndex = receiveBuffer.resourceType;
                    
                    // If the allocation vector shows that space is available for a resource type, grant it to a child.	       
                    if (allocationVector[vectorIndex] > 0) { 
                        printEventMessage(REQUEST_RESOURCE, receiveBuffer.processID, receiveBuffer.resourceType, nextChild, true);
                        processTable[nextChild].request[receiveBuffer.resourceType] = 0;
               
                        updateAllocationMatrix(nextChild, receiveBuffer.resourceType, allocationMatrix, REQUEST);
                        updateAllocationVector(receiveBuffer.resourceType, allocationVector, REQUEST);
                        requestsGrantedImmediately++;
                    }
                        
                    // If space is unavailable for a resource type according to allocation vector.
                    // Reject resource type, send child to wait queue, and make it sleep until it is finally available.
                    else {
                        printEventMessage(REQUEST_RESOURCE, receiveBuffer.processID, receiveBuffer.resourceType, nextChild, false);
                        processTable[nextChild].request[receiveBuffer.resourceType] = 1;
                        
                        // Only add process to wait queue if it is not already in it.
                        if (searchQueue(&resourceQueue[receiveBuffer.resourceType], receiveBuffer.processID) == false) {
                            enqueue(&resourceQueue[receiveBuffer.resourceType], receiveBuffer.processID);
                        }
                        
                        processTable[nextChild].request[receiveBuffer.resourceType] = 1;
                        processTable[nextChild].blocked = 1;
                    }
                }
                int matrixIndex = 5 * nextChild + receiveBuffer.resourceType;
                
                // If user.c passes back a partial time quantum, send blocked process to BLOCKED queue.
                if (receiveBuffer.selection == RELEASE && allocationMatrix[matrixIndex] > 0) {
						  printEventMessage(RELEASE_RESOURCE, receiveBuffer.processID, receiveBuffer.resourceType, nextChild, false);
                    updateRequestMatrix(nextChild, receiveBuffer.resourceType, requestMatrix, RELEASE);
                    updateAllocationMatrix(nextChild, receiveBuffer.resourceType, allocationMatrix, RELEASE);
                    updateAllocationVector(receiveBuffer.resourceType, allocationVector, RELEASE);
                }
                    
                // If the user process sends back a negative number for a time quantum, end child process.
                if (receiveBuffer.selection == TERMINATE_PROCESS) {     
                    int status, pid;
                    
                    updateRequestMatrix(nextChild, receiveBuffer.resourceType, requestMatrix, TERMINATE_PROCESS);
                    printChildTerminationMessage(allocationMatrix, nextChild, receiveBuffer.processID);
                    updateAllocationVector(nextChild, allocationMatrix, allocationVector, TERMINATE_PROCESS);
                    updateAllocationMatrix(nextChild, receiveBuffer.resourceType, allocationMatrix, TERMINATE_PROCESS);
                   
						  terminateChildren(GRACEFUL, receiveBuffer.processID, &childrenActive);
			
						  // If any children are blocked due to unavailable resource types prior to child termination, unblock them.
						  int p;
						  for (p = 0; p < PROCESS_COUNT; p++) {
						     if (processTable[p].occupied == 1 && processTable[p].blocked == 1) {
         				     if (canRequestBeFulfilled(requestMatrix, allocationMatrix, p, RESOURCE_COUNT) == true) {
            				     processTable[p].blocked = 0;
         					  }
      					  }
   					  }
               
                    receiveBuffer.selection = REQUEST;
                    sendBuffer.processID = processTable[nextChild].processID;
                    sendBuffer.selection = receiveBuffer.selection;
                    sendBuffer.resourceType = receiveBuffer.resourceType;
                   
                    // Remove process ID from all wait queues after it terminates.
                    for (i = 0; i < QUEUE_COUNT; i++) {
                        removeFromQueue(&resourceQueue[i], sendBuffer.processID);
                    }
                    
                    // If all processes terminated, end program. Otherwise, continue for-loop.
                    if (totalChildrenLaunched != proc) {
                        continue;
                    }
                    if (processesTerminatedGracefully + processesTerminatedByDeadlock == proc) {
                        processesFinished = true;
                        break;
                    }
                }
                fflush(logOutputFP);
            } 
            
            // Allow OSS to send messages.
            if (processTable[nextChild].blocked == 0 && processTable[nextChild].processID > 0) {
                sendBuffer.processID = processTable[nextChild].processID;
                sendBuffer.selection = receiveBuffer.selection;
                sendBuffer.resourceType = receiveBuffer.resourceType; 
                sendMessageToUSER();
            }
            
            // If no more children are running and the maximum # of total children have been launched, end loop/program.
            if (childrenActive == 0 && totalChildrenLaunched == proc) {	    
                processesFinished = true;
                break;
            }
            
            // Keep restarting the loop until it needs to work with a new process.
            if ((nextChild == maxChildIndex) && systemNanoOnly < nextLaunchTimeNano) {
                nextChild = minChildIndex - 1;
            }
        } 
    }

    printProcessTable();
    printResourceTable(allocationMatrix);
    
    fclose(logOutputFP);
    periodicallyTerminateProgram(EXIT_SUCCESS);
    
    return EXIT_SUCCESS;
}
