typedef unsigned long u64; typedef unsigned int u32;
#define LOG_BASE 0x91A00000UL
static char *p;
static void putc_(char c){ *p++ = c; }
static void puts_(const char *s){ while(*s) putc_(*s++); }
static void hex(u64 v,int n){ for(int i=n-1;i>=0;i--){ int d=(v>>(i*4))&15; putc_(d<10?'0'+d:'a'+d-10);} }
static u32 rd(u64 a){ return *(volatile u32*)a; }
static void dump(const char *name,u64 base,u32 len){
  puts_("\n[HERO2DBG] "); puts_(name); puts_(" @"); hex(base,8);
  for(u32 o=0;o<len;o+=16){
    puts_("\n[HERO2DBG] "); hex(base+o,8); puts_(":");
    for(int i=0;i<16;i+=4){ putc_(' '); hex(rd(base+o+i),8); }
  }
}
static u64 sysreg_sctlr(int el){ u64 v=0;
  if(el==2) __asm__ volatile("mrs %0, sctlr_el2":"=r"(v)); else __asm__ volatile("mrs %0, sctlr_el1":"=r"(v)); return v; }
static void wr(u64 a,u32 v){ *(volatile u32*)a=v; __asm__ volatile("dsb sy"); }
static void delay(u32 n){ for(volatile u32 i=0;i<n;i++); }
#define DECON 0x13960000UL
static void show(const char*t){ puts_("\n[HERO2DBG] "); puts_(t); puts_(" 0x60="); hex(rd(DECON+0x60),8);
  puts_(" 0x70="); hex(rd(DECON+0x70),8); puts_(" 0x00="); hex(rd(DECON),8); puts_(" 0x80="); hex(rd(DECON+0x80),8); }
void diag_main(u64 dtb,u64 load,u64 cel){
#ifdef TRIG_MODE
  p=(char*)LOG_BASE;
  puts_("\n\n[HERO2DBG] ===== hero2lte trig test mode " ); hex(TRIG_MODE,1); puts_(" =====");
  puts_("\n[HERO2DBG] load="); hex(load,10); puts_(" dtb="); hex(dtb,10); puts_(" EL="); hex((cel>>2)&3,1);
  show("before");
  /* paint: white top half, blue bottom half */
  volatile u32 *fb=(volatile u32*)0xE2A00000UL;
  for(u32 i=0;i<1440*2560;i++) fb[i]= (i<1440*1280)?0xFFFFFFFF:0xFF0000FF;
  __asm__ volatile("dsb sy");
  u32 v=rd(DECON+0x70);
#if TRIG_MODE == 1
  /* HW (TE) trigger, unmasked, no auto-mask: continuous refresh */
  v = (v | (1u<<4)) & ~((1u<<5)|(1u<<12));
  wr(DECON+0x70,v);
  wr(DECON+0x60, rd(DECON+0x60) | (1u<<31));
#else
  /* SW trigger one-shot */
  v = (v & ~(1u<<4)) | (1u<<8);
  wr(DECON+0x60, rd(DECON+0x60) | (1u<<31));
  wr(DECON+0x70,v);
#endif
  show("after");
  delay(10000000);
  show("later");
  puts_("\n[HERO2DBG] ===== end =====\n\n");
  __asm__ volatile("dsb sy");
  return;
#endif
  p=(char*)LOG_BASE;
  int el=(cel>>2)&3;
  puts_("\n\n[HERO2DBG] ===== hero2lte diag v1 =====");
  puts_("\n[HERO2DBG] load="); hex(load,10); puts_(" dtb="); hex(dtb,10);
  puts_(" EL="); hex(el,1); puts_(" SCTLR="); hex(sysreg_sctlr(el),8);
  dump("DECON_F",0x13960000,0x800);
  dump("VPP0",0x13951000,0x100); dump("VPP1",0x13952000,0x100);
  dump("VPP2",0x13953000,0x100); dump("VPP3",0x13954000,0x100);
  dump("DSIM0",0x13900000,0x100);
  puts_("\n[HERO2DBG] ===== end =====\n\n");
  __asm__ volatile("dsb sy");
}
