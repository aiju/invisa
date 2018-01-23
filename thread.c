#include "visa.h"
#include "dat.h"
#include "fns.h"

void
newrendez(Rendez *r, Lock *l)
{
	memset(r, 0, sizeof(Rendez));
	r->l = l;
	InitializeConditionVariable(&r->cv);
}

void
putrendez(Rendez *r)
{
	memset(r, 0, sizeof(Rendez));
}

void
rsleep(Rendez *r)
{
	SleepConditionVariableCS(&r->cv, r->l, 0);
}

void
rwakeup(Rendez *r)
{
	WakeConditionVariable(&r->cv);
}

void
rwakeupall(Rendez *r)
{
	WakeAllConditionVariable(&r->cv);
}

void
newrwlock(RWLock *l)
{
	memset(l, 0, sizeof(RWLock));
	newlock(&l->l);
	newrendez(&l->r, &l->l);
}

void
putrwlock(RWLock *l)
{
	putlock(&l->l);
	memset(l, 0, sizeof(RWLock));
}

void
rlock(RWLock *l)
{
	lock(&l->l);
	while(l->wr || l->wrwait)
		rsleep(&l->r);
	l->rd++;
	unlock(&l->l);
}

void
runlock(RWLock *l)
{
	lock(&l->l);
	assert(l->rd > 0);
	if(--l->rd == 0)
		rwakeupall(&l->r);
	unlock(&l->l);
}

void
wlock(RWLock *l)
{
	lock(&l->l);
	while(l->rd || l->wr){
		l->wrwait++;
		rsleep(&l->r);
		l->wrwait--;
	}
	l->wr = 1;
	unlock(&l->l);
}

void
wunlock(RWLock *l)
{
	lock(&l->l);
	assert(l->wr > 0);
	l->wr = 0;
	rwakeupall(&l->r);
	unlock(&l->l);
}
