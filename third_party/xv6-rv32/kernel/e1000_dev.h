//
// E1000 硬件寄存器和描述符定义。
//

#define E1000_CTL      (0x00000/4)
#define E1000_ICR      (0x000C0/4)
#define E1000_IMS      (0x000D0/4)
#define E1000_RCTL     (0x00100/4)
#define E1000_TCTL     (0x00400/4)
#define E1000_TIPG     (0x00410/4)
#define E1000_RDBAL    (0x02800/4)
#define E1000_RDH      (0x02810/4)
#define E1000_RDT      (0x02818/4)
#define E1000_RDLEN    (0x02808/4)
#define E1000_RDTR     (0x02820/4)
#define E1000_RADV     (0x0282C/4)
#define E1000_TDBAL    (0x03800/4)
#define E1000_TDLEN    (0x03808/4)
#define E1000_TDH      (0x03810/4)
#define E1000_TDT      (0x03818/4)
#define E1000_MTA      (0x05200/4)
#define E1000_RA       (0x05400/4)

#define E1000_CTL_RST        0x00400000
#define E1000_TCTL_EN        0x00000002
#define E1000_TCTL_PSP       0x00000008
#define E1000_TCTL_CT_SHIFT  4
#define E1000_TCTL_COLD_SHIFT 12
#define E1000_RCTL_EN        0x00000002
#define E1000_RCTL_BAM       0x00008000
#define E1000_RCTL_SZ_2048   0x00000000
#define E1000_RCTL_SECRC     0x04000000
#define E1000_TXD_CMD_EOP    0x01
#define E1000_TXD_CMD_RS     0x08
#define E1000_TXD_STAT_DD    0x01
#define E1000_RXD_STAT_DD    0x01
#define E1000_RXD_STAT_EOP   0x02

struct tx_desc {
  uint64 addr;
  uint16 length;
  uint8 cso;
  uint8 cmd;
  uint8 status;
  uint8 css;
  uint16 special;
};

struct rx_desc {
  uint64 addr;
  uint16 length;
  uint16 csum;
  uint8 status;
  uint8 errors;
  uint16 special;
};
