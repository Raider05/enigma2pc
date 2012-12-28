/*
 * tsRingbuffer.cpp: A ring buffer
 *
 * Video Disk Recorder
 * Copyright (C) 2000, 2003, 2006, 2008 Klaus Schmidinger
 * The author can be reached at kls@tvdr.de
 * The project's page is at http://www.tvdr.de
 *
 */

#include <lib/base/tsRingbuffer.h>
#include <stdlib.h>
#include <unistd.h>


// --- cRingBuffer -----------------------------------------------------------

#define OVERFLOWREPORTDELTA 5 // seconds between reports
#define PERCENTAGEDELTA     10
#define PERCENTAGETHRESHOLD 70

cRingBuffer::cRingBuffer(int Size, bool Statistics)
{
  size = Size;
  statistics = Statistics;
  maxFill = 0;
  lastPercent = 0;
  putTimeout = getTimeout = 0;
  lastOverflowReport = 0;
  overflowCount = overflowBytes = 0;
}

cRingBuffer::~cRingBuffer()
{
  if (statistics)
     printf("buffer stats: %d (%d%%) used\n", maxFill, maxFill * 100 / (size - 1));
}

void cRingBuffer::UpdatePercentage(int Fill)
{
  if (Fill > maxFill)
     maxFill = Fill;
  int percent = Fill * 100 / (Size() - 1) / PERCENTAGEDELTA * PERCENTAGEDELTA; // clamp down to nearest quantum
  if (percent != lastPercent) {
    if ((percent >= PERCENTAGETHRESHOLD && percent > lastPercent)
        || (percent < PERCENTAGETHRESHOLD && lastPercent >= PERCENTAGETHRESHOLD)) {
      printf("buffer usage: %d%%\n", percent);
      lastPercent = percent;
    }
  }
}

void cRingBuffer::WaitForPut(void)
{
  if (putTimeout)
     readyForPut.Wait(putTimeout);
}

void cRingBuffer::WaitForGet(void)
{
  if (getTimeout)
     readyForGet.Wait(getTimeout);
}

void cRingBuffer::EnablePut(void)
{
  if (putTimeout && Free() > Size() / 3)
     readyForPut.Signal();
}

void cRingBuffer::EnableGet(void)
{
  if (getTimeout && Available() > Size() / 3)
     readyForGet.Signal();
}

void cRingBuffer::SetTimeouts(int PutTimeout, int GetTimeout)
{
  putTimeout = PutTimeout;
  getTimeout = GetTimeout;
}

void cRingBuffer::ReportOverflow(int Bytes)
{
  overflowCount++;
  overflowBytes += Bytes;
  if (time(NULL) - lastOverflowReport > OVERFLOWREPORTDELTA) {
     printf("ERROR: %d ring buffer overflow%s (%d bytes dropped)\n", overflowCount, overflowCount > 1 ? "s" : "", overflowBytes);
     overflowCount = overflowBytes = 0;
     lastOverflowReport = time(NULL);
     }
}

// --- cRingBufferLinear -----------------------------------------------------

#ifdef DEBUGRINGBUFFERS
#define MAXRBLS 30
#define DEBUGRBLWIDTH 45

cRingBufferLinear *cRingBufferLinear::RBLS[MAXRBLS] = { NULL };

void cRingBufferLinear::AddDebugRBL(cRingBufferLinear *RBL)
{
  for (int i = 0; i < MAXRBLS; i++) {
      if (!RBLS[i]) {
         RBLS[i] = RBL;
         break;
         }
      }
}

void cRingBufferLinear::DelDebugRBL(cRingBufferLinear *RBL)
{
  for (int i = 0; i < MAXRBLS; i++) {
      if (RBLS[i] == RBL) {
         RBLS[i] = NULL;
         break;
         }
      }
}

void cRingBufferLinear::PrintDebugRBL(void)
{
  bool printed = false;
  for (int i = 0; i < MAXRBLS; i++) {
      cRingBufferLinear *p = RBLS[i];
      if (p) {
         printed = true;
         int lh = p->lastHead;
         int lt = p->lastTail;
         int h = lh * DEBUGRBLWIDTH / p->Size();
         int t = lt * DEBUGRBLWIDTH / p->Size();
         char buf[DEBUGRBLWIDTH + 10];
         memset(buf, '-', DEBUGRBLWIDTH);
         if (lt <= lh)
            memset(buf + t, '*', max(h - t, 1));
         else {
            memset(buf, '*', h);
            memset(buf + t, '*', DEBUGRBLWIDTH - t);
            }
         buf[t] = '<';
         buf[h] = '>';
         buf[DEBUGRBLWIDTH] = 0;
         printf("%2d %s %8d %8d %s\n", i, buf, p->lastPut, p->lastGet, p->description);
         }
      }
  if (printed)
     printf("\n");
  }
#endif

cRingBufferLinear::cRingBufferLinear(int Size, int Margin, bool Statistics, const char *Description)
:cRingBuffer(Size, Statistics)
{
  description = Description ? strdup(Description) : NULL;
  tail = head = margin = Margin;
  gotten = 0;
  buffer = NULL;
  if (Size > 1) { // 'Size - 1' must not be 0!
     if (Margin <= Size / 2) {
        buffer = (uint8_t*)malloc(sizeof(uint8_t) * Size);
        if (!buffer)
           printf("ERROR: can't allocate ring buffer (size=%d)\n", Size);
        Clear();
        }
     else
        printf("ERROR: invalid margin for ring buffer (%d > %d)\n", Margin, Size / 2);
     }
  else
     printf("ERROR: invalid size for ring buffer (%d)\n", Size);
#ifdef DEBUGRINGBUFFERS
  lastHead = head;
  lastTail = tail;
  lastPut = lastGet = -1;
  AddDebugRBL(this);
#endif
}

cRingBufferLinear::~cRingBufferLinear()
{
#ifdef DEBUGRINGBUFFERS
  DelDebugRBL(this);
#endif
  free(buffer);
  free(description);
}

int cRingBufferLinear::DataReady(const uint8_t *Data, int Count)
{
  return Count >= margin ? Count : 0;
}

int cRingBufferLinear::Available(void)
{
  int diff = head - tail;
  return (diff >= 0) ? diff : Size() + diff - margin;
}

void cRingBufferLinear::Clear(void)
{
  tail = head = margin;
#ifdef DEBUGRINGBUFFERS
  lastHead = head;
  lastTail = tail;
  lastPut = lastGet = -1;
#endif
  maxFill = 0;
  EnablePut();
}

int cRingBufferLinear::Read(int FileHandle, int Max)
{
  int Tail = tail;
  int diff = Tail - head;
  int free = (diff > 0) ? diff - 1 : Size() - head;
  if (Tail <= margin)
     free--;
  int Count = -1;
  errno = EAGAIN;

  if (free > 0) {
     if (0 < Max && Max < free)
        free = Max;
     Count = read(FileHandle, buffer + head, free);
     if (Count > 0) {
        int Head = head + Count;
        if (Head >= Size())
           Head = margin;
        head = Head;
        if (statistics) {
           int fill = head - Tail;
           if (fill < 0)
              fill = Size() + fill;
           else if (fill >= Size())
              fill = Size() - 1;
           UpdatePercentage(fill);
           }
        }
     }
#ifdef DEBUGRINGBUFFERS
  lastHead = head;
  lastPut = Count;
#endif
  EnableGet();
  if (free == 0)
     WaitForPut();
  return Count;
}

int cRingBufferLinear::Put(const uint8_t *Data, int Count)
{
  if (Count > 0) {
     int Tail = tail;
     int rest = Size() - head;
     int diff = Tail - head;
     int free = ((Tail < margin) ? rest : (diff > 0) ? diff : Size() + diff - margin) - 1;
     if (statistics) {
        int fill = Size() - free - 1 + Count;
        if (fill >= Size())
           fill = Size() - 1;
        UpdatePercentage(fill);
        }
     if (free > 0) {
        if (free < Count)
           Count = free;
        if (Count >= rest) {
           memcpy(buffer + head, Data, rest);
           if (Count - rest)
              memcpy(buffer + margin, Data + rest, Count - rest);
           head = margin + Count - rest;
           }
        else {
           memcpy(buffer + head, Data, Count);
           head += Count;
           }
        }
     else
        Count = 0;
#ifdef DEBUGRINGBUFFERS
     lastHead = head;
     lastPut = Count;
#endif
     EnableGet();
     if (Count == 0)
        WaitForPut();
     }
  return Count;
}

uint8_t *cRingBufferLinear::Get(int &Count)
{
  int Head = head;
  int rest = Size() - tail;
  if (rest < margin && Head < tail) {
     int t = margin - rest;
     memcpy(buffer + t, buffer + tail, rest);
     tail = t;
     rest = Head - tail;
     }
  int diff = Head - tail;
  int cont = (diff >= 0) ? diff : Size() + diff - margin;
  if (cont > rest)
     cont = rest;
  uint8_t *p = buffer + tail;

  if ((cont = DataReady(p, cont)) > 0) {
     Count = gotten = cont;
     return p;
     }
  WaitForGet();
  return NULL;
}

void cRingBufferLinear::Del(int Count)
{
  if (Count > gotten) {
     printf("ERROR: invalid Count in cRingBufferLinear::Del: %d (limited to %d)\n", Count, gotten);
     Count = gotten;
     }
  if (Count > 0) {
     int Tail = tail;
     Tail += Count;
     gotten -= Count;
     if (Tail >= Size())
        Tail = margin;
     tail = Tail;
     EnablePut();
     }
#ifdef DEBUGRINGBUFFERS
  lastTail = tail;
  lastGet = Count;
#endif
}


