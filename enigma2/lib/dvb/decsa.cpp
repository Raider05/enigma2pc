/*
 * Based on "Softcam plugin to VDR (C++)"
 */

#include <lib/dvb/decsa.h>

cDeCSA::cDeCSA(int _adapter, int _demux) {
  adapter = _adapter;
  demux = _demux;

  cs=dvbcsa_bs_batch_size();
  pcks = new dvbcsa_bs_batch_s[cs+1];

  for (int i=0; i<=cs; i++) {
    pcks[i].data = NULL;
  }

  memset(csa_bs_key_even,0,sizeof(csa_bs_key_even));
  memset(csa_bs_key_odd,0,sizeof(csa_bs_key_odd));
  memset(pidmap,0,sizeof(pidmap));
  ResetState();
}

cDeCSA::~cDeCSA()
{
  for(int i=0; i<MAX_CSA_IDX; i++) {
    if(csa_bs_key_even[i])
      dvbcsa_bs_key_free(csa_bs_key_even[i]);
    if(csa_bs_key_odd[i])
      dvbcsa_bs_key_free(csa_bs_key_odd[i]);
  }
}

void cDeCSA::ResetState(void)
{
  printf("adapter%d/demux%d: reset state", adapter, demux);
  memset(even_odd,0,sizeof(even_odd));
  memset(flags,0,sizeof(flags));
  memset(usedPids,0,sizeof(usedPids));
  lastData=0;
}

static bool CheckNull(const unsigned char *data, int len)
{
  while(--len>=0)
    if(data[len])
      return false;
  return true;
}

bool cDeCSA::GetKeyStruct(int idx)
{
  if(!csa_bs_key_even[idx])
    csa_bs_key_even[idx] = dvbcsa_bs_key_alloc();
  if(!csa_bs_key_odd[idx])
    csa_bs_key_odd[idx] = dvbcsa_bs_key_alloc();

  return (csa_bs_key_even[idx]!=0 && csa_bs_key_odd[idx]!=0);
}

bool cDeCSA::SetDescr(ca_descr_t *ca_descr, bool initial)
{
  cMutexLock lock(&mutex);

  int idx=ca_descr->index;
  if(idx<MAX_CSA_IDX && GetKeyStruct(idx)) {
    if(!initial && ca_descr->parity==(even_odd[idx]&0x40)>>6) {
      if(flags[idx] & (ca_descr->parity?FL_ODD_GOOD:FL_EVEN_GOOD)) {
        printf("adapter%d/demux%d idx %d: %s key in use (%d ms)", adapter, demux ,idx,ca_descr->parity?"odd":"even",MAX_REL_WAIT);
        if(wait.TimedWait(mutex,MAX_REL_WAIT))
          printf("adapter%d/demux%d idx %d: successfully waited for release\n", adapter, demux, idx);
        else
          printf("adapter%d/demux%d idx %d: timed out. setting anyways\n", adapter, demux, idx);
      }
      else
        printf("adapter%d/demux%d idx %d: late key set...\n", adapter, demux, idx);
    }
    if(ca_descr->parity==0) {
      dvbcsa_bs_key_set(ca_descr->cw, csa_bs_key_even[idx]);

      if(!CheckNull(ca_descr->cw,8))
        flags[idx] |= FL_EVEN_GOOD|FL_ACTIVITY;
      else
        printf("adapter%d/demux%d idx %d: zero even CW\n", adapter, demux, idx);
      wait.Broadcast();
    } else {
      dvbcsa_bs_key_set(ca_descr->cw, csa_bs_key_odd[idx]);

      if(!CheckNull(ca_descr->cw,8))
        flags[idx] |= FL_ODD_GOOD|FL_ACTIVITY;
      else
        printf("adapter%d/demux%d idx%d: zero odd CW\n", adapter, demux, idx);
      wait.Broadcast();
    }
  }

  return true;
}

bool cDeCSA::SetCaPid(ca_pid_t *ca_pid)
{
  cMutexLock lock(&mutex);

  if(ca_pid->index<MAX_CSA_IDX && ca_pid->pid<MAX_CSA_PIDS) {
    int idx;

    if (ca_pid->index!=-1) {
      idx = ca_pid->index;
      usedPids[idx]++;
    } else {
      idx = pidmap[ca_pid->pid];
      usedPids[idx]--;
      if (usedPids[idx]==0) {
        even_odd[idx] = 0;
        flags[idx] = 0;
      }
    }

    pidmap[ca_pid->pid]=ca_pid->index;
    //printf("adapter%d/demux%d idx %d: set pid %04x\n", adapter, demux, ca_pid->index, ca_pid->pid);
    //printf("adapter%d/demux%d idx %d: udedPids %d\n", adapter, demux, idx, usedPids[idx]);
  }

  return true;
}

bool cDeCSA::Decrypt(unsigned char *data, int len, int& packetsCount)
{
  cMutexLock lock(&mutex);
//  printf("Begin Decrypting %d\n", len);
  int currIdx=-1;
  len-=(TS_SIZE-1);
  int l;
  int packets=0, cryptedPackets=0;

  for(l=0; l<len && cryptedPackets<cs; l+=TS_SIZE) {
    unsigned int ev_od=data[l+3]&0xC0;
    int adaptation_field_exist = (data[l+3]&0x30)>>4;

    if((ev_od==0x80 || ev_od==0xC0) && adaptation_field_exist!=2) { // encrypted
      int idx=pidmap[((data[l+1]<<8)+data[l+2])&(MAX_CSA_PIDS-1)];
      if(currIdx<0 || idx==currIdx) { // same or no index
        currIdx=idx;
        if(ev_od!=even_odd[idx]) {
          if (cryptedPackets==0) {
            even_odd[idx]=ev_od;
            wait.Broadcast();
            printf("adapter%d/demux%d idx %d: change to %s key\n", adapter, demux, idx, (ev_od&0x40)?"odd":"even");
          
            bool doWait=false;
            if(ev_od&0x40) {
              flags[idx]&=~FL_EVEN_GOOD;
              if(!(flags[idx]&FL_ODD_GOOD)) doWait=true;
            }
            else {
              flags[idx]&=~FL_ODD_GOOD;
              if(!(flags[idx]&FL_EVEN_GOOD)) doWait=true;
            }
            if(doWait) {
              printf("adapter%d/demux%d idx %d: %s key not ready (%d ms)\n",
                  adapter, demux, idx, (ev_od&0x40)?"odd":"even", MAX_KEY_WAIT);
              if(flags[idx]&FL_ACTIVITY) {
                flags[idx]&=~FL_ACTIVITY;
                if(wait.TimedWait(mutex,MAX_KEY_WAIT))
                  printf("adapter%d/demux%d idx %d: successfully waited for key\n", adapter, demux, idx);
                else
                  printf("adapter%d/demux%d idx %d: timed out. proceeding anyways\n", adapter, demux, idx);
              } else
                printf("adapter%d/demux%d idx %d: not active. wait skipped\n", adapter, demux, idx);
            }
          } else {
            break;
          }
        }
      }
      else
        break;

      if (adaptation_field_exist==1) {
        pcks[cryptedPackets].data = data+l+4;
        pcks[cryptedPackets].len = 184;
      } else if (adaptation_field_exist==3) {
        pcks[cryptedPackets].data = data+l+5+data[l+4];
        pcks[cryptedPackets].len = 183-data[l+4];
      }
      cryptedPackets++;
    }

    data[l+3] &= 0x3F;
    packets++;
  }

  if (cryptedPackets>0) {
//  	printf("Begin Decrypting cryptedPackets %d\n",cryptedPackets);
    for (int i=cryptedPackets;i<=cs;i++) {
      pcks[i].data = NULL;
    }

    if (GetKeyStruct(currIdx)) {
//    	printf("Begin Decrypting GetKeyStruct %d\n",currIdx);
      if (even_odd[currIdx]&0x40) {
        dvbcsa_bs_decrypt(csa_bs_key_odd[currIdx], pcks, 184);
      } else {
        dvbcsa_bs_decrypt(csa_bs_key_even[currIdx], pcks, 184);
      }
    }
  }

  packetsCount = packets;

  return true;
}


