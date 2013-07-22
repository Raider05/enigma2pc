/*
 * Based on "Softcam plugin to VDR (C++)"
 */

#include <lib/dvb/decsa.h>

static bool CheckNull(const unsigned char *data, int len)
{
  while(--len>=0)
    if(data[len])
      return false;
  return true;
}

cDeCSA::cDeCSA(int _adapter, int _demux)
  :stall(MAX_STALL_MS)
{
  adapter = _adapter;
  demux = _demux;

  cs=dvbcsa_bs_batch_size();
  cs_tsbbatch_even = (dvbcsa_bs_batch_s *) malloc((cs + 1) * sizeof(struct dvbcsa_bs_batch_s));
  cs_tsbbatch_odd = (dvbcsa_bs_batch_s *) malloc((cs + 1) * sizeof(struct dvbcsa_bs_batch_s));
  memset(cs_key_even, 0, sizeof(cs_key_even));
  memset(cs_key_odd, 0, sizeof(cs_key_odd));
  memset(pidmap, 0, sizeof(pidmap));

  ResetState();
}

cDeCSA::~cDeCSA()
{
  for(int i=0; i<MAX_CSA_IDX; i++) {
    if (cs_key_even[i])
      dvbcsa_bs_key_free(cs_key_even[i]);
    if (cs_key_odd[i])
      dvbcsa_bs_key_free(cs_key_odd[i]);
  }
  free(cs_tsbbatch_even);
  free(cs_tsbbatch_odd);
}

void cDeCSA::ResetState(void)
{
  printf("adapter%d/demux%d: reset state", adapter, demux);
  memset(even_odd,0,sizeof(even_odd));
  memset(flags,0,sizeof(flags));
//  memset(usedPids,0,sizeof(usedPids));
  lastData=0;
}

bool cDeCSA::GetKeyStruct(int idx)
{
  if (!cs_key_even[idx])
    cs_key_even[idx] = dvbcsa_bs_key_alloc();
  if (!cs_key_odd[idx])
    cs_key_odd[idx] = dvbcsa_bs_key_alloc();
  return (cs_key_even[idx] != 0) && (cs_key_odd[idx] != 0);
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
      dvbcsa_bs_key_set(ca_descr->cw, cs_key_even[idx]);

      if(!CheckNull(ca_descr->cw,8))
        flags[idx] |= FL_EVEN_GOOD|FL_ACTIVITY;
      else
        printf("adapter%d/demux%d idx %d: zero even CW\n", adapter, demux, idx);
      wait.Broadcast();
    } else {
      dvbcsa_bs_key_set(ca_descr->cw, cs_key_odd[idx]);

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

    pidmap[ca_pid->pid] = ca_pid->index;
    printf("adapter%d/demux%d idx %d: set pid %04x\n", adapter, demux, ca_pid->index, ca_pid->pid);
    //printf("adapter%d/demux%d idx %d: udedPids %d\n", adapter, demux, idx, usedPids[idx]);
  }

  return true;
}

unsigned char ts_packet_get_payload_offset(unsigned char *ts_packet)
{
  if (ts_packet[0] != TS_SYNC_BYTE)
    return 0;

  unsigned char adapt_field   = (ts_packet[3] &~ 0xDF) >> 5; // 11x11111
  unsigned char payload_field = (ts_packet[3] &~ 0xEF) >> 4; // 111x1111

  if (!adapt_field && !payload_field)     // Not allowed
    return 0;

  if (adapt_field)
  {
    unsigned char adapt_len = ts_packet[4];
    if (payload_field && adapt_len > 182) // Validity checks
      return 0;
    if (!payload_field && adapt_len > 183)
      return 0;
    if (adapt_len + 4 > TS_SIZE)  // adaptation field takes the whole packet
      return 0;
    return 4 + 1 + adapt_len;     // ts header + adapt_field_len_byte + adapt_field_len
  }
  else
  {
    return 4; // No adaptation, data starts directly after TS header
  }
}

bool cDeCSA::Decrypt(unsigned char *data, int len, int& packetsCount)
{
  cMutexLock lock(&mutex);
//  printf("Begin Decrypting %d\n", len);
  if (!cs_tsbbatch_even || !cs_tsbbatch_odd)
  {
    printf("Error allocating memory for DeCSA\n");
    return false;
  }

  int ccs = 0, currIdx=-1;
  int payload_len, offset;
  int cs_fill_even = 0;
  int cs_fill_odd = 0;
  len-=(TS_SIZE-1);
  int l;
  int packets=0;

  for(l=0; l<len; l+=TS_SIZE) {
    if (data[l] != TS_SYNC_BYTE)
    {                           // let higher level cope with that
      break;
    }
    unsigned int ev_od=data[l+3]&0xC0;
    if(ev_od==0x80 || ev_od==0xC0) { // encrypted
      offset = ts_packet_get_payload_offset(data + l);
      payload_len = TS_SIZE - offset;
      int idx=pidmap[((data[l+1]<<8)+data[l+2])&(MAX_CSA_PIDS-1)];
      if(currIdx<0 || idx==currIdx) { // same or no index
        data[l + 3] &= 0x3F;
        currIdx=idx;
        if(ccs == 0 && ev_od!=even_odd[idx]) {
//          if (cryptedPackets==0) {
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
//          } else {
//            break;
//          }
        }
      }
//      else
//        break;

      if (((ev_od & 0x40) >> 6) == 0) {
          cs_tsbbatch_even[cs_fill_even].data = &data[l + offset];
          cs_tsbbatch_even[cs_fill_even].len = payload_len;
          cs_fill_even++;
      } else {
          cs_tsbbatch_odd[cs_fill_odd].data = &data[l + offset];
          cs_tsbbatch_odd[cs_fill_odd].len = payload_len;
          cs_fill_odd++;
      }

      if(++ccs >= cs)
        break;
    }

    packets++;
  }

  if (GetKeyStruct(currIdx)) {
   if (cs_fill_even)
    {
      cs_tsbbatch_even[cs_fill_even].data = NULL;
      dvbcsa_bs_decrypt(cs_key_even[currIdx], cs_tsbbatch_even, 184);
      cs_fill_even = 0;
    }
    if (cs_fill_odd)
    {
      cs_tsbbatch_odd[cs_fill_odd].data = NULL;
      dvbcsa_bs_decrypt(cs_key_odd[currIdx], cs_tsbbatch_odd, 184);
      cs_fill_odd = 0;
    }
    stall.Set(MAX_STALL_MS);

  }

  packetsCount = packets;

  return true;
}


