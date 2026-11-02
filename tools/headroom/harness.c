#include "coop_m1.c"
static long sc(long n,long a,long b,long c,long d,long e,long f){ long r; __asm__ volatile("push %%ebp; mov %7,%%ebp; int $0x80; pop %%ebp":"=a"(r):"0"(n),"b"(a),"c"(b),"d"(c),"S"(d),"D"(e),"m"(f):"memory"); return r; }
static void out(const char* s){ u32 n=0; while(s[n])n++; sc(4,1,(long)s,n,0,0,0); }
static void outn(const char* s,u32 n){ sc(4,1,(long)s,n,0,0,0); }
static void odec(u32 v){ char b[16]; char* e=hr_dec(b,v); outn(b,e-b); }
static u32 g_fake=5000; static char wbuf[4096]; static u32 wlen, nCreate;
static i32 __attribute__((stdcall)) fakeQuery(struct MmStats* s){ s->totalPages=16384; s->availPages=g_fake; s->imagePages=3000; s->poolPages=500; s->cachePages=100; s->vmCommitted=40<<20; s->vmReserved=1<<20; return 0; }
static i32 __attribute__((stdcall)) fakeCreate(void** h,u32 a,void* oa,void* io,void* al,u32 at,u32 sh,u32 disp,u32 opt){ *h=(void*)0x1234; nCreate++; out("[create] "); out(((char**)oa)[1] ? *(char**)(((u32*)((u32*)oa)[1])+1) : "?"); out("\n"); return 0;}
static i32 __attribute__((stdcall)) fakeWrite(void*h,void*e,void*a,void*c,void*io,const void*b,u32 n,void*off){ for(u32 i=0;i<n;i++)wbuf[i]=((const char*)b)[i]; wlen=n; return 0;}
static i32 __attribute__((stdcall)) fakeClose(void*h){ return 0;}
void _start(void){
  long m=sc(192,0x55F000,0x2000,3,0x32,-1,0); /* mmap2 PROT_RW, MAP_PRIVATE|FIXED|ANON */
  if(m!=0x55F000){ out("mmap failed\n"); sc(1,1,0,0,0,0,0); }
  *(u32*)K_MmQueryStatistics=(u32)fakeQuery; *(u32*)K_NtCreateFile=(u32)fakeCreate; *(u32*)K_NtWriteFile=(u32)fakeWrite; *(u32*)K_NtClose=(u32)fakeClose;
  u32 now=0;
  /* menus: 100 frames, 9000 free */
  g_fake=9000; for(;now<100;now++){ hr_tick(now);} 
  /* level 7 begins: p1 appears, key unknown for 40 frames then 7 */
  s_p1=(u8*)0x1000; s_key=0xFF; g_fake=4000;
  for(;now<140;now++){ g_fake-=2; hr_tick(now);} 
  s_key=7;
  for(;now<400;now++){ hr_tick(now);} 
  g_dbg.player2=0x2000; g_fake=3700; /* P2 spawned: costs ~300 pages */
  for(;now<700;now++){ if(now==500)g_fake=3650; hr_tick(now);} 
  /* level ends */
  s_p1=0; g_dbg.player2=0; g_fake=9100;
  for(;now<800;now++){ hr_tick(now);} 
  out("creates="); odec(nCreate); out(" status="); odec(g_hr.status); out(" calls="); odec(g_hr.calls); out("\n");
  sc(4,1,(long)wbuf,wlen,0,0,0);
  sc(1,0,0,0,0,0,0);
}
