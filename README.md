# OS5: Resource Management Simulator
  (Intro goes here)
## Key Features:

## Program Code Operations:
### Simulated System Clock:

### Message Queue:
The two files, *oss.c* and *worker.c*, both utilize two different message queues, `sendBuffer` and `receiveBuffer`. They both operate under a struct that contains two members:
  - `messageType`- stores the child process's **Process ID (PID)**.
    - Both *oss.c* and *user.c* files use this information to determine which child process must receive a message and send it back.
  - `quantumData`- stores a runtime value in nanoseconds. However, the two files use these numbers slightly differently:
    - (talk about oss.c)
    - (talk about worker.c)
### Deadlock Detection Algorithm:


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
- **Allocated** - 
- **Request** - 
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
