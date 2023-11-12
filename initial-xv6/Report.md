# Priority Based Scheduler in xv-6

There are two types of priorities - 
Static Priority (SP) and
Dynamic Priority (DP). 

DP is dependent on three variables - rTime, sTime and wTime.

```rTime``` - total time the process has been running since it was last scheduled.

```sTime``` - total time the process has spent sleeping (i.e., blocked and not using CPU time) since it was last scheduled.

```wTime``` - total time the process has spent in the ready queue waiting to be scheduled since creation.


A new system call called ```setpriority``` that takes in two arguments - ```pid``` and ```new_sp``` and sets the SP of the given process with the new value.


It was observed that ```rTime``` was most of the times equal to zero. The max it went was to one. This is because the scheduler is pre-emptive. Thus at every clock tick, the ```rTime``` is reset to zero.

If two processes have the same DP, then the one that was scheduled the least number of times is given higher priority.

The plots of the various variables with time are given below.

|                            |                             | 
|       :-----------:        |       :------------:        |
| ![SP plot](plot_sp.png)    | ![SP plot](plot_rbi.png)    | 
| ![SP plot](plot_rtime.png) | ![SP plot](plot_stime.png)  | 
| ![SP plot](plot_wtime.png) | ![SP plot](plot_numsch.png) | 


Due to the pre-emptive nature of the scheduler, ```DP``` loses its significance and is equal to ```SP``` most times.

The only time ```RBI``` is relevant is when the process is created, as the ```RBI``` takes its default value of 25.

All these plots are obtained by running the default schedulertest on a multi-core CPU.




# Cafe Sim

## Calculating average waiting time

Waiting time of a customer is defined as 

```
wTime = leaving time - arrival time - coffee prep time
```

by this definition, we get wTime = 1 if a customer served his coffee, and wTime = leaving time - arrival time

Average waiting time is calculated by finding the mean of the wait times of every customer.


If there were infinite baristas, then the wait time would only depend the tolerance time of the customer. As there would be a barista always to serve the customer his coffee.


## Coffee wastage

A coffee is wasted if the customer leaves without collecting his order as his tolerance time is up.

If a customer has already left the cafe, his order is not taken up by any barista to not waste the resources.





# Icecream Parlour

- Press Ctrl+X to terminate input.


## Minimising Incomplete Orders


One solution to minimise incomplete orders would be to reserve the toppings when the customer places an order. 

This ensures that the order is fully completed. However, a challenge would be to also pre-allocate the machine to service that  order.



## Ingredient Replenishment

If ingredients can be replenished from a nearby supplier as and when required, the scenario would simplify a lot.

All orders would be accepted and we would only need to check for the availability of a machine. 




## Unserviced orders

Again, we could reserve the ingredients and machine slots to reduce the number of unserviced orders. 

However, we would need a scheduling policy to allocate the order to the a machine such that the allocation is optimal. 




