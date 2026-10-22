/* Recovery-only controlled FTS521 command replay. No firmware/flash writes.
 * Caller disables vendor host IRQ with stm_fts_cmd F0 and MUST restore F1.
 * Build: aarch64-linux-gnu-gcc -O2 -Wall -Wextra -static ... -o replay */
#include <stdint.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <sys/ioctl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <linux/gpio.h>

static int gpio_fd = -1;
static struct gpiohandle_data levels = {.values={1,1}};
static void pause_bit(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC,&t);
    int64_t start=(int64_t)t.tv_sec*1000000000+t.tv_nsec;
    do { clock_gettime(CLOCK_MONOTONIC,&t); }
    while((int64_t)t.tv_sec*1000000000+t.tv_nsec-start<5000);
}
static int line(unsigned n,int high)
{
    levels.values[n]=high;
    int r=ioctl(gpio_fd,GPIOHANDLE_SET_LINE_VALUES_IOCTL,&levels);
    pause_bit();
    return r;
}
static int sample(unsigned n)
{
    struct gpiohandle_data got;
    if(ioctl(gpio_fd,GPIOHANDLE_GET_LINE_VALUES_IOCTL,&got))return -1;
    return got.values[n];
}
static int scl_high(void)
{
    if(line(1,1))return -1;
    for(unsigned i=0;i<1000;i++) { if(sample(1)==1)return 0;pause_bit(); }
    return -1;
}
static int start(void)
{
    if(line(0,1)||scl_high()||sample(0)!=1)return -1;
    return line(0,0)||line(1,0)?-1:0;
}
static void stop(void){line(0,0);scl_high();line(0,1);}
static int write_byte(uint8_t b)
{
    for(unsigned i=0;i<8;i++) {
        if(line(0,!!(b&128))||scl_high()||line(1,0))return -1;
        b<<=1;
    }
    if(line(0,1)||scl_high())return -1;
    int ack=sample(0)==0;
    line(1,0);
    return ack?0:-1;
}
static int read_byte(int ack)
{
    unsigned b=0;
    line(0,1);
    for(unsigned i=0;i<8;i++) {
        if(scl_high())return -1;
        int v=sample(0);
        if(v<0)return -1;
        b=(b<<1)|v;line(1,0);
    }
    line(0,!ack);scl_high();line(1,0);line(0,1);
    return b;
}

static int xfer(int fd, uint8_t *cmd, unsigned nc, uint8_t *buf, unsigned nb)
{
    if(gpio_fd>=0) {
        int err=start()||write_byte(0x92);
        for(unsigned i=0;i<nc&&!err;i++)err=write_byte(cmd[i]);
        if(nb&&!err)err=start()||write_byte(0x93);
        for(unsigned i=0;i<nb&&!err;i++) {
            int b=read_byte(i+1<nb);
            if(b<0)err=-1;else buf[i]=b;
        }
        stop();
        if(err)fprintf(stderr,"GPIO I2C transfer failed cmd=%02x SDA=%d SCL=%d\n",
            cmd[0],sample(0),sample(1));
        return err?-1:0;
    }
    struct i2c_msg msgs[2] = {
        {.addr=0x49, .len=nc, .buf=cmd},
        {.addr=0x49, .flags=I2C_M_RD, .len=nb, .buf=buf}
    };
    struct i2c_rdwr_ioctl_data x = {.msgs=msgs, .nmsgs=nb?2:1};
    if(ioctl(fd,I2C_RDWR,&x)!=(int)x.nmsgs) {perror("I2C_RDWR");return -1;}
    return 0;
}

int main(int argc,char **argv)
{
    setbuf(stdout,NULL);
    if(argc==2&&!strcmp(argv[1],"--gpio")) {
        int chip=open("/dev/gpiochip0",O_RDWR);
        struct gpiochip_info info;
        if(chip<0||ioctl(chip,GPIO_GET_CHIPINFO_IOCTL,&info)) {perror("gpiochip");return 1;}
        printf("GPIO chip label=%s lines=%u\n",info.label,info.lines);
        if(strcmp(info.label,"f000000.pinctrl")||info.lines!=180)return 1;
        struct gpiohandle_request req={.lineoffsets={36,37},.lines=2,
            .flags=GPIOHANDLE_REQUEST_OUTPUT|GPIOHANDLE_REQUEST_OPEN_DRAIN,
            .default_values={1,1},.consumer_label="umi-fts-replay"};
        if(ioctl(chip,GPIO_GET_LINEHANDLE_IOCTL,&req)) {perror("GPIO request");return 1;}
        gpio_fd=req.fd;close(chip);
        printf("GPIO open-drain idle SDA=%d SCL=%d\n",sample(0),sample(1));
    }
    int fd=open("/dev/i2c-3",O_RDWR);
    if(fd<0){perror("open");return 1;}
    uint8_t cmds[][6]={{0xa0,0,1},{0xa2,0,0},{0xa2,1,0},{0xa2,2,0},
        {0xa0,0,1},{0xa2,3,0,0,0,3}};
    for(unsigned i=0;i<6;i++) {
        if(xfer(fd,cmds[i],i==5?6:3,NULL,0))return 1;
    }
    puts("Exact XNU command sequence replayed; capture active for 40 seconds");
    time_t end=time(NULL)+40;
    unsigned events=0, fingers=0, empty=0;
    while(time(NULL)<end) {
        uint8_t cmd=0x86,b[32*8]={0};
        if(xfer(fd,&cmd,1,b,8))return 1;
        unsigned n=b[7]&31;
        if(n && xfer(fd,&cmd,1,b+8,n*8))return 1;
        for(unsigned i=0;i<=n;i++) {
            uint8_t *p=b+i*8;
            if(!p[0]){empty++;continue;}
            int finger=p[0]==0x13||p[0]==0x23||p[0]==0x33;
            events++;fingers+=finger;
            if(events<24 || (finger && fingers<40))
                printf("evt %02x %02x %02x %02x %02x %02x %02x %02x\n",
                    p[0],p[1],p[2],p[3],p[4],p[5],p[6],p[7]);
        }
        usleep(8000);
    }
    printf("RESULT events=%u finger_events=%u empty=%u\n",events,fingers,empty);
    close(fd);
    if(gpio_fd>=0)close(gpio_fd);
    return 0;
}
