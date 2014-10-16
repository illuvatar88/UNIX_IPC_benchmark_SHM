/*
This file holds code for benchmarking latencies and throughput for a prototype of 'n' processes
performing performing IPC writes across cores on the same scif node via the scif RMA/RMM. The 
sending processes write data to the appropriate offset on the registered memory on the receiver 
daemon. Each process measures its transfer latency and throughput. 
*/

using namespace std;
#include <iostream>
#include <omp.h>
#include <stdlib.h>
#include <sched.h>
#include <errno.h>
#include <unistd.h>
#include "timer.cpp"
#include <string.h>
#include <cstdio>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/shm.h>

#define RECV_BUF_SIZE_MB 2;  //size of receive buffer for each proc in MB

int proc_set_affinity(int proc_num, pid_t pid);
void proc_get_affinity(int proc_num, pid_t pid, int NUM_PROCS);
void check_errno(int err_no);

int main (int argc, char* argv[])
{
  pid_t pid;
  int pagesize = sysconf(_SC_PAGESIZE);
  char file_path[40] = "/home/prabhashankar/work/";  //default file path
  if (argc < 4) {
    cerr<<"Format ./lauch_benchmark.out <NUM_PROCS> <msg_size> <iterations> <file_path(optional)>\n";
    return 1;
  }
  int NUM_PROCS = atoi(argv[1]);
  size_t msg_size = atoi(argv[2]);
  int iter = atoi(argv[3]);
  if (argc == 6) {
	  strcpy(file_path, argv[4]);
  }
  int shared_buf_size_MB = RECV_BUF_SIZE_MB;
  int shared_buf_size_bytes = shared_buf_size_MB * 1024 * 1024;
  int shared_space_bytes = shared_buf_size_MB * 1024 * 1024 * NUM_PROCS;
  //fn returns number of SCIF nodes in nw "NUM_NODES", nodes numbers in "nodes" and calling node id in "self"
  int NO_VALS = msg_size / sizeof(double);  //Number of double precision values being written
  //creating a shared memory segment
  int shared_segment_id;
  if ((shared_segment_id = shmget(IPC_PRIVATE, shared_space_bytes, IPC_CREAT | IPC_EXCL | 0666)) < 0) {
    cerr<<"Error during shmget\n";
    check_errno(errno);
    return 1;
  }
  //array holding stop indicators corresponding to each sending/receiving process
  //the stop indicators are set by each process once it has completed sending/receving
  int *stop_array = (int*)mmap(NULL, NUM_PROCS*sizeof(int), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0);
  //shared variable to be set when all the sending processes have completed sending
  int volatile *stop = (int*)mmap(NULL, sizeof(int), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0);
  //variable proc_num holds the process number of the forked process
  int proc_num = 0;
  //loop to fork NUM_PROCS number of processes
  for (; proc_num <= NUM_PROCS - 1 ; proc_num++) {
    pid = fork();
    if (pid == 0) {
      break; //each forked child breaks off from the loop
    }
    else if (pid == -1) {
      cerr<<"Fork failed\n";
      return 1;
    }
  }
  int TOT_NUM_PROCS = sysconf(_SC_NPROCESSORS_CONF);
  //fn to set affinity for each process
  if (proc_set_affinity((proc_num * 4 + 1) % TOT_NUM_PROCS, pid)  == -1)  //Setting affinity
    return 1;
  //fn to check affinity set
  proc_get_affinity(proc_num, pid, NUM_PROCS);
  //Attaching the shared segment to the proc's address space
  double *shared_space;
  if ((shared_space = (double*)shmat(shared_segment_id, 0, 0)) < 0) {
    cerr<<"Error during shmat for proc: "<<proc_num<<endl;
    return 1;
  }
  //"proc_num"s < NUM_PROCS are used as senders
  if (proc_num < NUM_PROCS) {
    double begin_time_init = timerval();  //initialization start time of transfer registered
    double *src_buf;  //buffer to hold data to be sent
    int ret = posix_memalign((void**) & src_buf, pagesize, NO_VALS*sizeof(double));
    if (ret) {
      cerr <<"Proc :"<<proc_num<<" could not allocate src_buf\n";
      return 1;
    }
    double end_time_init = timerval(); //Register end of initialization
    //initializing source buffer
    for(int i=0 ; i<NO_VALS ; i++) {
      src_buf[i] = i + proc_num;
    }
    double begin_time_tx = timerval(); //Register start of transfer time
    //if sending process then perform transfer "iter" number of times. 
    for (int i = 1 ; i <= iter ; i++) {
      memcpy(shared_space + proc_num * shared_buf_size_bytes / sizeof(double), src_buf, NO_VALS * sizeof(double));
    }
    double end_time_tx = timerval(); //Register end of transfer
    stop_array[proc_num] = 1;  //Setting the stop indicator corresponding to the process
    int stop_sum = 0;
    for (int i = 0 ; i < NUM_PROCS ; i++) {
      stop_sum += stop_array[i];
    }
    //if stop indicators of all procs are set then increment stop indicators in local and remote daemons
    if (stop_sum == NUM_PROCS) {
      (*stop)++;
    }
    //Calculating latency in us
    double latency_tx=(end_time_tx - begin_time_tx) * 1000000 / iter;
    double latency_init=(end_time_init - begin_time_init) * 1000000;
    //Calculating throughput in MBps
    double throughput=double(NO_VALS * sizeof(double) * 1000000) / ((latency_tx) * double(1024 * 1024));
    cout<<"Proc :"<<proc_num<<" Initialization Latency, Transfer Latency, Throughput : "<<latency_init<<" us, "<<latency_tx<<" us, "<<throughput<<" MBps"<<endl;
    if (pid != 0) {
      for (proc_num = 0 ; proc_num <= NUM_PROCS-1 ; proc_num++) {
        wait(NULL);
      }
    }
    free(src_buf);
    shmdt(shared_space);
    return 0;
  }
  //proc_num = NUM_PROCS is used as receiver daemon
  if (proc_num == NUM_PROCS) {
    *stop = 0;
    //loop for receiving data...only stops when local procs have completed
    while (*stop != 1);
    //Code block to check written data...un-comment block to print
/*
    for (int proc = 0 ; proc < NUM_PROCS ; proc++) {
      double *recv_buf = shared_space + proc * shared_buf_size_bytes / sizeof(double);
      cout<<" Proc "<<proc<<" Received vals :";
      for (int j = 0 ; j < NO_VALS ; j++) {
        cout<<*(recv_buf + j)<<" ";
      }
      cout<<endl;
    }
*/
    shmdt(shared_space);
  }
  munmap(stop_array, sizeof(int) * NUM_PROCS);
  munmap((void*)stop, sizeof(int));
  shmctl(shared_segment_id, IPC_RMID, 0);
  return 0;
}

void check_errno(int err_no) {
  switch (err_no) {
    case EBADF: cerr<<"EBADF"<<endl;break;
    case ECONNRESET: cerr<<"ECONNRESET"<<endl;break;
    case EFAULT: cerr<<"EFAULT"<<endl;break;
    case ENXIO: cerr<<"ENXIO"<<endl;break;
    case EINVAL: cerr<<"EINVAL"<<endl;break;
    case ENODEV: cerr<<"ENODEV"<<endl;break;
    case ENOTCONN: cerr<<"ENOTCONN"<<endl;break;
    case ENOTTY: cerr<<"ENOTTY"<<endl;break;
    case ECONNREFUSED: cerr<<"ECONNREFUSED"<<endl;break;
    case EINTR: cerr<<"EINTR"<<endl;break;
    case EISCONN: cerr<<"EISCONN"<<endl;break;
    case ENOBUFS: cerr<<"ENOBUFS"<<endl;break;
    case ENOSPC: cerr<<"ENOSPC"<<endl;break;
    case EOPNOTSUPP: cerr<<"EOPNOTSUPP"<<endl;break;
    case ENOMEM: cerr<<"ENOMEM"<<endl;break;
    case EACCES: cerr<<"EACCES"<<endl;break;
    case EADDRINUSE: cerr<<"EADDRINUSE"<<endl;break;
    case EAGAIN: cerr<<"EAGAIN"<<endl;break;
    case ENOENT: cerr<<"ENOENT"<<endl;break;
    case ENFILE: cerr<<"ENFILE"<<endl;break;
    case EPERM: cerr<<"EPERM"<<endl;break;
    default: cerr<<"errno not resolved\n";
  }
}

int proc_set_affinity(int proc_num, pid_t pid) {
  cpu_set_t mask;
  CPU_ZERO(&mask);
  CPU_SET(proc_num, &mask);
  errno=0;
  if(sched_setaffinity(pid, sizeof(mask), &mask) == -1) {
    cerr<<"Setting affinity failed for proc :"<<proc_num<<endl;
    switch(errno) {
      case ESRCH: cerr<<"ESRCH\n";break;
      case EFAULT: cerr<<"EFAULT\n";break;
      case EINVAL: cerr<<"EINVAL\n";break;
      default: cerr<<"Errno not resolved\n";
    }
    return -1;
  }
  return 0;
}

void proc_get_affinity(int proc_num, pid_t pid, int NUM_PROCS)
{
  cpu_set_t mycpuid;
  sched_getaffinity(pid, sizeof(mycpuid), &mycpuid);
  int TOT_NUM_PROCS = sysconf(_SC_NPROCESSORS_CONF);
  for (int cpu_num = 1 ; cpu_num <= TOT_NUM_PROCS ; cpu_num++) {
    if (CPU_ISSET(cpu_num, &mycpuid)) {
      cout<<"Affinity set for proc "<<proc_num<<" :"<<cpu_num<<endl;
    }
  }
}
