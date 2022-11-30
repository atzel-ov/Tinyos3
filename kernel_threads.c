
#include "tinyos.h"
#include "kernel_sched.h"
#include "kernel_proc.h"
#include "kernel_cc.h"
#include "kernel_streams.h"


/** 
  @brief Create a new thread in the current process.
  Initialize the ptcb and make the new thread READY.
  */
Tid_t sys_CreateThread(Task task, int argl, void* args)
{

  TCB* tcb; //Initialization of a thread (TCB)
  PCB* curproc = CURPROC;

  /*
    When we initialize a thread we have to spawn a new thread, thus we add 
    a new thread in the current process . We do that by calling a function
    called : start_main_ptcb_thread 
  */
  tcb = spawn_thread(curproc, start_main_ptcb_thread);
  acquire_ptcb(tcb, task, argl, args); // We acquire a ptcb with our new thread pointing at it 
  
  curproc->thread_count++;  // Since we created a thread we add 1 to the count
	
  wakeup(tcb);  // thread becomes ready

  return (Tid_t)tcb->ptcb;
  
}




/**
  @brief Return the Tid of the current thread.
 */
Tid_t sys_ThreadSelf()
{
	return (Tid_t)cur_thread()->ptcb;
}



/**
  @brief Join the given thread.
  When the current thread is in RUNNING state, it stops running
  and waits until the given thread ends
  */
int sys_ThreadJoin(Tid_t tid, int* exitval)
{

  PTCB* ptcb = (PTCB*) tid;
  PCB* curproc = CURPROC;


  if(rlist_find(&curproc->ptcb_list, ptcb, NULL) == NULL){

    //if we find that the current ptcb list doesnt contain the thread we exit
    return -1;
  }


  if(cur_thread()->ptcb == ptcb){

    //When the cuurent thread joins itself we exit
    return -1;
  }

  if(ptcb->detached == 1){

    //If the joined thread is detached, it canNOT be joined
    return -1;
  }

  // as multiple threads can join each time we call threadJoin, we must store how many so we can free the memory of each one 
  increase_refcount(ptcb); 



  while((ptcb->detached != 1) && (ptcb->exited != 1)) {

    // putting curthread to SLEEP state at the exit condvar of the joined thread and unlocking curthreads mutex
    kernel_wait(&(ptcb->exit_cv), SCHED_USER);  

  }


  decrease_refcount(ptcb);


  if(ptcb->detached == 1){

    //If the thread got detached while the curthread is waiting return -1
    return -1;
  }

  if(exitval != NULL){
    *exitval = ptcb->exitval; //while the exit status is not NULL get the new exit status of the joined thread 
  }

  // After everything was successfull we free up the memory used for the joined thread
  if(ptcb->refcount == 1){
    rlist_remove(&(ptcb->ptcb_list_node)); //When the refcount is 1 we must remove the ptcb
    free(ptcb);  

  }


	return 0;
}




/**
  @brief Detach the given thread.
  When a joined thread gets detached all the threads "sleeping" on its exit cv
  must get signaled and thus ready to start running again
  */
int sys_ThreadDetach(Tid_t tid)
{
  PTCB* ptcb = (PTCB*)tid; 
  PCB* curproc = CURPROC;

  

  if(rlist_find(&curproc->ptcb_list, ptcb, NULL) == NULL){ 

    return -1;
  }



  if(ptcb->exited == 1){

    return -1;
  }


  ptcb->detached = 1;


  kernel_broadcast(&ptcb->exit_cv);


	return 0;
}




/**
  @brief Terminate the current thread.
  */
void sys_ThreadExit(int exitval)
{

  PTCB* ptcb = cur_thread()->ptcb;


  ptcb->exitval = exitval; 
  ptcb->exited = 1;
  
  // the thread is exited thus we unlock the mutex for the next thread to lock it and start running
  kernel_broadcast(&(ptcb->exit_cv)); 

  PCB* curproc = CURPROC;
  curproc->thread_count--;


  if(curproc->thread_count == 0) {

    if(get_pid(curproc) != 1){
    /* Reparent any children of the exiting process to the 
       initial task */
      PCB* initpcb = get_pcb(1);
      while(!is_rlist_empty(& curproc->children_list)) {
        rlnode* child = rlist_pop_front(& curproc->children_list);
        child->pcb->parent = initpcb;
        rlist_push_front(& initpcb->children_list, child);
      }

      /* Add exited children to the initial task's exited list 
         and signal the initial task */
      if(!is_rlist_empty(& curproc->exited_list)) {
        rlist_append(& initpcb->exited_list, &curproc->exited_list);
        kernel_broadcast(& initpcb->child_exit);
      }

      /* Put me into my parent's exited list */
      rlist_push_front(& curproc->parent->exited_list, &curproc->exited_node);
      kernel_broadcast(& curproc->parent->child_exit);

    }

    assert(is_rlist_empty(& curproc->children_list));
    assert(is_rlist_empty(& curproc->exited_list));


    /* 
      Do all the other cleanup we want here, close files etc. 
     */

    /* Clean up PTCB list nodes*/

    while(is_rlist_empty(&curproc->ptcb_list) != 0){

      rlnode* ptcb_node;
      ptcb_node = rlist_pop_front(&curproc->ptcb_list);
      free(ptcb_node->ptcb);
    }

    /* Release the args data */
    if(curproc->args) {
      free(curproc->args);
      curproc->args = NULL;
    }

    /* Clean up FIDT */
    for(int i=0;i<MAX_FILEID;i++) {
      if(curproc->FIDT[i] != NULL) {
        FCB_decref(curproc->FIDT[i]);
        curproc->FIDT[i] = NULL;
      }
    }


    /* Disconnect my main_thread */
    curproc->main_thread = NULL;

    /* Now, mark the process as exited. */
    curproc->pstate = ZOMBIE;
  }



  /* Bye-bye cruel world */
  kernel_sleep(EXITED, SCHED_USER);

}



