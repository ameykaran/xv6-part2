#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <string.h>

#define ANSI_FG_COLOR_RED "\x1b[31m"
#define ANSI_FG_COLOR_GREEN "\x1b[32m"
#define ANSI_FG_COLOR_YELLOW "\x1b[33m"
#define ANSI_FG_COLOR_BLUE "\x1b[34m"
#define ANSI_FG_COLOR_MAGENTA "\x1b[35m"
#define ANSI_FG_COLOR_CYAN "\x1b[36m"
#define ANSI_FG_COLOR_WHITE "\x1b[37m"
#define ANSI_COLOR_RESET "\x1b[0m"

#define max(a, b) ((a) > (b) ? (a) : (b))

struct customer
{
    int id;
    int arrival_time;
    char coffee_name[100];
    int coffee_time;
    int tolerance_time;
    int start_time;
    char is_served;
    char arr_printed, start_printed, served_printed, leave_printed, order_taken;
    int assigned_barista;
};

struct coffee
{
    char name[100];
    int time_to_prepare;
};

struct safe_int
{
    int data;
    sem_t lock;
};

struct safe_int wasted_coffee = {.data = 0}, num_cust_served = {.data = 0};

int ticks = -1;
pthread_mutex_t time_lock = PTHREAD_MUTEX_INITIALIZER;

int n, k, b;
struct customer *customers;
struct coffee *coff;

int *barista_free;
sem_t barista_free_sem;

int total_wait_time = 0;

void *timer()
{
    while (1)
    {
        pthread_mutex_lock(&time_lock);
        ticks += 1;
        // printf("Time: %d\n", ticks);
        pthread_mutex_unlock(&time_lock);
        sleep(1);
    }
}

int get_free_barista()
{
    int least_barista = -1;
    for (int i = 0; i < b; i++)
        if (barista_free[i] == 1)
        {
            least_barista = i;
            barista_free[i] = 0;
            break;
        }
    return least_barista;
}

void *serve_order(void *arg)
{
    struct customer *cust = (struct customer *)arg;
    int barista_id = cust->assigned_barista;

    int index = cust->id - 1;

    printf(ANSI_FG_COLOR_CYAN "Barista %d begins preparing the order of customer %d at %d second(s)\n" ANSI_COLOR_RESET, barista_id, cust->id, cust->start_time);

    int cust_left = 0;
    for (int i = 0; i < cust->coffee_time; i++)
    {
        sleep(1);
        if (cust->start_time + i - cust->arrival_time == cust->tolerance_time + 1)
        {
            printf(ANSI_FG_COLOR_RED "Customer %d leaves without their order at %d second(s)\n" ANSI_COLOR_RESET, cust->id, cust->start_time + i);
            cust_left = 1;
        }
    }

    printf(ANSI_FG_COLOR_BLUE "Barista %d completes the order of customer %d at %d second(s)\n" ANSI_COLOR_RESET, barista_id, cust->id, cust->start_time + cust->coffee_time);

    if (!cust_left)
    {
        printf(ANSI_FG_COLOR_GREEN "Customer %d leaves with their order at %d second(s)\n" ANSI_COLOR_RESET, cust->id, cust->start_time + cust->coffee_time);
    }
    else
    {
        sem_wait(&wasted_coffee.lock);
        wasted_coffee.data++;
        sem_post(&wasted_coffee.lock);
    }

    sem_wait(&num_cust_served.lock);
    num_cust_served.data++;
    sem_post(&num_cust_served.lock);

    sleep(1);
    sem_wait(&barista_free_sem);
    barista_free[barista_id - 1] = 1;
    sem_post(&barista_free_sem);

    return NULL;
}

int main()
{
    sem_init(&wasted_coffee.lock, 0, 1);
    sem_init(&num_cust_served.lock, 0, 1);
    sem_init(&barista_free_sem, 1, 1);

    scanf("%d %d %d", &b, &k, &n);
    customers = (struct customer *)malloc(n * sizeof(struct customer));
    coff = (struct coffee *)malloc(k * sizeof(struct coffee));
    barista_free = (int *)malloc(b * sizeof(int));

    for (int i = 0; i < b; i++)
        barista_free[i] = 1;

    for (int i = 0; i < k; i++)
        scanf("%s %d", coff[i].name, &coff[i].time_to_prepare);

    for (int i = 0; i < n; i++)
    {
        scanf("%d %s %d %d", &customers[i].id, customers[i].coffee_name, &customers[i].arrival_time, &customers[i].tolerance_time);
        customers[i].is_served = 0;
        for (int j = 0; j < k; j++)
            if (strcmp(customers[i].coffee_name, coff[j].name) == 0)
            {
                customers[i].coffee_time = coff[j].time_to_prepare;
                break;
            }
        customers[i].arr_printed = 0;
        customers[i].start_printed = 0;
        customers[i].served_printed = 0;
        customers[i].leave_printed = 0;
        customers[i].order_taken = 0;
    }

    pthread_t timer_thread;
    pthread_create(&timer_thread, NULL, timer, NULL);

    while (num_cust_served.data < n)
    {
        pthread_mutex_lock(&time_lock);
        for (int i = 0; i < n; i++)
        {
            if (customers[i].is_served == 1 || customers[i].order_taken == 1)
                continue;

            if (customers[i].arrival_time > ticks)
                continue;

            if (customers[i].arrival_time == ticks && !customers[i].arr_printed)
            {
                printf(ANSI_FG_COLOR_WHITE "Customer %d arrived at %d second(s)\n" ANSI_COLOR_RESET, customers[i].id, customers[i].arrival_time);
                printf(ANSI_FG_COLOR_YELLOW "Customer %d orders a", customers[i].id);
                if (customers[i].coffee_name[0] == 'A' || customers[i].coffee_name[0] == 'E' || customers[i].coffee_name[0] == 'I' || customers[i].coffee_name[0] == 'O' || customers[i].coffee_name[0] == 'U')
                    printf("n");
                printf(" %s\n" ANSI_COLOR_RESET, customers[i].coffee_name);
                customers[i].arr_printed = 1;
            }

            if (ticks - customers[i].arrival_time > customers[i].tolerance_time && !customers[i].order_taken)
            {
                // printf("f%d %d %d\n", cust->start_time, cust->arrival_time, cust->tolerance_time);
                total_wait_time += (ticks - customers[i].arrival_time);
                printf(ANSI_FG_COLOR_RED "Customer %d is frustrated and leaves at %d second(s)\n" ANSI_COLOR_RESET, customers[i].id, ticks);

                sem_wait(&num_cust_served.lock);
                num_cust_served.data++;
                sem_post(&num_cust_served.lock);
                customers[i].order_taken = 1;

                continue;
            }

            if (customers[i].arrival_time + 1 <= ticks && !customers[i].order_taken)
            {
                if (i != 0 && customers[i - 1].order_taken == 0)
                {
                    // i--;
                    continue;
                }

                sem_wait(&barista_free_sem);
                int least_barista = get_free_barista(barista_free, b);
                sem_post(&barista_free_sem);

                if (least_barista != -1)
                {
                    customers[i].start_time = ticks;
                    total_wait_time += (ticks - customers[i].arrival_time);

                    customers[i].assigned_barista = least_barista + 1;
                    customers[i].order_taken = 1;

                    pthread_t cust_thread;
                    pthread_create(&cust_thread, NULL, serve_order, (void *)&customers[i]);
                }
            }
        }
        pthread_mutex_unlock(&time_lock);
    }

    printf("\n");
    printf(ANSI_FG_COLOR_MAGENTA "%d coffee(s) wasted\n" ANSI_COLOR_RESET, wasted_coffee.data);
    printf(ANSI_FG_COLOR_MAGENTA "Average waiting time: %f\n" ANSI_COLOR_RESET, (float)total_wait_time / n);
    return 0;
}