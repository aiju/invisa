#define newlock InitializeCriticalSection
#define lock EnterCriticalSection
#define unlock LeaveCriticalSection
#define putlock DeleteCriticalSection
void newrendez(Rendez *, Lock *);
void putrendez(Rendez *);
void rsleep(Rendez *);
void rwakeup(Rendez *);
void rwakeupall(Rendez *);
void newrwlock(RWLock *);
void putrwlock(RWLock *);
void rlock(RWLock *);
void runlock(RWLock *);
void wlock(RWLock *);
void wunlock(RWLock *);
Session* vibegin(ViSession, int);
ViStatus viend(Session *, ViStatus);
Attr* getattr(Session *, ViAttr, int);
Attr* newattri(Session *, ViAttr, int, ViAttrState);
ViAttrState attrval(Session *, ViAttr, ViAttrState);
ViStatus statusor(ViStatus, ViStatus);
ViStatus bufwrite(Session *, void *, ViUInt32, ViUInt32 *);
ViStatus bufread(Session *, void *, ViUInt32, ViUInt32 *);
ViStatus bufungetc(Session *, ViByte, int);
ViStatus newsession(ViPSession, Session **, Session *);
ViStatus bufwrflush(Session *, int);
void bufrdflush(Session *);
ViStatus iobufread(Session *, void *, ViUInt32, ViUInt32 *);
