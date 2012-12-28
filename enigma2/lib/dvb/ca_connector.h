#ifndef __lib_dvb_ca_connector_h
#define __lib_dvb_ca_connector_h

#include <linux/dvb/ca.h>
#include <lib/base/thread.h>
#include <netlink/netlink.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/ctrl.h>

#include <lib/gdi/gmaindc.h>
#include <lib/dvb/idvb.h>

// attributes
enum {
	ATTR_UNSPEC,
	ATTR_CA_SIZE,
	ATTR_CA_NUM,
	ATTR_CA_DESCR,
	ATTR_CA_PID,
        __ATTR_MAX,
};
#define ATTR_MAX (__ATTR_MAX - 1)

// commands
enum {
	CMD_UNSPEC,
	CMD_ASK_CA_SIZE,
	CMD_SET_CW,
	CMD_SET_PID,
	CMD_MAX,
};

class caConnector: public eThread, public Object
{
	DECLARE_REF(caConnector);
private:
	int ca_size;
	struct nla_policy ca_policy[ATTR_MAX + 1];
	struct nl_sock *sock;
	static caConnector *instance;

	virtual void thread();
	static int parse_cb(struct nl_msg *msg, void *arg);
	static caConnector *getInstance() { return instance; }
public:
	caConnector();
	~caConnector();
};

#endif
