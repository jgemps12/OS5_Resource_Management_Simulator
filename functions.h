#ifndef FUNCTIONS_H_
#define FUNCTIONS_H_


// For memory queue.
#define PERMISSIONS 0644        

// For multi-level feedback queue.
#define MAX_SIZE 1000
#define QUEUE_COUNT 5


#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>



/*********************************STRUCTS************************************/
// For process table operations.
struct PCB {
   int occupied;                            // Is the entry in the process table empty (0) or full (1)?
   pid_t processID;                         // Child's process ID.
   int startSeconds;                        // Time when a process FORKED (in seconds).
   long int startNanoseconds;               // Time when a process FORKED (in nanoseconds).
   int allocated[5];                        // How many of each resource is allocated to a process.
   int request[5];                          // Which resource is being requested (represented by an element containing 1).
   int blocked;                             // Is the process blocked (1) or unblocked (0)?
};
extern struct PCB processTable[20];


// Enumerates options that the child should do.
enum ResourceTask {
   REQUEST,
   RELEASE,
   TERMINATE_PROCESS
};

// For placing children into wait queues or termination queues.
typedef struct MultiLevelQueue {
   int processEntries[MAX_SIZE];
   int front;
   int rear;
} MultiLevelQueue;

// Holds message queue information.
typedef struct messageBuffer {
   long int processID;
   int resourceType;
   ResourceTask selection;
} messageBuffer;


// Variables for message buffer.
extern messageBuffer sendBuffer;
extern messageBuffer receiveBuffer;
extern int messageQueueID;
extern key_t key;




/****************************GLOBAL VARIABLES********************************/

// For log file operations.
extern char logfile[105];
extern char suffix[];
extern FILE *logOutputFP;
extern char *logfileFP;

// For shared memory operations.
extern int secondsShmid;
extern long int nanoShmid;
extern int logfileShmid;

// For system clock operations.
extern int systemClockSeconds;
extern long long int systemClockNano;
extern long long int systemNanoOnly;
extern long int systemClockIncrement;

// For system clock sharing between processes.
extern int *secondsShared;
extern long int *nanosecondsShared;

// For time conversions.
extern long int oneMillionNanoseconds;
extern long int halfBillionNanoseconds;
extern long int oneBillionNanoseconds;
extern long int oneQuarterSecond;
extern long int hundredMS;

// For feedback queue operations.
extern int currentChildIndex;

// For program statistics.
extern int totalRequestsGranted;
extern int requestsGrantedImmediately;
extern int requestsGrantedAfterWaiting;
extern int processesTerminatedByDeadlock;
extern int processesTerminatedGracefully;
extern double deadlockDetectionTermPercentage;
extern int deadlockDetectionAlgCount;



/*************************FUNCTION PROTOTYPES********************************/

//For initialization.
void initializeLogfile();
void initializeMessageQueue();
void initializeFeedbackQueue(MultiLevelQueue *);
void initializeMatrix(int []);

// For resource type queues when children have to wait for a resource to become available.
bool isQueueEmpty(MultiLevelQueue *);
void enqueue(MultiLevelQueue *, pid_t);
bool searchQueue(MultiLevelQueue *, int);
void removeFromQueue(MultiLevelQueue *, int);
void printAllResourceQueues(MultiLevelQueue *);
void printOneQueue(MultiLevelQueue *);

// For user input validation.
void checkForOptargEntryError(int, char []);
void checkForSimulExceedsProcError(int, int);

// For system clock/time operations.
long long int incrementClock(int *, long long int *, int);
long long int convertSystemTimeToNanosecondsOnly (int *, long long int *);
long long int determineNextLaunchNanoseconds(int, long long int);
long int determineBoundB(long int);
void slowDownProgram();

// For process table operations.
int addToProcessTable(pid_t);
int findIndexInProcessTable(pid_t);
void removeFromProcessTable(pid_t);
void printProcessTable();
void printProcessTableToLogfile();

// For matrix and vector operations.
void updateRequestMatrix(int, int, int *, ResourceTask);
void updateAllocationMatrix(int, int, int *, ResourceTask);
void updateAllocationVector(int, int *, ResourceTask);                                  // For REQUEST and RELEASE.
void updateAllocationVector(int, int *, int *, ResourceTask);                           // For TERMINATE_PROCESS.
void releaseOneResource(int *, int *, int *, MultiLevelQueue *);
void printResourceTable(int []);
void printResourceTableToLogfile(int []);
void printChildTerminationMessage(int *, int, long int);

// For message passing operations.
void sendMessageToUSER();
void receiveMessageFromUSER(int);

// For guiding the user.
void printHelpMessage();

// For terminating the program.
void printStatistics();
void detachAndClearSharedMemory();
void removeMessageQueue();
void periodicallyTerminateProgram(int);




#endif
