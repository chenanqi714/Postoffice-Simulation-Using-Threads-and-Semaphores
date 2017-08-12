/*
* Demonstrate use of semaphores for synchronization.
*/
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>
#define NUM_CUSTOMER 50
#define MAX_CAPACITY 10
#define NUM_POSTWORKER 3
#define ms_buy_stamps 1000000    //time to buy stamps is 1 sec (1000000 usec)
#define ms_mail_letter 1500000   //time to mail a letter is 1.5 sec (1500000 usec)
#define ms_mail_package 2000000  //time to mail a package is 2 sec (2000000 usec)
typedef int bool;
#define true 1
#define false 0

//semaphore
sem_t max_capacity;      //only ten customers are allowed to be in the post office at one time
sem_t mutex;             //only one customer or one post worker is allowed to modify the queue at one time
sem_t mutex2;            //only one post worker is allowed to increment the count value
sem_t customer_ready;    //post worker can only serve the customer when the customer has signaled ready
sem_t scale;             //only one post worker can use the scale at one time

int count = 0;

//customer_node data structure
typedef struct customer_node {
	int customer_number;
	int task;
	sem_t finished;
	struct customer_node* next;
}customer_node;

//initial queue (empty)
customer_node* head = NULL;
customer_node* tail = NULL;

//queue functions
//check if queue is empty
bool queue_is_empty(customer_node** head, customer_node** tail) {
	if (*head == NULL && *tail == NULL)
		return true;
	else
		return false;
}
//add a customer to queue
void enqueue(customer_node* cust, customer_node** head, customer_node** tail) {
	if (queue_is_empty(head, tail)) {
		*head = cust;
		*tail = cust;
	}
	else {
		(*tail)->next = cust;
		*tail = cust;
	}
}
//take out a customer from queue
customer_node* dequeue(customer_node** head, customer_node** tail) {
	customer_node* temp;
	if (queue_is_empty(head, tail)) {
		printf("queue is empty\n");
	}
	else {
		if (*head == *tail) {
			temp = *head;
			*head = NULL;
			*tail = NULL;
		}
		else {
			temp = *head;
			*head = (*head)->next;
		}
	}
	return(temp);
}


//function to check status of thread creation and join
void check_thread_status(int status) {
	if (status != 0)
	{
		printf("Create/Join thread failed\n");
		exit(1);
	}
}

//function to initialize semaphore and check its status
void sem_init_check(sem_t* semaphore, int init_value) {
	if (sem_init(semaphore, 0, init_value) == -1)
	{
		printf("Init semaphore failed\n");
		exit(1);
	}
}

//function to wait semaphore and check its status
void sem_wait_check(sem_t* semaphore) {
	if (sem_wait(semaphore) == -1)
	{
		printf("Wait on semaphore failed\n");
		exit(1);
	}
}

//function to post semaphore and check its status
void sem_post_check(sem_t* semaphore) {
	if (sem_post(semaphore) == -1)
	{
		printf("Post semaphore failed\n");
		exit(1);
	}
}


//function to assign task to customer
int assign_task()
{
	int task = rand() % 3;
	return task;
}

//function to serve customer
void service_customer(customer_node** cust, int worker_number) {
	switch ((*cust)->task) {
		case 0: // buy stamps
			printf("Customer %d asks postwoker %d to buy stamps\n", (*cust)->customer_number, worker_number);
			usleep(ms_buy_stamps);
			break;
		case 1: // mail letter
			printf("Customer %d asks postwoker %d to mail a letter\n", (*cust)->customer_number, worker_number);
			usleep(ms_mail_letter);
			break;
		case 2: // mail package
			printf("Customer %d asks postwoker %d to mail a package\n", (*cust)->customer_number, worker_number);
			// wait scale
			sem_wait_check(&scale);
			printf("Scales in use by postal worker %d\n", worker_number);
			// use scale
			usleep(ms_mail_package);
			// signal scale
			sem_post_check(&scale);
			printf("Scales released by postal worker %d\n", worker_number);
			break;
		default:
			printf("Wrong task\n");
	}
}

//customer thread
void *customer(void *arg)
{
	customer_node* cust = (customer_node*)arg;
	
	//wait max_capacity
	sem_wait_check(&max_capacity);
	//enter post office
	printf("Customer %d enters post office\n", cust->customer_number);
	//enqueue
	sem_wait_check(&mutex);
	enqueue(cust, &head, &tail);
	sem_post_check(&mutex);
	//signal ready;
	sem_post_check(&customer_ready);
	//wait finished
	sem_wait_check(&(cust->finished));
	//leave post office
	printf("Customer %d leaves post office\n", cust->customer_number);
	//signal max_capacity
	sem_post_check(&max_capacity);
	free(cust);
	return NULL;
}

//post worker thread
void *worker(void *arg)
{
	int* temp = (int *)arg;
	int i;
	int worker_number = *temp;
	customer_node* cust;
	free(arg);
	
	while (1) {
		/*
		  wait customer_ready, 
		  note if all customers have been served, 
		  it will keep waiting until it is signaled by 
		  the post worker finishing serving the last customer
		*/
		sem_wait_check(&customer_ready);
		//if all customers have been served, exit.
		if (count == NUM_CUSTOMER)
			break;
		//dequeue
		sem_wait_check(&mutex);
		cust = dequeue(&head, &tail);
		sem_post_check(&mutex);
		//serve customer
		printf("Postworker %d serving customer %d\n", worker_number, cust->customer_number);
		service_customer(&cust, worker_number);
		//signal finished
		sem_post_check(&(cust->finished));
		printf("Postworker %d finishes serving customer %d\n", worker_number, cust->customer_number);
		//increment count by 1
		sem_wait_check(&mutex2);
		count++;
		sem_post_check(&mutex2);
		/*if count == 50, all customers have been served, 
		the worker that finishes serving the last customer will
		tell the other workers to exit before he exits.
		Others after serving customers will all wait for customer_ready semaphore,
		so he needs to signal the customer_ready semaphore NUM_POSTWORKER - 1 times
		*/
		if (count == NUM_CUSTOMER) { // signal customer_ready semaphore NUM_POSTWORKER - 1 times
			for(i = 0; i < NUM_POSTWORKER - 1; ++i)
			    sem_post_check(&customer_ready);
			break;
		}
	}
}

int main(int argc, char *argv[])
{
	int customer_number;
	int worker_number;
	pthread_t sem_customer[NUM_CUSTOMER];
	pthread_t sem_worker[NUM_POSTWORKER];
	int status;
	
	/* initialize and check semaphores*/
	sem_init_check(&max_capacity, MAX_CAPACITY);
	sem_init_check(&customer_ready, 0);
	sem_init_check(&mutex, 1);
	sem_init_check(&mutex2, 1);
	sem_init_check(&scale, 1);
	
	srand(time(0));
	
	//create postworkers
	for (worker_number = 0; worker_number < NUM_POSTWORKER; worker_number++) {
		int *arg = (int*)malloc(sizeof(int));
		*arg = worker_number;
		status = pthread_create(&sem_worker[worker_number], NULL, worker, (void*)arg);
		check_thread_status(status);
		printf("Postworker %d created\n", worker_number);
	}
	
	//create customers
	for (customer_number = 0; customer_number < NUM_CUSTOMER; customer_number++) {
		customer_node *cust = (customer_node*)malloc(sizeof(customer_node));
		cust->customer_number = customer_number;
		cust->task = assign_task();
		cust->next = NULL;
		sem_init_check(&(cust->finished), 0); //initialize semaphore cust->finished = 0 for each customer;
		status = pthread_create(&sem_customer[customer_number], NULL, customer, (void*)cust);
		check_thread_status(status);
		printf("Customer %d created\n", cust->customer_number);
	}

	/*
	* Wait for all threads to complete.
	*/
	for (customer_number = 0; customer_number < NUM_CUSTOMER; customer_number++) {
		status = pthread_join(sem_customer[customer_number], NULL);
		check_thread_status(status);
		printf("Joined customer %d\n", customer_number);
	}

	for (worker_number = 0; worker_number < NUM_POSTWORKER; worker_number++) {
		status = pthread_join(sem_worker[worker_number], NULL);
		check_thread_status(status);
		printf("Joined postworker %d\n", worker_number);
	}

	return 0;
}


