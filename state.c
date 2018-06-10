// Simple simulation of a compressed state vector transmission

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

// If you set this to 1,  you will get the "manual" test driver
// If you set to 0 you will get the random test driver
#define TEST_MANUAL 0

typedef struct
{
  short topic;
  int data;
} RECORD;

// # of state records
#define RECORDS 1000
// Max # of nonrepeating characters in a packet
#define WORKSIZE 16  

// Note: Topic 0 is sequence number topic -1 is EOF.

RECORD *state;  // transmitter's state
RECORD *target; // receiver's state


RECORD *tx_prev;  // last frame for tx

RECORD *rx_prev;  // last frame for rx


typedef uint8_t CBUFREC[(RECORDS)*sizeof(RECORD)];

// Do we want to do xor or not?
int xorflag=1;

// statistic counter
int rxcount=0;
int txcount=0;

void receive(short run, unsigned char *data)
{
  

  
  static int first=1;  // need to set this if you get out of sync which can't happen here

  // this is our current pointer to the receive packet so it needs to be static
  static unsigned char *rxp=NULL;
  // pointer to the XOR data source
  unsigned char *rxpd;

  int i;
  

  // Init rxp if needed
  if (!rxp) rxp=(unsigned char *)target;
  // Compute rxpd every time
  rxpd=(unsigned char *) rx_prev;
  rxpd+=rxp-(unsigned char *)target;
  
  // Run==0 is end of state data
  // so save the current record as the XOR
  // source for next time and done
  if (run==0)
    {
      memcpy(rx_prev,target,sizeof(CBUFREC));
      rxp=NULL;
      first=0;
    }
  
    else
    {
#if 0  // dumb debug code
  if (run)
    {
      int i=0;
      short r=run;
      printf("DEBUG: %d, ",run);
      if (run<0) printf("%02X",*data);
      else while(r--) printf("%02X ",data[i++]);
      printf("\n");
      
      
    }
  #endif

    
      if (run<0)  // repeating 
	{
	  rxcount+=2;  // we heard 2 bytes
	  // expand it
	  memset(rxp,*data,(-run)+1);
	  // do the XOR unless first time
	  if (!first&&xorflag) for (i=0;i<(-run)+1;i++) rxp[i]^=rxpd[i];
	  // Adjust pointer
	  rxp+=(-run)+1;
	}
      else
	{

	  // How many bytes we just heard
	  rxcount+=run+1;
	  // move data
	  memcpy(rxp,data,run);
	  // do the XOR unless first time
	  if (!first&&xorflag) for (i=0;i<run;i++) rxp[i]^=rxpd[i];
	  // Adjust pointer
	  rxp+=run;
	}
    }
}

// simulated transmit function
void xmit(short run, unsigned char *data)
{
  receive(run,data);
}



// large, so make on heap
CBUFREC tx;

void transmit()
{
  // Note: work is never null terminated
  // so don't need the extra byte
  unsigned char work[WORKSIZE];
  static int first=1;
  int i=0;
  int mode=0;  // 0 is neutral -n is accumulating length, +n is accumulating a count
  int localxorflag;
  // the character in question and the
  // previous character
  int8_t curr, prev=-1;
  // pointer to where we are in the state vector
  uint8_t *p=(uint8_t *)state;
  // Pointer to the XOR source data
  uint8_t *p0=(uint8_t *)tx_prev;

  // Is previous empty?
  int empty=1;
  
  // in truth, we could xor all data
  // because tx_prev is all zeros first time
  // but in a real life situation
  // you could need to force a full resend
  // so I made this a flag

  localxorflag=!first&xorflag;
  first=0;

  // XOR processing happens here

  // Here we copy the state vector
  // to tx and XOR it (or not)
  // After we copy a byte, we also save
  // it in the XOR state (p0/tx_next) for next time
  // Since we are done with that part of tx_next
  // for the rest of the loop
  for (i=0;i<sizeof(CBUFREC);i++)
    {
      tx[i]=p[i]^(localxorflag?p0[i]:0);
      p0[i]=p[i];  // save for next time
    }
  i=0;  // reset i just for good habit
  
  // Compression happens here
  
  // Now for each byte
  int j;
  for (j=0;j<sizeof(CBUFREC);j++) 
    {
      curr=tx[j];
      if (mode==0)  // we don't know if we are accumulating or counting yet
	{
	  if (empty)  // queue is empty so fill it
	    {
	      empty=0;
	      prev=*p;
	      continue;
	    }
	}

      // if we are going to start counting or continue counting
      if (curr==prev && mode<=0)
	{
	  mode--;
	  // limit count to 255
	  if (mode>-255) continue;
	  mode++;
	}
      // if we are accumulating
      if (curr!=prev && mode>=0)
	{
	  // build accumulation in work[]
	  work[i++]=prev;
	  prev=curr;
	  mode++;
	  // we limit how many are in a run
	  // for no great reason
	  if (i<WORKSIZE) continue;  // do up to 16
	}

      // if we get here we either have a duplicate while accumulating
      // or we have a change while we are counting
      // if counting
      if (mode<0)
	{
	  // emit count
	  unsigned char buf[2];
	  buf[0]=prev;
	  xmit(mode,buf);
	  mode=0;
	  prev=curr;  // remember new byte
	  i=0;
	  continue;
	}
      else // we either have the end of a run or a full packet here
	{
	  if (i==WORKSIZE)  // full packet
	  {
	    xmit(mode,work);
	    // set up for next
	    mode=0;
	    i=0;
	    empty=1;
	    continue;
	  }
	else // end of run of unrelated
	  {
	    xmit(mode,work);
	    // set up for next
	    mode=-1;  
	    prev=curr;  // should be superfulous
	    i=0;
	    empty=0;
	    continue;
	  }
	}
    }
  // Ok we are done except for whatever
  // we have left over in the count/accumumlate
  if (mode)  // write whatever is left
    {
      if (mode<0)
	{
	  // last repeat
	  unsigned char tmp[2];
	  tmp[0]=prev;
	  xmit(mode,tmp);
	}
      else
	{
	  // last run
	  xmit(mode,work);
	}

    }
  
  // Send end marker
  xmit(0,0);  // end of state
}

// Dump things so we can watch
// The test setups only do 4 items so
// that's all we dump (change with MAXDUMP)

#define MAXDUMP 10

void _debugdump(RECORD *r, const char *tag)
{
  int i;
  printf("*****%s\n",tag);
  for (i=0;i<MAXDUMP;i++)
    printf("%d - %02X\n",r[i].topic,r[i].data);
  i=800;
  printf("%d - %02X\n",r[i].topic,r[i].data);
  i=999;
  printf("%d - %02X\n",r[i].topic,r[i].data);
  printf("*****\n");
}

void debugdump()
{
  _debugdump(state,"xmit");
}

void debugtarget()
{
  _debugdump(target,"rx");
}



#if TEST_MANUAL
// These are just test states
// might replace them with random
void setstate0()
{
  state[0].topic=0;
  state[0].data=0;
  state[1].topic=2;
  state[1].data=55;
  state[2].topic=8;
  state[2].data=310;
  state[3].topic=99;
  state[3].data=0;
  state[800].topic=800;
  state[800].data=801;
  state[999].topic=9999;
  state[999].data=-9999;
}

void setstate1()
{
  state[0].topic=0;
  state[0].data=1;
  state[1].topic=2;
  state[1].data=55;
  state[2].topic=8;
  state[2].data=311;
  state[3].topic=99;
  state[3].data=77;
  state[800].topic=801;
  state[800].data=799;
  state[999].topic=9999;
  state[999].data=9999;
}




void test(unsigned seed)
{
  // set up initial state
  setstate0();
  debugdump(); // view it
  
  transmit();  // send it
  txcount+=sizeof(CBUFREC);
  printf("TX/RX=%d/%d\n",txcount,rxcount);
  // look at result
  debugtarget();
  if (memcmp(state,target,sizeof(CBUFREC));
      printf("Warning! Mismatch!\n");
      else printf("Pass\n");
      


  // repeat
    setstate1();
    debugdump();
    transmit();
      txcount+=sizeof(CBUFREC);
    printf("TX/RX=%d/%d\n",txcount,rxcount);
    debugtarget();
      if (memcmp(state,target,sizeof(CBUFREC)))
	printf("Warning! Mismatch!\n");
      else printf("Pass\n");



}
#else

void randomize()
{
  int n=rand()%20;  // change up to 20 items
  while (n--)
    {
      double frac;
      int slot,t,d;
      frac=(double)rand()/RAND_MAX;
      slot=(RECORDS-1)*frac+1;
      t=(rand()&0xFFFF)+1;
      d=rand()&0xFFFF;
      if (slot>=1000) printf("DEBUG: Bad slot %d\n",slot);
      state[slot].topic=t;
      state[slot].data=d;
    }
  
   
}

  
// This test driver can create random samples
// If you pass a seed to main, it will produce the same random samples
// Which is nice for testing.
// If you put a ^ in front of the seed you will turn off the xor processing
// This will usually result in a bigger result because the overhead will
// explode the output (110% is common)
void test(unsigned seed)
{
  int i;
  float percent;
  srand(seed);

  // init (just to make sure
  state[0].topic=0;
  state[0].data=0;
  transmit();  // initial send
  
  for (i=1;i<RECORDS;i++)
  // do 100 tests
  for (i=0;i<1000;i++)
    {
      randomize();
      state[0].data++;
      transmit();
      txcount+=sizeof(CBUFREC);
      if (memcmp(state,target,sizeof(CBUFREC)))
	printf("Warning! Mismatch!\n");
    }

  percent=rxcount;
  percent/=txcount;
  percent*=100;
  printf("TX/RX=%d/%d (%2.2f)\n",txcount,rxcount,percent);
  printf("Done\n");
}

  


#endif

int main(int argc, char *argv[])
{
  state=malloc(RECORDS*sizeof(RECORD));
  if (!state) return -1;
  target=malloc(RECORDS*sizeof(RECORD));
  if (!target) return -1;

  tx_prev=malloc(RECORDS*sizeof(RECORD));
  if (!tx_prev) return -1;
  rx_prev=malloc(RECORDS*sizeof(RECORD));
  if (!rx_prev) return -1;

  // should have used calloc
  memset(target,0,RECORDS*sizeof(RECORD));
  memset(tx_prev,0,RECORDS*sizeof(RECORD));
  memset(rx_prev,0,RECORDS*sizeof(RECORD)); // in case you want to xor with it anyway

  unsigned seed;
  // ugly but not production code so...
  if (argc==1) seed=time(0); else
    {
      if (argv[1][0]=='^')
	{
	  xorflag=0;
	  argv[1]++;
	}
      seed=atoi(argv[1]);
    }
  

  test(seed);
  
  free(state);
  free(target);
  free(tx_prev);
  free(rx_prev);
  
  
  
}


 
