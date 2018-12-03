# RT-scheduler-RM-PS
Userspace, real-time POSIX scheduler demo, with rate monotonic strategy and polling server for asyncronous task support.

The polling server is capable to pause long task without changing task priorities, by swapping the execution context with (ancient) POSIX calls makecontext, getcontext, setcontext and swapcontext.

