/*
 * PF-on-Linux: BSD inpcb view over Linux sockets (ISC 2026).
 *
 * pf.c uses OpenBSD's PCB API to (a) find the socket owning an arriving
 * packet (pf_socket_lookup: uid/gid/pid match), and (b) link state keys to
 * sockets (sk_inp <-> inp_pf_sk).  The port creates one struct inpcb per
 * successful Linux socket lookup; it holds the Linux `struct sock` and a
 * BSD-ish socket view.  Reference semantics follow OpenBSD: lookups return
 * the pcb with one reference held; PF takes extra refs (mbufs, state keys)
 * via in_pcbref()/in_pcbunref().
 *
 * PFLINUX deviations (docs/porting-notes.md):
 *   - so_egid: Linux does not store the group of the socket owner in
 *     struct sock; reported as (gid_t)-1 ("unknown").
 *   - so_cpid: Linux does not store the owning pid in struct sock
 *     (/proc-based matching is a userland-side feature); reported NO_PID.
 *   - Wildcard match: OpenBSD in_pcblookup() finds bound-with-wildcard
 *     sockets in one pass; here established lookups are exact and the
 *     wildcard is found via in_pcblookup_listen(), which is exactly the
 *     sequence pf_socket_lookup already follows.
 */
#include <pfk.h>
#include <net/inet_hashtables.h>
#include <net/inet6_hashtables.h>

struct inpcbtable tcbtable, udbtable, tcb6table, udb6table;

static struct inpcb *
pfk_inpcb_of_sock(struct sock *sk)
{
	struct inpcb *inp;

	if (!sk)
		return NULL;

	/* our stable reference on the Linux socket */
	if (!refcount_inc_not_zero(&sk->__sk_common.skc_refcnt))
		return NULL;

	inp = pfk_malloc(sizeof(*inp), M_NOWAIT | M_ZERO);
	if (!inp) {
		sock_gen_put(sk);
		return NULL;
	}
	refcount_set(&inp->inp_refcnt, 1);
	inp->inp_sock = sk;
	inp->inp_socket = &inp->inp_soinfo_storage;
	inp->inp_socket->so_euid = __kuid_val(sk_uid(sk));
	/* PFLINUX: Linux stores no egid/owner-pid in struct sock */
	inp->inp_socket->so_egid = (gid_t)-1;
	inp->inp_socket->so_cpid = NO_PID;
	return inp;
}

struct inpcb *
in_pcbref(struct inpcb *inp)
{
	if (inp != NULL)
		refcount_inc(&inp->inp_refcnt);
	return inp;
}

void
in_pcbunref(struct inpcb *inp)
{
	struct sock *sk;

	if (inp == NULL)
		return;
	if (!refcount_dec_and_test(&inp->inp_refcnt))
		return;
	sk = inp->inp_sock;
	if (sk != NULL)
		sock_gen_put(sk);	/* drop our stable reference */
	pfk_free(inp);
}

/*
 * Established/exact 4-tuple lookup.  table selects the protocol family
 * (tcbtable: TCP, udbtable: UDP) — OpenBSD keeps them apart; Linux shares
 * the ehash for TCP but UDP needs its own lib lookup (which also covers
 * bound-wildcard matching, mirroring OpenBSD's behaviour).
 */
struct inpcb *
in_pcblookup(struct inpcbtable *table, struct in_addr faddr, u_int fport,
    struct in_addr laddr, u_int lport, u_int rtableid)
{
	struct sock *sk;
	struct inpcb *inp;

	rcu_read_lock();
	if (table == &udbtable) {
		sk = __udp4_lib_lookup(&init_net, faddr.s_addr, (__be16)fport,
		    laddr.s_addr, (__be16)lport, 0, 0, NULL);
	} else {
		sk = __inet_lookup_established(&init_net, faddr.s_addr,
		    (__be16)fport, laddr.s_addr, ntohs((__be16)lport), 0, 0);
	}
	inp = sk ? pfk_inpcb_of_sock(sk) : NULL;
	rcu_read_unlock();
	return inp;
}

struct inpcb *
in_pcblookup_listen(struct inpcbtable *table, struct in_addr laddr,
    u_int lport, struct mbuf *m, u_int rtableid)
{
	struct sock *sk;
	struct inpcb *inp;

	rcu_read_lock();
	if (table == &udbtable) {
		/* UDP: the lib lookup matches the wildcard bound socket */
		sk = __udp4_lib_lookup(&init_net, 0, 0, laddr.s_addr,
		    (__be16)lport, 0, 0, NULL);
	} else {
		sk = __inet_lookup_listener(&init_net, NULL, 0, 0, 0,
		    laddr.s_addr, ntohs((__be16)lport), 0, 0);
	}
	inp = sk ? pfk_inpcb_of_sock(sk) : NULL;
	rcu_read_unlock();
	return inp;
}

struct inpcb *
in6_pcblookup(struct inpcbtable *table, const struct in6_addr *faddr,
    u_int fport, const struct in6_addr *laddr, u_int lport, u_int rtableid)
{
	struct sock *sk;
	struct inpcb *inp;

	rcu_read_lock();
	if (table == &udb6table) {
		sk = __udp6_lib_lookup(&init_net, faddr, (__be16)fport,
		    (struct in6_addr *)laddr, (__be16)lport, 0, 0, NULL);
	} else {
		sk = __inet6_lookup_established(&init_net, faddr,
		    (__be16)fport, (struct in6_addr *)laddr,
		    ntohs((__be16)lport), 0, 0);
	}
	inp = sk ? pfk_inpcb_of_sock(sk) : NULL;
	rcu_read_unlock();
	return inp;
}

struct inpcb *
in6_pcblookup_listen(struct inpcbtable *table, struct in6_addr *laddr,
    u_int lport, struct mbuf *m, u_int rtableid)
{
	static const struct in6_addr any = { };
	struct sock *sk;
	struct inpcb *inp;

	rcu_read_lock();
	if (table == &udb6table) {
		sk = __udp6_lib_lookup(&init_net, &any, 0, laddr,
		    (__be16)lport, 0, 0, NULL);
	} else {
		sk = inet6_lookup_listener(&init_net, NULL, 0, &any, 0,
		    laddr, ntohs((__be16)lport), 0, 0);
	}
	inp = sk ? pfk_inpcb_of_sock(sk) : NULL;
	rcu_read_unlock();
	return inp;
}

int
pfk_pcb_init(void)
{
	return 0;
}

void
pfk_pcb_exit(void)
{
}
