#include <lib/dvb/ca_connector.h>
#include <lib/dvb/dvb.h>
#include <lib/actions/action.h>
#include <lib/base/init.h>
#include <lib/base/init_num.h>

caConnector *caConnector::instance;

DEFINE_REF(caConnector)

caConnector::caConnector()
{
	int family;
	struct nl_msg *msg;
	
	sock = nl_socket_alloc();
	genl_connect(sock);

	family = genl_ctrl_resolve(sock, "CA_SEND");
	if (family<0) {
		eDebug("Cannot resolve family name of generic netlink socket");
		return;
	}

	ca_policy[ATTR_CA_SIZE].type = NLA_U32;
	ca_policy[ATTR_CA_NUM].type = NLA_U16;
	ca_policy[ATTR_CA_DESCR].type = NLA_UNSPEC;
	ca_policy[ATTR_CA_PID].type = NLA_UNSPEC;

	ASSERT(instance == 0);
	instance = this;

	msg = nlmsg_alloc();
	genlmsg_put(msg, NL_AUTO_PID, NL_AUTO_SEQ, family, 0, NLM_F_ECHO,
			CMD_ASK_CA_SIZE, 1);
	nl_send_auto_complete(sock, msg);
	nl_socket_modify_cb(sock, NL_CB_VALID, NL_CB_CUSTOM, parse_cb, NULL);
	nl_recvmsgs_default(sock);
	nlmsg_free(msg);
	nl_socket_modify_cb(sock, NL_CB_VALID, NL_CB_CUSTOM, parse_cb, NULL);
	nl_socket_disable_seq_check(sock);

	run();
}

caConnector::~caConnector()
{
	instance = 0;
	kill(true);

	nl_close(sock);
	nl_socket_free(sock);
}

void caConnector::thread()
{
	hasStarted();

	while (1) {
		nl_recvmsgs_default(sock);
	}
}

int caConnector::parse_cb(struct nl_msg *msg, void *arg) {
	caConnector *connector = caConnector::getInstance();
	ePtr<eDVBResourceManager> res_mgr;
	int ret;

	eDVBResourceManager::getInstance(res_mgr);
	if (!res_mgr) {
		eDebug("no resource manager !!!!!!!");
		return -1;
	}

	struct nlmsghdr *nlh = nlmsg_hdr(msg);
	struct nlattr *attrs[ATTR_MAX+1];

	genlmsg_parse(nlh, 0, attrs, ATTR_MAX, connector->ca_policy);

	if (attrs[ATTR_CA_SIZE]) {
		uint32_t value = nla_get_u32(attrs[ATTR_CA_SIZE]);
		connector->ca_size = value;
	}
	if (attrs[ATTR_CA_NUM] && attrs[ATTR_CA_DESCR]) {
		unsigned short ca_num = nla_get_u16(attrs[ATTR_CA_NUM]);
		ca_descr_t *ca = (ca_descr_t*)nla_data(attrs[ATTR_CA_DESCR]);
		eDebug("CA_SET_DESCR ca_num %04X, idx %d, parity %d, cw %02X...%02X", ca_num, ca->index,
				ca->parity, ca->cw[0], ca->cw[7]);

		ePtr<eDVBDemux> demux;
		ret = res_mgr->getAdapterDemux(demux, (ca_num>>8)&0xFF, ca_num&0xFF);
		if (ret) {
			eDebug("caConnector: DEMUX NOT FOUND !!");
			return -1;
		}

		if(!demux->setCaDescr(ca,0)) {
			eDebug("CA_SET_DESCR failed (%s). Expect a black screen.",strerror(errno));
		}
	}
	if (attrs[ATTR_CA_NUM] && attrs[ATTR_CA_PID]) {
		unsigned short ca_num = nla_get_u16(attrs[ATTR_CA_NUM]);
		ca_pid_t *ca_pid = (ca_pid_t*)nla_data(attrs[ATTR_CA_PID]);

		eDebug("CA_PID ca_num %04X, pid %04X, index %d", ca_num, ca_pid->pid, ca_pid->index);
		
		ePtr<eDVBDemux> demux;
		ret = res_mgr->getAdapterDemux(demux, (ca_num>>8)&0xFF, ca_num&0xFF);
		if (ret) {
			eDebug("caConnector: DEMUX NOT FOUND !!");
			return -1;
		}

		if(!demux->setCaPid(ca_pid)) {
			eDebug("CA_SET_PID failed");
		}
	}

	return 0;
}

eAutoInitPtr<caConnector> init_caConnector(eAutoInitNumbers::dvb-1, "caConnector");
