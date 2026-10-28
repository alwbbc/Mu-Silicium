/* Minimal PID 1 for the hero2lte bring-up: no libc, raw aarch64 syscalls. */
typedef unsigned long u64;
static long sc(long n,long a,long b,long c,long d,long e){
  register long x8 __asm__("x8")=n, x0 __asm__("x0")=a, x1 __asm__("x1")=b,
                x2 __asm__("x2")=c, x3 __asm__("x3")=d, x4 __asm__("x4")=e;
  __asm__ volatile("svc 0":"+r"(x0):"r"(x8),"r"(x1),"r"(x2),"r"(x3),"r"(x4):"memory");
  return x0;
}
#define SYS_openat 56
#define SYS_close 57
#define SYS_read 63
#define SYS_write 64
#define SYS_mount 40
#define SYS_mkdirat 34
#define SYS_nanosleep 101
#define SYS_dup3 24
#define AT_FDCWD -100
static long slen(const char*s){long n=0;while(s[n])n++;return n;}
static int out=1;
static void puts_(const char*s){ sc(SYS_write,out,(long)s,slen(s),0,0); }
static void putn(u64 v){ char b[24]; int i=23; b[i]=0; do{ b[--i]='0'+v%10; v/=10; }while(v); puts_(b+i); }
static void cat(const char*path,int maxlines){
  char buf[4096]; long fd=sc(SYS_openat,AT_FDCWD,(long)path,0,0,0);
  puts_("\n--- "); puts_(path); puts_(" ---\n");
  if(fd<0){ puts_("(cannot open)\n"); return; }
  int lines=0; long n;
  while((n=sc(SYS_read,fd,(long)buf,sizeof buf,0,0))>0){
    long s=0;
    for(long i=0;i<n;i++) if(buf[i]=='\n' && ++lines>=maxlines){ sc(SYS_write,out,(long)buf,i+1,0,0); sc(SYS_close,fd,0,0,0,0); return; }
    sc(SYS_write,out,(long)buf+s,n-s,0,0);
  }
  sc(SYS_close,fd,0,0,0,0);
}
static void sleep_s(long s){ long ts[2]={s,0}; sc(SYS_nanosleep,(long)ts,0,0,0,0); }
void _start(void){
  sc(SYS_mkdirat,AT_FDCWD,(long)"/dev",0755,0,0);
  sc(SYS_mkdirat,AT_FDCWD,(long)"/proc",0755,0,0);
  sc(SYS_mkdirat,AT_FDCWD,(long)"/sys",0755,0,0);
  sc(SYS_mount,(long)"devtmpfs",(long)"/dev",(long)"devtmpfs",0,0);
  sc(SYS_mount,(long)"proc",(long)"/proc",(long)"proc",0,0);
  sc(SYS_mount,(long)"sysfs",(long)"/sys",(long)"sysfs",0,0);
  long fd=sc(SYS_openat,AT_FDCWD,(long)"/dev/console",2,0,0);
  if(fd>=0){ sc(SYS_dup3,fd,1,0,0,0); out=1; }
  puts_("\n\n=============================================\n");
  puts_("  Linux on Galaxy S7 Edge (hero2lte) via UEFI\n");
  puts_("  Project Silicium Mu-Silicium hero2lte-port\n");
  puts_("=============================================\n");
  cat("/proc/version",5);
  cat("/proc/cpuinfo",12);
  cat("/proc/meminfo",4);
  cat("/proc/cmdline",2);
  cat("/sys/firmware/efi/fw_platform_size",2);
  for(u64 t=0;;t+=10){ sleep_s(10); puts_("alive, uptime "); putn(t+10); puts_("s\n"); }
}
