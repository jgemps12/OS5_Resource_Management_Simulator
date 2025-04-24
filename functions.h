#ifndef FUNCTIONS_H_
#define FUNCTIONS_H_


// For memory queue.
#define PERMISSIONS 0644        

// For multi-level feedback queue.
#define MAX_SIZE 1000
#define QUEUE_COUNT 4


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

// For multilevel feedback queue.
typedef struct MultiLevelQueue {
   int processEntries[MAX_SIZE];
   int front;
   int rear;
} MultiLevelQueue;


// For process table operations.
struct PCB {
   int occupied;                            // Is the entry in the process table empty (0) or full (1)?
   pid_t processID;                         // Child's process ID.
   int startSeconds;                        // Time when a process FORKED (in seconds).
   long int startNanoseconds;               // Time when a process FORKED (in nanoseconds).
   int serviceTimeSeconds;                  // Total time when a process was SCHEDULED (in seconds).
   long int serviceTimeNanoseconds;         // Total time when a process was SCHEDULED (in nanoseconds).
   int eventWaitSeconds;                    // Time when a process becomes UNBLOCKED (in seconds).
   long long int eventWaitNanoseconds;      // Time when a process becomes UNBLOCKED (in nanoseconds).
   int blocked;                             // Is the process blocked (1) or unblocked (0)?
};
extern struct PCB processTable[20];


// For message queue operations.
typedef struct messageBuffer {
   long int messageType;
   char stringData[100];
   long int quantumData;
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

// For process table operations.
extern int lastTablePrintSeconds;
extern long int lastTablePrintNano;

// For time conversions.
extern long int oneMillionNanoseconds;
extern long int halfBillionNanoseconds;
extern long int oneBillionNanoseconds;
extern long int oneQuarterSecond;
extern long int hundredMS;

// For feedback queue operations.
extern int currentChildIndex;




/*************************FUNCTION PROTOTYPES********************************/

//For initialization.
void initializeLogfile();
void initializeMessageQueue();
void initializeFeedbackQueue(MultiLevelQueue *);

// For user input validation.
void checkForOptargEntryError(int, char []);
void checkForSimulExceedsProcError(int, int);

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




#endif
