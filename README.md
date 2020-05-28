# MARKDOWN EXERCISE ANSWERS

## KERNEL TICK
For periodic operations, systems provide a hardware component called the programmable interrupt timer (PIT). The timer allows the operating system to schedule an interrupt with some fixed time, for example every 10 milliseconds. T As the timer hits at a fixed and known interval, every execution of the timer interrupt handler allows the kernel to know that, those 10 milliseconds have elapsed. Every hit of the timer is known as a tick.

## TICKLESS KERNEL
In tickless kernels there is no fixed timer tick. Instead, the kernel schedules the next timer tick in response to its next event. Perhaps a better name is a "dynamic tick" kernel. This allows the system to negate the tradeoffs of a fixed interval: High frequency when you need the granularity, low frequency when you don't. Moreover, if the system is idle, there is no reason to schedule a periodic timer tick at all. The system can go idle and thus improve battery life.

## CHANGING STACK SIZES
Normally a to high stack size leads to memory issues and a too low number may be too mush for more complex tasks and the sistem would crash.

## INSTRUCTIONS TO USE
**First screen:**\
A, B, C, D for button counter)\
Right Mouse Click to reset the counters\

**Second screen:**\
I for incrementing a variable "X" every second\
X for manually incrementing "X" every press\
V for manually incrementing "V" every press\
*The counters will restart every 15 seconds*

**Lastly:**\
To switch between tasks press E\
To quit press Q\

THAT IS ALL :)
