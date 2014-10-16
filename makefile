CChost = icc
CCmic = icc -mmic
SENDER = launch_benchmark.out
SCRIPT = unix_IPC_shm.cpp
HOST = 0
MIC0 = 1
MIC1 = 2
MIC2 = 3
MIC3 = 4

# Update the following default parameter values as desired

# name (not scif_id) of mic to run benchmark
MIC ?= mic0

# number of iterations
ITER ?= 10000

# number of processes to be forked
NUM_PROCS ?= 1

# msg_size to be used when running run_host or run_mic
MSG_SIZE ?= 1024

# max size to be used when running getvals_host or getvals_mic
MAX_MSG_SIZE ?= 1048576

# comlete path of the folder where files are copied
FILE_PATH ?= /home/prabhashankar/work/

host:
	$(CChost) -o $(SENDER) $(SCRIPT)	
mic:
	$(CCmic) -o $(SENDER) $(SCRIPT)
run_host: clean host
	$(FILE_PATH)/launch_benchmark.out $(NUM_PROCS) $(MSG_SIZE) $(ITER) $(FILE_PATH)
run_mic: clean mic
	ssh $(MIC) $(FILE_PATH)/launch_benchmark.out $(NUM_PROCS) $(MSG_SIZE) $(ITER) $(FILE_PATH)
clean:
	rm -f *o *.out
getvals_host: clean host
	sh $(FILE_PATH)/get_vals.sh $(NUM_PROCS) $(MAX_MSG_SIZE) $(ITER) $(FILE_PATH)
getvals_mic: clean mic
	ssh $(MIC) sh $(FILE_PATH)/get_vals.sh $(NUM_PROCS) $(MAX_MSG_SIZE) $(ITER) $(FILE_PATH)