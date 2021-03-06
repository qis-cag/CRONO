#include <cstdio>
#include <cstdlib>
#include <pthread.h>
//#include "carbon_user.h"     /* For the Graphite Simulator*/
#include <time.h>
#include <sys/timeb.h>

#define MAX            100000000
#define INT_MAX        100000000
// #define DEBUG              1
#define BILLION 1E9


int _W[8][8] =
{
   {0,   2,      1,      17,     MAX,    MAX,    MAX,    MAX},
   {2,   0,      MAX,    MAX,    2,      6,      MAX,    MAX},
   {1,   MAX,    0,      MAX,    MAX,    MAX,    MAX,    8},
   {17,  MAX,    MAX,    0,      MAX,    2,      1,      9},
   {MAX, 2,      MAX,    MAX,    0,      4,      MAX,    MAX},
   {MAX, 6,      MAX,    2,      4,      0,      5,      MAX},
   {MAX, MAX,    MAX,    1,      MAX,    5,      0,      3},
   {MAX, MAX,    8,      9,      MAX,    MAX,    3,      0}
};

int min = INT_MAX;
int min_index = 0;
pthread_mutex_t lock;
pthread_mutex_t locks[4194304];
int u = 0;


void init_weights(int N, int DEG, int** W, int** W_index)
{
   // Initialize to -1
   for(int i = 0; i < N; i++)
      for(int j = 0; j < DEG; j++)
         W_index[i][j]= -1;

   // Populate Index Array
   for(int i = 0; i < N; i++)
   {
      int last = 0;
      for(int j = 0; j < DEG; j++)
      {
         if(W_index[i][j] == -1)
         {        
            int neighbor = i+j;
            //W_index[i][j] = i+j;//rand()%(DEG);

            if(neighbor > last)
            {
               W_index[i][j] = neighbor;
               last = W_index[i][j];
            }
            else
            {
               if(last < (N-1))
               {
                  W_index[i][j] = (last + 1);
                  last = W_index[i][j];
               }
            }
         }
         else
         {
            last = W_index[i][j];
         }
         if(W_index[i][j]>=N)
         {
            W_index[i][j] = N-1;
         }
      }
   }

   // Populate Cost Array
   for(int i = 0; i < N; i++)
   {
      for(int j = 0; j < DEG; j++)
      {
         double v = drand48();
         /*if(v > 0.8 || W_index[i][j] == -1)
           {       W[i][j] = MAX;
           W_index[i][j] = -1;
           }

           else*/ if(W_index[i][j] == i)
         W[i][j] = 0;

         else
            W[i][j] = (int) (v*100) + 1;
         //printf("   %d  ",W_index[i][j]);
      }
      //printf("\n");
   }
}





int initialize_single_source(int* D, int* Q, int source, int N);

typedef struct
{
   int*      local_min;
   int*      global_min;
   int*      Q;
   int*      D;
   //int**     W;
   int**     W_index;
   int*      d_count;
   int       tid;
   int       P;
   int       N;
   int       DEG;
   pthread_barrier_t* barrier_total;
   pthread_barrier_t* barrier;
} thread_arg_t;

int local_min_buffer[1024];
int Total_tid[1024] = {0};
int global_min_buffer;
int terminate = 0;
int range=1;
int old_range =1;
int difference=0;
int pid=0;
int P_global = 256;
int change = 0;
int *test;
int *test1;
int largest=0;
long long Total = 0;
thread_arg_t thread_arg[1024];
pthread_t   thread_handle[1024];

void* do_work(void* args)
{
   volatile thread_arg_t* arg = (thread_arg_t*) args;

   volatile int* Q          = arg->Q;
   //int** W                  = arg->W;
   int** W_index            = arg->W_index;
   const int N              = arg->N;
   int uu = 0;

   int node = 0;

   pthread_barrier_wait(arg->barrier_total);

   while(terminate==0)
   {   
      pthread_mutex_lock(&lock);
      uu++;
      node = uu; 
      //printf("\n%d %d %d",tid,node,terminate);
      pthread_mutex_unlock(&lock);
      if(node>=N)
      {   
         terminate=1;
         break;
      }   

      for(int i = 0; i < test[node]; i++)
      {   
         int neighbor = W_index[node][i];
         if(W_index[node][i]>=N)
            continue;

         pthread_mutex_lock(&locks[neighbor]);

         Q[W_index[node][i]] = 0;

         pthread_mutex_unlock(&locks[neighbor]);
      }   
   }

   pthread_barrier_wait(arg->barrier_total);

   return NULL;
}


int main(int argc, char** argv)
{
   //char filename[100];
   const char *filename = argv[2];
   //printf("Please Enter The Name Of The File You Would Like To Fetch\n");
   //scanf("%s", filename);
   FILE *file0 = fopen(filename,"r");

   int lines_to_check=0;
   char c;
   int number0;
   int number1;
   int inter = -1; 
   int N = 2097152; //can be read from file if needed, this is a default upper limit
   int DEG = 12;     //also can be reda from file if needed, upper limit here again

   const int P1 = atoi(argv[1]);

   int P = P1;
   P_global = P1;
   //change = change1;
   old_range = change;
   range = change;

   if (DEG > N)
   {
      fprintf(stderr, "Degree of graph cannot be grater than number of Vertices\n");
      exit(EXIT_FAILURE);
   }

   int* D;
   int* Q;
   if(posix_memalign((void**) &D, 64, N * sizeof(int)))
   {
      fprintf(stderr, "Allocation of memory failed\n");
      exit(EXIT_FAILURE);
   }
   if(posix_memalign((void**) &Q, 64, N * sizeof(int)))
   {
      fprintf(stderr, "Allocation of memory failed\n");
      exit(EXIT_FAILURE);
   }
   if(posix_memalign((void**) &test, 64, N * sizeof(int)))
   {
      fprintf(stderr, "Allocation of memory failed\n");
      exit(EXIT_FAILURE);
   }
   if(posix_memalign((void**) &test1, 64, N * sizeof(int)))
   {
      fprintf(stderr, "Allocation of memory failed\n");
      exit(EXIT_FAILURE);
   }
   int d_count = N;
   pthread_barrier_t barrier_total;
   pthread_barrier_t barrier;

   //int** W = (int**) malloc(N*sizeof(int*));
   int** W_index = (int**) malloc(N*sizeof(int*));
   for(int i = 0; i < N; i++)
   {
      //W[i] = (int *)malloc(sizeof(int)*N);
      //int ret = posix_memalign((void**) &W[i], 64, DEG*sizeof(int));
      int re1 = posix_memalign((void**) &W_index[i], 64, DEG*sizeof(int));
      if (re1!=0)
      {
         fprintf(stderr, "Could not allocate memory\n");
         exit(EXIT_FAILURE);
      }
   }

   for(int i=0;i<N;i++)
   {
      for(int j=0;j<DEG;j++)
      {
         //W[i][j] = 1000000000;
         W_index[i][j] = INT_MAX;
      }
      test[i]=0;
      test1[i]=0;
   }

   for(c=getc(file0); c!=EOF; c=getc(file0))
   {
      if(c=='\n')
         lines_to_check++;

      if(lines_to_check>3)
      {
         int f0 = fscanf(file0, "%d %d", &number0,&number1);
         if(f0 != 2 && f0 != EOF)
         {
            printf ("Error: Read %d values, expected 2. Parsing failed.\n",f0);
            exit (EXIT_FAILURE);
         }
         //printf("\n%d %d",number0,number1);
         if(number0>largest)
            largest=number0;
         if(number1>largest)
            largest=number1;
         inter = test[number0];

         //W[number0][inter] = drand48();
         W_index[number0][inter] = number1;
         //previous_node = number0;
         test[number0]++;
         test1[number0]=1; test1[number1]=1;
      }
   }
   printf("\nFile Read, Largest Vertex:%d",largest);

   //init_weights(N, DEG, W, W_index);
   /*for(int i = 0;i<100;i++)
     {
     for(int j = 0;j<4;j++)
     {
     printf(" %d ",W_index[i][j]);
     }
     printf("\n");
     }*/

   pthread_barrier_init(&barrier_total, NULL, P);
   pthread_barrier_init(&barrier, NULL, P);

   pthread_mutex_init(&lock, NULL);

   for(int i=0; i<largest+1; i++)
   {
      if(test1[i]==1)
         pthread_mutex_init(&locks[i], NULL);
   }

   initialize_single_source(D, Q, 0, N);

   for(int j = 0; j < P; j++) {
      thread_arg[j].local_min  = local_min_buffer;
      thread_arg[j].global_min = &global_min_buffer;
      thread_arg[j].Q          = Q;
      thread_arg[j].D          = D;
      //thread_arg[j].W          = W;
      thread_arg[j].W_index    = W_index;
      thread_arg[j].d_count    = &d_count;
      thread_arg[j].tid        = j;
      thread_arg[j].P          = P;
      thread_arg[j].N          = N;
      thread_arg[j].DEG        = DEG;
      thread_arg[j].barrier_total = &barrier_total;
      thread_arg[j].barrier    = &barrier;
   }

   struct timespec requestStart, requestEnd;
   clock_gettime(CLOCK_REALTIME, &requestStart);

   // Enable Graphite performance and energy models
   //CarbonEnableModels();

   for(int j = 1; j < P; j++) {
      pthread_create(thread_handle+j,
            NULL,
            do_work,
            (void*)&thread_arg[j]);
   }
   do_work((void*) &thread_arg[0]);

   for(int j = 1; j < P; j++) { //mul = mul*2;
      pthread_join(thread_handle[j],NULL);
   }

   // Disable Graphite performance and energy models
   //CarbonDisableModels();

   printf("\nThreads Joined!");

   clock_gettime(CLOCK_REALTIME, &requestEnd);
   double accum = ( requestEnd.tv_sec - requestStart.tv_sec ) + ( requestEnd.tv_nsec - requestStart.tv_nsec ) / BILLION;
   printf( "\nTime Taken:\n%lf seconds", accum );

   /*for(int j=0;j<largest;j++)
     {
     if(test1[j]==1)
     printf("\n %d %d",j,Q[j]);
     }*/
   return 0;
}

int initialize_single_source(int*  D,
      int*  Q,
      int   source,
      int   N)
{
   for(int i = 0; i < N+1; i++)
   {
      D[i] = 0;
      Q[i] = 1;
   }

   //D[source] = 0;
   return 0;
}
