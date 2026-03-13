//
// 简化版网络协议栈：只支持本实验用到的 IPv4/UDP/ARP。
//

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "net.h"
#include "defs.h"

static uint32 local_ip = MAKE_IP_ADDR(10, 0, 2, 15);
static uint8 local_mac[ETHADDR_LEN] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };
static uint8 broadcast_mac[ETHADDR_LEN] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

char *
mbufpull(struct mbuf *m, unsigned int len)
{
  char *tmp = m->head;
  if(m->len < len)
    return 0;
  m->len -= len;
  m->head += len;
  return tmp;
}

char *
mbufpush(struct mbuf *m, unsigned int len)
{
  m->head -= len;
  if(m->head < m->buf)
    panic("mbufpush");
  m->len += len;
  return m->head;
}

char *
mbufput(struct mbuf *m, unsigned int len)
{
  char *tmp = m->head + m->len;
  m->len += len;
  if(m->len > MBUF_SIZE)
    panic("mbufput");
  return tmp;
}

char *
mbuftrim(struct mbuf *m, unsigned int len)
{
  if(len > m->len)
    return 0;
  m->len -= len;
  return m->head + m->len;
}

struct mbuf *
mbufalloc(unsigned int headroom)
{
  struct mbuf *m;

  if(headroom > MBUF_SIZE)
    return 0;
  m = kalloc();
  if(m == 0)
    return 0;
  m->next = 0;
  m->head = (char *)m->buf + headroom;
  m->len = 0;
  memset(m->buf, 0, sizeof(m->buf));
  return m;
}

void
mbuffree(struct mbuf *m)
{
  kfree(m);
}

void
mbufq_pushtail(struct mbufq *q, struct mbuf *m)
{
  m->next = 0;
  if(q->head == 0){
    q->head = q->tail = m;
    return;
  }
  q->tail->next = m;
  q->tail = m;
}

struct mbuf *
mbufq_pophead(struct mbufq *q)
{
  struct mbuf *head = q->head;
  if(head == 0)
    return 0;
  q->head = head->next;
  if(q->head == 0)
    q->tail = 0;
  return head;
}

int
mbufq_empty(struct mbufq *q)
{
  return q->head == 0;
}

void
mbufq_init(struct mbufq *q)
{
  q->head = 0;
  q->tail = 0;
}

static ushort
in_cksum(const unsigned char *addr, int len)
{
  int nleft = len;
  const ushort *w = (const ushort *)addr;
  uint sum = 0;
  ushort answer = 0;

  while(nleft > 1){
    sum += *w++;
    nleft -= 2;
  }
  if(nleft == 1){
    *(uchar *)(&answer) = *(const uchar *)w;
    sum += answer;
  }
  sum = (sum & 0xffff) + (sum >> 16);
  sum += (sum >> 16);
  return ~sum;
}

static void
net_tx_eth(struct mbuf *m, uint16 ethtype)
{
  struct eth *ethhdr;

  ethhdr = mbufpushhdr(m, *ethhdr);
  memmove(ethhdr->shost, local_mac, ETHADDR_LEN);
  memmove(ethhdr->dhost, broadcast_mac, ETHADDR_LEN);
  ethhdr->type = htons(ethtype);
  if(e1000_transmit(m))
    mbuffree(m);
}

static void
net_tx_ip(struct mbuf *m, uint8 proto, uint32 dip)
{
  struct ip *iphdr;

  iphdr = mbufpushhdr(m, *iphdr);
  memset(iphdr, 0, sizeof(*iphdr));
  iphdr->ip_vhl = (4 << 4) | (20 >> 2);
  iphdr->ip_p = proto;
  iphdr->ip_src = htonl(local_ip);
  iphdr->ip_dst = htonl(dip);
  iphdr->ip_len = htons(m->len);
  iphdr->ip_ttl = 100;
  iphdr->ip_sum = in_cksum((unsigned char *)iphdr, sizeof(*iphdr));

  net_tx_eth(m, ETHTYPE_IP);
}

void
net_tx_udp(struct mbuf *m, uint32 dip, uint16 sport, uint16 dport)
{
  struct udp *udphdr;

  udphdr = mbufpushhdr(m, *udphdr);
  udphdr->sport = htons(sport);
  udphdr->dport = htons(dport);
  udphdr->ulen = htons(m->len);
  udphdr->sum = 0;

  net_tx_ip(m, IPPROTO_UDP, dip);
}

static int
net_tx_arp(uint16 op, uint8 dmac[ETHADDR_LEN], uint32 dip)
{
  struct mbuf *m;
  struct arp *arphdr;

  m = mbufalloc(MBUF_DEFAULT_HEADROOM);
  if(m == 0)
    return -1;

  arphdr = mbufputhdr(m, *arphdr);
  arphdr->hrd = htons(ARP_HRD_ETHER);
  arphdr->pro = htons(ETHTYPE_IP);
  arphdr->hln = ETHADDR_LEN;
  arphdr->pln = sizeof(uint32);
  arphdr->op = htons(op);

  memmove(arphdr->sha, local_mac, ETHADDR_LEN);
  arphdr->sip = htonl(local_ip);
  memmove(arphdr->tha, dmac, ETHADDR_LEN);
  arphdr->tip = htonl(dip);

  net_tx_eth(m, ETHTYPE_ARP);
  return 0;
}

static void
net_rx_arp(struct mbuf *m)
{
  struct arp *arphdr;
  uint8 smac[ETHADDR_LEN];
  uint32 sip, tip;

  arphdr = mbufpullhdr(m, *arphdr);
  if(arphdr == 0)
    goto done;

  if(ntohs(arphdr->hrd) != ARP_HRD_ETHER ||
     ntohs(arphdr->pro) != ETHTYPE_IP ||
     arphdr->hln != ETHADDR_LEN ||
     arphdr->pln != sizeof(uint32))
    goto done;

  tip = ntohl(arphdr->tip);
  if(ntohs(arphdr->op) != ARP_OP_REQUEST || tip != local_ip)
    goto done;

  memmove(smac, arphdr->sha, ETHADDR_LEN);
  sip = ntohl(arphdr->sip);
  net_tx_arp(ARP_OP_REPLY, smac, sip);

done:
  mbuffree(m);
}

static void
net_rx_udp(struct mbuf *m, uint16 len, struct ip *iphdr)
{
  struct udp *udphdr;
  uint32 sip;
  uint16 sport, dport;

  udphdr = mbufpullhdr(m, *udphdr);
  if(udphdr == 0)
    goto fail;
  if(ntohs(udphdr->ulen) != len)
    goto fail;
  len -= sizeof(*udphdr);
  if(len > m->len)
    goto fail;
  mbuftrim(m, m->len - len);

  sip = ntohl(iphdr->ip_src);
  sport = ntohs(udphdr->sport);
  dport = ntohs(udphdr->dport);
  sockrecvudp(m, sip, dport, sport);
  return;

fail:
  mbuffree(m);
}

static void
net_rx_ip(struct mbuf *m)
{
  struct ip *iphdr;
  uint16 len;

  iphdr = mbufpullhdr(m, *iphdr);
  if(iphdr == 0)
    goto fail;
  if(iphdr->ip_vhl != ((4 << 4) | (20 >> 2)))
    goto fail;
  if(in_cksum((unsigned char *)iphdr, sizeof(*iphdr)))
    goto fail;
  if(htons(iphdr->ip_off) != 0)
    goto fail;
  if(htonl(iphdr->ip_dst) != local_ip)
    goto fail;
  if(iphdr->ip_p != IPPROTO_UDP)
    goto fail;

  len = ntohs(iphdr->ip_len) - sizeof(*iphdr);
  net_rx_udp(m, len, iphdr);
  return;

fail:
  mbuffree(m);
}

void
net_rx(struct mbuf *m)
{
  struct eth *ethhdr;
  uint16 type;

  ethhdr = mbufpullhdr(m, *ethhdr);
  if(ethhdr == 0){
    mbuffree(m);
    return;
  }

  type = ntohs(ethhdr->type);
  if(type == ETHTYPE_IP)
    net_rx_ip(m);
  else if(type == ETHTYPE_ARP)
    net_rx_arp(m);
  else
    mbuffree(m);
}
