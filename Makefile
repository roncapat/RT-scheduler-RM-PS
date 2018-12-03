CCFLAGS = -W -Wall -Wextra
LDFLAGS = -lrt -lpthread

all: sched demo

sched: rate_monotonic_with_polling_server.cpp context_swapping.cpp 
	g++ rate_monotonic_with_polling_server.cpp context_swapping.cpp $(CCFLAGS) $(LDFLAGS) -o sched_rm_ps

demo: context_swapping__demo.cpp context_swapping.cpp
	g++ context_swapping__demo.cpp context_swapping.cpp $(CCFLAGS) $(LDFLAGS) -o swap_demo

clean:
	rm -f sched_rm_ps swap_demo
