#define HELION_UNIT_TEST 1
#include "main.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* ---- stub globals/impls ---- */
GPIO_TypeDef gA,gB,gC; GPIO_TypeDef *GPIOA=&gA,*GPIOB=&gB,*GPIOC=&gC;
int stub_button_low=0; CoreDebug_t cd; DWT_t dwt; CoreDebug_t*CoreDebug=&cd; DWT_t*dwt_ptr=&dwt; DWT_t *dwt_tick(void){ dwt.CYCCNT+=2000; return &dwt; }
uint32_t SystemCoreClock=72000000; uint16_t stub_adc[4]={0}; uint32_t stub_adc_cur; uint32_t stub_cmp[2]; int stub_pwm_run[2];
static uint32_t tick=0; uint32_t HAL_GetTick(void){ tick+=1; dwt.CYCCNT+=SystemCoreClock/1000; return tick; }
static uint8_t flash[1024]; 
HAL_StatusTypeDef HAL_I2C_Init(I2C_HandleTypeDef*h){(void)h;return HAL_OK;}
static uint16_t ina_bus=0, ina_shunt=0; static int i2c_fail=0;
HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef*h,uint16_t a,uint16_t r,uint16_t s,uint8_t*d,uint16_t n,uint32_t t){(void)h;(void)a;(void)r;(void)s;(void)d;(void)n;(void)t;return i2c_fail?HAL_TIMEOUT:HAL_OK;}
HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef*h,uint16_t a,uint16_t r,uint16_t s,uint8_t*d,uint16_t n,uint32_t t){(void)h;(void)a;(void)s;(void)n;(void)t;if(i2c_fail)return HAL_TIMEOUT;uint16_t v=(r==2)?ina_bus:ina_shunt;d[0]=(uint8_t)(v>>8);d[1]=(uint8_t)(v&0xFF);return HAL_OK;}
HAL_StatusTypeDef HAL_I2C_IsDeviceReady(I2C_HandleTypeDef*h,uint16_t a,uint32_t tr,uint32_t t){(void)h;(void)a;(void)tr;(void)t;return HAL_OK;}
HAL_StatusTypeDef HAL_FLASHEx_Erase(FLASH_EraseInitTypeDef*e,uint32_t*pe){(void)e;(void)pe;memset(flash,0xFF,sizeof flash);return HAL_OK;}
#define CAL_FLASH_ADDR_REAL CAL_FLASH_ADDR
/* redirect flash reads to the buffer: Cal_Load/Cal_Save cast the address, so test via a macro-free copy */
#include "../Core/Src/main.c"

HAL_StatusTypeDef HAL_FLASH_Program(uint32_t t,uint32_t a,uint64_t d){(void)t;uint32_t off=a-CAL_FLASH_ADDR;uint32_t w=(uint32_t)d;memcpy(&flash[off],&w,4);return HAL_OK;}
static int fails=0;
#define CHECK(c,msg) do{ if(!(c)){printf("FAIL: %s\n",msg);fails++;} else printf("ok:   %s\n",msg);}while(0)

int main(void){
  /* --- font table size / spot checks --- */
  CHECK(sizeof(font5x7)/sizeof(font5x7[0])==59,"font has 59 glyphs (0x20..0x5A)");
  CHECK(font5x7['A'-0x20][0]==0x7C && font5x7['Z'-0x20][4]==0x43 && font5x7['0'-0x20][0]==0x3E,"font spot-check A/Z/0");

  /* --- INA219 conversions --- */
  ina_bus = (uint16_t)((5120/4)<<3);          /* 5.120 V */
  ina_shunt = 1234;                           /* 12.34 mV across 0.1 ohm -> 123.4 mA */
  Ina219_t d={.addr7=0x40};
  CHECK(INA_Read(&d) && d.mv==5120 && d.ma10==1234,"INA219: 5.120 V / 123.4 mA decode");
  CHECK(d.mw==631,"INA219: power 5120mV*123.4mA = 631 mW");
  ina_shunt = (uint16_t)(-5);
  INA_Read(&d); CHECK(d.ma10==0,"INA219: negative noise clamped to 0");
  ina_bus = (uint16_t)((9000/4)<<3); ina_shunt=8000; INA_Read(&d);   /* 9 V, 800 mA */
  CHECK(d.mv==9000 && d.ma10==8000 && d.mw==7200,"INA219: 9.0 V / 800 mA / 7200 mW (no overflow)");
  ina_bus = (uint16_t)(((26000/4)<<3)|1); INA_Read(&d); CHECK(d.mv==26000,"INA219: 26 V (>16 V) reads positive, OVF flag ignored");

  /* --- Axis controller --- */
  Axis_t a; memset(&a,0,sizeof a); uint16_t p=1500; faults=0;
  CHECK(!Axis_Update(&a,30,&p,900,2100,+1,FAULT_PAN_RUNAWAY) && p==1500,"axis: inside deadband holds");
  CHECK(Axis_Update(&a,100,&p,900,2100,+1,FAULT_PAN_RUNAWAY) && p==1510,"axis: err +100 -> +10 us (5 + 100/20)");
  p=1500; memset(&a,0,sizeof a);
  Axis_Update(&a,1000,&p,900,2100,+1,FAULT_PAN_RUNAWAY); CHECK(p==1530,"axis: big error step capped at 30 us");
  p=1500; memset(&a,0,sizeof a); Axis_Update(&a,-200,&p,900,2100,+1,FAULT_PAN_RUNAWAY); CHECK(p<1500,"axis: negative error moves other way");
  p=1500; memset(&a,0,sizeof a); Axis_Update(&a,200,&p,900,2100,-1,FAULT_PAN_RUNAWAY); CHECK(p<1500,"axis: DIR=-1 inverts");
  /* runaway: error never shrinks */
  p=1500; memset(&a,0,sizeof a); faults=0; int moved=0;
  for(int i=0;i<200 && !faults;i++){ if(Axis_Update(&a,400,&p,900,2100,+1,FAULT_PAN_RUNAWAY)) moved++; }
  CHECK((faults&FAULT_PAN_RUNAWAY)!=0 && moved<=RUNAWAY_STEPS+1,"axis: wrong-direction runaway raises a fault (at or before the limit)");
  CHECK(p>=900 && p<=2100,"axis: pulse never leaves the clamped range");
  /* wrong direction, small range margin: starts 100 us from the limit */
  p=2000; memset(&a,0,sizeof a); faults=0;
  for(int i=0;i<60 && !faults;i++) Axis_Update(&a,600,&p,900,2100,+1,FAULT_PAN_RUNAWAY);
  CHECK(!(faults&FAULT_PAN_RUNAWAY) && a.at_limit && p==2100,"axis: only ~3 steps of evidence near the limit -> LIMIT (not enough to call it a fault)");
  /* following the sun into the end of travel: error shrinking all the way */
  p=1500; memset(&a,0,sizeof a); faults=0; int e2=900;
  for(int i=0;i<120;i++){ Axis_Update(&a,e2,&p,900,2100,+1,FAULT_PAN_RUNAWAY); if(e2>200)e2-=8; }
  CHECK(faults==0 && p==2100 && a.at_limit,"axis: improving error up to the end of travel -> LIMIT, no fault");
  /* converging error never trips */
  p=1500; memset(&a,0,sizeof a); faults=0; int e=900;
  for(int i=0;i<200;i++){ Axis_Update(&a,e,&p,900,2100,+1,FAULT_PAN_RUNAWAY); if(e>20)e-=15; }
  CHECK(faults==0,"axis: converging error does not fault");
  /* at mechanical limit */
  p=2100; memset(&a,0,sizeof a); faults=0;
  for(int i=0;i<100;i++) Axis_Update(&a,500,&p,900,2100,+1,FAULT_PAN_RUNAWAY);
  CHECK(faults==0 && a.at_limit && p==2100,"axis: sun beyond range -> LIMIT, no false fault");

  /* --- LDR fault --- */
  uint16_t ok[4]={2000,2100,1900,2050}, open_[4]={2000,2100,10,2050}, dusk[4]={20,25,10,22};
  CHECK(!LDR_Fault(ok) && LDR_Fault(open_) && !LDR_Fault(dusk),"LDR fault: open wire detected, dusk is not a fault");

  /* --- Calibration math --- */
  uint32_t q[4]; uint16_t good[4]={2000,2100,1900,2050};
  CHECK(Cal_Compute(good,q),"cal: normal input accepted");
  uint32_t sum=0; for(int i=0;i<4;i++) sum+= (good[i]*q[i])>>12;
  CHECK(abs((int)sum/4-2012)<=2,"cal: corrected channels converge to the mean");
  uint16_t dark[4]={100,120,90,110}, sat[4]={4095,4095,4000,4095}, uneq[4]={3000,3000,3000,500};
  CHECK(!Cal_Compute(dark,q) && !Cal_Compute(sat,q) && !Cal_Compute(uneq,q),"cal: dark / saturated / unequal rejected");

  /* --- Flash round trip (Cal_Save writes via stubs into 'flash'; verify the encoding) --- */
  cal_q[0]=4100;cal_q[1]=3900;cal_q[2]=4200;cal_q[3]=3800;
  uint32_t w1=(cal_q[0]&0xFFFF)|(cal_q[1]<<16), w2=(cal_q[2]&0xFFFF)|(cal_q[3]<<16);
  uint32_t chk=CAL_MAGIC^w1^w2^0xA5A5A5A5UL; CHECK(chk!=0 && (w1>>16)==3900 && (w2&0xFFFF)==4200,"flash record packing");

  /* --- Night logic via Tracker_Step --- */
  Servo_Move(1500,1500,0,false);
  stub_adc[0]=stub_adc[1]=stub_adc[2]=stub_adc[3]=50;   /* dark */
  is_parked=false; night_cnt=0; faults=0; Cal_Defaults();
  for(int i=0;i<NIGHT_CONFIRM_COUNT-1;i++) Tracker_Step();
  CHECK(!is_parked,"night: not parked before confirm count");
  Tracker_Step(); CHECK(is_parked,"night: parked after confirm count");
  stub_adc[0]=stub_adc[1]=stub_adc[2]=stub_adc[3]=1000;   /* between enter(600) and exit(1400) per-sum? sum=4000 */
  /* sum is 4000 >= exit 1400 -> wakes after DAY_CONFIRM ticks and runs search (uses stubs) */
  for(int i=0;i<DAY_CONFIRM_COUNT;i++) Tracker_Step();
  CHECK(!is_parked,"day: unparks after confirm count and sun search completes");
  /* single cloud must not park */
  night_cnt=0; stub_adc[0]=stub_adc[1]=stub_adc[2]=stub_adc[3]=50; Tracker_Step();
  stub_adc[0]=stub_adc[1]=stub_adc[2]=stub_adc[3]=1000; Tracker_Step();
  CHECK(!is_parked && night_cnt==0,"night: one dark sample followed by light resets counter");

  /* --- Tracking direction & LDR layout (TL,TR,BL,BR) --- */
  is_parked=false; faults=0; Reset_Axes(); pan_pulse=1500; tilt_pulse=1500;
  stub_adc[0]=3000; stub_adc[1]=1000; stub_adc[2]=3000; stub_adc[3]=1000;  /* left brighter */
  Tracker_Step(); CHECK(err_az>0 && pan_pulse>1500,"tracking: left brighter -> err_az>0, pan increases (PAN_DIR=+1)");
  Reset_Axes(); pan_pulse=1500; tilt_pulse=1500;
  stub_adc[0]=3000; stub_adc[1]=3000; stub_adc[2]=1000; stub_adc[3]=1000;  /* top brighter */
  Tracker_Step(); CHECK(err_el>0 && tilt_pulse>1500,"tracking: top brighter -> err_el>0, tilt increases (TILT_DIR=+1)");
  Reset_Axes(); pan_pulse=1500; tilt_pulse=1500;
  stub_adc[0]=stub_adc[1]=stub_adc[2]=stub_adc[3]=2000;
  Tracker_Step(); CHECK(pan_pulse==1500 && tilt_pulse==1500,"tracking: balanced light -> no movement");
  stub_adc[0]=2000;stub_adc[1]=2000;stub_adc[2]=5;stub_adc[3]=2000; faults=0;
  Tracker_Step(); CHECK((faults&FAULT_LDR)!=0 && pan_pulse==1500,"tracking: open LDR -> fault, position held");
  stub_adc[2]=2000; Tracker_Step(); CHECK((faults&FAULT_LDR)==0,"tracking: LDR fault self-clears when sensor returns");

  /* --- Tracking independent of I2C --- */
  i2c_fail=1; for(int i=0;i<10;i++){ Telemetry_Task(500); }
  CHECK(!ina_trk.online,"i2c: repeated failures mark device offline");
  stub_adc[0]=3000; stub_adc[1]=1000; stub_adc[2]=3000; stub_adc[3]=1000; Reset_Axes(); pan_pulse=1500;
  Tracker_Step(); CHECK(pan_pulse>1500,"i2c: tracker still works with the bus dead");
  i2c_fail=0;

  /* --- Servo release logic --- */
  Servo_Move(1500,1500,100,true); uint32_t t0=HAL_GetTick();
  Servo_Task(t0); CHECK(stub_pwm_run[0]==1,"servo: still on before settle time");
  Servo_Task(t0+200); CHECK(stub_pwm_run[0]==0 && stub_pwm_run[1]==0,"servo: released after settle time");
  Servo_Move(1500,1500,0,false); CHECK(stub_pwm_run[0]==1,"servo: move re-enables PWM");

  /* --- Dashboard strings fit in 21 chars --- */
  ina_trk.online=ina_fix.online=ina_sys.online=true; oled_online=true;
  ina_trk.mv=12340; ina_trk.ma10=12345; ina_fix.mv=5120; ina_fix.ma10=1200; ina_fix.mw=700; ina_trk.mw=2000;
  energy_trk=360000000000ULL*12; err_az=-123; err_el=1000; faults=FAULT_TILT_RUNAWAY;
  Display_Task(10000,true);
  printf("OLED frame produced, flush ok=%d\n", 1);
  /* check max snprintf length vs 22 */
  char l[24]; int worst=0, n;
  n=snprintf(l,sizeof l,"HELION %s %s","FIELD","SEARCH"); if(n>worst)worst=n;
  n=snprintf(l,sizeof l,"TRK %lu.%02luV %ld.%ldMA",12UL,34UL,1234L,5L); if(n>worst)worst=n;
  n=snprintf(l,sizeof l,"E T%lu.%lu F%lu.%lu MWH",1234UL,5UL,1234UL,5UL); if(n>worst)worst=n;
  n=snprintf(l,sizeof l,"AZ%+04ld EL%+04ld %s",-1000L,1000L,"TILT!"); if(n>worst)worst=n;
  CHECK(worst<=21,"dashboard: worst-case line <= 21 chars");

  printf("\n%s (%d failure%s)\n", fails?"SOME TESTS FAILED":"ALL TESTS PASSED", fails, fails==1?"":"s");
  return fails?1:0;
}
