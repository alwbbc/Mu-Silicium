/* keylog: print key events from /dev/input/event0..7 (libc-free, aarch64) */
typedef unsigned long u64; typedef unsigned short u16; typedef int s32;
static long sc(long n,long a,long b,long c,long d){register long x8 __asm__("x8")=n,x0 __asm__("x0")=a,x1 __asm__("x1")=b,x2 __asm__("x2")=c,x3 __asm__("x3")=d;__asm__ volatile("svc 0":"+r"(x0):"r"(x8),"r"(x1),"r"(x2),"r"(x3):"memory");return x0;}
struct pfd{int fd;short ev,rev;}; struct iev{u64 s,us;u16 type,code;s32 value;};
static long slen(const char*s){long n=0;while(s[n])n++;return n;}
static void out(const char*s){sc(64,1,(long)s,slen(s),0);}
static void num(long v){char b[24];int i=23;b[i]=0;if(v<0){out("-");v=-v;}do{b[--i]='0'+v%10;v/=10;}while(v);out(b+i);}
static const char*name(int c){switch(c){case 114:return "VOLUME_DOWN";case 115:return "VOLUME_UP";case 116:return "POWER";case 172:return "HOME";default:return "KEY";}}
void _start(void){
  struct pfd p[8]; int n=0; char path[]="/dev/input/event0";
  for(int i=0;i<8;i++){path[16]='0'+i; long fd=sc(56,-100,(long)path,0,0); if(fd>=0){p[n].fd=fd;p[n].ev=1;n++;}}
  out("keylog: watching "); num(n); out(" input device(s). Press Vol+/Vol-/Home/Power.\n");
  if(!n) for(;;) sc(73,0,0,0,0);
  for(;;){
    if(sc(73,(long)p,n,0,0)<=0) continue; /* ppoll(fds,n,NULL,NULL) */
    for(int i=0;i<n;i++) if(p[i].rev&1){ struct iev e; if(sc(63,p[i].fd,(long)&e,sizeof e,0)==sizeof e && e.type==1){
      out(name(e.code)); out(" ("); num(e.code); out(") "); out(e.value==1?"pressed\n":e.value==0?"released\n":"repeat\n"); } }
  }
}
