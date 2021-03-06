/* drivers/hwmon/mt6516/amit/epl8865.c - EPL8865 ALS/PS driver
 *
 * Author: MingHsien Hsieh <minghsien.hsieh@mediatek.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

/** VERSION: 1.04**/

#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/miscdevice.h>
#include <asm/uaccess.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/kobject.h>
#include <linux/earlysuspend.h>
#include <linux/platform_device.h>
#include <asm/atomic.h>

#include <linux/hwmsensor.h>
#include <linux/hwmsen_dev.h>
#include <linux/sensors_io.h>
#include <asm/io.h>
#include <cust_eint.h>
#include <cust_alsps.h>
#include <linux/hwmsen_helper.h>
#include "epl8800.h"
#include "epl8865_gesture.h"
#include <linux/input/mt.h>

#ifdef MT6575
#include <mach/mt6575_devs.h>
#include <mach/mt6575_typedefs.h>
#include <mach/mt6575_gpio.h>
#include <mach/mt6575_pm_ldo.h>
#endif

#ifdef MT6589
#include <mach/devs.h>
#include <mach/mt_typedefs.h>
#include <mach/mt_gpio.h>
#include <mach/mt_pm_ldo.h>
#endif

#ifdef MT6582
#include <mach/devs.h>
#include <mach/mt_typedefs.h>
#include <mach/mt_gpio.h>
#include <mach/mt_pm_ldo.h>
#endif


//add for fix resume issue
#include <linux/earlysuspend.h>
#include <linux/wakelock.h>
//add for fix resume issue end

///add by amiral@{
#include <linux/xlog.h>
#define ANDROID_LOG_DEBUG "EPL8865_PROXIMITY"
#define EPL8865(fmt, arg...) xlog_printk(ANDROID_LOG_DEBUG, "[EPL8865] ",  fmt, ##arg)
///@}

/******************************************************************************
 * extern functions
*******************************************************************************/
#ifdef MT6575
extern void mt65xx_eint_unmask(unsigned int line);
extern void mt65xx_eint_mask(unsigned int line);
extern void mt65xx_eint_set_polarity(kal_uint8 eintno, kal_bool ACT_Polarity);
extern void mt65xx_eint_set_hw_debounce(kal_uint8 eintno, kal_uint32 ms);
extern kal_uint32 mt65xx_eint_set_sens(kal_uint8 eintno, kal_bool sens);
extern void mt65xx_eint_registration(kal_uint8 eintno, kal_bool Dbounce_En,
                                     kal_bool ACT_Polarity, void (EINT_FUNC_PTR)(void),
                                     kal_bool auto_umask);
#endif


#ifdef MT6589
extern void mt65xx_eint_unmask(unsigned int line);
extern void mt65xx_eint_mask(unsigned int line);
extern void mt65xx_eint_set_polarity(kal_uint8 eintno, kal_bool ACT_Polarity);
extern void mt65xx_eint_set_hw_debounce(kal_uint8 eintno, kal_uint32 ms);
extern kal_uint32 mt65xx_eint_set_sens(kal_uint8 eintno, kal_bool sens);
extern void mt65xx_eint_registration(kal_uint8 eintno, kal_bool Dbounce_En,
                                     kal_bool ACT_Polarity, void (EINT_FUNC_PTR)(void),
                                     kal_bool auto_umask);
#endif

#ifdef MT6582
extern void mt_eint_unmask(unsigned int line);
extern void mt_eint_mask(unsigned int line);
extern void mt_eint_set_polarity(kal_uint8 eintno, kal_bool ACT_Polarity);
extern void mt_eint_set_hw_debounce(kal_uint8 eintno, kal_uint32 ms);
extern kal_uint32 mt_eint_set_sens(kal_uint8 eintno, kal_bool sens);
//extern void mt_eint_registration(kal_uint8 eintno, kal_bool Dbounce_En,
//                                     kal_bool ACT_Polarity, void (EINT_FUNC_PTR)(void),
//                                     kal_bool auto_umask);
extern void mt_eint_registration(unsigned int eint_num, unsigned int flow, void (EINT_FUNC_PTR)(void), unsigned int is_auto_umask);

#endif


/*-------------------------MT6516&MT6575 define-------------------------------*/
#ifdef MT6575
#define POWER_NONE_MACRO MT65XX_POWER_NONE
#endif
#ifdef MT6589
#define POWER_NONE_MACRO MT65XX_POWER_NONE
#endif

#ifdef MT6572
#define POWER_NONE_MACRO MT65XX_POWER_NONE
#endif

#ifdef MT6582
#define POWER_NONE_MACRO MT65XX_POWER_NONE
#endif

/******************************************************************************
 *  configuration
 ******************************************************************************/
#define POLLING_MODE_GES		0
#define POLLING_MODE_HS		        0

#define LUX_PER_COUNT			700		// 0.7

static int ALS_INTT				= EPL_INTT_ALS_20;
static int PS_INTT 				= EPL_INTT_PS_90;

#define HS_INTT_CENTER				EPL_INTT_PS_40
static int HS_INTT 				= HS_INTT_CENTER;

static int GES_CURRENT 				= EPL_DRIVE_200MA;
static int GES_INTT				= EPL_INTT_ALS_100;
static int GES_CYCLE				= 1;//2 cycle
static int GES_GAIN				= 1;//m gain
static int GES_ADC				= 2;//interrupt: 10 bit(2), polling: 8 bit(1)

#define PHONE_CAL 1
static int ps_threshold_low_offset = 800;
static int ps_threshold_high_offset = 1000;

#define PS_DELAY 			40
#define ALS_DELAY 			120
#define ALS_GES_DELAY                   35
#define GES_DELAY 		      	5
#define HS_DELAY 		       30
/******************************************************************************
*******************************************************************************/

#define TXBYTES 			2
#define RXBYTES				2

#define PACKAGE_SIZE 			8
#define I2C_RETRY_COUNT 		2

#define EPL8865_DEV_NAME   		"EPL8865"
#define DRIVER_VERSION          "1.08"

// for heart rate
static struct mutex sensor_mutex;
static bool change_int_time = false;
static int hs_count=0;
static int hs_idx=0;
static int show_hs_raws_flag=0;
static int hs_als_flag=0;

#define GES_ALS_PERIOD	100
u16 ges_frame=0;

typedef struct _epl_ps_als_factory
{
    bool cal_file_exist;
    bool cal_finished;
    u16 ps_cal_h;
    u16 ps_cal_l;
    char ps_s1[16];
    char ps_s2[16];
};

typedef struct _epl_raw_data
{
    u8 raw_bytes[PACKAGE_SIZE];
    u8 buffer[8];
    u16 renvo;
    u16 ps_state;
    u16 ps_raw;
    u16 als_ch0_raw;
    u16 als_ch1_raw;
    u16 als_lux;
    u16 ges_data[4];
    u16 hs_data[200];
    u16 als_index;
    struct _epl_ps_als_factory ps_als_factory;
} epl_raw_data;

/*----------------------------------------------------------------------------*/
#define APS_TAG                 	  	"[ALS/PS]ALSPS_PROXIMITY"
#define APS_FUN(f)              	  	printk(KERN_INFO APS_TAG"%s\n", __FUNCTION__)
#define APS_ERR(fmt, args...)    	printk(KERN_ERR  APS_TAG"%s %d : "fmt, __FUNCTION__, __LINE__, ##args)
#define APS_LOG(fmt, args...)    	printk(KERN_INFO APS_TAG fmt, ##args)
#define APS_DBG(fmt, args...)    	printk(KERN_INFO fmt, ##args)

/*----------------------------------------------------------------------------*/
static struct i2c_client *epl8865_i2c_client = NULL;


/*----------------------------------------------------------------------------*/
static const struct i2c_device_id epl8865_i2c_id[] = {{/*"EPL8865"*/EPL8865_DEV_NAME ,0},{}};
static struct i2c_board_info __initdata i2c_EPL8865= {I2C_BOARD_INFO(/*"EPL8865"*/EPL8865_DEV_NAME, (0x82>>1))};


/*the adapter id & i2c address will be available in customization*/
//static unsigned short epl8865_force[] = {0x00, 0x92, I2C_CLIENT_END, I2C_CLIENT_END};
//static const unsigned short *const epl8865_forces[] = { epl8865_force, NULL };
//static struct i2c_client_address_data epl8865_addr_data = { .forces = epl8865_forces,};


/*----------------------------------------------------------------------------*/
static int epl8865_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id);
static int epl8865_i2c_remove(struct i2c_client *client);
static int epl8865_i2c_detect(struct i2c_client *client, int kind, struct i2c_board_info *info);


/*----------------------------------------------------------------------------*/
static int epl8865_i2c_suspend(struct i2c_client *client, pm_message_t msg);
static int epl8865_i2c_resume(struct i2c_client *client);

static void epl8865_eint_func(void);
static int set_psensor_intr_threshold(uint16_t low_thd, uint16_t high_thd);
int epl8865_read_als(struct i2c_client *client, u16 *data);
int epl8865_ps_operate(void* self, uint32_t command, void* buff_in, int size_in,
                       void* buff_out, int size_out, int* actualout);
static struct epl8865_priv *g_epl8865_ptr = NULL;

/*----------------------------------------------------------------------------*/
typedef enum
{
    CMC_BIT_ALS   	= 1,
    CMC_BIT_PS     	= 2,
    CMC_BIT_GES  	= 4,
    CMC_BIT_HS  		= 8,
} CMC_BIT;

/*----------------------------------------------------------------------------*/
struct epl8865_i2c_addr      /*define a series of i2c slave address*/
{
    u8  write_addr;
    u8  ps_thd;     /*PS INT threshold*/
};

/*----------------------------------------------------------------------------*/
struct epl8865_priv
{
    struct alsps_hw  *hw;
    struct i2c_client *client;
    struct delayed_work  eint_work;
    struct delayed_work  polling_work;
    struct input_dev *gs_input_dev;

    /*i2c address group*/
    struct epl8865_i2c_addr  addr;

    int 		polling_mode_ges;
    int 		polling_mode_hs;
    int		ir_type;

    /*misc*/
    atomic_t    	trace;
    atomic_t   	als_suspend;
    atomic_t    	ps_suspend;
    atomic_t	ges_suspend;
    atomic_t	hs_suspend;

    /*data*/
    u16		lux_per_count;
    ulong       	enable;         	/*record HAL enalbe status*/
    ulong      	pending_intr;   	/*pending interrupt*/

    /*data*/
    u16         	als_level_num;
    u16         	als_value_num;
    u32         	als_level[C_CUST_ALS_LEVEL-1];
    u32         	als_value[C_CUST_ALS_LEVEL];

    /*early suspend*/
#if defined(CONFIG_HAS_EARLYSUSPEND)
    struct early_suspend    early_drv;
#endif
};



/*----------------------------------------------------------------------------*/
static struct i2c_driver epl8865_i2c_driver =
{
    .probe     	= epl8865_i2c_probe,
    .remove     = epl8865_i2c_remove,
    .detect     	= epl8865_i2c_detect,
    .suspend    = epl8865_i2c_suspend,
    .resume     = epl8865_i2c_resume,
    .id_table   	= epl8865_i2c_id,
    //.address_data = &epl8865_addr_data,
    .driver = {
        //.owner          = THIS_MODULE,
        .name           = EPL8865_DEV_NAME,
    },
};


static struct epl8865_priv *epl8865_obj = NULL;
static struct platform_driver epl8865_alsps_driver;
static struct wake_lock ps_lock;
static epl_raw_data	gRawData;

/*
//====================I2C write operation===============//
//regaddr: ELAN epl8865 Register Address.
//bytecount: How many bytes to be written to epl8865 register via i2c bus.
//txbyte: I2C bus transmit byte(s). Single byte(0X01) transmit only slave address.
//data: setting value.
//
// Example: If you want to write single byte to 0x1D register address, show below
//	      epl8865_I2C_Write(client,0x1D,0x01,0X02,0xff);
//
*/
static int epl8865_I2C_Write(struct i2c_client *client, uint8_t regaddr, uint8_t bytecount, uint8_t txbyte, uint8_t data)
{
    //EPL8865("epl8865_I2C_Write %d\n",data);
    uint8_t buffer[2];
    int ret = 0;
    int retry;

    buffer[0] = (regaddr<<3) | bytecount ;
    buffer[1] = data;
    //EPL8865("epl8865_I2C_Write  regaddr:%d,bytecount:%d,txbyte:%d,data:%d,buffer[0]:%d,buffer[1]:%d\n",regaddr,bytecount,txbyte,data,buffer[0],buffer[1]);
    for(retry = 0; retry < I2C_RETRY_COUNT; retry++)
    {
        ret = i2c_master_send(client, buffer, txbyte);
       //EPL8865("epl8865_I2C_Write  ret:%d\n",ret);
        if (ret == txbyte)
        {
            break;
        }

        //APS_ERR("i2c write error,TXBYTES %d\n",ret);
        mdelay(10);
    }


    if(retry>=I2C_RETRY_COUNT)
    {
       //APS_ERR("i2c write retry over %d\n", I2C_RETRY_COUNT);
        return -EINVAL;
    }

    return ret;
}

static int epl8865_I2C_Write_long(struct i2c_client *client, uint8_t regaddr, uint8_t bytecount, uint8_t txbyte)
{
    int ret = 0;
    int retry;

    gRawData.buffer[0] = (regaddr<<3) | bytecount ;
    //EPL8865("epl8865_I2C_Write_long  regaddr:%d,bytecount:%d,txbyte:%d,gRawData.buffer[0]:%d\n",regaddr,bytecount,txbyte,gRawData.buffer[0]);
    for(retry = 0; retry < I2C_RETRY_COUNT; retry++)
    {
        ret = i2c_master_send(client, gRawData.buffer, txbyte);
       //APS_ERR("epl8865_I2C_Write_long,ret:%d\n",ret);
        if (ret == txbyte)
        {
            break;
        }

        //APS_ERR("epl8865_I2C_Write_long,i2c error,TXBYTES %d\n",ret);
        mdelay(10);
    }


    if(retry>=I2C_RETRY_COUNT)
    {
        //APS_ERR("i2c write retry over %d\n", I2C_RETRY_COUNT);
        return -EINVAL;
    }

    return ret;
}



/*
//====================I2C read operation===============//
*/
static int epl8865_I2C_Read(struct i2c_client *client)
{
    uint8_t buffer[RXBYTES];
    int ret = 0, i =0;
    int retry;

    for(retry = 0; retry < I2C_RETRY_COUNT; retry++)
    {
        ret = i2c_master_recv(client, buffer, RXBYTES);
        //EPL8865("epl8865_I2C_Read  ret:%d\n",ret);
        if (ret == RXBYTES)
            break;

        //APS_ERR("i2c read error,RXBYTES %d\r\n",ret);
        mdelay(10);
    }

    if(retry>=I2C_RETRY_COUNT)
    {
        //APS_ERR("i2c read retry over %d\n", I2C_RETRY_COUNT);
        return -EINVAL;
    }

    for(i=0; i<RXBYTES; i++)
        gRawData.raw_bytes[i] = buffer[i];

    return ret;
}


static int elan_epl8865_I2C_Read_long(struct i2c_client *client, int bytecount)
{
    uint8_t buffer[bytecount];
    int ret = 0, i =0;
    int retry;

    for(retry = 0; retry < I2C_RETRY_COUNT; retry++)
    {
        ret = i2c_master_recv(client, buffer, bytecount);
        //EPL8865("epl8865_I2C_Read_long  ret:%d\n",ret);
        if (ret == bytecount)
            break;

        //APS_ERR("i2c read error,RXBYTES %d\r\n",ret);
        mdelay(10);
    }

    if(retry>=I2C_RETRY_COUNT)
    {
        //APS_ERR("i2c read retry over %d\n", I2C_RETRY_COUNT);
        return -EINVAL;
    }

    for(i=0; i<bytecount; i++)
        gRawData.raw_bytes[i] = buffer[i];

    return ret;
}

static void epl8865_notify_event(int event)
{
    struct input_dev *idev = epl8865_obj->gs_input_dev;
    //GES_LOG("notify event %d\n",event);

    if(event<EVENT_UNKNOWN)
    {
        input_report_key(idev, KEYCODE_ARRAY[event], 1);
        input_report_key(idev, KEYCODE_ARRAY[event], 0);
        input_sync(idev);
    }
}

static void epl8865_hs_enable(struct epl8865_priv *epld, bool interrupt, bool full_enable)
{
    int ret;
    uint8_t regdata = 0;
    struct i2c_client *client = epld->client;
    //EPL8865("epl8865_hs_enable,interrupt:%d,full_enable:%d\n",interrupt,full_enable);
    if(full_enable)
    {
        regdata =  EPL_INT_CH1 | EPL_IR_INTERNAL | (interrupt? EPL_INT_FRAME_ENABLE : EPL_INT_DISABLE );
        ret = epl8865_I2C_Write(client,REG_6,W_SINGLE_BYTE,0x02, regdata);

        regdata =  EPL_DRIVE_200MA| EPL_IR_MODE_CURRENT;
        epl8865_I2C_Write(client,REG_9,W_SINGLE_BYTE,0x02,regdata);

        regdata = EPL_EN_VOS_ENABLED | EPL_EN_GFIN_ENABLED | EPL_DOC_ON_DISABLED;
        ret =   epl8865_I2C_Write(client,REG_28,W_SINGLE_BYTE,0x02,regdata);

        regdata =  EPL_PS_MODE |EPL_12BIT_ADC | EPL_L_GAIN|EPL_S_SENSING_MODE;
        ret = epl8865_I2C_Write(client,REG_0,W_SINGLE_BYTE,0x02,regdata);

        regdata = EPL_CK_FAST_1M | EPL_IRONCTRL_OFF;
        ret = epl8865_I2C_Write(client,REG_7,W_SINGLE_BYTE,0x02, regdata);

        regdata = HS_INTT |EPL_SENSING_2_TIME;
        ret = epl8865_I2C_Write(client,REG_1,W_SINGLE_BYTE,0x02,regdata);
        ret = epl8865_I2C_Write(client,REG_8,W_SINGLE_BYTE,0x02,EPL_C_RESET);

    }

    ret = epl8865_I2C_Write(client,REG_8,W_SINGLE_BYTE,0x02,EPL_C_START_RUN);

    if(epld->polling_mode_hs == 1){
        msleep(HS_DELAY);
    }
}

static void epl8865_gesture_enable(struct epl8865_priv *epld, bool interrupt, bool full_enable)
{
    int ret;
    uint8_t regdata = 0;
    struct i2c_client *client = epld->client;
    //EPL8865("epl8865_gesture_enable,interrupt:%d,full_enable:%d\n",interrupt,full_enable);
    if(full_enable)
    {
        regdata =  EPL_INT_CH1 | EPL_IR_INTERNAL | (interrupt? EPL_INT_FRAME_ENABLE : EPL_INT_DISABLE );
        ret = epl8865_I2C_Write(client,REG_6,W_SINGLE_BYTE,0x02, regdata);

        regdata = GES_CURRENT | EPL_IR_MODE_CURRENT;
        epl8865_I2C_Write(client,REG_9,W_SINGLE_BYTE,0x02,regdata);

        regdata = EPL_EN_VOS_ENABLED | EPL_EN_GFIN_ENABLED | EPL_DOC_ON_DISABLED;
        epl8865_I2C_Write(client,REG_28,W_SINGLE_BYTE,0x02,regdata);

        regdata =  EPL_GES_MODE | (GES_ADC<<2) | GES_GAIN |EPL_S_SENSING_MODE;
        ret = epl8865_I2C_Write(client,REG_0,W_SINGLE_BYTE,0X02,regdata);

        regdata = GES_INTT | (GES_CYCLE<<5);
        ret = epl8865_I2C_Write(client,REG_1,W_SINGLE_BYTE,0X02,regdata);
        ret = epl8865_I2C_Write(client,REG_8,W_SINGLE_BYTE,0X02,EPL_C_RESET);
    }

	epld->ir_type=epld->ir_type+1;

	if(epld->ir_type==4)
		epld->ir_type=0;


    //EPL8865("epl8865_gesture_enable,epld->ir_type:%d\n",epld->ir_type);
    if(epld->ir_type==0)
    {
        gRawData.buffer[1]=EPL_CK_FAST_2M | EPL_IRONCTRL_OFF;
    }
    else
    {
        gRawData.buffer[1]=EPL_CK_FAST_2M | EPL_IRONCTRL_ON;
    }

    gRawData.buffer[2]=EPL_C_START_RUN;
    ret = epl8865_I2C_Write_long(client,REG_7,W_TWO_BYTE,0x03);

}

//~~~~ add by ELAN Robert at 2014/2/8

static int elan_calibration_atoi(char* s)
{
    int num=0,flag=0;
    int i=0;
    //printk("[ELAN] %s\n", __func__);
    for(i=0; i<=strlen(s); i++)
    {
        if(s[i] >= '0' && s[i] <= '9')
            num = num * 10 + s[i] -'0';
        else if(s[0] == '-' && i==0)
            flag =1;
        else
            break;
    }
    if(flag == 1)
        num = num * -1;
    return num;
}

static int elan_calibaration_read(struct epl8865_priv *epl_data)
{
	struct file *fp_h;
	struct file *fp_l;
	struct i2c_client *client = epl_data->client;
	mm_segment_t fs;
	loff_t pos;
	//APS_LOG("[ELAN] %s\n", __func__);

      //modify by ELAN Robert, checking calibration exist
	if(gRawData.ps_als_factory.cal_file_exist == 1)
	{
		//fp_h = filp_open("/data/alsps/h-threshold.dat", O_RDWR, 0777);
		fp_h = filp_open("/data/data/com.eminent.ps.calibration/h-threshold.dat", O_RDWR, 0777); //modify by ELAN Robert at 2014/03/27
		if (IS_ERR(fp_h))
		{
			APS_ERR("[ELAN]create file_h error\n");
			gRawData.ps_als_factory.cal_file_exist = 0;

		}

		//fp_l = filp_open("/data/alsps/l-threshold.dat", O_RDWR, 0777);
		fp_l = filp_open("/data/data/com.eminent.ps.calibration/l-threshold.dat", O_RDWR, 0777); //modify by ELAN Robert at 2014/03/27

		if (IS_ERR(fp_l))
		{
			APS_ERR("[ELAN]create file_l error\n");
			gRawData.ps_als_factory.cal_file_exist = 0;
		}
	}

	//modify by ELAN Robert, open calibration and read high / low threshold to hw structure. if open file fail, high / low threshold will use default.
	if(gRawData.ps_als_factory.cal_file_exist == 1)
	{
        int read_ret = 0;
		fs = get_fs();
		set_fs(KERNEL_DS);
		pos = 0;
		//gRawData.als_factory.s1 = {NULL, NULL, NULL, NULL, NULL};
		read_ret = vfs_read(fp_h, gRawData.ps_als_factory.ps_s1, sizeof(gRawData.ps_als_factory.ps_s1), &pos);
		gRawData.ps_als_factory.ps_s1[read_ret] = '\0';

		pos = 0;
		//gRawData.als_factory.s2 = {NULL, NULL, NULL, NULL, NULL};
		read_ret = vfs_read(fp_l, gRawData.ps_als_factory.ps_s2, sizeof(gRawData.ps_als_factory.ps_s2), &pos);
		gRawData.ps_als_factory.ps_s2[read_ret] = '\0';


		filp_close(fp_h, NULL);
		filp_close(fp_l, NULL);
		set_fs(fs);

		gRawData.ps_als_factory.ps_cal_h = elan_calibration_atoi(gRawData.ps_als_factory.ps_s1);
		gRawData.ps_als_factory.ps_cal_l = elan_calibration_atoi(gRawData.ps_als_factory.ps_s2);
		epl_data->hw->ps_threshold_high = gRawData.ps_als_factory.ps_cal_h;
		epl_data->hw->ps_threshold_low = gRawData.ps_als_factory.ps_cal_l;
		APS_LOG("[ELAN] read cal_h: %d , cal_l : %d\n", gRawData.ps_als_factory.ps_cal_h,gRawData.ps_als_factory.ps_cal_l);
	}

	gRawData.ps_als_factory.cal_finished = 1;
	return 0;
}
//add by ELAN Robert at 2014/2/8~~~~

static int epl8865_psensor_enable(struct epl8865_priv *epl_data, int enable)
{
    int ret = 0;
    int ps_state;
    uint8_t regdata;
    struct i2c_client *client = epl_data->client;
    bool enable_als = test_bit(CMC_BIT_ALS, &epl_data->enable) && atomic_read(&epl_data->als_suspend)==0;
    int real_ps_state;

//APS_LOG("[ELAN epl8865] %s enable = %d\n", __func__, enable);
//EPL8865("epl8865_psensor_enable enable:%d\n",enable);

    ret = epl8865_I2C_Write(client,REG_6,W_SINGLE_BYTE,0x02, EPL_INT_DISABLE |  EPL_INT_CH1);

    if(enable)
    {
        regdata =  EPL_PS_MODE |EPL_10BIT_ADC | EPL_L_GAIN ;
        //EPL8865("epl8865_psensor_enable,regdata1:%d\n",regdata);

        regdata	=regdata | (epl_data->hw->polling_mode_ps==0? EPL_C_SENSING_MODE : EPL_S_SENSING_MODE);
        //EPL8865("epl8865_psensor_enable,regdata2:%d\n",regdata);

        ret = epl8865_I2C_Write(client,REG_0,W_SINGLE_BYTE,0X02,regdata);

        regdata = PS_INTT|EPL_SENSING_8_TIME;
        //EPL8865("epl8865_psensor_enable,regdata3:%d\n",regdata);
        ret = epl8865_I2C_Write(client,REG_1,W_SINGLE_BYTE,0X02,regdata);

        regdata = EPL_DRIVE_100MA | EPL_IR_MODE_CURRENT;
        //EPL8865("epl8865_psensor_enable,regdata4:%d\n",regdata);
        epl8865_I2C_Write(client,REG_9,W_SINGLE_BYTE,0x02,regdata);

         if(gRawData.ps_als_factory.cal_finished == 0 &&  gRawData.ps_als_factory.cal_file_exist ==1)
		    ret=elan_calibaration_read(epl_data);

        //APS_LOG("[%s] cal_finished = %d\, cal_file_exist = %d\n", __func__, gRawData.ps_als_factory.cal_finished , gRawData.ps_als_factory.cal_file_exist);

        set_psensor_intr_threshold(epl_data->hw ->ps_threshold_low,epl_data->hw ->ps_threshold_high);

        ret = epl8865_I2C_Write(client,REG_7,W_SINGLE_BYTE,0x02, EPL_CK_FAST_1M | EPL_IRONCTRL_OFF);
        ret = epl8865_I2C_Write(client,REG_8,W_SINGLE_BYTE,0X02,EPL_C_RESET);
        ret = epl8865_I2C_Write(client,REG_8,W_SINGLE_BYTE,0x02,EPL_C_START_RUN);

        msleep(PS_DELAY);

        epl8865_I2C_Write(client,REG_13,R_SINGLE_BYTE,0x01,0);
        epl8865_I2C_Read(client);
        real_ps_state= !((gRawData.raw_bytes[0]&0x04)>>2);
        //APS_LOG("[%s]: real_ps_state=%d \r\n", __func__, real_ps_state);

        epl8865_I2C_Write(client,REG_16,R_TWO_BYTE,0x01,0x00);
        epl8865_I2C_Read(client);
        gRawData.ps_raw = (gRawData.raw_bytes[1]<<8) | gRawData.raw_bytes[0];
        //APS_LOG("[%s]: gRawData.ps_raw=%d \r\n", __func__, gRawData.ps_raw);
#if 1
        //EPL8865("epl8865_psensor_enable,enable_als:%d\n",enable_als);
        if(enable_als == true)
        {
            if((real_ps_state==0 && gRawData.ps_raw > epl_data->hw->ps_threshold_high) ||
                    (real_ps_state==1 && gRawData.ps_raw < epl_data->hw->ps_threshold_low))
            {
                //APS_LOG("[%s]:change ps_state(real_ps_state=%d, gRawData.ps_state=%d) \r\n", __func__, real_ps_state, gRawData.ps_state);
                ps_state = real_ps_state;
            }
            else
            {
                ps_state = gRawData.ps_state;
            }
        }
        else
        {
            ps_state = real_ps_state;
            //APS_LOG("[%s]: PS only \r\n", __func__);
        }
#endif
        if(epl_data->hw->polling_mode_ps==0)
        {
            //EPL8865("epl8865_psensor_enable,polling_mode_ps:%d,gRawData.ps_state:%d,ps_state:%d\n",epl_data->hw->polling_mode_ps,gRawData.ps_state,ps_state);
            if(gRawData.ps_state != ps_state)
            {
#if 1
                hwm_sensor_data sensor_data;
                gRawData.ps_state = ps_state;

                sensor_data.values[0] = ps_state;
                sensor_data.value_divide = 1;
                sensor_data.status = SENSOR_STATUS_ACCURACY_MEDIUM;
                //let up layer to know
                hwmsen_get_interrupt_data(ID_PROXIMITY, &sensor_data);

                regdata =  EPL_INT_ACTIVE_LOW | EPL_INT_CH1;
                ret = epl8865_I2C_Write(client,REG_6,W_SINGLE_BYTE,0x02, regdata);
                //APS_LOG("[%s]: Driect report ps status .............\r\n", __func__);
#else
                regdata =   EPL_INT_FRAME_ENABLE | EPL_INT_CH1;
                //EPL8865("epl8865_psensor_enable,regdata5:%d\n",regdata);
                ret = epl8865_I2C_Write(client,REG_6,W_SINGLE_BYTE,0x02, regdata);
#endif
            }
            else
            {
                regdata =  EPL_INT_ACTIVE_LOW | EPL_INT_CH1;
                //EPL8865("epl8865_psensor_enable,regdata6:%d\n",regdata);
                ret = epl8865_I2C_Write(client,REG_6,W_SINGLE_BYTE,0x02, regdata);
                //PS_LOG("[%s]: EPL_INT_ACTIVE_LOW .............\r\n", __func__);
            }
        }

    }
    else
    {
        ret = epl8865_I2C_Write(client,REG_0,W_SINGLE_BYTE,0x02,EPL_S_SENSING_MODE);
    }

    if(ret<0)
    {
        //APS_ERR("[ELAN epl8865 error]%s: ps enable %d fail\n",__func__,ret);
    }
    else
    {
        ret = 0;
    }

    return ret;
}


static int epl8865_lsensor_enable(struct epl8865_priv *epl_data, int enable, bool ges_mode)
{
    int ret = 0;
    uint8_t regdata;
    struct i2c_client *client = epl_data->client;

    //APS_LOG("[ELAN epl8865] %s enable = %d\n", __func__, enable);
    //EPL8865("epl8865_lsensor_enable enable:%d\n",enable);

    if(enable)
    {
        regdata = EPL_INT_DISABLE;
        //EPL8865("epl8865_lsensor_enable regdata1:%d\n",regdata);
        ret = epl8865_I2C_Write(client,REG_6,W_SINGLE_BYTE,0x02, regdata);

        if(ges_mode){
            regdata =  EPL_ALS_MODE |EPL_8BIT_ADC | EPL_AUTO_GAIN |EPL_S_SENSING_MODE;
            //EPL8865("epl8865_lsensor_enable ges_mode==true ,regdata2:%d\n",regdata);
        }else{
            regdata =  EPL_ALS_MODE |EPL_10BIT_ADC | EPL_AUTO_GAIN |EPL_S_SENSING_MODE;
           // EPL8865("epl8865_lsensor_enable ges_mode==false ,regdata2:%d\n",regdata);
        }
        ret = epl8865_I2C_Write(client,REG_0,W_SINGLE_BYTE,0X02, regdata);

        regdata = ALS_INTT| EPL_SENSING_16_TIME;
        //EPL8865("epl8865_lsensor_enable,regdata3:%d\n",regdata);
        ret = epl8865_I2C_Write(client,REG_1,W_SINGLE_BYTE,0X02, regdata);
        ret = epl8865_I2C_Write(client,REG_7,W_SINGLE_BYTE,0x02, EPL_CK_FAST_1M | EPL_IRONCTRL_OFF);

        regdata = EPL_EN_VOS_ENABLED | EPL_EN_GFIN_ENABLED | EPL_DOC_ON_ENABLED;
        //EPL8865("epl8865_lsensor_enable,regdata4:%d\n",regdata);
        epl8865_I2C_Write(client,REG_28,W_SINGLE_BYTE,0x02, regdata);
        ret = epl8865_I2C_Write(client,REG_10,W_SINGLE_BYTE,0X02, EPL_GO_MID);
        ret = epl8865_I2C_Write(client,REG_11,W_SINGLE_BYTE,0x02, EPL_GO_LOW);

        ret = epl8865_I2C_Write(client,REG_8,W_SINGLE_BYTE,0X02, EPL_C_RESET);
        ret = epl8865_I2C_Write(client,REG_8,W_SINGLE_BYTE,0x02, EPL_C_START_RUN);
        if(ges_mode)
           msleep(ALS_GES_DELAY);
        else
            msleep(ALS_DELAY);
    }


    if(ret<0)
    {
        APS_ERR("[ELAN epl8865 error]%s: als_enable %d fail\n",__func__,ret);
    }
    else
    {
        ret = 0;
    }

    return ret;
}

static void epl8865_read_hs(void)
{
    mutex_lock(&sensor_mutex);
    struct epl8865_priv *epld = epl8865_obj;
    struct i2c_client *client = epld->client;
    int max_frame = 200;
    int idx = hs_idx+hs_count;
    u16 data;


    epl8865_I2C_Write(client,REG_16,R_TWO_BYTE,0x01,0x00);
    elan_epl8865_I2C_Read_long(client, 2);
    data=(gRawData.raw_bytes[1]<<8)|gRawData.raw_bytes[0];
    //EPL8865("epl8865_read_hs,data:%d\n",data);

    if(data>60800&& HS_INTT>HS_INTT_CENTER-2)
    {
        HS_INTT--;
        change_int_time=true;
    }
    else if(data>6400 && data <25600 && HS_INTT<HS_INTT_CENTER+2)
    {
        HS_INTT++;
        change_int_time=true;
    }
    else
    {
        change_int_time=false;

        if(idx>=max_frame)
            idx-=max_frame;

        gRawData.hs_data[idx] = data;

        if(hs_count>=max_frame)
        {
            hs_idx++;
            if(hs_idx>=max_frame)
                hs_idx=0;
        }

        hs_count++;
        if(hs_count>=max_frame)
            hs_count=max_frame;
    }
    mutex_unlock(&sensor_mutex);

}

static void epl8865_gesture_rawdata(void)
{
    struct epl8865_priv *epld = epl8865_obj;
    struct i2c_client *client = epld->client;
	int shift = GES_ADC>=2? 6 : 7;
    //EPL8865("epl8865_gesture_rawdata,shift:%d\n",shift);
    epl8865_I2C_Write(client,REG_14,R_EIGHT_BYTE,0x01,0x00);
    elan_epl8865_I2C_Read_long(client, 8);

    gRawData.ges_data[0]=(gRawData.raw_bytes[1]<<8 | gRawData.raw_bytes[0])>>shift;
    gRawData.ges_data[1]=(gRawData.raw_bytes[3]<<8 | gRawData.raw_bytes[2])>>shift;
    gRawData.ges_data[2]=(gRawData.raw_bytes[5]<<8 | gRawData.raw_bytes[4])>>shift;
    gRawData.ges_data[3]=(gRawData.raw_bytes[7]<<8 | gRawData.raw_bytes[6])>>shift;

    add_gesture_data(gRawData.ges_data, epld->ir_type);

    int event = detect_gesture_event();
    if(event!=EVENT_UNKNOWN)
        epl8865_notify_event(event);
    ges_frame++;


	 bool read_als = test_bit(CMC_BIT_ALS, &epld->enable) && atomic_read(&epld->als_suspend)==0 && ges_frame>=GES_ALS_PERIOD && !is_work_triger;

	 if(is_work_triger)
		ges_frame=1;

	 if(read_als)
	 {
		epl8865_lsensor_enable(epld, 1, true);
		epl8865_read_als(epld->client, &gRawData.als_ch1_raw);
		ges_frame=0;
	 }

}



static int epl8865_get_als_value(struct epl8865_priv *obj, u16 als)
{
    int idx;
    int invalid = 0;
    int lux = 0;

    lux = (als * obj->lux_per_count)/1000;

    for(idx = 0; idx < obj->als_level_num; idx++)
    {
        if(lux < obj->hw->als_level[idx])
        {
            break;
        }
    }

    if(idx >= obj->als_value_num)
    {
        APS_ERR("exceed range\n");
        idx = obj->als_value_num - 1;
    }

    if(!invalid)
    {
        gRawData.als_lux = obj->hw->als_value[idx];
        APS_DBG("ALS: %05d => %05d\n", als, obj->hw->als_value[idx]);
        return obj->hw->als_value[idx];
    }
    else
    {
        APS_ERR("ALS: %05d => %05d (-1)\n", als, obj->hw->als_value[idx]);
        return gRawData.als_lux;
    }
}


static int set_psensor_intr_threshold(uint16_t low_thd, uint16_t high_thd)
{
    int ret = 0;
    struct epl8865_priv *epld = epl8865_obj;
    struct i2c_client *client = epld->client;

    uint8_t high_msb ,high_lsb, low_msb, low_lsb;

    APS_LOG("%s\n", __func__);

    high_msb = (uint8_t) (high_thd >> 8);
    high_lsb   = (uint8_t) (high_thd & 0x00ff);
    low_msb  = (uint8_t) (low_thd >> 8);
    low_lsb    = (uint8_t) (low_thd & 0x00ff);

    APS_LOG("%s: low_thd = %d, high_thd = %d \n",__func__, low_thd, high_thd);

    epl8865_I2C_Write(client,REG_2,W_SINGLE_BYTE,0x02,high_lsb);
    epl8865_I2C_Write(client,REG_3,W_SINGLE_BYTE,0x02,high_msb);
    epl8865_I2C_Write(client,REG_4,W_SINGLE_BYTE,0x02,low_lsb);
    epl8865_I2C_Write(client,REG_5,W_SINGLE_BYTE,0x02,low_msb);

    return ret;
}



/*----------------------------------------------------------------------------*/
static void epl8865_dumpReg(struct i2c_client *client)
{
    APS_LOG("chip id REG 0x00 value = %8x\n", i2c_smbus_read_byte_data(client, 0x00));
    APS_LOG("chip id REG 0x01 value = %8x\n", i2c_smbus_read_byte_data(client, 0x08));
    APS_LOG("chip id REG 0x02 value = %8x\n", i2c_smbus_read_byte_data(client, 0x10));
    APS_LOG("chip id REG 0x03 value = %8x\n", i2c_smbus_read_byte_data(client, 0x18));
    APS_LOG("chip id REG 0x04 value = %8x\n", i2c_smbus_read_byte_data(client, 0x20));
    APS_LOG("chip id REG 0x05 value = %8x\n", i2c_smbus_read_byte_data(client, 0x28));
    APS_LOG("chip id REG 0x06 value = %8x\n", i2c_smbus_read_byte_data(client, 0x30));
    APS_LOG("chip id REG 0x07 value = %8x\n", i2c_smbus_read_byte_data(client, 0x38));
    APS_LOG("chip id REG 0x09 value = %8x\n", i2c_smbus_read_byte_data(client, 0x48));
    APS_LOG("chip id REG 0x0D value = %8x\n", i2c_smbus_read_byte_data(client, 0x68));
    APS_LOG("chip id REG 0x0E value = %8x\n", i2c_smbus_read_byte_data(client, 0x70));
    APS_LOG("chip id REG 0x0F value = %8x\n", i2c_smbus_read_byte_data(client, 0x71));
    APS_LOG("chip id REG 0x10 value = %8x\n", i2c_smbus_read_byte_data(client, 0x80));
    APS_LOG("chip id REG 0x11 value = %8x\n", i2c_smbus_read_byte_data(client, 0x88));
    APS_LOG("chip id REG 0x13 value = %8x\n", i2c_smbus_read_byte_data(client, 0x98));

 

//EPL8865("epl8865_I2C_Read  ret:%d\n",ret);

}


/*----------------------------------------------------------------------------*/
int hw8k_init_device(struct i2c_client *client)
{
    APS_LOG("hw8k_init_device.........\r\n");

    epl8865_i2c_client=client;

    //APS_LOG(" I2C Addr==[0x%x],line=%d\n",epl8865_i2c_client->addr,__LINE__);
    //EPL8865(" I2C Addr==[0x%x],line=%d\n",epl8865_i2c_client->addr,__LINE__);

    return 0;
}

/*----------------------------------------------------------------------------*/
int epl8865_get_addr(struct alsps_hw *hw, struct epl8865_i2c_addr *addr)
{
   //EPL8865("epl8865_get_addr,hw:%d,addr:%d\n",hw,addr);
    if(!hw || !addr)
    {
        return -EFAULT;
    }
    addr->write_addr= hw->i2c_addr[0];
    return 0;
}


/*----------------------------------------------------------------------------*/
static void epl8865_power(struct alsps_hw *hw, unsigned int on)
{
    static unsigned int power_on = 0;

    //APS_LOG("power %s\n", on ? "on" : "off");
#ifndef MT6582
    if(hw->power_id != POWER_NONE_MACRO)
    {
        if(power_on == on)
        {
            APS_LOG("ignore power control: %d\n", on);
        }
        else if(on)
        {
            if(!hwPowerOn(hw->power_id, hw->power_vol, "EPL8865"))
            {
                APS_ERR("power on fails!!\n");
            }
        }
        else
        {
            if(!hwPowerDown(hw->power_id, "EPL8865"))
            {
                APS_ERR("power off fail!!\n");
            }
        }
    }
    power_on = on;

#endif
}



/*----------------------------------------------------------------------------*/
static int epl8865_check_intr(struct i2c_client *client)
{
    struct epl8865_priv *obj = i2c_get_clientdata(client);
    int mode;

    APS_LOG("int pin = %d\n", mt_get_gpio_in(GPIO_ALS_EINT_PIN));

    //if (mt_get_gpio_in(GPIO_ALS_EINT_PIN) == 1) /*skip if no interrupt*/
    //   return 0;

    epl8865_I2C_Write(obj->client,REG_13,R_SINGLE_BYTE,0x01,0);
    epl8865_I2C_Read(obj->client);
    mode =(gRawData.raw_bytes[0]>>3)&7;
    //APS_LOG("mode %d\n", mode);
    //EPL8865("mode %d,pending_intr:%d\n", mode,&obj->pending_intr);

    if(mode==0x01)// PS
    {
        set_bit(CMC_BIT_PS, &obj->pending_intr);
    }
    else
    {
        clear_bit(CMC_BIT_PS, &obj->pending_intr);
    }

    //APS_LOG("check intr: 0x%08X\n", obj->pending_intr);

    return 0;

}


/*----------------------------------------------------------------------------*/

int epl8865_read_als(struct i2c_client *client, u16 *data)
{
    struct epl8865_priv *obj = i2c_get_clientdata(client);
    uint8_t setting;
    u16 ch1;

    if(client == NULL)
    {
        //APS_DBG("CLIENT CANN'T EQUL NULL\n");
        //EPL8865("CLIENT CANN'T EQUL NULL\n");
        return -1;
    }

    epl8865_I2C_Write(client,REG_13,R_SINGLE_BYTE,0x01,0);
    epl8865_I2C_Read(client);
    setting = gRawData.raw_bytes[0];
    if(((setting>>3)&7)!=0x00)
    {
        //APS_ERR("read als data in wrong mode\n");
        //EPL8865("read als data in wrong mode\n");
    }

    epl8865_I2C_Write(obj->client,REG_16,R_TWO_BYTE,0x01,0x00);
    epl8865_I2C_Read(obj->client);
    ch1 = (gRawData.raw_bytes[1]<<8) | gRawData.raw_bytes[0];

    // FIX: mid gain and low gain cannot report ff in auton gain
    if(setting>>6 ==2&& ch1==65535)
    {
       //APS_LOG("setting %d, gain %x, als %d\n", setting, setting>>6,  ch1);
        //APS_LOG("skip FF in auto gain\n\n");
        //EPL8865("setting %d, gain %x, als %d\n", setting, setting>>6,  ch1);
    }
    else
    {
        switch (setting>>6)
        {
            case EPL_H_GAIN:
                ch1 = ch1  >> 6;
                break;

            case EPL_M_GAIN:
                ch1 = ch1  >> 3;
                break;
        }

        *data =  ch1;
        //APS_LOG("read als raw data = %d\n",ch1);
        //EPL8865("read als raw data = %d\n",ch1);
    }

    return 0;
}


/*----------------------------------------------------------------------------*/
long epl8865_read_ps(struct i2c_client *client, u16 *data)
{
    struct epl8865_priv *obj = i2c_get_clientdata(client);
    uint8_t setting;
    int real_ps_state, ps_state;
    bool enable_als = test_bit(CMC_BIT_ALS, &obj->enable) && atomic_read(&obj->als_suspend)==0;
    //EPL8865("%s\n",__func__);
    if(client == NULL)
    {
        APS_DBG("CLIENT CANN'T EQUL NULL\n");
        return -1;
    }

    epl8865_I2C_Write(obj->client,REG_13,R_SINGLE_BYTE,0x01,0);
    epl8865_I2C_Read(obj->client);
    setting = gRawData.raw_bytes[0];
    if(((setting>>3)&7)!=0x01)
    {
        APS_ERR("read ps data in wrong mode\n");
    }
    real_ps_state = !((gRawData.raw_bytes[0]&0x04)>>2);

    epl8865_I2C_Write(obj->client,REG_16,R_TWO_BYTE,0x01,0x00);
    epl8865_I2C_Read(obj->client);
    gRawData.ps_raw = (gRawData.raw_bytes[1]<<8) | gRawData.raw_bytes[0];
#if 1
    if(enable_als == true)
    {
        if((real_ps_state==0 && gRawData.ps_raw > obj->hw->ps_threshold_high) ||
                (real_ps_state==1 && gRawData.ps_raw < obj->hw->ps_threshold_low))
        {
            APS_LOG("[%s]:change ps_state(real_ps_state=%d, gRawData.ps_state=%d) \r\n", __func__, real_ps_state, gRawData.ps_state);
            ps_state = real_ps_state;
        }
        else
        {
            ps_state = gRawData.ps_state;
        }
    }
    else
    {
        ps_state = real_ps_state;
        APS_LOG("[%s]: PS only \r\n", __func__);
    }
#endif
    gRawData.ps_state = ps_state;

    *data = gRawData.ps_raw;
    //APS_LOG("read ps raw data = %d\n", gRawData.ps_raw);
    //APS_LOG("read ps binary data = %d\n", gRawData.ps_state);

    return 0;
}

void  gesture_calibration(void)
{
    struct file *fp;
    mm_segment_t fs;
    loff_t pos;
    int value;
    char buffer[10]= {0};

    fp = filp_open("/data/data/com.eminent.gesture.calibration/gesture.dat", O_RDONLY, S_IRUSR);
    if (IS_ERR(fp))
    {
        APS_ERR("NO gesture calibration file\n");
        return;
    }
    fs = get_fs();
    set_fs(KERNEL_DS);
    pos = 0;
    vfs_read(fp, buffer, sizeof(buffer), &pos);

    sscanf(buffer, "%d", &value);
    crosstalk = value;
    zoom_enabled = true;

    filp_close(fp, NULL);
    set_fs(fs);
}


void epl8865_restart_polling(void)
{
    struct epl8865_priv *obj = epl8865_obj;
    int queue_flag = work_busy(&obj->polling_work);
    cancel_delayed_work(&obj->polling_work);
    //APS_LOG("[%s]: queue_flag=%d \r\n", __func__, queue_flag);
    if(queue_flag == 0)
    {
        schedule_delayed_work(&obj->polling_work, msecs_to_jiffies(50));
    }
    else
    {
        schedule_delayed_work(&obj->polling_work, msecs_to_jiffies(ALS_DELAY+2*PS_DELAY+50));
    }
}


void epl8865_polling_work(struct work_struct *work)
{
    struct epl8865_priv *obj = epl8865_obj;
    struct i2c_client *client = obj->client;

    bool enable_ps = test_bit(CMC_BIT_PS, &obj->enable) && atomic_read(&obj->ps_suspend)==0;
    bool enable_als = test_bit(CMC_BIT_ALS, &obj->enable) && atomic_read(&obj->als_suspend)==0;
    bool enable_ges = test_bit(CMC_BIT_GES, &obj->enable) && atomic_read(&obj->ges_suspend)==0;
    bool enable_hs = test_bit(CMC_BIT_HS, &obj->enable) && atomic_read(&obj->hs_suspend)==0;

    //APS_LOG("als / ps / gesture /hs enable: %d / %d / %d / %d \n", enable_als, enable_ps ,enable_ges, enable_hs);

    cancel_delayed_work(&obj->polling_work);

    if((enable_ps && obj->hw->polling_mode_ps == 1) || (enable_als==true && enable_ges==false && enable_hs==false) ||(enable_ps && enable_als) || (enable_als==true && enable_ps==false && enable_ges==false))
    {
        schedule_delayed_work(&obj->polling_work, msecs_to_jiffies(ALS_DELAY+2*PS_DELAY+30));
    }

    if(enable_als &&  (enable_ges==false || enable_ps==true))
    {
        epl8865_lsensor_enable(obj, 1, false);
        epl8865_read_als(client, &gRawData.als_ch1_raw);
    }

    if(enable_hs)
    {
        if (obj->polling_mode_hs==0)
        {
            epl8865_hs_enable(obj, true, true);
        }
        else
        {
            epl8865_read_hs();
            epl8865_hs_enable(obj, false, true);
            schedule_delayed_work(&obj->polling_work, msecs_to_jiffies(10));
        }
    }

    else if(enable_ps)
    {
        epl8865_psensor_enable(obj, 1);
        if(obj->hw->polling_mode_ps==1)
        {
            epl8865_read_ps(client, &gRawData.ps_raw);
        }
    }
    else if(enable_ges)
    {
        if (obj->polling_mode_ges==0)
        {
            epl8865_gesture_enable(obj, true, true);
        }
        else
        {
            epl8865_gesture_rawdata();
            epl8865_gesture_enable(obj, false, ges_frame==0?true:false);//v8
	        schedule_delayed_work(&obj->polling_work, msecs_to_jiffies(10));
            /*epl8865_gesture_enable(obj, false, true);
            schedule_delayed_work(&obj->polling_work, msecs_to_jiffies(5));*/
        }
    }

    if(enable_als==false && enable_ps==false && enable_ges==false && enable_hs==false)
    {
        //APS_LOG("disable sensor\n");
        //EPL8865("disable sensor\n");
        cancel_delayed_work(&obj->polling_work);
        epl8865_I2C_Write(client,REG_6,W_SINGLE_BYTE,0x02,EPL_INT_DISABLE);
        epl8865_I2C_Write(client,REG_0,W_SINGLE_BYTE,0x02,EPL_S_SENSING_MODE);
    }

}


/*----------------------------------------------------------------------------*/
void epl8865_eint_func(void)
{
    struct epl8865_priv *obj = g_epl8865_ptr;

    // APS_LOG(" interrupt fuc\n");

    if(!obj)
    {
        return;
    }

#ifdef MT6582
    mt_eint_mask(CUST_EINT_ALS_NUM);
#else
    mt65xx_eint_mask(CUST_EINT_ALS_NUM);
#endif

    schedule_delayed_work(&obj->eint_work, 0);
}



/*----------------------------------------------------------------------------*/
static void epl8865_eint_work(struct work_struct *work)
{
    struct epl8865_priv *epld = g_epl8865_ptr;
    struct epl8865_priv *obj = epl8865_obj;
    hwm_sensor_data sensor_data;
    int err;
    int real_ps_state;

    bool enable_als = test_bit(CMC_BIT_ALS, &obj->enable) && atomic_read(&obj->als_suspend)==0;
    bool enable_ps = test_bit(CMC_BIT_PS, &obj->enable) && atomic_read(&obj->ps_suspend)==0;
    bool enable_ges = test_bit(CMC_BIT_GES, &obj->enable) && atomic_read(&obj->ges_suspend)==0;

    //EPL8865("xxxxx eint work %s\n",__func__);
    if(test_bit(CMC_BIT_HS, &epld->enable) && atomic_read(&epld->hs_suspend)==0)
    {
        epl8865_read_hs();
        epl8865_hs_enable(epld, true, change_int_time);
    }
    else if(test_bit(CMC_BIT_PS, &epld->enable))
    {
        //APS_LOG("xxxxx eint work\n");
        if((err = epl8865_check_intr(epld->client)))
        {
            APS_ERR("check intrs: %d\n", err);
        }

        if(epld->pending_intr)
        {
            epl8865_I2C_Write(epld->client,REG_13,R_SINGLE_BYTE,0x01,0);
            epl8865_I2C_Read(epld->client);
            real_ps_state= !((gRawData.raw_bytes[0]&0x04)>>2);
            //APS_LOG("[%s]: real_ps_state=%d \r\n", __func__, real_ps_state);

            epl8865_I2C_Write(epld->client,REG_16,R_TWO_BYTE,0x01,0x00);
            epl8865_I2C_Read(epld->client);
            gRawData.ps_raw = (gRawData.raw_bytes[1]<<8) | gRawData.raw_bytes[0];
            //APS_LOG("ps raw_data = %d\n", gRawData.ps_raw);
#if 1
            if(enable_als == true)
            {
                if((real_ps_state==0 && gRawData.ps_raw > epld->hw->ps_threshold_high) ||
                        (real_ps_state==1 && gRawData.ps_raw < epld->hw->ps_threshold_low))
                {
                    //APS_LOG("[%s]:change ps_state(real_ps_state=%d, gRawData.ps_state=%d) \r\n", __func__, real_ps_state, gRawData.ps_state);
                    gRawData.ps_state = real_ps_state;
                }

            }
            else
            {
                gRawData.ps_state = real_ps_state;
                //APS_LOG("[%s]: PS only \r\n", __func__);
            }
#endif
            //APS_LOG("[%s]:ps state = %d\n", __func__, gRawData.ps_state);

            sensor_data.values[0] = gRawData.ps_state;
            sensor_data.value_divide = 1;
            sensor_data.status = SENSOR_STATUS_ACCURACY_MEDIUM;

            //let up layer to know
            if((err = hwmsen_get_interrupt_data(ID_PROXIMITY, &sensor_data)))
            {
                //APS_ERR("get interrupt data failed\n");
                //APS_ERR("call hwmsen_get_interrupt_data fail = %d\n", err);
            }
        }

        epl8865_I2C_Write(epld->client,REG_6,W_SINGLE_BYTE,0x02,EPL_INT_ACTIVE_LOW | EPL_INT_CH1);
        epl8865_I2C_Write(epld->client,REG_8,W_SINGLE_BYTE,0x02,EPL_DATA_UNLOCK);
    }
    else if(test_bit(CMC_BIT_GES, &epld->enable) && atomic_read(&epld->ges_suspend)==0)
    {
        epl8865_gesture_rawdata();
		epl8865_gesture_enable(epld, true, ges_frame==0?true:false);//v8
           //epl8865_gesture_enable(epld, true, false);//
    }

#ifdef MT6575
    mt65xx_eint_unmask(CUST_EINT_ALS_NUM);
#endif

#ifdef MT6589
    mt65xx_eint_unmask(CUST_EINT_ALS_NUM);
#endif

#ifdef MT6582
    mt_eint_unmask(CUST_EINT_ALS_NUM);
#endif
}



/*----------------------------------------------------------------------------*/
int epl8865_setup_eint(struct i2c_client *client)
{
    struct epl8865_priv *obj = i2c_get_clientdata(client);

   //APS_LOG("epl8865_setup_eint\n");
  //EPL8865("epl8865_setup_eint\n");

    g_epl8865_ptr = obj;

    /*configure to GPIO function, external interrupt*/
//amiral:
    //mt_set_gpio_mode(GPIO_ALS_EINT_PIN, GPIO_ALS_EINT_PIN_M_EINT);//orginal..
mt_set_gpio_mode(GPIO_ALS_EINT_PIN, GPIO_ALS_EINT_PIN_M_MDEINT);//add 
    mt_set_gpio_dir(GPIO_ALS_EINT_PIN, GPIO_DIR_IN);
    mt_set_gpio_pull_enable(GPIO_ALS_EINT_PIN, GPIO_PULL_ENABLE);
    mt_set_gpio_pull_select(GPIO_ALS_EINT_PIN, GPIO_PULL_UP);

#ifdef  MT6575
    mt65xx_eint_set_sens(CUST_EINT_ALS_NUM, CUST_EINT_EDGE_SENSITIVE);
    mt65xx_eint_set_polarity(CUST_EINT_ALS_NUM, CUST_EINT_ALS_POLARITY);
    mt65xx_eint_set_hw_debounce(CUST_EINT_ALS_NUM, CUST_EINT_ALS_DEBOUNCE_CN);
    mt65xx_eint_registration(CUST_EINT_ALS_NUM, CUST_EINT_ALS_DEBOUNCE_EN, CUST_EINT_POLARITY_LOW, epl8865_eint_func, 0);
    mt65xx_eint_unmask(CUST_EINT_ALS_NUM);
#endif

#ifdef  MT6589
    mt65xx_eint_set_sens(CUST_EINT_ALS_NUM, CUST_EINT_EDGE_SENSITIVE);
    mt65xx_eint_set_polarity(CUST_EINT_ALS_NUM, CUST_EINT_ALS_POLARITY);
    mt65xx_eint_set_hw_debounce(CUST_EINT_ALS_NUM, CUST_EINT_ALS_DEBOUNCE_CN);
    mt65xx_eint_registration(CUST_EINT_ALS_NUM, CUST_EINT_ALS_DEBOUNCE_EN, CUST_EINT_POLARITY_LOW, epl8865_eint_func, 0);
    mt65xx_eint_unmask(CUST_EINT_ALS_NUM);
#endif

#ifdef MT6582
	//mt_eint_set_hw_debounce(CUST_EINT_ALS_NUM, CUST_EINT_ALS_DEBOUNCE_CN);
	mt_eint_registration(CUST_EINT_ALS_NUM, CUST_EINT_ALS_TYPE, epl8865_eint_func, 0);

	mt_eint_unmask(CUST_EINT_ALS_NUM);
#endif
    return 0;
}




/*----------------------------------------------------------------------------*/
static int epl8865_init_client(struct i2c_client *client)
{
    struct epl8865_priv *obj = i2c_get_clientdata(client);
    int err=0;

    //APS_LOG("[Agold spl] I2C Addr==[0x%x],line=%d\n",epl8865_i2c_client->addr,__LINE__);
    //EPL8865("[Agold spl] I2C Addr==[0x%x],line=%d\n",epl8865_i2c_client->addr,__LINE__);
    /*  interrupt mode */


    APS_FUN();

    if(obj->hw->polling_mode_ps == 0)
    {
#ifdef MT6582
        mt_eint_mask(CUST_EINT_ALS_NUM);
#else
        mt65xx_eint_mask(CUST_EINT_ALS_NUM);
#endif

        if((err = epl8865_setup_eint(client)))
        {
            APS_ERR("setup eint: %d\n", err);
            return err;
        }
        APS_LOG("epl8865 interrupt setup\n");
    }


    if((err = hw8k_init_device(client)) != 0)
    {
        APS_ERR("init dev: %d\n", err);
        return err;
    }


    if((err = epl8865_check_intr(client)))
    {
        APS_ERR("check/clear intr: %d\n", err);
        return err;
    }


    /*  interrupt mode */
//if(obj->hw->polling_mode_ps == 0)
    //     mt65xx_eint_unmask(CUST_EINT_ALS_NUM);

    return err;
}


/*----------------------------------------------------------------------------*/
static ssize_t epl8865_show_reg(struct device_driver *ddri, char *buf)
{
EPL8865("%s\n",__func__);
    if(!epl8865_obj)
    {
        APS_ERR("epl8865_obj is null!!\n");
        return 0;
    }
    ssize_t len = 0;
    struct i2c_client *client = epl8865_obj->client;

    len += snprintf(buf+len, PAGE_SIZE-len, "chip id REG 0x00 value = %8x\n", i2c_smbus_read_byte_data(client, 0x00));
    len += snprintf(buf+len, PAGE_SIZE-len, "chip id REG 0x01 value = %8x\n", i2c_smbus_read_byte_data(client, 0x08));
    len += snprintf(buf+len, PAGE_SIZE-len, "chip id REG 0x02 value = %8x\n", i2c_smbus_read_byte_data(client, 0x10));
    len += snprintf(buf+len, PAGE_SIZE-len, "chip id REG 0x03 value = %8x\n", i2c_smbus_read_byte_data(client, 0x18));
    len += snprintf(buf+len, PAGE_SIZE-len, "chip id REG 0x04 value = %8x\n", i2c_smbus_read_byte_data(client, 0x20));
    len += snprintf(buf+len, PAGE_SIZE-len, "chip id REG 0x05 value = %8x\n", i2c_smbus_read_byte_data(client, 0x28));
    len += snprintf(buf+len, PAGE_SIZE-len, "chip id REG 0x06 value = %8x\n", i2c_smbus_read_byte_data(client, 0x30));
    len += snprintf(buf+len, PAGE_SIZE-len, "chip id REG 0x07 value = %8x\n", i2c_smbus_read_byte_data(client, 0x38));
    len += snprintf(buf+len, PAGE_SIZE-len, "chip id REG 0x09 value = %8x\n", i2c_smbus_read_byte_data(client, 0x48));
    len += snprintf(buf+len, PAGE_SIZE-len, "chip id REG 0x0D value = %8x\n", i2c_smbus_read_byte_data(client, 0x68));
    len += snprintf(buf+len, PAGE_SIZE-len, "chip id REG 0x0E value = %8x\n", i2c_smbus_read_byte_data(client, 0x70));
    len += snprintf(buf+len, PAGE_SIZE-len, "chip id REG 0x0F value = %8x\n", i2c_smbus_read_byte_data(client, 0x71));
    len += snprintf(buf+len, PAGE_SIZE-len, "chip id REG 0x10 value = %8x\n", i2c_smbus_read_byte_data(client, 0x80));
    len += snprintf(buf+len, PAGE_SIZE-len, "chip id REG 0x11 value = %8x\n", i2c_smbus_read_byte_data(client, 0x88));
    len += snprintf(buf+len, PAGE_SIZE-len, "chip id REG 0x13 value = %8x\n", i2c_smbus_read_byte_data(client, 0x98));
    len += snprintf(buf+len, PAGE_SIZE-len, "chip id REG 0x17 value = %8x\n", i2c_smbus_read_byte_data(client, 0xb8));

    return len;

}

/*----------------------------------------------------------------------------*/
static ssize_t epl8865_show_status(struct device_driver *ddri, char *buf)
{
    //EPL8865("%s\n",__func__);
    ssize_t len = 0;
    struct epl8865_priv *epld = epl8865_obj;


    if(!epl8865_obj)
    {
        APS_ERR("epl8865_obj is null!!\n");
        return 0;
    }
    len += snprintf(buf+len, PAGE_SIZE-len, "chip is %s, ver is %s \n", EPL8865_DEV_NAME, DRIVER_VERSION);
    len += snprintf(buf+len, PAGE_SIZE-len, "als/ps/ges enable is %d-%d-%d\n",test_bit(CMC_BIT_ALS, &epld->enable) ,test_bit(CMC_BIT_PS, &epld->enable) ,test_bit(CMC_BIT_GES, &epld->enable) );
    len += snprintf(buf+len, PAGE_SIZE-len, "als/ps int time is %d-%d\n",ALS_INTT, PS_INTT);
    len += snprintf(buf+len, PAGE_SIZE-len, "ps threshold is %d / %d\n",epld->hw->ps_threshold_low, epld->hw->ps_threshold_high);
    len += snprintf(buf+len, PAGE_SIZE-len, "gesture polling mode is %d\n",epld->polling_mode_ges);
    len += snprintf(buf+len, PAGE_SIZE-len, "gesture work is %d \n",WORK_TH);
    len += snprintf(buf+len, PAGE_SIZE-len, "gesture int time: %d,  opt: %d\n", GES_INTT, opt);
    len += snprintf(buf+len, PAGE_SIZE-len, "crosstalk: %d\n", crosstalk);
    len += snprintf(buf+len, PAGE_SIZE-len, "heart int time: %d\n", HS_INTT);

    return len;
}


/*----------------------------------------------------------------------------*/
static ssize_t epl8865_show_renvo(struct device_driver *ddri, char *buf)
{
    ssize_t len = 0;
    len += snprintf(buf+len, PAGE_SIZE-len, "%x",gRawData.renvo);
    return len;
}


/*----------------------------------------------------------------------------*/
static ssize_t epl8865_store_als_enable(struct device_driver *ddri, const char *buf, size_t count)
{
    uint16_t mode=0;
    struct epl8865_priv *obj = epl8865_obj;
    APS_FUN();

    sscanf(buf, "%hu",&mode);
    //EPL8865("epl8865_store_als_enable,mode:%d\n",mode);
    if(mode)
        set_bit(CMC_BIT_ALS, &obj->enable);
    else
        clear_bit(CMC_BIT_ALS, &obj->enable);

    epl8865_restart_polling();
    return count;
}

/*----------------------------------------------------------------------------*/
static ssize_t epl8865_store_als_int_time(struct device_driver *ddri, const char *buf, size_t count)
{
    if(!epl8865_obj)
    {
        APS_ERR("epl8865_obj is null!!\n");
        return 0;
    }

    sscanf(buf, "%d", &ALS_INTT);
    //APS_LOG("als int time is %d\n", ALS_INTT);
    return count;
}

/*----------------------------------------------------------------------------*/
static ssize_t epl8865_store_ps_enable(struct device_driver *ddri, const char *buf, size_t count)
{
    uint16_t mode=0;
    struct epl8865_priv *obj = epl8865_obj;
    APS_FUN();

    sscanf(buf, "%hu",&mode);
    //EPL8865("epl8865_store_ps_enable,mode:%d\n",mode);
    if(mode)
        set_bit(CMC_BIT_PS, &obj->enable);
    else
        clear_bit(CMC_BIT_PS, &obj->enable);

    epl8865_restart_polling();
    return count;
}

/*----------------------------------------------------------------------------*/
static ssize_t epl8865_show_ps_cal_raw(struct device_driver *ddri, char *buf)
{
    struct epl8865_priv *obj = epl8865_obj;
    u16 ch1;
    u32 ch1_all=0;
    int count =5;
    int i;
    ssize_t len = 0;

    if(!epl8865_obj)
    {
        APS_ERR("epl8865_obj is null!!\n");
        return 0;
    }


    set_bit(CMC_BIT_PS, &obj->enable);
    epl8865_restart_polling();

    for(i=0; i<count; i++)
    {
        msleep(PS_DELAY);
        if(obj->hw->polling_mode_ps == 0)
        {
            epl8865_I2C_Write(obj->client,REG_16,R_TWO_BYTE,0x01,0x00);
            epl8865_I2C_Read(obj->client);
            gRawData.ps_raw = (gRawData.raw_bytes[1]<<8) | gRawData.raw_bytes[0];
        }
       // APS_LOG("epl8865_show_ps_cal_raw: gRawData.ps_raw=%d \r\n", gRawData.ps_raw);
           ch1_all = ch1_all+ gRawData.ps_raw;

    }

    ch1 = (u16)ch1_all/count;
	//APS_LOG("epl8865_show_ps_cal_raw =  %d\n", ch1);

    len += snprintf(buf+len, PAGE_SIZE-len, "%d \r\n", ch1);

	return len;
}

static ssize_t epl8865_show_ps_threshold(struct device_driver *ddri, char *buf)
{
    ssize_t len = 0;
    struct epl8865_priv *obj = epl8865_obj;

    len += snprintf(buf+len, PAGE_SIZE-len, "gRawData.ps_als_factory(H/L): %d/%d \r\n", gRawData.ps_als_factory.ps_cal_h, gRawData.ps_als_factory.ps_cal_l);
    len += snprintf(buf+len, PAGE_SIZE-len, "ps_threshold(H/L): %d/%d \r\n", epl8865_obj->hw->ps_threshold_high, epl8865_obj->hw->ps_threshold_low);
    return len;
}



/*----------------------------------------------------------------------------*/
static ssize_t epl8865_store_ps_int_time(struct device_driver *ddri, const char *buf, size_t count)
{
//EPL8865("epl8865_obj is null!!\n");
    if(!epl8865_obj)
    {
        APS_ERR("epl8865_obj is null!!\n");
        return 0;
    }
    sscanf(buf, "%d", &PS_INTT);
    APS_LOG("ps int time is %d\n", PS_INTT);
    return count;
}

/*----------------------------------------------------------------------------*/
static ssize_t epl8865_store_ps_threshold(struct device_driver *ddri, const char *buf, size_t count)
{
    if(!epl8865_obj)
    {
        APS_ERR("epl8865_obj is null!!\n");
        return 0;
    }

    sscanf(buf, "%d,%d", &epl8865_obj->hw->ps_threshold_low, &epl8865_obj->hw->ps_threshold_high);
    gRawData.ps_als_factory.ps_cal_h = epl8865_obj->hw->ps_threshold_high;
    gRawData.ps_als_factory.ps_cal_l = epl8865_obj->hw->ps_threshold_low;

    epl8865_restart_polling();

    return count;
}

/*----------------------------------------------------------------------------*/
static ssize_t epl8865_store_ps_polling_mode(struct device_driver *ddri, const char *buf, size_t count)
{
    struct epl8865_priv *obj = epl8865_obj;
    struct i2c_client *client = obj->client;
    struct hwmsen_object obj_ps;
    sscanf(buf, "%d",&obj->hw->polling_mode_ps);

    hwmsen_detach(ID_PROXIMITY);
    //APS_LOG("epld->hw->polling_mode_ps=%d \r\n", obj->hw->polling_mode_ps);

    obj_ps.self = obj;
    obj_ps.polling = obj->hw->polling_mode_ps;
    obj_ps.sensor_operate = epl8865_ps_operate;
    if(hwmsen_attach(ID_PROXIMITY, &obj_ps))
    {
        APS_ERR("[%s]: attach fail !\n", __FUNCTION__);
    }

    return count;
}


/*----------------------------------------------------------------------------*/
static ssize_t epl8865_store_ges_enable(struct device_driver *ddri, const char *buf, size_t count)
{
    uint16_t mode=0;
    struct epl8865_priv *obj = epl8865_obj;
    APS_FUN();

    sscanf(buf, "%hu",&mode);
    //EPL8865("epl8865_store_ges_enable,mode:%d\n",mode);
    if(mode)
        set_bit(CMC_BIT_GES, &obj->enable);
    else
        clear_bit(CMC_BIT_GES, &obj->enable);

    is_work_triger=false;
    is_hold_triger=false;

    if(mode)
    {
        start_idx=0;
        ges_count=0;
        ges_frame=0;
        obj->ir_type = 0;
        gesture_calibration();
		if(obj->polling_mode_ges==1)
			epl8865_gesture_enable(obj, false, true);
    }

	if(mode==2)
	{
		zoom_enabled = true;
		crosstalk=MAX;
	}
	else
	{
		zoom_enabled = false;
	}

    epl8865_restart_polling();
    return count;
}


/*----------------------------------------------------------------------------*/
static ssize_t epl8865_show_ges_raws(struct device_driver *ddri, char *buf)
{
    u16 *tmp = (u16*)buf;
    u16 length= ges_count;
    int byte_count=2+length*2;
    int i=0;
    int start = 0;

    tmp[0]= length;
    for(i=start; i<length; i++)
    {
        tmp[i+1]= ges_raws[i];
        ges_raws[i]=0;
    }
ges_count=0;
    return byte_count;
}

/*----------------------------------------------------------------------------*/
static ssize_t epl8865_store_ges_int_time(struct device_driver *ddri, const char *buf, size_t count)
{
    sscanf(buf, "%d",&GES_INTT);
    return count;
}

/*----------------------------------------------------------------------------*/
static ssize_t epl8865_store_ges_cycle(struct device_driver *ddri, const char *buf, size_t count)
{
    sscanf(buf, "%d",&GES_CYCLE);
    return count;
}

/*----------------------------------------------------------------------------*/
static ssize_t epl8865_store_ges_opt(struct device_driver *ddri, const char *buf, size_t count)
{
    sscanf(buf, "%d",&opt);
    return count;
}

/*----------------------------------------------------------------------------*/
static ssize_t epl8865_store_ges_avg_length(struct device_driver *ddri, const char *buf, size_t count)
{
    sscanf(buf, "%d",&avg_length);
    return count;
}

/*----------------------------------------------------------------------------*/
static ssize_t epl8865_store_ges_debug(struct device_driver *ddri, const char *buf, size_t count)
{
    sscanf(buf, "%d",&GESTURE_DEBUG);
    return count;
}

/*----------------------------------------------------------------------------*/
static ssize_t epl8865_store_ges_work_threshold(struct device_driver *ddri, const char *buf, size_t count)
{
    sscanf(buf, "%d",&WORK_TH);
    return count;
}

/*----------------------------------------------------------------------------*/
static ssize_t epl8865_store_ges_polling_mode(struct device_driver *ddri, const char *buf, size_t count)
{
    struct epl8865_priv *epld = epl8865_obj;
    sscanf(buf, "%d",&epld->polling_mode_ges);
    return count;
}

/*----------------------------------------------------------------------------*/
static ssize_t epl8865_store_ges_gain(struct device_driver *ddri, const char *buf, size_t count)
{
    sscanf(buf, "%d",&GES_GAIN);
    return count;
}

/*----------------------------------------------------------------------------*/
static ssize_t epl8865_store_ges_mode(struct device_driver *ddri, const char *buf, size_t count)
{
    sscanf(buf, "%d",&ges_mode);
    return count;
}

/*----------------------------------------------------------------------------*/
static ssize_t epl8865_store_ges_crosstalk(struct device_driver *ddri, const char *buf, size_t count)
{
    sscanf(buf, "%d",&crosstalk);
    zoom_enabled = true;
    return count;
}
/*----------------------------------------------------------------------------*/
static ssize_t epl8865_store_ges_zoom_delta(struct device_driver *ddri, const char *buf, size_t count)
{
    sscanf(buf, "%d",&zoom_delta);
    return count;
}

/*----------------------------------------------------------------------------*/
static ssize_t epl8865_store_hs_enable(struct device_driver *ddri, const char *buf, size_t count)
{
    uint16_t mode=0;
    struct epl8865_priv *obj = epl8865_obj;
    bool enable_als = test_bit(CMC_BIT_ALS, &obj->enable) && atomic_read(&obj->als_suspend)==0;
    APS_FUN();

    sscanf(buf, "%hu",&mode);
//EPL8865("epl8865_store_hs_enable,mode:%d\n",mode);

    if(mode){
        if(enable_als == true){
            atomic_set(&obj->als_suspend, 1);
            hs_als_flag = 1;
            if(obj->polling_mode_hs == 1)
                msleep(ALS_DELAY);
        }
        set_bit(CMC_BIT_HS, &obj->enable);
    }
    else{
        clear_bit(CMC_BIT_HS, &obj->enable);
        if(obj->polling_mode_hs == 1)
                msleep(HS_DELAY);

        if(hs_als_flag == 1){
            atomic_set(&obj->als_suspend, 0);
            hs_als_flag = 0;

        }

    }

    if(mode)
    {
        hs_idx=0;
        hs_count=0;
    }

    epl8865_restart_polling();
    return count;
}
/*----------------------------------------------------------------------------*/
static ssize_t epl8865_show_hs_raws(struct device_driver *ddri, char *buf)
{
    mutex_lock(&sensor_mutex);

    u16 *tmp = (u16*)buf;
    u16 length= hs_count;
    int byte_count=2+length*2;
    int i=0;
    int start = hs_idx;

    tmp[0]= length;

    if(length == 0){
        tmp[0] = 1;
        length = 1;
        show_hs_raws_flag = 1;
    }
    for(i=start; i<length; i++){
        if(show_hs_raws_flag == 1){
            tmp[i+1] = 0;
            show_hs_raws_flag = 0;
        }
        else{
            tmp[i+1] = gRawData.hs_data[i];
        }

    }

    hs_count=0;
    hs_idx=0;
    mutex_unlock(&sensor_mutex);

    return byte_count;
}

static ssize_t epl8865_store_hs_polling(struct device_driver *ddri, const char *buf, size_t count)
{
    struct epl8865_priv *obj = epl8865_obj;
    APS_FUN();

    sscanf(buf, "%hu",&obj->polling_mode_hs);

    //APS_LOG("HS polling mode: %d\n", obj->polling_mode_hs);

    return count;
}


/*----------------------------------------------------------------------------*/
static DRIVER_ATTR(elan_status,					S_IROTH  | S_IWOTH, epl8865_show_status,  	  		NULL										);
static DRIVER_ATTR(elan_reg,    					S_IROTH  | S_IWOTH, epl8865_show_reg,   				NULL										);
static DRIVER_ATTR(elan_renvo,    				S_IROTH  | S_IWOTH, epl8865_show_renvo,   				NULL										);
static DRIVER_ATTR(als_enable,					S_IROTH  | S_IWOTH, NULL,   							epl8865_store_als_enable					);
static DRIVER_ATTR(als_int_time,     				S_IROTH  | S_IWOTH, NULL,   					 		epl8865_store_als_int_time					);
static DRIVER_ATTR(ps_cal_raw, 				S_IROTH  | S_IWOTH, epl8865_show_ps_cal_raw, 	  	NULL										);
static DRIVER_ATTR(ps_enable,					S_IROTH  | S_IWOTH, NULL,   							epl8865_store_ps_enable					);
static DRIVER_ATTR(ps_int_time,     				S_IROTH  | S_IWOTH, NULL,   							epl8865_store_ps_int_time					);
static DRIVER_ATTR(ps_threshold,     				S_IROTH  | S_IWOTH, epl8865_show_ps_threshold,   					 		epl8865_store_ps_threshold					);
static DRIVER_ATTR(ps_polling_mode,			S_IROTH  | S_IWOTH, NULL,   							epl8865_store_ps_polling_mode				);
static DRIVER_ATTR(ges_raws, 					S_IROTH  | S_IWOTH, epl8865_show_ges_raws, 	  		NULL										);
static DRIVER_ATTR(ges_enable,					S_IROTH  | S_IWOTH, NULL,   					 		epl8865_store_ges_enable					);
static DRIVER_ATTR(ges_mode,				S_IROTH  | S_IWOTH, NULL,								epl8865_store_ges_mode					);
static DRIVER_ATTR(ges_int_time,				S_IROTH  | S_IWOTH, NULL,   					 		epl8865_store_ges_int_time					);
static DRIVER_ATTR(ges_polling_mode,			S_IROTH  | S_IWOTH, NULL,   					 		epl8865_store_ges_polling_mode				);
static DRIVER_ATTR(ges_cycle,					S_IROTH  | S_IWOTH, NULL,   					 		epl8865_store_ges_cycle						);
static DRIVER_ATTR(ges_opt,					S_IROTH  | S_IWOTH, NULL,   					 		epl8865_store_ges_opt						);
static DRIVER_ATTR(ges_avg_length,				S_IROTH  | S_IWOTH, NULL,   							epl8865_store_ges_avg_length				);
static DRIVER_ATTR(ges_debug,					S_IROTH  | S_IWOTH, NULL,   					 		epl8865_store_ges_debug					);
static DRIVER_ATTR(ges_work_threshold,			S_IROTH  | S_IWOTH, NULL,   							epl8865_store_ges_work_threshold			);
static DRIVER_ATTR(ges_gain,					S_IROTH  | S_IWOTH, NULL,   							epl8865_store_ges_gain						);
static DRIVER_ATTR(ges_crosstalk,				S_IROTH  | S_IWOTH, NULL,   							epl8865_store_ges_crosstalk					);
static DRIVER_ATTR(ges_zoom_delta,				S_IROTH  | S_IWOTH, NULL,   							epl8865_store_ges_zoom_delta				);
static DRIVER_ATTR(hs_enable,					S_IROTH  | S_IWOTH, NULL,   							epl8865_store_hs_enable					);
static DRIVER_ATTR(hs_raws,					S_IROTH  | S_IWOTH, epl8865_show_hs_raws, 	  		NULL										);
static DRIVER_ATTR(hs_polling,					S_IROTH  | S_IWOTH, NULL, epl8865_store_hs_polling);

/*----------------------------------------------------------------------------*/
static struct driver_attribute * epl8865_attr_list[] =
{
    &driver_attr_elan_status,
    &driver_attr_elan_reg,
    &driver_attr_elan_renvo,
    &driver_attr_als_enable,
    &driver_attr_als_int_time,
    &driver_attr_ps_enable,
    &driver_attr_ps_cal_raw,
    &driver_attr_ps_int_time,
    &driver_attr_ps_threshold,
    &driver_attr_ps_polling_mode,
    &driver_attr_ges_cycle,
    &driver_attr_ges_enable,
    &driver_attr_ges_mode,
    &driver_attr_ges_int_time,
    &driver_attr_ges_polling_mode,
    &driver_attr_ges_opt,
    &driver_attr_ges_avg_length,
    &driver_attr_ges_debug,
    &driver_attr_ges_work_threshold,
    &driver_attr_ges_raws,
    &driver_attr_ges_gain,
    &driver_attr_ges_crosstalk,
    &driver_attr_ges_zoom_delta,
    &driver_attr_hs_enable,
    &driver_attr_hs_raws,
    &driver_attr_hs_polling,
};

/*----------------------------------------------------------------------------*/
static int epl8865_create_attr(struct device_driver *driver)
{
    int idx, err = 0;
    int num = (int)(sizeof(epl8865_attr_list)/sizeof(epl8865_attr_list[0]));
    if (driver == NULL)
    {
        return -EINVAL;
    }

    for(idx = 0; idx < num; idx++)
    {
        if(err = driver_create_file(driver, epl8865_attr_list[idx]))
        {
            APS_ERR("driver_create_file (%s) = %d\n", epl8865_attr_list[idx]->attr.name, err);
            break;
        }
    }
    return err;
}



/*----------------------------------------------------------------------------*/
static int epl8865_delete_attr(struct device_driver *driver)
{
    int idx ,err = 0;
    int num = (int)(sizeof(epl8865_attr_list)/sizeof(epl8865_attr_list[0]));

    if (!driver)
        return -EINVAL;

    for (idx = 0; idx < num; idx++)
    {
        driver_remove_file(driver, epl8865_attr_list[idx]);
    }

    return err;
}



/******************************************************************************
 * Function Configuration
******************************************************************************/
static int epl8865_open(struct inode *inode, struct file *file)
{
//EPL8865("%s\n",__func__);
    file->private_data = epl8865_i2c_client;

    APS_FUN();

    if (!file->private_data)
    {
        APS_ERR("null pointer!!\n");
        return -EINVAL;
    }

    return nonseekable_open(inode, file);
}

/*----------------------------------------------------------------------------*/
static int epl8865_release(struct inode *inode, struct file *file)
{
    APS_FUN();
    file->private_data = NULL;
    return 0;
}

/*----------------------------------------------------------------------------*/
static long epl8865_unlocked_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct i2c_client *client = (struct i2c_client*)file->private_data;
    struct epl8865_priv *obj = i2c_get_clientdata(client);
    int err = 0;
    void __user *ptr = (void __user*) arg;
    int dat;
    uint32_t enable;

    //EPL8865("epl8865_unlocked_ioctl,cmd:%d\n",cmd);

    clear_bit(CMC_BIT_GES, &obj->enable);

    switch (cmd)
    {
        case ALSPS_SET_PS_MODE:
            if(copy_from_user(&enable, ptr, sizeof(enable)))
            {
                err = -EFAULT;
                goto err_out;
            }

            if(enable)
            {
                set_bit(CMC_BIT_PS, &obj->enable);
            }
            else
            {
                clear_bit(CMC_BIT_PS, &obj->enable);
            }
            epl8865_restart_polling();
            break;


        case ALSPS_GET_PS_MODE:
            enable=test_bit(CMC_BIT_PS, &obj->enable);
            if(copy_to_user(ptr, &enable, sizeof(enable)))
            {
                err = -EFAULT;
                goto err_out;
            }
            break;


        case ALSPS_GET_PS_DATA:
            dat = gRawData.ps_state;

            //APS_LOG("ioctl ps state value = %d \n", dat);

            if(copy_to_user(ptr, &dat, sizeof(dat)))
            {
                err = -EFAULT;
                goto err_out;
            }
            break;


        case ALSPS_GET_PS_RAW_DATA:
            dat = gRawData.ps_raw;

            //APS_LOG("ioctl ps raw value = %d \n", dat);
            if(copy_to_user(ptr, &dat, sizeof(dat)))
            {
                err = -EFAULT;
                goto err_out;
            }
            break;


        case ALSPS_SET_ALS_MODE:
            if(copy_from_user(&enable, ptr, sizeof(enable)))
            {
                err = -EFAULT;
                goto err_out;
            }
            if(enable)
            {
                set_bit(CMC_BIT_ALS, &obj->enable);
                epl8865_restart_polling();
            }
            else
            {
                clear_bit(CMC_BIT_ALS, &obj->enable);
            }

            break;



        case ALSPS_GET_ALS_MODE:
            enable=test_bit(CMC_BIT_ALS, &obj->enable);
            if(copy_to_user(ptr, &enable, sizeof(enable)))
            {
                err = -EFAULT;
                goto err_out;
            }
            break;



        case ALSPS_GET_ALS_DATA:
            dat = epl8865_get_als_value(obj, gRawData.als_ch1_raw);
            //APS_LOG("ioctl get als data = %d\n", dat);

            if(copy_to_user(ptr, &dat, sizeof(dat)))
            {
                err = -EFAULT;
                goto err_out;
            }
            break;


        case ALSPS_GET_ALS_RAW_DATA:
            dat = gRawData.als_ch1_raw;
            //APS_LOG("ioctl get als raw data = %d\n", dat);

            if(copy_to_user(ptr, &dat, sizeof(dat)))
            {
                err = -EFAULT;
                goto err_out;
            }
            break;

        case ALSPS_GET_PS_THRESHOLD_HIGH:
            dat = obj->hw ->ps_threshold_high;
            if(copy_to_user(ptr, &dat, sizeof(dat)))
            {
                err = -EFAULT;
                goto err_out;
            }
            break;

        case ALSPS_GET_PS_THRESHOLD_LOW:
            dat = obj->hw ->ps_threshold_low;
            if(copy_to_user(ptr, &dat, sizeof(dat)))
            {
                err = -EFAULT;
                goto err_out;
            }
            break;

        default:
            //APS_ERR("%s not supported = 0x%04x\n", __FUNCTION__, cmd);
            err = -ENOIOCTLCMD;
            break;
    }

err_out:
    return err;
}


/*----------------------------------------------------------------------------*/
static struct file_operations epl8865_fops =
{
    .owner = THIS_MODULE,
    .open = epl8865_open,
    .release = epl8865_release,
    .unlocked_ioctl = epl8865_unlocked_ioctl,
};


/*----------------------------------------------------------------------------*/
static struct miscdevice epl8865_device =
{
    .minor = MISC_DYNAMIC_MINOR,
    .name = "als_ps",
    .fops = &epl8865_fops,
};


/*----------------------------------------------------------------------------*/
static int epl8865_i2c_suspend(struct i2c_client *client, pm_message_t msg)
{
    //struct epl8865_priv *obj = i2c_get_clientdata(client);
    APS_FUN();
#if 0
    if(msg.event == PM_EVENT_SUSPEND)
    {
        if(!obj)
        {
            APS_ERR("null pointer!!\n");
            return -EINVAL;
        }

        atomic_set(&obj->als_suspend, 1);
        atomic_set(&obj->ps_suspend, 1);
        atomic_set(&obj->ges_suspend, 1);
        atomic_set(&obj->hs_suspend, 1);

        if(test_bit(CMC_BIT_PS,  &obj->enable) && obj->hw->polling_mode_ps==0)
            epl8865_restart_polling();

        epl8865_power(obj->hw, 0);
    }
#endif
    return 0;

}



/*----------------------------------------------------------------------------*/
static int epl8865_i2c_resume(struct i2c_client *client)
{
   /* struct epl8865_priv *obj = i2c_get_clientdata(client);
    int err;
    APS_FUN();

    if(!obj)
    {
        APS_ERR("null pointer!!\n");
        return -EINVAL;
    }*/
#if 0
    epl8865_power(obj->hw, 1);

    msleep(50);

    atomic_set(&obj->ps_suspend, 0);

    if(err = epl8865_init_client(client))
    {
        APS_ERR("initialize client fail!!\n");
        return err;
    }

    if(obj->hw->polling_mode_ps == 0)
        epl8865_setup_eint(client);


    if(test_bit(CMC_BIT_PS,  &obj->enable))
        epl8865_restart_polling();
#endif
    return 0;
}



/*----------------------------------------------------------------------------*/
static void epl8865_early_suspend(struct early_suspend *h)
{
    /*early_suspend is only applied for ALS*/
    struct epl8865_priv *obj = container_of(h, struct epl8865_priv, early_drv);

    APS_FUN();

    if(!obj)
    {
        APS_ERR("null pointer!!\n");
        return;
    }

    atomic_set(&obj->als_suspend, 1);
    atomic_set(&obj->ges_suspend, 1);

}



/*----------------------------------------------------------------------------*/
static void epl8865_late_resume(struct early_suspend *h)
{
    /*late_resume is only applied for ALS*/
    struct epl8865_priv *obj = container_of(h, struct epl8865_priv, early_drv);
    APS_FUN();

    if(!obj)
    {
        APS_ERR("null pointer!!\n");
        return;
    }

    atomic_set(&obj->als_suspend, 0);
    atomic_set(&obj->ps_suspend, 0);
    atomic_set(&obj->ges_suspend, 0);
    atomic_set(&obj->hs_suspend, 0);

    if(test_bit(CMC_BIT_ALS, &obj->enable) || test_bit(CMC_BIT_GES, &obj->enable))
        epl8865_restart_polling();

}


/*----------------------------------------------------------------------------*/
int epl8865_ps_operate(void* self, uint32_t command, void* buff_in, int size_in,
                       void* buff_out, int size_out, int* actualout)
{
    int err = 0;
    int value;
    hwm_sensor_data* sensor_data;
    struct epl8865_priv *epld = epl8865_obj;
    struct i2c_client *client = epld->client;

    struct epl8865_priv *obj = (struct epl8865_priv *)self;

    //APS_LOG("epl8865_ps_operate command = %x,command:%d\n",command,command);
    switch (command)
    {
        case SENSOR_DELAY:
            if((buff_in == NULL) || (size_in < sizeof(int)))
            {
                APS_ERR("Set delay parameter error!\n");
                err = -EINVAL;
            }
            break;


        case SENSOR_ENABLE:
            if((buff_in == NULL) || (size_in < sizeof(int)))
            {
                APS_ERR("Enable sensor parameter error!\n");
                err = -EINVAL;
            }
            else
            {
                value = *(int *)buff_in;
                //APS_LOG("ps enable = %d\n", value);


                if(value)
                {
                    wake_lock(&ps_lock);
                    if(obj->hw->polling_mode_ps==0)
                        gRawData.ps_state = 2;
                    set_bit(CMC_BIT_PS, &obj->enable);
#if PHONE_CAL

                    //APS_LOG("[PHONE_CAL]    ----->ps raw data = %d\n", gRawData.ps_raw);
                    epld->hw ->ps_threshold_low = gRawData.ps_raw + ps_threshold_low_offset;
                    epld->hw ->ps_threshold_high = gRawData.ps_raw + ps_threshold_high_offset;

                    set_psensor_intr_threshold(epld->hw->ps_threshold_low,epld->hw->ps_threshold_high);
                   //APS_LOG("[PHONE_CAL]    ----->ps_threshold_low = %d,   ps_threshold_high = %d \r\n", epld->hw->ps_threshold_low, epld->hw->ps_threshold_high);
#endif

                }
                else
                {
                    clear_bit(CMC_BIT_PS, &obj->enable);
                    wake_unlock(&ps_lock);
                }
                epl8865_restart_polling();
            }

            break;



        case SENSOR_GET_DATA:
            //APS_LOG(" get ps data !!!!!!\n");
            if((buff_out == NULL) || (size_out< sizeof(hwm_sensor_data)))
            {
                APS_ERR("get sensor data parameter error!\n");
                err = -EINVAL;
            }
            else
            {
                APS_LOG("---SENSOR_GET_DATA---\n\n");

                sensor_data = (hwm_sensor_data *)buff_out;
                sensor_data->values[0] = gRawData.ps_state;
                sensor_data->values[1] = gRawData.ps_raw;
                sensor_data->value_divide = 1;
                sensor_data->status = SENSOR_STATUS_ACCURACY_MEDIUM;
            }
            break;


        default:
            APS_ERR("proxmy sensor operate function no this parameter %d!\n", command);
            err = -1;
            break;



    }

    return err;

}



int epl8865_als_operate(void* self, uint32_t command, void* buff_in, int size_in,
                        void* buff_out, int size_out, int* actualout)
{
    int err = 0;
    int value;
    hwm_sensor_data* sensor_data;
    struct epl8865_priv *obj = (struct epl8865_priv *)self;

    APS_FUN();
    //APS_LOG("epl8865_als_operate command = %x,command:%d\n",command,command);

    switch (command)
    {
        case SENSOR_DELAY:
            if((buff_in == NULL) || (size_in < sizeof(int)))
            {
                APS_ERR("Set delay parameter error!\n");
                err = -EINVAL;
            }
            break;


        case SENSOR_ENABLE:
            if((buff_in == NULL) || (size_in < sizeof(int)))
            {
                APS_ERR("Enable sensor parameter error!\n");
                err = -EINVAL;
            }
            else
            {
                value = *(int *)buff_in;
                if(value)
                {
                    set_bit(CMC_BIT_ALS, &obj->enable);
                    epl8865_restart_polling();
                }
                else
                {
                    clear_bit(CMC_BIT_ALS, &obj->enable);
                }

            }
            break;


        case SENSOR_GET_DATA:
            //APS_LOG("get als data !!!!!!\n");
            if((buff_out == NULL) || (size_out< sizeof(hwm_sensor_data)))
            {
                APS_ERR("get sensor data parameter error!\n");
                err = -EINVAL;
            }
            else
            {
                sensor_data = (hwm_sensor_data *)buff_out;
                sensor_data->values[0] = epl8865_get_als_value(obj, gRawData.als_ch1_raw);
                sensor_data->value_divide = 1;
                sensor_data->status = SENSOR_STATUS_ACCURACY_MEDIUM;
                //APS_LOG("get als data->values[0] = %d\n", sensor_data->values[0]);
            }
            break;

        default:
            APS_ERR("light sensor operate function no this parameter %d!\n", command);
            err = -1;
            break;



    }

    return err;

}


/*----------------------------------------------------------------------------*/

static int epl8865_i2c_detect(struct i2c_client *client, int kind, struct i2c_board_info *info)
{
    strcpy(info->type, EPL8865_DEV_NAME);
    return 0;
}


/*----------------------------------------------------------------------------*/
static int epl8865_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{  
    struct epl8865_priv *obj;
    struct hwmsen_object obj_ps, obj_als;
    int err = 0, i=0;
    APS_FUN();

    client->timing = 400;//
    epl8865_dumpReg(client);
    if(i2c_smbus_read_byte_data(client, 0xb8) != 0x88){
        APS_LOG("elan ALS/PS sensor is failed. \n");
        goto exit;
    }

    if(!(obj = kzalloc(sizeof(*obj), GFP_KERNEL)))
    {
        err = -ENOMEM;
        goto exit;
    }

    memset(obj, 0, sizeof(*obj));

    epl8865_obj = obj;
    obj->hw = get_cust_alsps_hw();

    epl8865_get_addr(obj->hw, &obj->addr);

    obj->als_level_num = sizeof(obj->hw->als_level)/sizeof(obj->hw->als_level[0]);
    obj->als_value_num = sizeof(obj->hw->als_value)/sizeof(obj->hw->als_value[0]);
    BUG_ON(sizeof(obj->als_level) != sizeof(obj->hw->als_level));
    memcpy(obj->als_level, obj->hw->als_level, sizeof(obj->als_level));
    BUG_ON(sizeof(obj->als_value) != sizeof(obj->hw->als_value));
    memcpy(obj->als_value, obj->hw->als_value, sizeof(obj->als_value));

    INIT_DELAYED_WORK(&obj->eint_work, epl8865_eint_work);
    INIT_DELAYED_WORK(&obj->polling_work, epl8865_polling_work);
    wake_lock_init(&ps_lock, WAKE_LOCK_SUSPEND, "ps wakelock");

    obj->client = client;

    obj->gs_input_dev = input_allocate_device();
    set_bit(EV_KEY, obj->gs_input_dev->evbit);
    set_bit(EV_REL, obj->gs_input_dev->evbit);
    set_bit(EV_ABS, obj->gs_input_dev->evbit);

    obj->gs_input_dev->evbit[0] |= BIT_MASK(EV_REP);
    obj->gs_input_dev->keycodemax = 500;
    obj->gs_input_dev->name ="elan_gesture";


    for(i=0; i<EVENT_UNKNOWN; i++)
        obj->gs_input_dev->keybit[BIT_WORD(KEYCODE_ARRAY[i])] |= BIT_MASK(KEYCODE_ARRAY[i]);

    if (input_register_device(obj->gs_input_dev))
        APS_ERR("register input error\n");

    mutex_init(&sensor_mutex);


    i2c_set_clientdata(client, obj);

    atomic_set(&obj->trace, 0x00);
    atomic_set(&obj->als_suspend, 0);
    atomic_set(&obj->ps_suspend, 0);
    atomic_set(&obj->ges_suspend, 0);
    atomic_set(&obj->hs_suspend, 0);

    obj->polling_mode_ges = POLLING_MODE_GES;
    obj->lux_per_count = LUX_PER_COUNT;
    obj->enable = 0;
    obj->pending_intr = 0;
    obj->ir_type=0;

    epl8865_i2c_client = client;

    epl8865_I2C_Write(client,REG_0,W_SINGLE_BYTE,0x02, EPL_S_SENSING_MODE);
    epl8865_I2C_Write(client,REG_6,W_SINGLE_BYTE,0x02, EPL_INT_DISABLE);

    epl8865_I2C_Write(client,0x17,R_TWO_BYTE,0x01,0x00);
    epl8865_I2C_Read(client);
    gRawData.renvo = (gRawData.raw_bytes[1]<<8)|gRawData.raw_bytes[0];

    if(err = epl8865_init_client(client))
    {
        goto exit_init_failed;
    }


    if(err = misc_register(&epl8865_device))
    {
        //APS_ERR("epl8865_device register failed\n");
        goto exit_misc_device_register_failed;
    }

    if(err = epl8865_create_attr(&epl8865_alsps_driver.driver))
    {
        //APS_ERR("create attribute err = %d\n", err);
        goto exit_create_attr_failed;
    }

    obj_ps.self = epl8865_obj;

    if( obj->hw->polling_mode_ps)
    {
        obj_ps.polling = 1;
        //APS_LOG("ps_interrupt == false\n");
    }
    else
    {
        obj_ps.polling = 0;//interrupt mode
        //APS_LOG("ps_interrupt == true\n");
    }


    obj_ps.sensor_operate = epl8865_ps_operate;



    if(err = hwmsen_attach(ID_PROXIMITY, &obj_ps))
    {
        //APS_ERR("attach fail = %d\n", err);
        goto exit_create_attr_failed;
    }

    gRawData.ps_als_factory.cal_file_exist = 1;
    gRawData.ps_als_factory.cal_finished = 0;

    obj_als.self = epl8865_obj;
    obj_als.polling = 1;
    obj_als.sensor_operate = epl8865_als_operate;
    //APS_LOG("als polling mode\n");


    if(err = hwmsen_attach(ID_LIGHT, &obj_als))
    {
        //APS_ERR("attach fail = %d\n", err);
        goto exit_create_attr_failed;
    }



#if defined(CONFIG_HAS_EARLYSUSPEND)
    obj->early_drv.level    = EARLY_SUSPEND_LEVEL_DISABLE_FB - 1,
                   obj->early_drv.suspend  = epl8865_early_suspend,
                                  obj->early_drv.resume   = epl8865_late_resume,
                                                 register_early_suspend(&obj->early_drv);
#endif

    if(obj->hw->polling_mode_ps ==0|| obj->polling_mode_ges==0 || obj->polling_mode_hs == 0)
        epl8865_setup_eint(client);

    APS_LOG("%s: OK\n", __func__);
    return 0;

exit_create_attr_failed:
    misc_deregister(&epl8865_device);
exit_misc_device_register_failed:
exit_init_failed:
    //i2c_detach_client(client);
//	exit_kfree:
    kfree(obj);
exit:
    epl8865_i2c_client = NULL;

    APS_ERR("%s: err = %d\n", __func__, err);
    return err;



}



/*----------------------------------------------------------------------------*/
static int epl8865_i2c_remove(struct i2c_client *client)
{
    int err;

    if(err = epl8865_delete_attr(&epl8865_i2c_driver.driver))
    {
        APS_ERR("epl8865_delete_attr fail: %d\n", err);
    }

    if(err = misc_deregister(&epl8865_device))
    {
        APS_ERR("misc_deregister fail: %d\n", err);
    }

    epl8865_i2c_client = NULL;
    i2c_unregister_device(client);
    kfree(i2c_get_clientdata(client));

    return 0;
}



/*----------------------------------------------------------------------------*/
static int epl8865_probe(struct platform_device *pdev)
{
    struct alsps_hw *hw = get_cust_alsps_hw();

    epl8865_power(hw, 1);

    //epl8865_force[0] = hw->i2c_num;

    if(i2c_add_driver(&epl8865_i2c_driver))
    {
        APS_ERR("add driver error\n");
        return -1;
    }
    return 0;
}



/*----------------------------------------------------------------------------*/
static int epl8865_remove(struct platform_device *pdev)
{
    struct alsps_hw *hw = get_cust_alsps_hw();
    APS_FUN();
    epl8865_power(hw, 0);

    //APS_ERR("EPL8865 remove \n");
    i2c_del_driver(&epl8865_i2c_driver);
    return 0;
}



/*----------------------------------------------------------------------------*/
static struct platform_driver epl8865_alsps_driver =
{
    .probe      = epl8865_probe,
    .remove     = epl8865_remove,
    .driver     = {
        .name  = "als_ps",
        //.owner = THIS_MODULE,
    }
};
/*----------------------------------------------------------------------------*/
static int __init epl8865_init(void)
{
//EPL8865("epl8865_init:%s\n",__func__);
    struct alsps_hw *hw = get_cust_alsps_hw();
    APS_FUN();
    i2c_register_board_info(hw->i2c_num, &i2c_EPL8865, 1);
    if(platform_driver_register(&epl8865_alsps_driver))
    {
        APS_ERR("failed to register driver");
        return -ENODEV;
    }

    return 0;
}
/*----------------------------------------------------------------------------*/
static void __exit epl8865_exit(void)
{
    APS_FUN();
    platform_driver_unregister(&epl8865_alsps_driver);
}
/*----------------------------------------------------------------------------*/
module_init(epl8865_init);
module_exit(epl8865_exit);
/*----------------------------------------------------------------------------*/
MODULE_AUTHOR("renato.pan@eminent-tek.com");
MODULE_DESCRIPTION("EPL8865 ALPsr driver");
MODULE_LICENSE("GPL");

