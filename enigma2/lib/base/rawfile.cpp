#include <cstdio>
#include <unistd.h>
#include <fcntl.h>
#include <lib/base/rawfile.h>
#include <lib/base/eerror.h>

DEFINE_REF(eRawFile);

eRawFile::eRawFile(int packetsize)
	: iTsSource(packetsize)
	, m_lock(false)
	, m_fd(-1)
	, m_splitsize(0)
	, m_totallength(0)
	, m_current_offset(0)
	, m_base_offset(0)
	, m_last_offset(0)
	, m_nrfiles(0)
	, m_current_file(0)
	, m_fadvise_chunk(0)
{
}

eRawFile::~eRawFile()
{
	close();
}

int eRawFile::open(const char *filename)
{
	close();
	m_basename = filename;
	scan();
	m_current_offset = 0;
	m_last_offset = 0;
	m_fd = ::open(filename, O_RDONLY | O_LARGEFILE);
	posix_fadvise(m_fd, 0, 0, POSIX_FADV_SEQUENTIAL);
	return m_fd;
}

void eRawFile::setfd(int fd)
{
	close();
	m_nrfiles = 1;
	m_fd = fd;
}

off_t eRawFile::lseek_internal(off_t offset)
{
//	eDebug("lseek: %lld, %d", offset, whence);
		/* if there is only one file, use the native lseek - the file could be growing! */
	if (m_nrfiles < 2)
	{
		return ::lseek(m_fd, offset, SEEK_SET);
	}
	m_current_offset = offset;
	return m_current_offset;
}

int eRawFile::close()
{
	int ret = 0;
	if (m_fd >= 0)
	{
		posix_fadvise(m_fd, 0, 0, POSIX_FADV_DONTNEED);
		ret = ::close(m_fd);
		m_fd = -1;
	}
	return ret;
}

ssize_t eRawFile::read(off_t offset, void *buf, size_t count)
{
	eSingleLocker l(m_lock);

	if (offset != m_current_offset)
	{
		m_current_offset = lseek_internal(offset);
		if (m_current_offset < 0)
			return m_current_offset;
	}

	switchOffset(m_current_offset);

	if (m_nrfiles >= 2)
	{
		if (m_current_offset + count > m_totallength)
			count = m_totallength - m_current_offset;
		if (count < 0)
			return 0;
	}
	
	int ret;
	
	ret = ::read(m_fd, buf, count);

	if (ret > 0)
	{
		m_current_offset = m_last_offset += ret;
	}
	return ret;
}

int eRawFile::valid()
{
	return m_fd != -1;
}

void eRawFile::scan()
{
	m_nrfiles = 0;
	m_totallength = 0;
	while (m_nrfiles < 1000) /* .999 is the last possible */
	{
		int f = openFileUncached(m_nrfiles);
		if (f < 0)
			break;
		if (!m_nrfiles)
			m_splitsize = ::lseek(f, 0, SEEK_END);
		m_totallength += ::lseek(f, 0, SEEK_END);
		::close(f);
		++m_nrfiles;
	}
//	eDebug("found %d files, splitsize: %llx, totallength: %llx", m_nrfiles, m_splitsize, m_totallength);
}

int eRawFile::switchOffset(off_t off)
{
	if (m_splitsize)
	{
		int filenr = off / m_splitsize;
		if (filenr >= m_nrfiles)
			filenr = m_nrfiles - 1;
		if (filenr != m_current_file)
		{	
//			eDebug("-> %d", filenr);
			close();
			m_fd = openFileUncached(filenr);
			m_last_offset = m_base_offset = m_splitsize * filenr;
			m_current_file = filenr;
		}
	} else
		m_base_offset = 0;
	
	if (off != m_last_offset)
	{
		m_last_offset = ::lseek(m_fd, off - m_base_offset, SEEK_SET) + m_base_offset;
		return m_last_offset;
	} else
	{
		return m_last_offset;
	}
}

int eRawFile::openFileUncached(int nr)
{
	std::string filename = m_basename;
	if (nr)
	{
		char suffix[5];
		snprintf(suffix, 5, ".%03d", nr);
		filename += suffix;
	}
	return ::open(filename.c_str(), O_RDONLY | O_LARGEFILE);
}

off_t eRawFile::length()
{
	if (m_nrfiles >= 2)
	{
		return m_totallength;
	}
	else
	{
		struct stat st;
		if (::fstat(m_fd, &st) < 0)
			return -1;
		return st.st_size;
	}
}

off_t eRawFile::offset()
{
	return m_last_offset;
}

#define KILOBYTE(n) ((n) * 1024)
#define MEGABYTE(n) ((n) * 1024LL * 1024LL)
#define AUDIO_STREAM_S   0xC0
#define AUDIO_STREAM_E   0xDF
#define VIDEO_STREAM_S   0xE0
#define VIDEO_STREAM_E   0xEF

eDecryptRawFile::eDecryptRawFile(int packetsize)
 : eRawFile(packetsize)
{
	ringBuffer = new cRingBufferLinear(KILOBYTE(2024),TS_SIZE,true,"IN-TS");
	ringBuffer->SetTimeouts(200,200);
	bs_size = dvbcsa_bs_batch_size();
	delivered=false;
	lastPacketsCount = 0;
	stream_correct = false;
}

eDecryptRawFile::~eDecryptRawFile()
{
	delete ringBuffer;
}

void eDecryptRawFile::setDemux(ePtr<eDVBDemux> _demux) {
	demux = _demux;
}

uint8_t* eDecryptRawFile::getPackets(int &packetsCount) {
	int Count=0;
	if(delivered) {
		ringBuffer->Del(lastPacketsCount*TS_SIZE);
		delivered=false;
	}
	packetsCount = 0;

	if (ringBuffer->Available()<bs_size*TS_SIZE)
		return NULL;

	uint8_t* p=ringBuffer->Get(Count);
	if (Count>KILOBYTE(64))
		Count = KILOBYTE(64);

	if(p && Count>=TS_SIZE) {
		if(*p!=TS_SYNC_BYTE) {
			for(int i=1; i<Count; i++)
				if(p[i]==TS_SYNC_BYTE &&
						(i+TS_SIZE==Count || (i+TS_SIZE>Count && p[i+TS_SIZE]==TS_SYNC_BYTE)) ) {
					Count=i;
					break;
				}
				ringBuffer->Del(Count);
				eDebug("ERROR: skipped %d bytes to sync on TS packet", Count);
				return NULL;
		}
		if(!demux->decrypt(p, Count, packetsCount)) {
			cCondWait::SleepMs(20);
			return NULL;
		}
		lastPacketsCount = packetsCount;
		delivered=true;
		return p;
	}

	return NULL;
}

ssize_t eDecryptRawFile::read(off_t offset, void *buf, size_t count)
{
	eSingleLocker l(m_lock);
	int ret = 0;

	while (ringBuffer->Available()<KILOBYTE(128)) {
		ret = ringBuffer->Read(m_fd, KILOBYTE(64));
		if (ret<0)
			break;
	}

	int packetsCount = 0;

	uint8_t *data = getPackets(packetsCount);

	ret = -EBUSY;
	if (data && packetsCount>0) {
		if (!stream_correct) {
			for (int i=0;i<packetsCount;i++) {
				unsigned char* packet = data+i*TS_SIZE;
				int adaptation_field_exist = (packet[3]&0x30)>>4;
				unsigned char* wsk;
				int len;

				if (adaptation_field_exist==3) {
					wsk = packet+5+packet[4];
					len = 183-packet[4];
				} else {
					wsk = packet+4;
					len = 184;
				}

				if (len>4 && wsk[0]==0 && wsk[1]==0 && wsk[2]==1
						&& ((wsk[3]>=VIDEO_STREAM_S && wsk[3]<=VIDEO_STREAM_E)
						|| (wsk[3]>=AUDIO_STREAM_S && wsk[3]<=AUDIO_STREAM_E)) ) {
					stream_correct = true;
					printf("-------------------- I have PES ---------------------- %02X\n", wsk[3]);
					ret = (packetsCount-i)*TS_SIZE;
					memcpy(buf, packet, (packetsCount-i)*TS_SIZE);
					break;
				}
			}
          	} else {
			ret = packetsCount*TS_SIZE;
			memcpy(buf, data, ret);
		}
	}

	return ret;
}

