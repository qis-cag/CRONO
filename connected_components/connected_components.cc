#include <cstdio>
#include <cstdlib>
#include <pthread.h>
//#include "carbon_user.h"     /* For the Graphite Simulator*/
#include <time.h>
#include <sys/timeb.h>

#define MAX            100000000
#define INT_MAX        100000000
#define BILLION 1E9

//Thread Argument Structure
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

//Function Declarations
int initialize_single_source(int* D, int* Q, int source, int N);
void init_weights(int N, int DEG, int** W, int** W_index);

//Global Variables
pthread_mutex_t lock;
pthread_mutex_t locks[4194304];    //locks for each vertex
int local_min_buffer[1024];
int global_min_buffer;
int P_global = 256;
int *edges;
int *exist;
int largest=0;
thread_arg_t thread_arg[1024];
pthread_t   thread_handle[1024];   //Max threads and pthread handlers

//Primary Parallel Function
void* do_work(void* args)
{
   //Thread Variables
   volatile thread_arg_t* arg = (thread_arg_t*) args;
   int tid                  = arg->tid;      //thread id
   int P                    = arg->P;        //Total threads
   int* D                   = arg->D;        //contains components
   //int** W                = arg->W;
   int** W_index            = arg->W_index;  //Graph Structure
   int mod                  = 1;             //modularity
   int v                    = 0;             //current vertex
   int start =  0;  //tid    * DEG / (arg->P);
   int stop  = 0;   //(tid+1) * DEG / (arg->P);

   //Chunk work for threads
   start =  tid    *  (largest+1) / (P);
   stop =  (tid+1) *  (largest+1) / (P);

   pthread_barrier_wait(arg->barrier_total);

   //Each component is its own, first phase
   for(v=start;v<stop;v++)
   {
      D[v] = v;
   }
   //printf("\n started P:%d %d",P, change); 
   pthread_barrier_wait(arg->barrier_total);

   //start connecting, second phase
   while(mod==1)
   { 
      mod=0;
      for(v=start;v<stop;v++)                  //for each vertex
      { 
         if(exist[v]==1)                       //if vertex exists
         { 
            for(int i = 0; i < edges[v]; i++)   //for each edge
            {
               int neighbor = W_index[v][i];
               pthread_mutex_lock(&locks[neighbor]);

               if((D[v] < D[i]) && (D[i] == D[D[i]]))
               {
                  mod=1;                      //some change occured
                  D[D[i]] = D[v];
               }				

               pthread_mutex_unlock(&locks[neighbor]);
            }
         }
      }
      //printf("\n third phase");
      //pthread_barrier_wait(arg->barrier_total);

	  //Third phase, assign components
      for(v=start;v<stop;v++)
      {
         while(D[v] != D[D[v]])
         {
            D[v] = D[D[v]];
         }
      }
      /*if(mod==1)
        {
        pthread_mutex_lock(&lock);
        change=1;
        pthread_mutex_lock(&lock);
        }*/
      //if(tid==0)
      //  printf("\n change:%d",mod);

      //pthread_barrier_wait(arg->barrier_total);
   }
   pthread_barrier_wait(arg->barrier_total);
   return NULL;
}

//Main
int main(int argc, char** argv)
{
   int N = 0;  //Graph vertices
   int DEG = 0; //edges per vertex
   FILE *file0 = NULL;
   const int select = atoi(argv[1]);

   //char filename[100];
   //If graph to be read from file
   if(select==1)
   {
      const char *filename = argv[3];
      //printf("Please Enter The Name Of The File You Would Like To Fetch\n");
      //scanf("%s", filename);
      file0 = fopen(filename,"r");
   }

   int lines_to_check=0;
   char c;
   int number0;
   int number1;
   int inter = -1; 

   if(select==1)
   {
      N = 2097152; //can be read from file if needed, this is a default upper limit
      DEG = 12;     //also can be reda from file if needed, upper limit here again
   }

   const int P1 = atoi(argv[2]);   //Max threads to be spawned

   int P = P1;
   P_global = P1;

   //If graph to be generated synthetically
   if(select==0)
   {
      N = atoi(argv[3]);      //Number of Vertices
      DEG = atoi(argv[4]);    //Edges per vertex
      printf("\nGraph with Parameters: N:%d DEG:%d\n",N,DEG);
   }

   if (DEG > N)
   {
      fprintf(stderr, "Degree of graph cannot be grater than number of Vertices\n");
      exit(EXIT_FAILURE);
   }

   //Memory allocations
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
   if(posix_memalign((void**) &edges, 64, N * sizeof(int)))
   {
      fprintf(stderr, "Allocation of memory failed\n");
      exit(EXIT_FAILURE);
   }
   if(posix_memalign((void**) &exist, 64, N * sizeof(int)))
   {
      fprintf(stderr, "Allocation of memory failed\n");
      exit(EXIT_FAILURE);
   }
   int d_count = N;
   pthread_barrier_t barrier_total;
   pthread_barrier_t barrier;

   //Graph data structures
   int** W = (int**) malloc(N*sizeof(int*));        //contains edge weights
   int** W_index = (int**) malloc(N*sizeof(int*));  //graph connectivity
   for(int i = 0; i < N; i++)
   {
      //W[i] = (int *)malloc(sizeof(int)*N);
      if(posix_memalign((void**) &W[i], 64, DEG*sizeof(int)))
      {
         fprintf(stderr, "Allocation of memory failed\n");
         exit(EXIT_FAILURE);
      }
      if(posix_memalign((void**) &W_index[i], 64, DEG*sizeof(int)))
      {
         fprintf(stderr, "Allocation of memory failed\n");
         exit(EXIT_FAILURE);
      }
   }

   //Initialize graph structures
   for(int i=0;i<N;i++)
   {
      for(int j=0;j<DEG;j++)
      {
         //W[i][j] = 1000000000;
         W_index[i][j] = INT_MAX;
      }
      edges[i]=0;
      exist[i]=0;
   }

   //Read graph from file
   if(select==1)
   {
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
            inter = edges[number0];

            //W[number0][inter] = drand48();
            W_index[number0][inter] = number1;
            //previous_node = number0;
            edges[number0]++;
            exist[number0]=1; exist[number1]=1;
         }
      }
      printf("\nFile Read, Largest Vertex:%d",largest);
   }

   //Generate graph synthetically
   if(select==0)
   {
      init_weights(N, DEG, W, W_index);
      largest = N-1;
   }

   //Synchronization Initializations
   pthread_barrier_init(&barrier_total, NULL, P);
   pthread_barrier_init(&barrier, NULL, P);
   pthread_mutex_init(&lock, NULL);

   for(int i=0; i<largest+1; i++)
   {
      if(select==0)
      {
         exist[i] = 1;
         edges[i] = DEG;
      }
      if(exist[i]==1)
         pthread_mutex_init(&locks[i], NULL);
   }

   //Initialize arrays
   initialize_single_source(D, Q, 0, N);

   //Thread arguments
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

   //CPU clock
   struct timespec requestStart, requestEnd;
   clock_gettime(CLOCK_REALTIME, &requestStart);

   // Enable Graphite performance and energy models
   //CarbonEnableModels();

   //Spawn threads
   for(int j = 1; j < P; j++) {
      pthread_create(thread_handle+j,
            NULL,
            do_work,
            (void*)&thread_arg[j]);
   }
   do_work((void*) &thread_arg[0]);  //main spawns itself

   //Join threads
   for(int j = 1; j < P; j++) { //mul = mul*2;
      pthread_join(thread_handle[j],NULL);
   }

   // Disable Graphite performance and energy models
   //CarbonDisableModels();

   printf("\nThreads Joined!");

   clock_gettime(CLOCK_REALTIME, &requestEnd);
   double accum = ( requestEnd.tv_sec - requestStart.tv_sec ) + ( requestEnd.tv_nsec - requestStart.tv_nsec ) / BILLION;
   printf( "\nTime Taken:\n%lf seconds\n", accum );

   /*for(int j=N-100;j<N;j++){
     if(exist[j]==1)
     printf("\n%d",D[j]);	
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
