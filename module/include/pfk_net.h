/*
 * PF-on-Linux BSD wire-protocol structs + checksum helpers (ISC 2026).
 * Field names match OpenBSD headers; staging seds rename `struct ip` etc.
 * to the pf_* names defined here so they never collide with Linux uapi.
 * Byte layouts below are the little-endian variants (x86/arm64); the
 * bitfield orderings replicate OpenBSD netinet headers exactly.
 */
#ifndef _PFK_NET_H
#define _PFK_NET_H

/* ---- IPv4 header (OpenBSD <netinet/ip.h> layout, LE bitfield order) ---- */
struct pf_ip {
#if defined(__LITTLE_ENDIAN_BITFIELD)
	__u8	ip_hl:4, ip_v:4;
#else
	__u8	ip_v:4, ip_hl:4;
#endif
	__u8	ip_tos;
	__be16	ip_len;
	__be16	ip_id;
	__be16	ip_off;
#define IP_RF 0x8000
#define IP_OFFLP 0x3fff
	__u8	ip_ttl;
	__u8	ip_p;
	__be16	ip_sum;
	struct	in_addr ip_src, ip_dst;
};
#define IP_MAXPACKET	65535
#define IP_DEFAULT_TTL	64
#define IPV4_TTL	64

/* ---- TCP (OpenBSD <netinet/tcp.h> names) ---- */
struct pf_tcphdr {
	__be16	th_sport, th_dport;
	__be32	th_seq, th_ack;
#if defined(__LITTLE_ENDIAN_BITFIELD)
	__u8	th_x2:4, th_off:4;
#else
	__u8	th_off:4, th_x2:4;
#endif
	__u8	th_flags;
	__be16	th_win;
	__be16	th_sum;
	__be16	th_urp;
};
#define TH_FIN	0x01
#define TH_SYN	0x02
#define TH_RST	0x04
#define TH_PUSH	0x08
#define TH_ACK	0x10
#define TH_URG	0x20
#define TH_ECE	0x40
#define TH_CWR	0x80
#define TH_FLAGS	(TH_FIN|TH_SYN|TH_RST|TH_ACK|TH_URG|TH_ECE|TH_CWR)

/* tcp_fsm.h states (same numeric as Linux TCP_* enum) */
#define TCPS_CLOSED		0
#define TCPS_LISTEN		1
#define TCPS_SYN_SENT		2
#define TCPS_SYN_RECEIVED	3
#define TCPS_ESTABLISHED	4
#define TCPS_CLOSE_WAIT		5
#define TCPS_FIN_WAIT_1		6
#define TCPS_CLOSING		7
#define TCPS_LAST_ACK		8
#define TCPS_FIN_WAIT_2		9
#define TCPS_TIME_WAIT		10

/* ---- UDP ---- */
struct pf_udphdr {
	__be16	uh_sport, uh_dport;
	__be16	uh_ulen;
	__be16	uh_sum;
};

/* ---- ICMP (OpenBSD <netinet/ip_icmp.h> names/values) ---- */
struct pf_icmp {
	__u8	icmp_type;
	__u8	icmp_code;
	__be16	icmp_cksum;
	union {
		__be16	ih_id[2];	/* id, seq */
		struct { __u8 type, code; __be16 cksum; } ih_type_hdr;
		__be32	ih_gwaddr;
		__u8	ih_pptr;	/* ICMP_PARAMPROB pointer byte */
		__be32	ih_void;
		struct { __be16 ipm_void; __be16 ipm_nextmtu; } ih_pmtu;
		struct ih_frags { __be16 ih_rrechk; __be16 ih_length; } ih_frag;
	} icmp_hun;
#define	icmp_id		icmp_hun.ih_id[0]
#define icmp_seq	icmp_hun.ih_id[1]
#define icmp_cksum_alt	icmp_hun.ih_type_hdr.cksum
#define icmp_gwaddr	icmp_hun.ih_gwaddr
#define icmp_pptr	icmp_hun.ih_pptr
#define icmp_void	icmp_hun.ih_void
#define icmp_pmvoid	icmp_hun.ih_pmtu.ipm_void
#define icmp_nextmtu	icmp_hun.ih_pmtu.ipm_nextmtu
	union {
		__s8	id_pad[8];
		__be16		id_ipid[2];
		struct pf_ip	id_ip;
		struct ih_pmtu  { __be16 reserved; __be16 mtu; } ih_pmtu;
		struct ih_rtradv  { __u8 irt_num_addrs, irt_wpa;
			__be16 lifetime; } ih_rtradv;
	} icmp_data;
#define	icmp_rip		icmp_data.id_ip
#define	icmp_ppsmtu		icmp_data.ih_pmtu.mtu
};

#define ICMP_ECHOREPLY		0
#define ICMP_UNREACH		3
#define ICMP_UNREACH_NET	0
#define ICMP_UNREACH_HOST	1
#define ICMP_UNREACH_PROTOCOL	2
#define ICMP_UNREACH_PORT	3
#define ICMP_UNREACH_NEEDFRAG	4
#define ICMP_UNREACH_SRCFAIL	5
#define ICMP_UNREACH_NET_UNKNOWN 6
#define ICMP_UNREACH_HOST_UNKNOWN 7
#define ICMP_UNREACH_ISOLATED	8
#define ICMP_UNREACH_NET_PROHIB	9
#define ICMP_UNREACH_HOST_PROHIB	10
#define ICMP_UNREACH_TOSNET	11
#define ICMP_UNREACH_TOSHOST	12
#define ICMP_UNREACH_FILTER_PROHIB 13
#define ICMP_UNREACH_HOST_PRECEDENCE 14
#define ICMP_UNREACH_PRECEDENCE_CUTOFF 15
#define ICMP_SOURCEQUENCH	4
#define ICMP_REDIRECT		5
#define ICMP_ALTHOSTADDR	6
#define ICMP_ECHO		8
#define ICMP_ROUTERADVERT	9
#define ICMP_ROUTERSOLICIT	10
#define ICMP_TIMXCEED		11
#define ICMP_TIMXCEED_INTRANS	0
#define ICMP_TIMXCEED_REASS	1
#define ICMP_PARAMPROB		12
#define ICMP_TSTAMP		13
#define ICMP_TSTAMPREPLY	14
#define ICMP_INFO_REQUEST	15
#define ICMP_INFO_REPLY		16
#define ICMP_ADDRESS		17
#define ICMP_ADDRESSREPLY	18
#define ICMP_MAXTYPE		18
/* OpenBSD <netinet/ip_icmp.h> additional type/code constants (values are
 * RFC/IANA-frozen; names as used by pf.c). */
#define ICMP_IREQ		15	/* information request */
#define ICMP_IREQREPLY		16	/* information reply */
#define ICMP_MASKREQ		17	/* address mask request */
#define ICMP_MASKREPLY		18	/* address mask reply */
#define ICMP_IPV6_WHEREAREYOU	33	/* IPv6 where-are-you */
#define ICMP_IPV6_IAMHERE	34	/* IPv6 i-am-here */
#define ICMP_MOBILE_REGREQUEST	35	/* mobile registration req */
#define ICMP_MOBILE_REGREPLY	36	/* mobile registration reply */
#define ICMP_MINLEN		8	/* abs minimum */
#define ICMP_PARAMPROB_ERRATPTR	0	/* error at indicated pointer */
#define ICMP_PARAMPROB_OPTABSENT 1	/* req. opt. absent */
#define ICMP_PARAMPROB_LENGTH	2	/* bad length */
#define ICMP_ECHOREQUEST	ICMP_ECHO
#define ICMP_INFOTYPE(type) \
	((type) == ICMP_ECHOREPLY || (type) == ICMP_ECHO || \
	(type) == ICMP_ROUTERADVERT || (type) == ICMP_ROUTERSOLICIT || \
	(type) == ICMP_TSTAMP || (type) == ICMP_TSTAMPREPLY || \
	(type) == ICMP_IREQ || (type) == ICMP_IREQREPLY || \
	(type) == ICMP_MASKREQ || (type) == ICMP_MASKREPLY)

/* ---- IPv6 (OpenBSD <netinet/ip6.h> ctl-union + macros) ---- */
#define IPV6_DEFHLIM	64	/* default hop limit */
#define IPV6_HLIMDEC	1	/* subtracted when forwarding */
struct pf_ip6_hdr {
	union {
		struct ip6_hdrctl {
			__u32 ip6_un1_flow;
			__be16 ip6_un1_plen;
			__u8 ip6_un1_nxt;
			__u8 ip6_un1_hlim;
		} ip6_un1;
		__u8 ip6_un2_vfc;	/* version << 4 | tclass >> 4 */
	} ip6_ctlun;
	struct in6_addr ip6_src;
	struct in6_addr ip6_dst;
};
#define ip6_vfc		ip6_ctlun.ip6_un2_vfc
#define ip6_flow	ip6_ctlun.ip6_un1->ip6_un1_flow
#define ip6_plen	ip6_ctlun.ip6_un1.ip6_un1_plen
#define ip6_nxt		ip6_ctlun.ip6_un1.ip6_un1_nxt
#define ip6_hlim	ip6_ctlun.ip6_un1.ip6_un1_hlim
#define ip6_hops	ip6_ctlun.ip6_un1.ip6_un1_hlim
/* NOTE ip6_flow above is a pointer trick in OpenBSD; PF reads/writes
 * it via explicit helpers only — provide the plain accessor instead: */
#undef ip6_flow
#define ip6_flow	ip6_ctlun.ip6_un1.ip6_un1_flow
#define IPV6_VERSION	0x60
#define IPV6_VERSION_MASK 0xf0
#define v_hl	i_vhl
#define traffic_class i_tc1, i_tc2

/* flow macros (OpenBSD) */
static inline __u32 PF6_FLOWlabel(const struct pf_ip6_hdr *h)
{ return be32_to_cpu(h->ip6_flow) & 0x000fffff; }
static inline __u8 PF6_VERSION(const struct pf_ip6_hdr *h)
{ return be32_to_cpu(h->ip6_flow) >> 28; }
static inline __u8 PF6_CLASS(const struct pf_ip6_hdr *h)
{ return (be32_to_cpu(h->ip6_flow) >> 20) & 0xff; }

/* ---- ICMPv6: PF code's field names (icmp6_type/code/cksum/identifier/
 * sequence/mtu) are provided by Linux's own <linux/icmpv6.h> struct
 * icmp6hdr + its member macros; staging seds `struct icmp6_hdr` →
 * `struct icmp6hdr` (see stage-port.sh). No private definition needed. */

/* ---- ICMPv6 (BSD <netinet6/icmp6.h> layout for pfvar_priv's hdr union) ---- */
struct pf_icmp6 {
	__u8		icmp6_type;
	__u8		icmp6_code;
	__be16		icmp6_cksum;
	union {
		__be32	un_data32[4];
		struct {
			__be16	icmp6_id;
			__be16	icmp6_seq;
		} un_h;
	} icmp6_dataun;
#define icmp6_pptr	icmp6_dataun.un_data32[0]	/* parameter prob */
};
#define icmp6_id	icmp6_dataun.un_h.icmp6_id
#define icmp6_seq	icmp6_dataun.un_h.icmp6_seq
#undef icmp6_mtu
#define icmp6_mtu	icmp6_dataun.un_data32[1]

/* ---- IPv6 multicast / neighbour discovery headers (BSD names) ---- */
struct pf_mld_hdr {
	__u8		mld_type;
	__u8		mld_pad[2];
	__be16		mld_cksum;
	__be32		mld_reserved;
	struct in6_addr	mld_addr;
};

struct pf_nd_neighbor_solicit {
	struct in6_addr	nd_ns_target;
	__be32		nd_ns_reserved;
	__be32		nd_ns_flags;
};

/* ---- BSD IPPROTO_ / IP levels (identical numerics to Linux; aliases kept) */
#define IPPROTO_MAX_	IPPROTO_MAX

/* ---- checksum helpers (ported in_cksum etc.) ---- */
u_int16_t pfk_in_cksum(const void *data, int len);	/* one's complement sum */
__sum16 pfk_icmp6_cksum(const struct pf_ip6_hdr *ip6,
	const struct icmp6hdr *ic6, int len);
void pfk_send_icmp_error(int family, struct sk_buff *skb_in, int type,
	int code, __be32 info);

/* ---- IPv6 extension/option headers (OpenBSD <netinet/ip6.h>) ---- */
struct ip6_opt {
	__u8	ip6o_type;
	__u8	ip6o_len;
};
#define IP6OPT_PAD1		0x00
#define IP6OPT_PADN		0x01
#define IP6OPT_JUMBO		0xC2
#define IP6OPT_ROUTER_ALERT	0x05
#define IP6OPT_MINLEN		2
struct ip6_opt_jumbo {
	__u8	ip6oj_type;
	__u8	ip6oj_len;
	__u8	ip6oj_jumbo_len[4];
};
#define IP6OPT_JUMBO_LEN	6
struct ip6_opt_router {
	__u8	ip6or_type;
	__u8	ip6or_len;
	__u8	ip6or_value[2];
};
struct ip6_rthdr {
	__u8	ip6r_nxt;
	__u8	ip6r_len;
	__u8	ip6r_type;
	__u8	ip6r_segleft;
};
#define IPV6_RTHDR_TYPE_0	0
struct ip6_rthdr0 {
	__u8	ip6r0_nxt;
	__u8	ip6r0_len;
	__u8	ip6r0_type;
	__u8	ip6r0_segleft;
	__u32	ip6r0_reserved;
};

/* ---- ICMPv6 types/codes (OpenBSD <netinet/icmp6.h>, RFC-frozen values) ---- */
#define ICMP6_PARAM_PROB		 4
#define ICMP6_WRUREQUEST		 139	/* who-are-you / NI query */
#define ICMP6_WRUREPLY			 140
#define ICMP6_NI_QUERY			 139
#define ICMP6_NI_REPLY			 140
#define ICMP6_MAXTYPE			 201
#define ICMP6_DST_UNREACH_NOROUTE	 0
#define ICMP6_DST_UNREACH_ADMIN		 1
#define ICMP6_DST_UNREACH_BEYONDSCOPE	 2
#define ICMP6_DST_UNREACH_ADDR		 3
#define ICMP6_DST_UNREACH_NOPORT		 4
#define ICMP6_TIME_EXCEED_TRANSIT	 0
#define ICMP6_TIME_EXCEED_REASSEMBLY	 1
#define ICMP6_PARAMPROB_HEADER		 0
#define ICMP6_PARAMPROB_NEXTHEADER	 1
#define ICMP6_PARAMPROB_OPTION		 2
#define MLD_LISTENER_QUERY		 130
#define MLD_LISTENER_REPORT		 131
#define MLD_LISTENER_DONE		 132
#define MLDV2_LISTENER_REPORT		 143
#define MLD_MTRACE_RESP			 200
#define MLD_MTRACE			 201
#define ND_ROUTER_SOLICIT		 133
#define ND_ROUTER_ADVERT		 134
#define ND_NEIGHBOR_SOLICIT		 135
#define ND_NEIGHBOR_ADVERT		 136
#define ND_REDIRECT			 137

/* ---- TCP (OpenBSD <netinet/tcp.h>/<netinet/tcp_var.h>/<netinet/tcp_seq.h>) */
#define TCP_NSTATES		11	/* netinet/tcp_fsm.h */
#define TCP_MAXWIN		65535	/* max value for (unscaled) window */
#define TCP_MAX_WINSHIFT	14	/* max window shift */
#define TCPOPT_SACK		5
#define TCPOLEN_SACK		8	/* 2*sizeof(tcp_seq) */
struct sackblk {
	__u32	start;		/* start seq no. of sack block */
	__u32	end;		/* end seq no. */
};
typedef __u32 tcp_seq;

#endif /* _PFK_NET_H */
