/* Read-only FTS521 register snapshot through recovery's I2C_RDWR interface.
 * No reset, firmware update, scan-mode change, or register-data write. */
#include <stdint.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
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

int main(void)
{
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
    close(fd);
    return 0;
}
