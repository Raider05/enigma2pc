#ifndef __dvb_decsa_h
#define __dvb_decsa_h

#include <linux/dvb/ca.h>
#include <lib/base/condVar.h>

extern "C" {
#include <dvbcsa/dvbcsa.h>
}

#define TS_SIZE          188
#define TS_SYNC_BYTE     0x47

#define MAX_REL_WAIT 100 // time to wait if key in used on set
#define MAX_KEY_WAIT 500 // time to wait if key not ready on change
#define MAX_STALL_MS 70

#define MAX_CSA_PIDS 8192
#define MAX_CSA_IDX  16
#define FL_EVEN_GOOD 1
#define FL_ODD_GOOD  2
#define FL_ACTIVITY  4

class cDeCSA {
private:
  int cs;
  unsigned char *lastData;
  unsigned char pidmap[MAX_CSA_PIDS];
  unsigned int even_odd[MAX_CSA_IDX], flags[MAX_CSA_IDX], usedPids[MAX_CSA_IDX];
  cMutex mutex;
  cCondVar wait;
  cTimeMs stall;
  int adapter, demux;
  struct dvbcsa_bs_key_s *cs_key_even[MAX_CSA_IDX];
  struct dvbcsa_bs_key_s *cs_key_odd[MAX_CSA_IDX];
  struct dvbcsa_bs_batch_s *cs_tsbbatch_even;
  struct dvbcsa_bs_batch_s *cs_tsbbatch_odd;

  bool GetKeyStruct(int idx);
  void ResetState(void);
public:
  cDeCSA(int _adapter, int _demux);
  ~cDeCSA();
  bool Decrypt(unsigned char *data, int len, int& packetsCount);

  bool SetDescr(ca_descr_t *ca_descr, bool initial);
  bool SetCaPid(ca_pid_t *ca_pid);
};

#endif
