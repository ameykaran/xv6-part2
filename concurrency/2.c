#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <time.h>

#define ANSI_CLEAR_SCREEN "\e[1;1H\e[2J"
#define ANSI_FG_COLOR_RED "\x1b[31m"
#define ANSI_FG_COLOR_GREEN "\x1b[32m"
#define ANSI_FG_COLOR_YELLOW "\x1b[33m"
#define ANSI_FG_COLOR_BLUE "\x1b[34m"
#define ANSI_FG_COLOR_MAGENTA "\x1b[35m"
#define ANSI_FG_COLOR_CYAN "\x1b[36m"
#define ANSI_FG_COLOR_WHITE "\x1b[37m"
#define ANSI_FG_COLOR_UNKNOWN "\x1b[38;5;224m"
#define ANSI_FG_COLOR_ORANGE "\x1b[38:5:208m"
#define ANSI_COLOR_RESET "\x1b[0m"

#define MAX_ORDERS 100
#define MAX_TOPPINGS 100
#define BUFF_SIZE 4096
#define LESS_THAN_SECOND ((int)(1e6 - 5 * 1e4))

/* Locks implemented at different items;
1. Customer - changing the num of orders fulfiilled
2. Topping - changing the quantity
3. Pending orders queue - getting the pending order.
4. Machine - changing the status of the machine
*/

int n, k, f, t;

typedef struct machine machine;

typedef struct order order;
typedef struct safe_int safe_int;
typedef enum order_status order_status;
typedef struct order_queue order_queue;
typedef struct customer customer;
typedef struct topping topping;
typedef struct flavour flavour;
typedef struct cust_node cust_node;
typedef struct cust_queue cust_queue;
typedef struct pending_order pending_order;
typedef struct pending_order_queue pending_order_queue;

struct safe_int
{
    int val;
    sem_t lock;
};
enum order_status
{
    ORDER_WAITING,
    ORDER_PREPARING,
    ORDER_SERVED,
    ORDER_REJECTED
};
struct order
{
    int id;
    int cust_oid;
    char flavour[100];
    int start_time;
    int prep_time;
    int order_time; // todo remove this; redundant
    order_status status;
    int taken_by_MID;
    struct order *next;
    customer *ordered_by;
    // sem_t lock;

    int num_toppings;
    char toppings[MAX_TOPPINGS][100];
};
struct order_queue
{
    order *head;
    order *tail;
    int size;
};
struct customer
{
    int id;
    int arrival_time;
    int num_ordered;
    safe_int num_fulfilled;
    char is_left;
    order_queue orders;
    int is_deleted;

    // int start_time[MAX_ORDERS];
    // int served_time[MAX_ORDERS];
    // int leave_time[MAX_ORDERS];
};
struct machine
{
    int id;
    int start_time, end_time, remaining_time;
    int num_orders_fulfilled;
    enum machine_status
    {
        MACHINE_IDLE, // Machine is free to use
        MACHINE_BUSY  // Machine is either in use or not working
    } status;
    sem_t lock;
    pthread_t *thread;
};
struct topping
{
    int id;
    char name[100];
    int quantity;
    sem_t lock;
};
struct flavour
{
    int id;
    char name[100];
    int prep_time;
};
struct cust_node
{
    customer *cust;
    struct cust_node *next;
    int is_deleted;
};
struct cust_queue
{
    cust_node *head;
    cust_node *tail;
    int size;
};

struct pending_order
{
    order *order;
    pending_order *next;
};

struct pending_order_queue
{
    pending_order *head;
    pending_order *tail;
    int size;
    sem_t lock;
};

void rtrim(char *word, char letter);
void commission_machine(int mid);
void decommission_machine(int mid);

#define PRINT(...)         \
    sem_wait(&print_lock); \
    printf(__VA_ARGS__);   \
    sem_post(&print_lock);

cust_queue customers;
machine *machines;
topping *toppings;
flavour *flavours;

cust_queue waiting_custs;
pending_order_queue pending_orders;
sem_t print_lock;
sem_t inventory_lock;

int ticks = -1;
pthread_mutex_t time_lock = PTHREAD_MUTEX_INITIALIZER;

void send_customers_into_parlour();
int order_preparable_ingredients(order *order, int reduce_bool);
machine *get_machine_by_id(int mid);
int get_flavour_time(char *flavour);
safe_int curr_machines = {.val = 0};
safe_int cust_turned_away_count = {.val = 0};
safe_int cust_fulfilled_count = {.val = 0};
safe_int cust_partial_count = {.val = 0};
// safe_int lowest_free_machine = {.val = 0};
int get_lowest_free_machine();

// Add a customer to the waiting line of the parlour
void add_cust(cust_queue *queue, customer *cust)
{
    if (cust == NULL)
        return;

    cust_node *new_node = (cust_node *)malloc(sizeof(cust_node));
    new_node->cust = cust;
    new_node->next = NULL;
    new_node->is_deleted = 0;

    if (queue->head == NULL)
    {
        queue->head = new_node;
        queue->tail = new_node;
    }
    else
    {
        queue->tail->next = new_node;
        queue->tail = new_node;
    }

    queue->size++;
}

void free_customer(customer *cust)
{
    order *curr = cust->orders.head;
    while (curr)
    {
        order *temp = curr;
        curr = curr->next;
        free(temp);
    }
    free(cust);
}

void remove_orders_by_customer(customer *cust);

// A customer leaves the parlour
void out_cust(customer *cust)
{
    if (cust == NULL)
        return;

    remove_orders_by_customer(cust);

    cust_queue *queue = &waiting_custs;
    cust_node *curr = queue->head, *prev = NULL;
    while (curr)
    {
        if (curr->cust == cust)
        {
            if (prev == NULL)
                queue->head = curr->next;
            else
                prev->next = curr->next;
            if (curr == queue->tail)
                queue->tail = prev;
            queue->size--;
            break;
        }
        prev = curr;
        curr = curr->next;
    }

    queue = &customers;
    curr = queue->head, prev = NULL;
    while (curr)
    {
        if (curr->cust == cust)
        {
            if (prev == NULL)
                queue->head = curr->next;
            else
                prev->next = curr->next;
            if (curr == queue->tail)
                queue->tail = prev;
            // free_customer(curr->cust);
            // free(curr);
            queue->size--;
            break;
        }
        prev = curr;
        curr = curr->next;
    }

    // sem_wait(&curr_capacity.lock);
    // curr_capacity.val--;
    // sem_post(&curr_capacity.lock);
}

// Add an order to the customer
void cust_add_order(order_queue *order_queue, order *order)
{
    if (order_queue->head == NULL)
    {
        order_queue->head = order;
        order_queue->tail = order;
    }
    else
    {
        order_queue->tail->next = order;
        order_queue->tail = order;
    }
    order_queue->size++;
}

void print_pending_orders()
{
    pending_order *curr_order = pending_orders.head;
    while (curr_order)
    {
        PRINT("Pending order: %d by cust %d\n", curr_order->order->cust_oid, curr_order->order->ordered_by->id);
        curr_order = curr_order->next;
    }
}
// Add the order to the queue of pending orders
void add_order_to_queue(order *order)
{
    pending_order *new_order = (pending_order *)malloc(sizeof(pending_order));
    new_order->order = order;
    new_order->next = NULL;

    if (pending_orders.head == NULL)
    {
        pending_orders.head = new_order;
        pending_orders.tail = new_order;
    }
    else
    {
        pending_orders.tail->next = new_order;
        pending_orders.tail = new_order;
    }

    pending_orders.size++;

#ifdef DEBUG
    print_pending_orders();
    PRINT("\n");
#endif
}

// Retrieve the first pending order, that can be prepared, by the lowest indexed machine, from the queue
order *pop_order_from_queue(int time_left, int mid)
{
    sem_wait(&pending_orders.lock);
#ifdef DEBUG
    printf("pop: ");
    print_pending_orders();
#endif
    pending_order *curr = pending_orders.head, *prev = NULL;
    while (curr)
    {
        if (curr->order->status == ORDER_WAITING && curr->order->prep_time <= time_left && mid == get_lowest_free_machine())
        {
            if (prev == NULL)
                pending_orders.head = curr->next;
            else
                prev->next = curr->next;
            if (curr == pending_orders.tail)
                pending_orders.tail = prev;
            pending_orders.size--;
            break;
        }
        prev = curr;
        curr = curr->next;
    }
    sem_post(&pending_orders.lock);
    if (curr)
        return curr->order;

    return NULL;
}

void remove_orders_by_customer(customer *cust)
{
    sem_wait(&pending_orders.lock);
    pending_order *curr = pending_orders.head, *prev = NULL;
    while (curr)
    {
        if (curr->order->ordered_by == cust)
        {
            if (prev == NULL)
                pending_orders.head = curr->next;
            else
                prev->next = curr->next;
            if (curr == pending_orders.tail)
                pending_orders.tail = prev;
            pending_orders.size--;

            curr->order->status = ORDER_REJECTED;
        }
        prev = curr;
        curr = curr->next;
    }
    sem_post(&pending_orders.lock);
}

// int cust_till_count = 0; // Number of customers arrived in the cafe until now.
// safe_int curr_capacity = {.val = 0};

safe_int curr_oid = {.val = 0}; // The current order id
// Get the next available order id
int get_next_oid()
{
    sem_wait(&curr_oid.lock);
    int val = curr_oid.val;
    curr_oid.val++;
    sem_post(&curr_oid.lock);
    return val;
}

void schedule_machines()
{
    for (int i = 0; i < n; i++)
    {
        if (machines[i].status == MACHINE_BUSY && machines[i].start_time == ticks)
            commission_machine(machines[i].id);

        if (machines[i].status == MACHINE_IDLE && machines[i].end_time == ticks)
            decommission_machine(machines[i].id);
    }
}

// runs in a separate thread
void *timer()
{
    while (1)
    {
        pthread_mutex_lock(&time_lock);
        ticks += 1;
        PRINT(ANSI_FG_COLOR_UNKNOWN "Time: %d\n" ANSI_COLOR_RESET, ticks);

        schedule_machines();
        send_customers_into_parlour();

        pthread_mutex_unlock(&time_lock);
        sleep(1);
    }
}

// runs in a separate thread
void *depart_customers()
{
    int local_ticks = 0;

    while (1)
    {
        sem_wait(&curr_machines.lock);
        if (curr_machines.val == 0 && waiting_custs.size == 0 && local_ticks > 0)
        {
            PRINT(ANSI_FG_COLOR_ORANGE "\n---Parlour is closed for the day---\n" ANSI_COLOR_RESET);
            sem_post(&curr_machines.lock);

            sem_wait(&cust_fulfilled_count.lock);
            sem_wait(&cust_partial_count.lock);
            sem_wait(&cust_turned_away_count.lock);

            PRINT("Total customers served: %d\n", cust_fulfilled_count.val);
            PRINT("Total customers turned away due to\n  ingredient shortage: %d\n  queue overflow: %d\n", cust_partial_count.val, cust_turned_away_count.val);

            sem_post(&cust_fulfilled_count.lock);
            sem_post(&cust_partial_count.lock);
            sem_post(&cust_turned_away_count.lock);

            exit(0);
        }
        else if (curr_machines.val == 0)
        {
            cust_node *cust = waiting_custs.head;
            while (cust)
            {
                sem_wait(&cust->cust->num_fulfilled.lock);
                if (cust->cust->num_fulfilled.val == cust->cust->num_ordered)
                {
                    PRINT(ANSI_FG_COLOR_GREEN "Customer %d has been served at %d second(s)\n" ANSI_COLOR_RESET, cust->cust->id, local_ticks);
                    sem_post(&cust->cust->num_fulfilled.lock);
                    out_cust(cust->cust);
                }
                else
                {
                    PRINT(ANSI_FG_COLOR_RED "Customer %d has left the parlour at %d second(s) due to no machine being available\n" ANSI_COLOR_RESET, cust->cust->id, local_ticks);
                    sem_wait(&cust_turned_away_count.lock);
                    cust_turned_away_count.val++;
                    sem_post(&cust_turned_away_count.lock);

                    sem_post(&cust->cust->num_fulfilled.lock);
                    out_cust(cust->cust);
                }

                cust = cust->next;
            }

            sem_post(&curr_machines.lock);
        }
        else
            sem_post(&curr_machines.lock);

        sleep(1);
        local_ticks++;
    }
}

void *machine_operations(void *arg)
{
    machine *mach = (machine *)arg;
    int mid = mach->id;
    int time_left = mach->remaining_time;

    if (time_left == 0)
        return NULL;

    int local_ticks = mach->start_time;

    while (time_left > 0)
    {
        order *curr_order = pop_order_from_queue(time_left, mach->id);
        usleep(mach->id * 10000);
        if (curr_order != NULL && curr_order->status == ORDER_WAITING)
        {
            if (order_preparable_ingredients(curr_order, 1) == 0)
            {
                PRINT(ANSI_FG_COLOR_RED "Order %d of customer %d rejected at %d second(s) due to ingredient shortage\n" ANSI_COLOR_RESET, curr_order->cust_oid, curr_order->ordered_by->id, local_ticks);

                curr_order->status = ORDER_REJECTED;
                PRINT(ANSI_FG_COLOR_RED "Customer %d has left the parlour due to order not being fulfilled\n" ANSI_COLOR_RESET, curr_order->ordered_by->id, local_ticks);
                sem_wait(&cust_partial_count.lock);
                cust_partial_count.val++;
                sem_post(&cust_partial_count.lock);

                out_cust(curr_order->ordered_by);
            }

            sem_wait(&mach->lock);
            mach->status = MACHINE_BUSY;
            sem_post(&mach->lock);
            curr_order->taken_by_MID = mid;
            curr_order->status = ORDER_PREPARING;
            curr_order->start_time = local_ticks;
            PRINT(ANSI_FG_COLOR_CYAN "Machine %d starts preparing the order %d of customer %d at %d second(s)\n" ANSI_COLOR_RESET,
                  mid + 1, curr_order->cust_oid, curr_order->ordered_by->id, local_ticks);

            sleep(curr_order->prep_time);

            // curr_order->start_time;
            local_ticks = curr_order->start_time + curr_order->prep_time;
            time_left -= curr_order->prep_time;

            PRINT(ANSI_FG_COLOR_BLUE "Machine %d completes preparing the order %d of customer %d at %d second(s)\n" ANSI_COLOR_RESET,
                  mid + 1, curr_order->cust_oid, curr_order->ordered_by->id, local_ticks);

            curr_order->status = ORDER_SERVED;
            sem_wait(&mach->lock);
            mach->status = MACHINE_IDLE;
            sem_post(&mach->lock);

            sem_wait(&curr_order->ordered_by->num_fulfilled.lock);

            curr_order->ordered_by->num_fulfilled.val++;
            if (curr_order->ordered_by->num_fulfilled.val == curr_order->ordered_by->num_ordered)
            {
                PRINT(ANSI_FG_COLOR_GREEN "Customer %d has collected their order(s) and left at %d second(s)\n" ANSI_COLOR_RESET, curr_order->ordered_by->id, local_ticks);
                sem_wait(&cust_fulfilled_count.lock);
                cust_fulfilled_count.val++;
                sem_post(&cust_fulfilled_count.lock);

                sem_post(&curr_order->ordered_by->num_fulfilled.lock);
                out_cust(curr_order->ordered_by);
            }
            sem_post(&curr_order->ordered_by->num_fulfilled.lock);

            mach->num_orders_fulfilled++;
            // sleep(1);
            // time_left -= 1;
            // local_ticks += 1;
        }
        // else
        // {
        sleep(1);
        time_left--;
        // }
        local_ticks++;
    }

    return NULL;
}

void print_customer_arrival(customer *cust)
{
    PRINT(ANSI_FG_COLOR_WHITE "Customer %d arrived at %d second(s)\n" ANSI_COLOR_RESET, cust->id, cust->arrival_time);
    PRINT(ANSI_FG_COLOR_YELLOW "Customer %d orders %d ice creams\n", cust->id, cust->num_ordered);

    order *order = cust->orders.head;
    for (int i = 0; i < cust->num_ordered; i++)
    {
        PRINT(ANSI_FG_COLOR_CYAN " Ice cream %d:" ANSI_COLOR_RESET, cust->id);
        PRINT(ANSI_FG_COLOR_MAGENTA " %s " ANSI_COLOR_RESET, order->flavour);
        for (int j = 0; j < order->num_toppings; j++)
        {
            sem_wait(&print_lock);
            printf("%s ", order->toppings[j]);
            sem_post(&print_lock);
        }
        order = order->next;
        PRINT("\n");
    }
}

// Called by timer when the machine is supposed to start operation
void commission_machine(int mid)
{
    machine *machine = get_machine_by_id(mid);
    PRINT(ANSI_FG_COLOR_ORANGE "Machine %d has started working at %d second(s)\n" ANSI_COLOR_RESET, mid + 1, machine->start_time);
    machines[mid].remaining_time = machines[mid].end_time - machines[mid].start_time;
    machine->status = MACHINE_IDLE;

    sem_wait(&curr_machines.lock);
    curr_machines.val++;
    sem_post(&curr_machines.lock);

    pthread_t *thread = (pthread_t *)malloc(sizeof(pthread_t));
    machine->thread = thread;
    pthread_create(thread, NULL, machine_operations, (void *)machine);
    // pthread_join(*thread, NULL);
}

// Called by timer when the machine is supposed to halt operations
void decommission_machine(int mid)
{
    machine *machine = get_machine_by_id(mid);

    sem_wait(&curr_machines.lock);
    curr_machines.val--;
    sem_post(&curr_machines.lock);

    PRINT(ANSI_FG_COLOR_ORANGE "Machine %d has stopped working at %d second(s)\n" ANSI_COLOR_RESET, mid + 1, machine->end_time);
    machine->status = MACHINE_BUSY;
}

void *take_order_worker(void *arg)
{
    customer *cust = (customer *)arg;
    order *head = cust->orders.head;

    for (int ord = 0; ord < cust->num_ordered; ord++)
    {
        head->order_time = ticks;
        add_order_to_queue(head);
        head = head->next;
    }

    return NULL;
}

#ifdef DEBUG
void print_waiting_custs()
{
    cust_node *head = waiting_custs.head;
    if (!head)
        return;
    PRINT("Current waiting customers ");
    while (head)
    {
        PRINT("%d ", head->cust->id);
        head = head->next;
    }
    PRINT("; Count %d\n", waiting_custs.size);
}
#endif

void send_customers_into_parlour()
{
    cust_node *curr = customers.head;
    for (int i = 0; i < customers.size; i++)
    {
        if (curr && !curr->is_deleted && curr->cust->arrival_time == ticks)
        {
#ifdef DEBUG
            print_waiting_custs();
#endif
            if (waiting_custs.size >= k)
            {
                PRINT(ANSI_FG_COLOR_RED "The icecream parlour is at its full capacity!\n"
                                        "Therefore, Customer %d is told to leave\n" ANSI_COLOR_RESET,
                      curr->cust->id);
                curr->cust->is_left = 1;

                sem_wait(&cust_turned_away_count.lock);
                cust_turned_away_count.val++;
                sem_post(&cust_turned_away_count.lock);

                out_cust(curr->cust);
                continue;
            }

            print_customer_arrival(curr->cust);

            int can_fulfill = 1;
            order *head = curr->cust->orders.head;
            for (int ord = 0; ord < curr->cust->num_ordered; ord++)
            {
                can_fulfill = can_fulfill & order_preparable_ingredients(head, 0);
                head = head->next;
            }

            head = curr->cust->orders.head;
#ifdef DEBUG
            PRINT("Customer %d order can fulfill: %d\n", curr->cust->id, can_fulfill);
#endif
            if (can_fulfill)
            {
                print_pending_orders();

                add_cust(&waiting_custs, curr->cust);
                // for (int ord = 0; ord < curr->cust->num_ordered; ord++)
                // {

                // usleep(LESS_THAN_SECOND);
                // sleep(1);
                for (int ord = 0; ord < curr->cust->num_ordered; ord++)
                {
                    head->order_time = ticks;
                    add_order_to_queue(head);
                    head = head->next;
                }

                // pthread_t thread;
                // pthread_create(&thread, NULL, take_order_worker, (void *)curr->cust);
                // pthread_join(thread, NULL);
                // head = head->next;
                // }
            }
            else
            {
                PRINT(ANSI_FG_COLOR_RED "Customer %d has left the parlour at %d second(s) due to ingredient shortage\n" ANSI_COLOR_RESET, curr->cust->id, ticks);
                sem_wait(&cust_partial_count.lock);
                cust_partial_count.val++;
                sem_post(&cust_partial_count.lock);

                print_pending_orders();
                out_cust(curr->cust);
            }
        }
        curr = curr->next;
    }
}

#define initialise_queue(queue) \
    queue.head = NULL;          \
    queue.tail = NULL;          \
    queue.size = 0;

void initialise()
{
    sem_init(&curr_oid.lock, 0, 1);
    sem_init(&curr_machines.lock, 0, 1);
    sem_init(&print_lock, 0, 1);
    sem_init(&inventory_lock, 0, 1);

    sem_init(&cust_turned_away_count.lock, 0, 1);
    sem_init(&cust_fulfilled_count.lock, 0, 1);
    sem_init(&cust_partial_count.lock, 0, 1);

    // sem_init(&lowest_free_machine.lock, 0, 1);

    scanf("%d %d %d %d", &n, &k, &f, &t);

    machines = (machine *)malloc(sizeof(machine) * n);
    flavours = (flavour *)malloc(sizeof(flavour) * f);
    toppings = (topping *)malloc(sizeof(topping) * t);

    initialise_queue(customers);
    initialise_queue(waiting_custs);
    initialise_queue(pending_orders);
    sem_init(&pending_orders.lock, 0, 1);
}

void get_input()
{
    for (int i = 0; i < n; i++)
    {
        machines[i].id = i;
        scanf("%d %d", &machines[i].start_time, &machines[i].end_time);
        machines[i].num_orders_fulfilled = 0;
        machines[i].remaining_time = 0;
        machines[i].status = MACHINE_BUSY;
        machines[i].thread = 0;
        sem_init(&machines[i].lock, 0, 1);
    }

    for (int i = 0; i < f; i++)
    {
        flavours[i].id = i;
        scanf("%s %d", flavours[i].name, &flavours[i].prep_time);
    }

    for (int i = 0; i < t; i++)
    {
        toppings[i].id = i;
        scanf("%s %d", toppings[i].name, &toppings[i].quantity);
        sem_init(&toppings[i].lock, 0, 1);
    }
}

void input_customers()
{
    customer *curr_cust = 0;
    int curr_cust_oid = 0;
    while (1)
    {
        char buffer[BUFF_SIZE];
        fgets(buffer, BUFF_SIZE, stdin);

        if (strlen(buffer) == 1 && buffer[0] == '\n')
            continue;

        // press Ctrl+X to finish input
        if (buffer[0] == 24)
            break;

        // check if first char is digit or not
        if ((buffer[0] - '0') >= 0 && (buffer[0] - '0') < 10)
        {
            customer *new_cust = (customer *)malloc(sizeof(customer));
            sscanf(buffer, "%d %d %d", &new_cust->id, &new_cust->arrival_time, &new_cust->num_ordered);
            new_cust->num_ordered = 0;
            new_cust->num_fulfilled.val = 0;
            new_cust->is_left = 0;
            new_cust->is_deleted = 0;
            curr_cust = new_cust;
            curr_cust_oid = 1;

            sem_init(&new_cust->num_fulfilled.lock, 0, 1);

            add_cust(&customers, new_cust);
        }

        else
        {
            if (curr_cust == 0)
            {
                PRINT("Error: No customer to add order to\n");
                break;
            }
            order *new_order = (order *)malloc(sizeof(order));
            assert(new_order != NULL);

            new_order->id = get_next_oid();
            new_order->num_toppings = 0;
            new_order->next = NULL;
            new_order->status = ORDER_WAITING;
            new_order->taken_by_MID = -1;
            new_order->ordered_by = curr_cust;
            new_order->start_time = -1;
            new_order->order_time = curr_cust->arrival_time;
            new_order->cust_oid = curr_cust_oid++;

            int ct = 0;
            char *topping = strtok(buffer, " ");
            while (topping != NULL)
            {
                rtrim(topping, '\n');
                if (ct == 0)
                {
                    strcpy(new_order->flavour, topping);
                    ct++;
                }
                else
                {
                    strcpy(new_order->toppings[new_order->num_toppings], topping);
                    new_order->num_toppings++;
                }
                topping = strtok(NULL, " ");
            }

            new_order->prep_time = get_flavour_time(new_order->flavour);

            cust_add_order(&curr_cust->orders, new_order);
            curr_cust->num_ordered++;
        }
    }
}

void print_parlour()
{
    PRINT(ANSI_CLEAR_SCREEN ANSI_FG_COLOR_ORANGE "---Parlour opens for the day---\n\n" ANSI_COLOR_RESET);
    PRINT("--INPUT STARTS--\n");

    PRINT(ANSI_FG_COLOR_RED "Machines: \n" ANSI_COLOR_RESET);
    for (int i = 0; i < n; i++)
    {
        PRINT("%d: %d %d\n", machines[i].id, machines[i].start_time, machines[i].end_time);
    }

    PRINT(ANSI_FG_COLOR_RED "Flavours: \n" ANSI_COLOR_RESET);
    for (int i = 0; i < f; i++)
    {
        PRINT("%d: %s %d\n", flavours[i].id, flavours[i].name, flavours[i].prep_time);
    }

    PRINT(ANSI_FG_COLOR_RED "Toppings: \n" ANSI_COLOR_RESET);
    for (int i = 0; i < t; i++)
    {
        PRINT("%d: %s %d\n", toppings[i].id, toppings[i].name, toppings[i].quantity);
    }

    PRINT(ANSI_FG_COLOR_RED "Customers: \n" ANSI_COLOR_RESET);
    cust_node *curr = customers.head;
    while (curr)
    {
        PRINT(ANSI_FG_COLOR_YELLOW "%d: Arrived at %d second(s)\n" ANSI_COLOR_RESET, curr->cust->id, curr->cust->arrival_time, curr->cust->num_ordered);

        order *order = curr->cust->orders.head;
        for (int i = 0; i < curr->cust->num_ordered; i++)
        {
            PRINT(ANSI_FG_COLOR_CYAN "  %s " ANSI_COLOR_RESET, order->flavour);
            for (int j = 0; j < order->num_toppings; j++)
            {
                sem_wait(&print_lock);
                printf("%s ", order->toppings[j]);
                sem_post(&print_lock);
            }
            order = order->next;
            PRINT("\n");
        }
        curr = curr->next;
    }

    PRINT("--INPUT ENDS--\n\n");
}

int main()
{
    initialise();
    get_input();
    input_customers();
    print_parlour();
    pthread_t thread;
    pthread_create(&thread, NULL, depart_customers, NULL);

    pthread_t timer_thread;
    pthread_create(&timer_thread, NULL, timer, NULL);
    // timer();
    pthread_join(thread, NULL);
    pthread_join(timer_thread, NULL);

    char buffer[10];
    fgets(buffer, 10, stdin);
    if (!strcmp(buffer, "exit\n"))
    {
        PRINT("Quitting...\n");
        exit(0);
    }
}

int get_flavour_time(char *flavour)
{
    for (int i = 0; i < f; i++)
        if (!strcmp(flavour, flavours[i].name))
            return flavours[i].prep_time;
    return -1;
}

// Check if there is adequate topping that is required;
// Reduce the amount of topping that is used if the topping is adequate
int get_toppings(char *topping, int quantity, int reduce_bool)
{
    int ret = 0;
    for (int i = 0; i < t; i++)
    {
        if (strcmp(topping, toppings[i].name))
            continue;

        sem_wait(&toppings[i].lock);
        if (toppings[i].quantity == -1)
            ret = 1;

        else if (toppings[i].quantity < quantity)
            ret = 0;

        else
        {
            if (reduce_bool)
                toppings[i].quantity -= quantity;
            ret = 1;
        }
        sem_post(&toppings[i].lock);
        break;
    }
    return ret;
}

void print_inventory()
{
    PRINT(ANSI_FG_COLOR_RED "Inventory: \n" ANSI_COLOR_RESET);
    for (int i = 0; i < t; i++)
    {
        PRINT("%d: %s %d\n", toppings[i].id, toppings[i].name, toppings[i].quantity);
    }
}

int order_preparable_ingredients(order *order, int reduce_bool)
{
    sem_wait(&inventory_lock);
    int res = 1;
    for (int i = 0; i < order->num_toppings; i++)
        res = res & get_toppings(order->toppings[i], 1, reduce_bool);
    sem_post(&inventory_lock);
#ifdef DEBUG
    print_inventory();
#endif
    return res;
}

// utilities

// Remove trailing character from a string
void rtrim(char *word, char letter)
{
    for (int i = strlen(word) - 1; i >= 0; i--)
    {
        if (word[i] == letter)
        {
            word[i] = '\0';
            break;
        }
    }
}

machine *get_machine_by_id(int mid)
{
    for (int i = 0; i < n; i++)
        if (machines[i].id == mid)
            return &machines[i];
    return NULL;
}

int get_lowest_free_machine()
{
    // sem_wait(&lowest_free_machine.lock);
    for (int i = 0; i < n; i++)
    {
        sem_wait(&machines[i].lock);
        if (machines[i].status == MACHINE_IDLE)
        {
            // lowest_free_machine.val = i;
            sem_post(&machines[i].lock);
            // sem_post(&lowest_free_machine.lock);
            return i;
        };
        sem_post(&machines[i].lock);
    }
    return -1;
}

/*
2 3 2 3
0 7
4 10
vanilla 3
chocolate 4
caramel -1
brownie 1
strawberry 1
1 1 2
vanilla caramel
chocolate brownie strawberry
2 2 1
vanilla strawberry caramel
*/

/*
2 3 1 1
0 4
1 5
f1 3
t1 1
1 0 1
f1 t1
2 0 1
f1 t1
0


Cust 2 left; Remove orders

---Parlour opens for the day---

--INPUT STARTS--
Machines:
0: 0 4
1: 1 5
Flavours:
0: f1 3
Toppings:
0: t1 1
Customers:
1: Arrived at 0 second(s)
  f1 t1
2: Arrived at 0 second(s)
  f1 t1
0: Arrived at 0 second(s)
--INPUT ENDS--

Time: 0
Machine 1 has started working at 0 second(s)
Customer 1 arrived at 0 second(s)
Customer 1 orders 1 ice creams
 Ice cream 1: f1 t1
Customer 2 arrived at 0 second(s)
Customer 2 orders 1 ice creams
 Ice cream 2: f1 t1
Customer 0 arrived at 0 second(s)
Customer 0 orders 0 ice creams
Time: 1
Machine 2 has started working at 1 second(s)
Machine 1 starts preparing the order 1 of customer 1 at 1 second(s)
Order 1 of customer 2 rejected at 1 second(s) due to ingredient shortage
Customer 2 has left the parlour due to order not being fulfilled
Machine 2 starts preparing the order 1 of customer 2 at 1 second(s)
Time: 2
Time: 3
Machine 1 completes preparing the order 1 of customer 1 at 4 second(s)
Customer 1 has collected their order(s) and left at 4 second(s)
Machine 2 completes preparing the order 1 of customer 2 at 4 second(s)
Customer 2 has collected their order(s) and left at 4 second(s)
Time: 4
Machine 1 has stopped working at 4 second(s)
Time: 5
Machine 2 has stopped working at 5 second(s)
Customer 0 has been served at 6 second(s)
Time: 6

---Parlour is closed for the day---
Total customers served: 2
Total customers turned away due to
  ingredient shortage: 1
  queue overflow: 0


*/