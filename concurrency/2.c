#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>

#define ANSI_FG_COLOR_RED "\x1b[31m"
#define ANSI_FG_COLOR_GREEN "\x1b[32m"
#define ANSI_FG_COLOR_YELLOW "\x1b[33m"
#define ANSI_FG_COLOR_BLUE "\x1b[34m"
#define ANSI_FG_COLOR_MAGENTA "\x1b[35m"
#define ANSI_FG_COLOR_CYAN "\x1b[36m"
#define ANSI_FG_COLOR_WHITE "\x1b[37m"
#define ANSI_COLOR_RESET "\x1b[0m"

#define MAX_ORDERS 100
#define BUFF_SIZE 4096

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

struct safe_int
{
    int val;
    sem_t lock;
};
enum order_status
{
    ORDER_PENDING,
    ORDER_PREPARING,
    // ORDER_READY,
    ORDER_SERVED
};
struct order
{
    int id;
    char flavour[100];
    order_status status;
    int taken_by_MID;
    struct order *next;

    int num_toppings;
    char toppings[][100];
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
    int num_fulfilled;
    char is_left;
    order_queue orders;
    int start_time[MAX_ORDERS];
    int served_time[MAX_ORDERS];
    int leave_time[MAX_ORDERS];
};
struct machine
{
    int id;
    int start_time, end_time;
    int num_orders_fulfilled;
};
struct topping
{
    int id;
    char name[100];
    int quantity;
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
};
struct cust_queue
{
    cust_node *head;
    cust_node *tail;
    int size;
};

// Add a customer to the waiting line of the parlour
void add_cust(cust_queue *queue, customer *cust)
{
    if (cust == NULL)
        return;

    // if (queue->size >= k)
    // {
    //     printf("The icecream parlour is at its full capacity"
    //            "Therefore, customer %d is told to leave\n",
    //            cust->id);
    //     cust->is_left = 1;
    //     // out_cust(&customers, cust);
    //     return;
    // }

    cust_node *new_node = (cust_node *)malloc(sizeof(cust_node));
    new_node->cust = cust;
    new_node->next = NULL;

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

// A customer leaves the parlour
void out_cust(cust_queue *queue, customer *cust)
{
    if (cust == NULL)
        return;

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
            free_customer(curr->cust);
            free(curr);
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

cust_queue customers;
machine *machines;
topping *toppings;
flavour *flavours;

cust_queue waiting_custs;

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

void initialise()
{
    sem_init(&curr_oid.lock, 0, 1);
    // sem_init(&curr_capacity.lock, 0, 1);

    scanf("%d %d %d %d", &n, &k, &f, &t);

    machines = (machine *)malloc(sizeof(machine) * n);
    flavours = (flavour *)malloc(sizeof(flavour) * f);
    toppings = (topping *)malloc(sizeof(topping) * t);
    customers.head = NULL;
    customers.tail = NULL;
    customers.size = 0;

    // cust_till_count = 0;
    waiting_custs.head = NULL;
    waiting_custs.tail = NULL;
    waiting_custs.size = 0;
}

void get_input()
{
    for (int i = 0; i < n; i++)
    {
        machines[i].id = i;
        scanf("%d %d", &machines[i].start_time, &machines[i].end_time);
        machines[i].num_orders_fulfilled = 0;
    }
    printf("Mac\n");
    for (int i = 0; i < f; i++)
    {
        flavours[i].id = i;
        scanf("%s %d", flavours[i].name, &flavours[i].prep_time);
    }
    printf("fla\n");

    for (int i = 0; i < t; i++)
    {
        toppings[i].id = i;
        scanf("%s %d", toppings[i].name, &toppings[i].quantity);
    }
    printf("top\n");
}

void rtrim(char *word, char letter);

void read_customers()
{
    customer *curr_cust = 0;
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
            new_cust->num_fulfilled = 0;
            new_cust->is_left = 0;
            curr_cust = new_cust;
            add_cust(&customers, new_cust);
        }

        else
        {
            if (curr_cust == 0)
            {
                printf("Error: No customer to add order to\n");
                break;
            }
            order *new_order = (order *)malloc(sizeof(order));
            assert(new_order != NULL);

            new_order->id = get_next_oid();
            new_order->num_toppings = 0;
            new_order->next = NULL;
            new_order->status = ORDER_PENDING;
            new_order->taken_by_MID = -1;

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

            cust_add_order(&curr_cust->orders, new_order);
            curr_cust->num_ordered++;
        }
    }
}

void print_parlour()
{
    printf(ANSI_FG_COLOR_RED "Machines: \n" ANSI_COLOR_RESET);
    for (int i = 0; i < n; i++)
    {
        printf("%d: %d %d\n", machines[i].id, machines[i].start_time, machines[i].end_time);
    }

    printf(ANSI_FG_COLOR_RED "Flavours: \n" ANSI_COLOR_RESET);
    for (int i = 0; i < f; i++)
    {
        printf("%d: %s %d\n", flavours[i].id, flavours[i].name, flavours[i].prep_time);
    }

    printf(ANSI_FG_COLOR_RED "Toppings: \n" ANSI_COLOR_RESET);
    for (int i = 0; i < t; i++)
    {
        printf("%d: %s %d\n", toppings[i].id, toppings[i].name, toppings[i].quantity);
    }

    printf(ANSI_FG_COLOR_RED "Customers: \n" ANSI_COLOR_RESET);
    cust_node *curr = customers.head;
    while (curr)
    {
        printf(ANSI_FG_COLOR_YELLOW "%d: Arrived at %d second(s)\n" ANSI_COLOR_RESET, curr->cust->id, curr->cust->arrival_time, curr->cust->num_ordered);

        order *order = curr->cust->orders.head;
        for (int i = 0; i < curr->cust->num_ordered; i++)
        {
            printf(ANSI_FG_COLOR_CYAN "  %s " ANSI_COLOR_RESET, order->flavour);
            for (int j = 0; j < order->num_toppings; j++)
                printf("%s ", order->toppings[j]);
            order = order->next;
            printf("\n");
        }
        curr = curr->next;
    }
}

int main()
{
    initialise();
    get_input();
    read_customers();
    print_parlour();
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

/*
2 3 2 3
0 7
4 10
vanilla 3
chocolate 4
caramel -1
brownie 4
strawberry 4
1 1 2
vanilla caramel
chocolate brownie strawberry
2 2 1
vanilla strawberry caramel
*/
