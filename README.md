# OS5: Resource Management Simulator
This program simulates an operating system by launching multiple child processes by incorporating elements from the first three OS projects. Those elements include Process Control Block (PCB) tables, simulated system clocks, message queues, and scheduling algorithms.

Expanding on those projects, this OS simulation incorporates the concept of **resource management** to child processes. This simulated operating system works with **5 *resource types***, which include **10 *instances*** of each resource type. Communication between parent (OSS) and child (worker) occurs as the child process randomly chooses one of *three* actions:
1. `REQUEST` - A child **requests** one of the 5 resource types.  
    - If a resource request is *granted*, then the child will acquire that resource and hold onto it.
    - If a resource request is *denied*, then the child will not acquire the resource. The child process will also become blocked and placed into a wait queue.
2. `RELEASE` - A child **releases** *one* resource. It will then become available for another child process to acquire.
3. `TERMINATE_PROCESS` - A child releases *all* of its resources, then **terminates gracefully**.

The operating system keeps track of how many resources from each type are acquired by each child process. Meanwhile, **Round-Robin scheduling** determines the order in which children can choose from the three possible actions described above.

Operation and flow of the system are regulated by two algorithms: **deadlock detection** and **deadlock recovery**. When two or more child processes request resources that are not currently available, they may wait indefinitely to access them. This situation causes a ***deadlock***, which prevents the affected processes from running. As a result, the operating system's functionality and throughput become compromised. To resolve this issue, these two algorithms come into play:
  - **Deadlock detection** determines whether the operating system is in a deadlocked state. It also selects the processes that are responsible for the deadlock.
  - **Deadlock recovery** terminates the processes selected by the deadlock detection algorithm. All the resources acquired by those processes are then released back into the system.


## Key Features:
- Uses shared memory to simulate a system clock that tracks time in seconds and nanoseconds.
- Uses a message queue to communicate between OSS and its children.
- Implements several vectors/matrices to support the capability of processes to **acquire or release resources**.
- Uses **Round-Robin scheduling** to determine which child can interact with resources next.
- Implements `fork()` and `waitpid()` for process creation and termination.
- Uses a **Process Control Block (PCB)** table and a **resource table** to record metadata, such as:
  - Process ID (PID).
  - Process start time.
  - Indication of which resource types are **requested** and **acquired** by a child.
  - Indication of whether a child is **blocked** (or unable to run temporarily).
  
## Program Code Operations:
### Simulated System Clock:
Throughout the duration of the program, the **simulated system clock** behaves based on two scenarios:
1.   If *no processes are present* in the system, then the system clock increments at a fixed rate of **1,000,000 nanoseconds** (1 ms).
2.   If *processes are present* in the system, then the system clock increments after a child acquires/releases a resource type or terminates. Incrementation occurs at a random value between ***0*** and ***B***, where ***B* = 50,000,000 nanoseconds** (50 ms).

### Message Queue:
The two files, *oss.c* and *worker.c*, both utilize two different message queues, `sendBuffer` and `receiveBuffer`. They both operate under a struct `messageBuffer` that contains three members:
  - `processID`- stores the child process's **Process ID (PID)**.
  - `resourceType`- stores a number (0 through 4) that represents one of 5 resource types, one of which a child wants to acquire or release.
  - `selection` - stores an integer indicating whether a child wants to:
    -  `0` - request a resource.
    -  `1` - release the resource.
    -  `2` - terminate gracefully. 

### Data Structures for Resource Management:
#### i.) Resource Vector:
The **resource vector** is a one-dimensional structure with *5 elements*. Each element is used to store the ***total number of resources*** in the operating system respective to each resource type. The vector serves as a *constant*, meaning the values never change. In other words, it is *always* the case that:

$$
\text{resourceVector} 
= \text{\\{10, 10, 10, 10, 10\\}}
$$

which means that resource types `R0`, `R1`, `R2`, `R3`, and `R4` all have 10 resources somewhere in the system.

#### ii.) Allocation Matrix:
The **allocation matrix** is a two-dimensional structure with *20 rows* (representing a process ID) and *5 columns* (representing a resource type). Each element in the matrix stores the ***number of resources for each type that each child currently holds***. For instance, it can be said that:
  - Process `P0` holds 2 instances of resource `R1` and 1 instance of resource `R3`.
  - Process `P2` holds 4 instances of `R2`.
  - Process `P3` holds 2 instances of `R0` and 7 instances of `R1`.
    
Then, the allocation matrix stores this information in this manner:

$$
\text{allocationMatrix} = 
\begin{Bmatrix}
& \text{R0} & \text{R1} & \text{R2} & \text{R3} & \text{R4}\\
\text{P0} & 0 & 2 & 0 & 1 & 0\\
\text{P1} & 0 & 0 & 0 & 0 & 0\\
\text{P2} & 0 & 0 & 4 & 0 & 0\\
\text{P3} & 2 & 7 & 0 & 0 & 0\\
\text{P4} & .. & .. & .. & .. & ..\\
\text{P5} & .. & .. & .. & .. & ..
\end{Bmatrix}
$$
#### iii.) Allocation Vector:
The **allocation vector** is another one-dimensional structure that also contains *5 elements*. Each element is used to store the ***number of unused resources*** in the operating system for each resource type. In other words, this vector stores the quantity of resources that child processes can still acquire. The quantities for each element are calculated by using this equation: 

$$
\text{allocationVector}\_{element} 
= \text{requestVector}\_{element} - \sum_{i=0}^{n} \text{allocationMatrix}\_{column}
$$


where, for each element of the **allocation vector**, the corresponding element of the **request vector** is subtracted by the *sum* of the corresponding column in the **allocation matrix** above in **ii.) Allocation Matrix**. 

$$
\text{allocationVector} 
= \text{\\{10, 10, 10, 10, 10\\}} - \text{\\{2, 2 + 7, 4, 1, 0\\}}
$$
$$
\text{allocationVector} 
= \text{\\{8, 1, 6, 9, 10\\}}
$$

After this calculation, the **allocation vector** shows that Resource `R0` has 8 instances available, Resource `R1` has 1 instance remaining, and so on.

#### iv.) Request Matrix:
### Deadlock Algorithms:
#### i.)Deadlock Detection:
#### ii.) Deadlock Recovery:


## How to Compile and Run:

1.) To *compile* the program, type:
```bash
make
```
2.) To *run* the program, type:
```bash
./oss -n <process_count> -s <max_simultaneous_process_count> -t <child_time_limit_seconds> -i <child_launch_interval_in_MS> -f <logfile_name>
```
### Command-Line Options:

| Option | Argument Format              | Required (yes/no)     | Description                                                                                         |
|--------|------------------------------|-----------------------|-----------------------------------------------------------------------------------------------------|
|  `-h`  | *(none)*                     | No                    | Displays help menu, then terminates program.                                                        |
|  `-n`  | an integer between 1 and 10. | No (default=1)        | Sets a total number of processes to run.                                                            |
|  `-s`  | an integer <= value of `-n`. | No (default=1)        | Sets a maximum number of processes to run simultaneously.                                           |
|  `-t`  | an integer >= 1.             | No (default=1)        | Sets a maximum number of seconds each process runs. Actual value is random between 1 and `-t` value.|
|  `-i`  | an integer >= 1.             | No (default=500)      | Sets an interval between child process launches (in *milliseconds*).                                |
|  `-f`  | a string.                    | No (default="logfile")| Sets a basename for a .txt file, which stores logging information about child processes.            |

## Example Output:

### Example 1: Console and Log File Output

Upon typing this command into the terminal:
```bash
./oss -n 3 -s 2 -t 5 -i 200 -f INFO
```
This program runs:
- 3 processes **total**.
- 2 processes **simultaneously**.
- Each process for a random time between 1 and 5 **seconds**, with 5 as the **maximum** time.
- Each new process launch **incremented** to a minimum of every 200 ms.
- A file called **INFO.txt** stores information about each child's **(CHANGE DESCRIPTION)**.
  
#### i.) Program Initialization:

#### ii.) Process Runtime 

#### iii.) Process Blocking:

 #### iv.) Process Unblocking:

#### v.) Process Termination:

#### vi.) Program Termination:

### Example 2: Process Control Block (PCB) Table Output
Every half second of simulated system time, a Process Table prints to the console and log file as shown below:

```bash


...
```
A full Process Table contains 20 rows, one for each child process slot. 

#### Column Definitions:
- **Entry** - the index of the process that resides in the table.
- **Occupied** - is the process running? (**1** if *yes*; **0** if *no*)
- **PID** - the Process ID of a child inside a table row.
- **StartS** - start time of the process in *seconds*.
- **StartN** - start time of the process in *nanoseconds*.
- **Allocated** - number of resources acquired for each resource type.
- **Request** - for each resource type, is the resource being requested by the child? (**1** if *yes*; **0** if *no*)
- **Blocked** - is the process blocked? (**1** if *yes*; **0** if *no*)
  
### Example 3: Resource Table Output
Every 20 times a resource is requested to a child process, a Resource Table prints to the console and log file as shown below:

```bash


...
```
A full Resource Table contains 20 rows, one for each child process slot. 

## Skills Learned:
- 
- 
- 
-
- 

## Tested On:
- Ubuntu 20.04.6 (LTS)
- GCC 10.5.0
- Make 4.2.1

## License:
This project is licensed under the [MIT License](LICENSE).
