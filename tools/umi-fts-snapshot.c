/* FTS521 snapshot through recovery's I2C_RDWR interface: read-only by default.
 * --scan-cycle temporarily changes scan mode; caller MUST wrap with F0/F1.
 * No firmware, persistent calibration, or flash writes in either mode. */
#include <stdint.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>

static int read_bytes(int fd, uint8_t *cmd, unsigned nc, uint8_t *buf, unsigned nb)
{
    struct i2c_msg msgs[2] = {
        {.addr=0x49, .len=nc, .buf=cmd},
        {.addr=0x49, .flags=I2C_M_RD, .len=nb, .buf=buf}
    };
    struct i2c_rdwr_ioctl_data xfer = {.msgs=msgs, .nmsgs=2};
    return ioctl(fd, I2C_RDWR, &xfer) == 2 ? 0 : -1;
}

int main(int argc, char **argv)
{
    int cycle = argc == 2 && !strcmp(argv[1], "--scan-cycle");
    if (argc != 1 && !cycle) {
        fprintf(stderr,"Usage: %s [--scan-cycle]\n",argv[0]); return 1;
    }
    int fd = open("/dev/i2c-3", O_RDWR);
    if (fd < 0) { perror("open"); return 1; }
    const uint32_t regs[] = {0x20000000,0x20000004,0x20000024,0x20000025,
        0x20000029,0x2000002d,0x20000030,0x20000032,0x20000034,0x20000078};
    for (unsigned i=0; i<sizeof(regs)/sizeof(regs[0]); ++i) {
        uint32_t r=regs[i];
        uint8_t cmd[5]={0xfa,r>>24,r>>16,r>>8,r}, buf[4]={0};
        if(read_bytes(fd,cmd,5,buf,4)) { perror("I2C_RDWR"); return 1; }
        printf("%08x: %02x %02x %02x %02x\n",r,buf[0],buf[1],buf[2],buf[3]);
    }
    uint8_t cmd[3]={0xa6,0,0}, info[208];
    if(read_bytes(fd,cmd,3,info,sizeof(info))) { perror("sysinfo"); return 1; }
    for(unsigned i=0;i<sizeof(info);i++) {
        if(i%16==0)printf("sysinfo %03x:",i);
        printf(" %02x",info[i]);
        if(i%16==15)puts("");
    }
    /* Vendor readSysInfo layout: 0x88 is mutual raw-frame address.
     * Read the first 32 nodes, without changing scan mode or consuming FIFO. */
    unsigned raw=info[0x88]|(info[0x89]<<8);
    if(info[0]!=0xa5 || !raw || raw>0xffc0) {
        fprintf(stderr,"Invalid raw-frame address %04x\n",raw); close(fd); return 1;
    }
    /* Opt-in controlled experiment: caller MUST disable host IRQ using F0
     * and restore F1 afterward even on failure. Default remains read-only.
     * Only scan OFF/ON; no persistent calibration or flash commands. */
    for(unsigned n=0;n<(cycle?16u:8u);n++) {
        if(cycle && (n==0 || n==8)) {
            uint8_t scan[3]={0xa0,0,n==8?1:0};
            struct i2c_msg msg={.addr=0x49,.len=3,.buf=scan};
            struct i2c_rdwr_ioctl_data x={.msgs=&msg,.nmsgs=1};
            if(ioctl(fd,I2C_RDWR,&x)!=1) {perror("scan change; restore F1!");close(fd);return 1;}
            printf("SCAN %s\n",n==8?"ON":"OFF"); usleep(250000);
        }
        uint8_t framecmd[3]={0xa6,raw>>8,raw}, frame[64];
        if(read_bytes(fd,framecmd,3,frame,64)) {perror("raw frame");close(fd);return 1;}
        uint32_t hash=2166136261u;
        int min=32767,max=-32768;
        for(unsigned i=0;i<64;i++) hash=(hash^frame[i])*16777619u;
        for(unsigned i=0;i<64;i+=2) {
            int v=(int16_t)(frame[i]|(frame[i+1]<<8));
            if(v<min)min=v;
            if(v>max)max=v;
        }
        printf("raw %04x sample%u hash=%08x min=%d max=%d\n",raw,n,hash,min,max);
        usleep(250000);
    }
    close(fd);
    return 0;
}
