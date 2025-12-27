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

// Determine the index that the Round-Robin for-loop starts.
// Since processes periodically terminate, the starting index can change (i.e., it is not ALWAYS 0).
int findMinimumLoopIndex() {
    int i;
    for (i = 0; i < PROCESS_COUNT; i++) {
        if (processTable[i].occupied == 1) {
            return i;
        }
    }
    return 0;
}

// Determine the index at which the Round-Robin for-loop ends.
int findMaximumLoopIndex() {
    int i;
    for (i = PROCESS_COUNT - 1; i >= 0; i--) {
        if (processTable[i].occupied == 1) {
            return i;
    }
}

return 0;
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
    
    // Table column names.
    printf("%-9s %-12s %-10s %-10s %-14s %-20s %-20s %-5s\n", "Entry", "Occupied", "PID", "StartS", "StartN", "Allocated", "Request", "Blocked");
    
    int i, j;
    
    for (i = 0; i < 20; i++) {
        // Prints first 5 columns (Entry, Occupied, PID, StartS, StartN).
        printf("%-9d %-12d %-11d", i, processTable[i].occupied, processTable[i].processID);
        printf("%-10d %-15ld", processTable[i].startSeconds, processTable[i].startNanoseconds);
        
        // Prints column 6 (Allocated).
        for (j = 0; j < 5; j++) {
            printf("%-3d", processTable[i].allocated[j]);
        }
        printf("%-6s", " ");
        
        // Prints column 7 (Request).
        for (j = 0; j < 5; j++) {
            printf("%-3d", processTable[i].request[j]);
        }
        printf("%-6s", " ");
        
        // Prints column 8 (Blocked).
        printf("%-7d\n", processTable[i].blocked);
    }
    
    printProcessTableToLogfile();
}

void printProcessTableToLogfile() {
    fprintf(logOutputFP, "OSS: Outputting process table:\n");
    fprintf(logOutputFP, "\nOSS PID: %d  SysClockS: %d  SysClockNano: %lld\n", getpid(), systemClockSeconds, systemClockNano);
    fprintf(logOutputFP, "Process Table:\n");
    
    // Table column names.
    fprintf(logOutputFP, "%-9s %-12s %-10s %-10s %-14s %-15s %-15s %-5s\n", "Entry", "Occupied", "PID", "StartS", "StartN", "Allocated", "Request", "Blocked");
    
    int i, j;
    
    for (i = 0; i < 20; i++) {
        // Prints first 5 columns (Entry, Occupied, PID, StartS, StartN).
        fprintf(logOutputFP, "%-9d %-12d %-11d", i, processTable[i].occupied, processTable[i].processID);
        fprintf(logOutputFP, "%-10d %-15ld", processTable[i].startSeconds, processTable[i].startNanoseconds);
        
        // Prints column 6 (Allocated).
        for (j = 0; j < 5; j++) {
            fprintf(logOutputFP, "%-2d", processTable[i].allocated[j]);
        }
        fprintf(logOutputFP, "%-6s", " ");
        
        // Prints column 7 (Request).
        for (j = 0; j < 5; j++) {
            fprintf(logOutputFP, "%-2d", processTable[i].request[j]);
        }
        fprintf(logOutputFP, "%-6s", " ");
        
        // Prints column 8 (Blocked).
        fprintf(logOutputFP, "%-7d\n", processTable[i].blocked);
    }
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
    int element; 
    
    if (option == TERMINATE_PROCESS) {
        element = 5 * row;
        
        for (i = 0; i < 5; i++) { 	 
            allocationVector[i] += allocationMatrix[element];
            element++;
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
            
            printEventMessage(RELEASE_RESOURCE, processID, resource, child, false);
            
            updateRequestMatrix(child, resource, requestMatrix, RELEASE);
            updateAllocationMatrix(child, resource, allocationMatrix, RELEASE);
            updateAllocationVector(resource, allocationVector, RELEASE);
            processTable[child].blocked = 0;
            
            removeFromQueue(&queue[resource], processID);
            requestsGrantedAfterWaiting++;
            
            break;
        }
    }
}

void printResourceTable(int matrix[]) {
    printf("OSS: Outputting resource table:\n");
    printf("\nOSS PID: %d  SysClockS: %d  SysClockNano: %lld\n", getpid(), systemClockSeconds, systemClockNano);
    printf("Resource table:\n");
    printf("%11s %7s %7s %7s %7s\n", "R0", "R1", "R2", "R3", "R4");
    
    int i, j, k;
    
    for (i = 0; i < 20; i++) {
        printf("P%d ", i);
        
        for (j = 0; j < 5; j++) {
            k = (5 * i) + j;
            if (i < 10 || j >= 1) {
                printf("%7d ", matrix[k]);
            }
            else {
                printf("%6d ", matrix[k]);
            }
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
    fprintf(logOutputFP, "%11s %7s %7s %7s %7s\n", "R0", "R1", "R2", "R3", "R4");

    
    int i, j, k;
    
    for (i = 0; i < 20; i++) {
        fprintf(logOutputFP, "P%d ", i);
        
        for (j = 0; j < 5; j++) {
            k = (5 * i) + j;
            if (i < 10 || j >= 1) {
                fprintf(logOutputFP, "%7d ", matrix[k]);
            }
            else {
                fprintf(logOutputFP, "%6d ", matrix[k]);
            }
        }
        fprintf(logOutputFP, "\n");
    }
    fprintf(logOutputFP, "\n\n");
}

/******************************************DEADLOCK ALGORITHM OPERATIONS***********************************************/
bool runDeadlockAlgorithm(int *requestMatrix, int *allocationMatrix, int *allocationVector, int processes, int resources, MultiLevelQueue *queue) {
    bool finish[PROCESS_COUNT];
    int deadlockedPIDs[PROCESS_COUNT];
    
    int deadlockedCount = 0;
    int p;

    // Message tells user that deadlock detection and recovery will begin. 
    printEventMessage(BEGIN_DEADLOCK_ALGORITHM, -1, -1, -1, false);
   
    /***** PHASE 1: DEADLOCK DETECTION---> look for any deadlocked processes, but do not handle them yet. *****/
    if (detectDeadlock(requestMatrix, allocationMatrix, allocationVector, resources, finish) == false) {
        printf("\tProcesses deadlocked:\tNONE.\n\n");
        fprintf(logOutputFP, "\tProcesses deadlocked:\tNONE.\n\n");
   
        return false;
    }

    // Collect and list any processes that are deadlocked from the for-loop below.
    printf("\tProcesses deadlocked:\t");
    fprintf(logOutputFP, "\tProcesses deadlocked:\t"); 
    
    for (p = 0; p < PROCESS_COUNT; p++) {
        if (processTable[p].occupied == 1 && finish[p] == false) {
            printf("P%d\t", p);
            fprintf(logOutputFP, "P%d\t", p);
            
            deadlockedPIDs[deadlockedCount++] = p;
        }
    }    

    /***** PHASE 2: DEADLOCK RECOVERY---> kill the first deadlocked process discovered by the algorithm. *****/
    recoverDeadlock(requestMatrix, allocationMatrix, allocationVector, resources, deadlockedPIDs, queue);

    return true;
 
}

bool detectDeadlock(int *requestMatrix, int *allocationMatrix, int *allocationVector, int resources, bool *finish) {
    int work[resources];
    bool progress;
    int i, p;

    initializeWorkAndFinishVectors(work, finish, allocationVector, resources);
    simulateProcessFinish(requestMatrix, allocationMatrix, work, finish, resources);

    for (p = 0; p < PROCESS_COUNT; p++) {
        if (processTable[p].occupied == 1 and finish[p] == false) {
            return true;
        }
    }

    return false;
}

bool recoverDeadlock(int *requestMatrix, int *allocationMatrix, int *allocationVector, int resources, int *deadlockedPIDs,  MultiLevelQueue *queue) {
    int victimIndex = deadlockedPIDs[0];;
    int i;
    
    if (victimIndex == -1) {
        printf("ERROR in functions.c: Deadlock resolution invoked, but no blocked processes found");
        return false;
    }
    
    int victimPID = processTable[victimIndex].processID;
    int location = victimIndex * resources;
   
    // Log termination output to console and logfile.
    printEventMessage(DEADLOCK_TERMINATION, victimPID, -1, victimIndex, false);	
    
    // Kill victim process.
    terminateChildren(DEADLOCK, victimPID, NULL);
    
    // Release all resources held by the victim process.
    releaseResourcesFromTerminatedChildren(requestMatrix, allocationMatrix, allocationVector, resources, victimIndex, location);
    
    // Unblock all process so children can acquire resources again.
    for (i = 0; i < PROCESS_COUNT; i++) {
        processTable[i].blocked = 0;
    }
    
    removeFromQueue(queue, victimPID);
    removeFromProcessTable(victimPID);
    
    return true;
}

void initializeWorkAndFinishVectors(int *work, bool *finish, int *allocationVector, int resources) {
    int i, p;
    
    // Initialize 'work' vector to hold currently available resources (i.e., the allocation vector).
    for (i = 0; i < resources; i++) {
        work[i] = allocationVector[i];
    }
    
    // Initialize 'finish' vector. No existing processes can finish during vector initialization. 
    // finish[p] equals 'true' when a process does not exist for a specific PCB table index (denoted by 'p').
    for (p = 0; p < PROCESS_COUNT; p++) {
        if (processTable[p].occupied == 0) {
            finish[p] = true;
        }
        else if (processTable[p].blocked == 0) {
            finish[p] = true;
        }
        else {
            finish[p] = false;
        }
    }
}

void simulateProcessFinish(int *requestMatrix, int *allocationMatrix, int *work, bool *finish, int resources) {
    bool progress;
    int i, p;
    
    do {
        progress = false;
        
        for (p = 0; p < PROCESS_COUNT; p++) {
            // Keep free slots in the Process Table as marked 'finished'.
            if (finish[p] == true || processTable[p].occupied == 0) {
                continue;
            }
            
            if (processTable[p].blocked == 1) {
                int location = p * resources;
                bool hasActiveRequest = false;
        
                // A request matrix's element is ALWAYS '0' or '1'---'1' if a process is actively trying to request a resource.
                // If a work vector's element equals 0, that means the resource is not available.
                // Therefore, a process cannot finish if requestMatrix[location + i] == 1 and work[i] == 0.
                for (i = 0; i < resources; i++) {
                    if (requestMatrix[location + i] > 0) {
                        hasActiveRequest = true;
                        break;
                    }
                }

                // If process is blocked without any pending resource requests, then skip.
                if (hasActiveRequest == false) {
                    continue;
                }
            }
            
            // Determine whether a blocked process can finish with available resources.
            int location = p * resources;
            bool processCanFinish = true;

            for (i = 0; i < resources; i++) {
                if (requestMatrix[location + i] > work[i]) {
                    processCanFinish = false;
                    break;
                }
            }
                
            // Release any allocated resources.
            if (processCanFinish == true) {
                for (i = 0; i < resources; i++) {
                    work[i] += allocationMatrix[location + i];
                }

                finish[p] = true;
                progress = true;
            }
        }
    }
    while (progress == true);
}

// Determines whether a resource type request can be granted for a child.
bool canRequestBeFulfilled(int *requestMatrix, int *work, int processIndex, int resources) {
    int location = processIndex * resources;
    int i;
    
    for (i = 0; i < resources; i++) {
        if (requestMatrix[location + i] > work[i]) {
            return false;
        }
    }
    return true;
}

// Release resources so that non-blocked children can continue requesting them.
void releaseResourcesFromTerminatedChildren(int *requestMatrix, int *allocationMatrix, int *allocationVector, int resources, int victimIndex, int location) {
    int i, p;
    for (i = 0; i < resources; i++) {
        allocationVector[i] += allocationMatrix[location + i];
        allocationMatrix[location + i] = 0;
        requestMatrix[location + i] = 0;
    }
    
    processTable[victimIndex].occupied = 0;
}

/********************************************MESSAGE PASSING OPERATIONS************************************************/
// Perform msgsnd() operations.
void sendMessageToWORKER() {
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
void receiveMessageFromWORKER(int i) {
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

/************************************************OUTPUT OPERATIONS****************************************************/
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

void printEventMessage(int event, int pid, int resource, int childIndex, bool requestGranted) {
    if (event == GENERATE_PROCESS) {
        printf("++OSS: Generating process with PID %d at time %d:%lld\n\n", pid, systemClockSeconds, systemClockNano);
        fprintf(logOutputFP, "++OSS: Generating process with PID %d at time %d:%lld\n\n", pid, systemClockSeconds, systemClockNano);
    }
    else if (event == REQUEST_RESOURCE) {
        printf("OSS: Detected Process P%d (PID %d) REQUESTING R%d at time %d:%lld.\n", childIndex, pid, resource, systemClockSeconds, systemClockNano);
        fprintf(logOutputFP, "OSS: Detected Process P%d (PID %d) REQUESTING R%d at time %d:%lld.\n", childIndex, pid, resource, systemClockSeconds, systemClockNano);
        
        if (requestGranted == true) {
            printf("OSS: Granting P%d (PID %d)'s request for R%d at time %d:%lld.\n", childIndex, pid, resource, systemClockSeconds, systemClockNano);
            fprintf(logOutputFP, "OSS: Granting P%d (PID %d)'s request for R%d at time %d:%lld.\n", childIndex, pid, resource, systemClockSeconds, systemClockNano);
        }
        else {
            printf("OSS: No instances of R%d are available. ", resource);
            printf("P%d (PID %d) added to wait queue at time %d:%lld.\n", childIndex, pid, systemClockSeconds, systemClockNano);
            fprintf(logOutputFP, "OSS: No instances of R%d are available. ", resource);
            fprintf(logOutputFP, "P%d (PID %d) added to wait queue at time %d:%lld.\n", childIndex, pid, systemClockSeconds, systemClockNano);	
        }
    }
    else if (event == RELEASE_RESOURCE) {
        printf("OSS: Process P%d (PID %d) is RELEASING R%d at time %d:%lld.\n", childIndex, pid, resource, systemClockSeconds, systemClockNano);
        fprintf(logOutputFP, "OSS: Process P%d (PID %d) is RELEASING R%d at time %d:%lld.\n", childIndex, pid, resource, systemClockSeconds, systemClockNano);
    }
    else if (event == BEGIN_DEADLOCK_ALGORITHM) {
        printf("OSS: Running deadlock detection algorithm at time %d:%lld.\n", systemClockSeconds, systemClockNano);
        fprintf(logOutputFP, "OSS: Running deadlock detection algorithm at time %d:%lld.\n", systemClockSeconds, systemClockNano);
    }	
    else if (event == DEADLOCK_TERMINATION) {
        printf("\n\tOSS: terminating P%d (PID %d) to remove deadlock.\n", childIndex, pid);
        fprintf(logOutputFP, "\n\tOSS: terminating P%d (PID %d) to remove deadlock.\n", childIndex, pid);
    }
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
            printf("P%d: %-5d", i, allocationMatrix[j]);
            fprintf(logOutputFP, "P%d: %-5d", i, allocationMatrix[j]);
        }
        j++;
    }
    printf("\n");
    fprintf(logOutputFP, "\n");
}

/***********************************************PROGRAM TERMINATION***************************************************/
void terminateChildren (int termination, int pidToTerminate, int * childrenActive) {
    int pid, status;
    
    if (termination == GRACEFUL) {
        kill(pidToTerminate, SIGTERM);
        
        while (1) {
            pid_t pid = waitpid(pidToTerminate, &status, WNOHANG);
            
            if (pid == 0) {
                usleep(10000);
                continue;
            }
            else {
                removeFromProcessTable(pidToTerminate);
                processesTerminatedGracefully++;
                (*childrenActive)--;
                break;
            }
        }
    }
    else if (termination == DEADLOCK) {
        if (kill(pidToTerminate, SIGTERM) == -1) {
            printf("ERROR in OSS: kill() function failed.\n\n");
            exit(-1);
        }
        waitpid(pidToTerminate, &status, 0);
    }
    else if (termination == END_PROGRAM) {
        int i;
        for (i = 0; i < 20; i++) {
            if (processTable[i].occupied == 1) {
                pid_t pid = processTable[i].processID;
                
                if (pid > 0) {
                    kill(processTable[i].processID, SIGTERM);
                    printf("Signal SIGTERM was sent to PID %d\n", pid);
                }
            }
        }
    }
}

void printStatistics () {
    totalRequestsGranted = requestsGrantedImmediately + requestsGrantedAfterWaiting;
    deadlockDetectionTermPercentage = ((double)processesTerminatedByDeadlock / ((double)processesTerminatedByDeadlock + (double)processesTerminatedGracefully)) * 100;
    
    printf("********************PROGRAM SUMMARY********************\n\n");
    
    printf("Total granted requests: %28d\n", totalRequestsGranted);
    printf("Requests granted immediately: %22d\n", requestsGrantedImmediately);
    printf("Requests granted after waiting: %20d\n", requestsGrantedAfterWaiting);
    printf("Deadlock detection terminations: %19d\n", processesTerminatedByDeadlock);
    printf("Graceful terminations: %29d\n", processesTerminatedGracefully);
    printf("Deadlock termination percentage: %18.2f%%\n", deadlockDetectionTermPercentage);
    printf("# of deadlock detection operations: %16d\n\n", deadlockDetectionAlgCount);
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
    terminateChildren(END_PROGRAM, -1, NULL);
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
