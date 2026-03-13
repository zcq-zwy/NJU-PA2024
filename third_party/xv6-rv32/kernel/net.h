//
// net lab 需要的包缓冲和协议头定义。
//

#define MBUF_SIZE              2048
#define MBUF_DEFAULT_HEADROOM  128

struct mbuf {
  struct mbuf *next;
  char *head;
  unsigned int len;
  char buf[MBUF_SIZE];
};

char *mbufpull(struct mbuf *m, unsigned int len);
char *mbufpush(struct mbuf *m, unsigned int len);
char *mbufput(struct mbuf *m, unsigned int len);
char *mbuftrim(struct mbuf *m, unsigned int len);

#define mbufpullhdr(mbuf, hdr) (typeof(hdr)*)mbufpull(mbuf, sizeof(hdr))
#define mbufpushhdr(mbuf, hdr) (typeof(hdr)*)mbufpush(mbuf, sizeof(hdr))
#define mbufputhdr(mbuf, hdr)  (typeof(hdr)*)mbufput(mbuf, sizeof(hdr))
#define mbuftrimhdr(mbuf, hdr) (typeof(hdr)*)mbuftrim(mbuf, sizeof(hdr))

struct mbuf *mbufalloc(unsigned int headroom);
void mbuffree(struct mbuf *m);

struct mbufq {
  struct mbuf *head;
  struct mbuf *tail;
};

void mbufq_pushtail(struct mbufq *q, struct mbuf *m);
struct mbuf *mbufq_pophead(struct mbufq *q);
int mbufq_empty(struct mbufq *q);
void mbufq_init(struct mbufq *q);

static inline uint16 bswaps(uint16 val)
{
  return (((val & 0x00ffU) << 8) | ((val & 0xff00U) >> 8));
}

static inline uint32 bswapl(uint32 val)
{
  return (((val & 0x000000ffUL) << 24) |
          ((val & 0x0000ff00UL) << 8) |
          ((val & 0x00ff0000UL) >> 8) |
          ((val & 0xff000000UL) >> 24));
}

#define ntohs bswaps
#define ntohl bswapl
#define htons bswaps
#define htonl bswapl

#define ETHADDR_LEN 6

struct eth {
  uint8 dhost[ETHADDR_LEN];
  uint8 shost[ETHADDR_LEN];
  uint16 type;
} __attribute__((packed));

#define ETHTYPE_IP  0x0800
#define ETHTYPE_ARP 0x0806

struct ip {
  uint8 ip_vhl;
  uint8 ip_tos;
  uint16 ip_len;
  uint16 ip_id;
  uint16 ip_off;
  uint8 ip_ttl;
  uint8 ip_p;
  uint16 ip_sum;
  uint32 ip_src, ip_dst;
};

#define IPPROTO_UDP 17

#define MAKE_IP_ADDR(a, b, c, d) \
  (((uint32)(a) << 24) | ((uint32)(b) << 16) | ((uint32)(c) << 8) | (uint32)(d))

struct udp {
  uint16 sport;
  uint16 dport;
  uint16 ulen;
  uint16 sum;
};

struct arp {
  uint16 hrd;
  uint16 pro;
  uint8 hln;
  uint8 pln;
  uint16 op;
  char sha[ETHADDR_LEN];
  uint32 sip;
  char tha[ETHADDR_LEN];
  uint32 tip;
} __attribute__((packed));

#define ARP_HRD_ETHER 1
enum {
  ARP_OP_REQUEST = 1,
  ARP_OP_REPLY = 2,
};

struct dns {
  uint16 id;
  uint8 rd:1;
  uint8 tc:1;
  uint8 aa:1;
  uint8 opcode:4;
  uint8 qr:1;
  uint8 rcode:4;
  uint8 cd:1;
  uint8 ad:1;
  uint8 z:1;
  uint8 ra:1;
  uint16 qdcount;
  uint16 ancount;
  uint16 nscount;
  uint16 arcount;
} __attribute__((packed));

struct dns_question {
  uint16 qtype;
  uint16 qclass;
} __attribute__((packed));

#define ARECORD 0x0001

struct dns_data {
  uint16 type;
  uint16 class;
  uint32 ttl;
  uint16 len;
} __attribute__((packed));
