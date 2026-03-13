#include <common.h>
#include <device/map.h>
#include <memory/paddr.h>

#define E1000_SIZE      0x20000

#define E1000_CTL       (0x00000/4)
#define E1000_ICR       (0x000C0/4)
#define E1000_IMS       (0x000D0/4)
#define E1000_RCTL      (0x00100/4)
#define E1000_TDBAL     (0x03800/4)
#define E1000_TDLEN     (0x03808/4)
#define E1000_TDH       (0x03810/4)
#define E1000_TDT       (0x03818/4)
#define E1000_RDBAL     (0x02800/4)
#define E1000_RDLEN     (0x02808/4)
#define E1000_RDH       (0x02810/4)
#define E1000_RDT       (0x02818/4)

#define E1000_CTL_RST   0x00400000
#define E1000_IMS_RXDW  (1u << 7)

#define E1000_TXD_STAT_DD  0x01
#define E1000_RXD_STAT_DD  0x01
#define E1000_RXD_STAT_EOP 0x02

#define ETHTYPE_IP      0x0800
#define IPPROTO_UDP     17

static uint32_t *e1000_regs = NULL;

struct tx_desc {
  uint64_t addr;
  uint16_t length;
  uint8_t cso;
  uint8_t cmd;
  uint8_t status;
  uint8_t css;
  uint16_t special;
} __attribute__((packed));

struct rx_desc {
  uint64_t addr;
  uint16_t length;
  uint16_t csum;
  uint8_t status;
  uint8_t errors;
  uint16_t special;
} __attribute__((packed));

struct eth {
  uint8_t dhost[6];
  uint8_t shost[6];
  uint16_t type;
} __attribute__((packed));

struct ip {
  uint8_t vhl;
  uint8_t tos;
  uint16_t len;
  uint16_t id;
  uint16_t off;
  uint8_t ttl;
  uint8_t proto;
  uint16_t sum;
  uint32_t src;
  uint32_t dst;
} __attribute__((packed));

struct udp {
  uint16_t sport;
  uint16_t dport;
  uint16_t len;
  uint16_t sum;
} __attribute__((packed));

static const uint8_t guest_mac[6] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };
static const uint8_t host_mac[6]  = { 0x52, 0x54, 0x00, 0xaa, 0xbb, 0xcc };
static const uint32_t guest_ip = 0x0a00020f; // 10.0.2.15
static const uint32_t host_ip  = 0x0a000202; // 10.0.2.2
static const uint32_t dns_ip   = 0x08080808; // 8.8.8.8

void xv6_plic_raise_irq(int irq);

static inline uint16_t be16(const uint8_t *buf) {
  return ((uint16_t)buf[0] << 8) | buf[1];
}

static inline uint32_t be32(const uint8_t *buf) {
  return ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
         ((uint32_t)buf[2] << 8) | (uint32_t)buf[3];
}

static inline void put_be16(uint8_t *buf, uint16_t val) {
  buf[0] = (uint8_t)(val >> 8);
  buf[1] = (uint8_t)val;
}

static inline void put_be32(uint8_t *buf, uint32_t val) {
  buf[0] = (uint8_t)(val >> 24);
  buf[1] = (uint8_t)(val >> 16);
  buf[2] = (uint8_t)(val >> 8);
  buf[3] = (uint8_t)val;
}

static uint16_t ip_checksum(const uint8_t *buf, size_t len) {
  uint32_t sum = 0;
  for (size_t i = 0; i + 1 < len; i += 2) {
    sum += ((uint16_t)buf[i] << 8) | buf[i + 1];
  }
  if (len & 1) sum += (uint16_t)buf[len - 1] << 8;
  while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
  return (uint16_t)(~sum);
}

static void raise_rx_irq(void) {
  if (e1000_regs[E1000_IMS] & E1000_IMS_RXDW) {
    e1000_regs[E1000_ICR] |= E1000_IMS_RXDW;
    xv6_plic_raise_irq(CONFIG_XV6_E1000_IRQ);
  }
}

static bool rx_ring_full(uint32_t qnum, uint32_t head, uint32_t tail) {
  return ((head + 1) % qnum) == tail;
}

static void inject_frame(const uint8_t *frame, size_t frame_len) {
  uint32_t qnum = e1000_regs[E1000_RDLEN] / sizeof(struct rx_desc);
  uint32_t head = e1000_regs[E1000_RDH];
  uint32_t tail = e1000_regs[E1000_RDT];
  struct rx_desc *ring;
  uint8_t *buf;

  if (qnum == 0 || rx_ring_full(qnum, head, tail)) return;
  ring = (struct rx_desc *)guest_to_host((paddr_t)e1000_regs[E1000_RDBAL]);
  buf = guest_to_host((paddr_t)ring[head].addr);

  if (frame_len > 2048) frame_len = 2048;
  memcpy(buf, frame, frame_len);
  ring[head].length = frame_len;
  ring[head].status = E1000_RXD_STAT_DD | E1000_RXD_STAT_EOP;
  e1000_regs[E1000_RDH] = (head + 1) % qnum;
  raise_rx_irq();
}

static size_t build_dns_reply(const uint8_t *req, size_t req_len, uint8_t *out, size_t out_sz) {
  size_t qname_len = 0, off, total;

  if (req_len < 12) return 0;
  while (12 + qname_len < req_len) {
    if (req[12 + qname_len] == 0) {
      qname_len++;
      break;
    }
    qname_len++;
  }
  if (qname_len == 0 || 12 + qname_len + 4 > req_len) return 0;

  total = 12 + qname_len + 4 + 2 + 10 + 4;
  if (total > out_sz) return 0;
  memset(out, 0, total);

  out[0] = req[0];
  out[1] = req[1];
  out[2] = 0x81;
  out[3] = 0x80;
  out[4] = 0x00;
  out[5] = 0x01;
  out[6] = 0x00;
  out[7] = 0x01;

  memcpy(out + 12, req + 12, qname_len + 4);
  off = 12 + qname_len + 4;
  out[off++] = 0xc0;
  out[off++] = 0x0c;
  out[off++] = 0x00;
  out[off++] = 0x01;
  out[off++] = 0x00;
  out[off++] = 0x01;
  out[off++] = 0x00;
  out[off++] = 0x00;
  out[off++] = 0x00;
  out[off++] = 0x3c;
  out[off++] = 0x00;
  out[off++] = 0x04;
  out[off++] = 128;
  out[off++] = 52;
  out[off++] = 129;
  out[off++] = 126;
  return off;
}

static void build_udp_frame(uint32_t src_ip, uint32_t dst_ip,
    uint16_t src_port, uint16_t dst_port,
    const uint8_t *payload, size_t payload_len,
    uint8_t *out, size_t *out_len) {
  size_t ip_len = sizeof(struct ip) + sizeof(struct udp) + payload_len;
  size_t total = sizeof(struct eth) + ip_len;
  uint8_t *ip = out + sizeof(struct eth);
  uint8_t *udp = ip + sizeof(struct ip);

  memset(out, 0, total);
  memcpy(out, guest_mac, 6);
  memcpy(out + 6, host_mac, 6);
  put_be16(out + 12, ETHTYPE_IP);

  ip[0] = 0x45;
  ip[8] = 64;
  ip[9] = IPPROTO_UDP;
  put_be16(ip + 2, (uint16_t)ip_len);
  put_be32(ip + 12, src_ip);
  put_be32(ip + 16, dst_ip);
  put_be16(ip + 10, ip_checksum(ip, sizeof(struct ip)));

  put_be16(udp + 0, src_port);
  put_be16(udp + 2, dst_port);
  put_be16(udp + 4, (uint16_t)(sizeof(struct udp) + payload_len));
  put_be16(udp + 6, 0);
  memcpy(udp + sizeof(struct udp), payload, payload_len);
  *out_len = total;
}

static void handle_udp(const uint8_t *payload, size_t payload_len,
    uint32_t src_ip, uint32_t dst_ip, uint16_t sport, uint16_t dport) {
  uint8_t frame[1600];
  uint8_t reply[512];
  size_t reply_len = 0, frame_len = 0;
  static const uint8_t host_msg[] = "this is the host!";

  if (dst_ip == host_ip) {
    build_udp_frame(host_ip, guest_ip, dport, sport,
        host_msg, sizeof(host_msg) - 1, frame, &frame_len);
    inject_frame(frame, frame_len);
    return;
  }

  if (dst_ip == dns_ip && dport == 53) {
    reply_len = build_dns_reply(payload, payload_len, reply, sizeof(reply));
    if (reply_len == 0) return;
    build_udp_frame(dns_ip, guest_ip, dport, sport, reply, reply_len, frame, &frame_len);
    inject_frame(frame, frame_len);
    return;
  }

  (void)src_ip;
}

static void handle_tx_desc(struct tx_desc *desc) {
  const uint8_t *frame = guest_to_host((paddr_t)desc->addr);
  size_t frame_len = desc->length;
  const struct eth *eth;
  const uint8_t *ip;
  size_t ip_len;
  uint8_t ihl;
  uint32_t src_ip, dst_ip;
  uint16_t sport, dport, udp_len;
  const uint8_t *udp;
  const uint8_t *payload;

  if (frame_len < sizeof(struct eth)) goto done;
  eth = (const struct eth *)frame;
  if (be16((const uint8_t *)&eth->type) != ETHTYPE_IP) goto done;

  ip = frame + sizeof(struct eth);
  ip_len = frame_len - sizeof(struct eth);
  if (ip_len < sizeof(struct ip)) goto done;
  ihl = (uint8_t)((ip[0] & 0x0f) * 4);
  if (ihl < sizeof(struct ip) || ip_len < ihl + sizeof(struct udp)) goto done;
  if (ip[9] != IPPROTO_UDP) goto done;

  src_ip = be32(ip + 12);
  dst_ip = be32(ip + 16);
  udp = ip + ihl;
  sport = be16(udp + 0);
  dport = be16(udp + 2);
  udp_len = be16(udp + 4);
  if (udp_len < sizeof(struct udp) || ip_len < ihl + udp_len) goto done;
  payload = udp + sizeof(struct udp);
  handle_udp(payload, udp_len - sizeof(struct udp), src_ip, dst_ip, sport, dport);

done:
  desc->status |= E1000_TXD_STAT_DD;
}

static void process_tx_ring(void) {
  struct tx_desc *ring;
  uint32_t qnum = e1000_regs[E1000_TDLEN] / sizeof(struct tx_desc);
  uint32_t head = e1000_regs[E1000_TDH];
  uint32_t tail = e1000_regs[E1000_TDT];

  if (qnum == 0) return;
  ring = (struct tx_desc *)guest_to_host((paddr_t)e1000_regs[E1000_TDBAL]);
  while (head != tail) {
    handle_tx_desc(&ring[head]);
    head = (head + 1) % qnum;
  }
  e1000_regs[E1000_TDH] = head;
}

static void reset_device(void) {
  memset(e1000_regs, 0, E1000_SIZE);
}

static void xv6_e1000_io_handler(uint32_t offset, int len, bool is_write) {
  assert(len == 4);
  if (!is_write) return;

  switch (offset / 4) {
    case E1000_CTL:
      if (e1000_regs[E1000_CTL] & E1000_CTL_RST) reset_device();
      break;
    case E1000_ICR:
      e1000_regs[E1000_ICR] = 0;
      break;
    case E1000_TDT:
      process_tx_ring();
      break;
    default:
      break;
  }
}

void init_xv6_e1000() {
  e1000_regs = (uint32_t *)new_space(E1000_SIZE);
  memset(e1000_regs, 0, E1000_SIZE);
  add_mmio_map("xv6-e1000", CONFIG_XV6_E1000_MMIO, e1000_regs, E1000_SIZE, xv6_e1000_io_handler);
}
