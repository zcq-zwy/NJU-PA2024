#include "kernel/types.h"
#include "kernel/net.h"
#include "kernel/stat.h"
#include "user/user.h"

static void
ping(uint16 sport, uint16 dport, int attempts)
{
  int fd, cc;
  char *obuf = "a message from xv6!";
  uint32 dst = MAKE_IP_ADDR(10, 0, 2, 2);
  char ibuf[128];

  if((fd = connect(dst, sport, dport)) < 0){
    fprintf(2, "ping: connect() failed\n");
    exit(1);
  }
  for(int i = 0; i < attempts; i++){
    if(write(fd, obuf, strlen(obuf)) < 0){
      fprintf(2, "ping: send() failed\n");
      exit(1);
    }
  }

  cc = read(fd, ibuf, sizeof(ibuf) - 1);
  if(cc < 0){
    fprintf(2, "ping: recv() failed\n");
    exit(1);
  }

  close(fd);
  ibuf[cc] = '\0';
  if(strcmp(ibuf, "this is the host!") != 0){
    fprintf(2, "ping didn't receive correct payload\n");
    exit(1);
  }
}

static void
encode_qname(char *qn, char *host)
{
  char *l = host;

  for(char *c = host; c < host + strlen(host) + 1; c++){
    if(*c == '.'){
      *qn++ = (char)(c - l);
      for(char *d = l; d < c; d++)
        *qn++ = *d;
      l = c + 1;
    }
  }
  *qn = '\0';
}

static void
decode_qname(char *qn)
{
  while(*qn != '\0'){
    int l = (uchar)*qn;
    if(l == 0)
      break;
    for(int i = 0; i < l; i++){
      *qn = *(qn + 1);
      qn++;
    }
    *qn++ = '.';
  }
}

static int
dns_req(uint8 *obuf)
{
  int len = 0;
  struct dns *hdr = (struct dns *)obuf;
  struct dns_question *h;
  char *qname;

  hdr->id = htons(6828);
  hdr->rd = 1;
  hdr->qdcount = htons(1);
  len += sizeof(struct dns);

  qname = (char *)(obuf + sizeof(struct dns));
  encode_qname(qname, "pdos.csail.mit.edu.");
  len += strlen(qname) + 1;

  h = (struct dns_question *)(qname + strlen(qname) + 1);
  h->qtype = htons(1);
  h->qclass = htons(1);
  len += sizeof(struct dns_question);
  return len;
}

static void
dns_rep(uint8 *ibuf, int cc)
{
  struct dns *hdr = (struct dns *)ibuf;
  struct dns_data *d;
  char *qname = 0;
  int len = sizeof(struct dns);
  int record = 0;

  if(!hdr->qr){
    printf("Not a DNS response for %d\n", ntohs(hdr->id));
    exit(1);
  }
  if(hdr->id != htons(6828)){
    printf("DNS wrong id: %d\n", ntohs(hdr->id));
    exit(1);
  }
  if(hdr->rcode != 0){
    printf("DNS rcode error: %x\n", hdr->rcode);
    exit(1);
  }

  for(int i = 0; i < ntohs(hdr->qdcount); i++){
    char *qn = (char *)(ibuf + len);
    qname = qn;
    decode_qname(qn);
    len += strlen(qn) + 1;
    len += sizeof(struct dns_question);
  }

  for(int i = 0; i < ntohs(hdr->ancount); i++){
    char *qn = (char *)(ibuf + len);
    if((uchar)qn[0] > 63){
      qn = (char *)(ibuf + (uchar)qn[1]);
      len += 2;
    } else {
      decode_qname(qn);
      len += strlen(qn) + 1;
    }

    d = (struct dns_data *)(ibuf + len);
    len += sizeof(struct dns_data);
    if(ntohs(d->type) == ARECORD && ntohs(d->len) == 4){
      uint8 *ip = ibuf + len;
      record = 1;
      printf("DNS arecord for %s is %d.%d.%d.%d\n",
             qname ? qname : "", ip[0], ip[1], ip[2], ip[3]);
      if(ip[0] != 128 || ip[1] != 52 || ip[2] != 129 || ip[3] != 126){
        printf("wrong ip address\n");
        exit(1);
      }
      len += 4;
    }
  }

  if(len != cc){
    printf("Processed %d data bytes but received %d\n", len, cc);
    exit(1);
  }
  if(!record){
    printf("Didn't receive an arecord\n");
    exit(1);
  }
}

static void
dns(void)
{
  uint8 obuf[1000];
  uint8 ibuf[1000];
  uint32 dst = MAKE_IP_ADDR(8, 8, 8, 8);
  int fd, len, cc;

  memset(obuf, 0, sizeof(obuf));
  memset(ibuf, 0, sizeof(ibuf));

  if((fd = connect(dst, 10000, 53)) < 0){
    fprintf(2, "dns: connect() failed\n");
    exit(1);
  }
  len = dns_req(obuf);
  if(write(fd, obuf, len) < 0){
    fprintf(2, "dns: send() failed\n");
    exit(1);
  }
  cc = read(fd, ibuf, sizeof(ibuf));
  if(cc < 0){
    fprintf(2, "dns: recv() failed\n");
    exit(1);
  }
  dns_rep(ibuf, cc);
  close(fd);
}

int
main(int argc, char *argv[])
{
  int i, ret;
  uint16 dport = NET_TESTS_PORT;

  printf("nettests running on port %d\n", dport);

  printf("testing ping: ");
  ping(2000, dport, 1);
  printf("OK\n");

  printf("testing single-process pings: ");
  for(i = 0; i < 100; i++)
    ping(2000, dport, 1);
  printf("OK\n");

  printf("testing multi-process pings: ");
  for(i = 0; i < 10; i++){
    if(fork() == 0){
      ping(2000 + i + 1, dport, 1);
      exit(0);
    }
  }
  for(i = 0; i < 10; i++){
    wait(&ret);
    if(ret != 0)
      exit(1);
  }
  printf("OK\n");

  printf("testing DNS\n");
  dns();
  printf("DNS OK\n");

  printf("all tests passed.\n");
  exit(0);
}
