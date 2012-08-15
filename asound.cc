#include "asound.h"
#include "linux.h"
#include "debug.h"
//TODO: cleanup and inline to alsa.cc

#define SNDRV_PROTOCOL_VERSION(major, minor, subminor) (((major)<<16)|((minor)<<8)|(subminor))
#define SNDRV_PROTOCOL_MAJOR(version) (((version)>>16)&0xffff)
#define SNDRV_PROTOCOL_MINOR(version) (((version)>>8)&0xff)
#define SNDRV_PROTOCOL_MICRO(version) ((version)&0xff)
#define SNDRV_PROTOCOL_INCOMPATIBLE(kversion, uversion)   (SNDRV_PROTOCOL_MAJOR(kversion) != SNDRV_PROTOCOL_MAJOR(uversion) ||   (SNDRV_PROTOCOL_MAJOR(kversion) == SNDRV_PROTOCOL_MAJOR(uversion) &&   SNDRV_PROTOCOL_MINOR(kversion) != SNDRV_PROTOCOL_MINOR(uversion)))
#define SNDRV_HWDEP_VERSION SNDRV_PROTOCOL_VERSION(1, 0, 1)

enum {
 SNDRV_HWDEP_IFACE_OPL2 = 0,
 SNDRV_HWDEP_IFACE_OPL3,
 SNDRV_HWDEP_IFACE_OPL4,
 SNDRV_HWDEP_IFACE_SB16CSP,
 SNDRV_HWDEP_IFACE_EMU10K1,
 SNDRV_HWDEP_IFACE_YSS225,
 SNDRV_HWDEP_IFACE_ICS2115,
 SNDRV_HWDEP_IFACE_SSCAPE,
 SNDRV_HWDEP_IFACE_VX,
 SNDRV_HWDEP_IFACE_MIXART,
 SNDRV_HWDEP_IFACE_USX2Y,
 SNDRV_HWDEP_IFACE_EMUX_WAVETABLE,
 SNDRV_HWDEP_IFACE_BLUETOOTH,
 SNDRV_HWDEP_IFACE_USX2Y_PCM,
 SNDRV_HWDEP_IFACE_PCXHR,
 SNDRV_HWDEP_IFACE_SB_RC,
 SNDRV_HWDEP_IFACE_HDA,
 SNDRV_HWDEP_IFACE_USB_STREAM,
 SNDRV_HWDEP_IFACE_LAST = SNDRV_HWDEP_IFACE_USB_STREAM
};

struct snd_hwdep_info {
 unsigned int device;
 int card;
 unsigned char id[64];
 unsigned char name[80];
 int iface;
 unsigned char reserved[64];
};

struct snd_hwdep_dsp_status {
 unsigned int version;
 unsigned char id[32];
 unsigned int num_dsps;
 unsigned int dsp_loaded;
 unsigned int chip_ready;
 unsigned char reserved[16];
};

struct snd_hwdep_dsp_image {
 unsigned int index;
 unsigned char name[64];
 unsigned char  *image;
 ulong length;
 unsigned long driver_data;
};

#define SNDRV_HWDEP_IOCTL_PVERSION _IOR ('H', 0x00, int)
#define SNDRV_HWDEP_IOCTL_INFO _IOR ('H', 0x01, struct snd_hwdep_info)
#define SNDRV_HWDEP_IOCTL_DSP_STATUS _IOR('H', 0x02, struct snd_hwdep_dsp_status)
#define SNDRV_HWDEP_IOCTL_DSP_LOAD _IOW('H', 0x03, struct snd_hwdep_dsp_image)

#define SNDRV_PCM_VERSION SNDRV_PROTOCOL_VERSION(2, 0, 10)

typedef unsigned long snd_pcm_uframes_t;
typedef signed long snd_pcm_sframes_t;

enum {
 SNDRV_PCM_CLASS_GENERIC = 0,
 SNDRV_PCM_CLASS_MULTI,
 SNDRV_PCM_CLASS_MODEM,
 SNDRV_PCM_CLASS_DIGITIZER,
 SNDRV_PCM_CLASS_LAST = SNDRV_PCM_CLASS_DIGITIZER,
};

enum {
 SNDRV_PCM_SUBCLASS_GENERIC_MIX = 0,
 SNDRV_PCM_SUBCLASS_MULTI_MIX,
 SNDRV_PCM_SUBCLASS_LAST = SNDRV_PCM_SUBCLASS_MULTI_MIX,
};

enum {
 SNDRV_PCM_STREAM_PLAYBACK = 0,
 SNDRV_PCM_STREAM_CAPTURE,
 SNDRV_PCM_STREAM_LAST = SNDRV_PCM_STREAM_CAPTURE,
};

typedef int  snd_pcm_access_t;
#define SNDRV_PCM_ACCESS_MMAP_INTERLEAVED (( snd_pcm_access_t) 0)
#define SNDRV_PCM_ACCESS_MMAP_NONINTERLEAVED (( snd_pcm_access_t) 1)
#define SNDRV_PCM_ACCESS_MMAP_COMPLEX (( snd_pcm_access_t) 2)
#define SNDRV_PCM_ACCESS_RW_INTERLEAVED (( snd_pcm_access_t) 3)
#define SNDRV_PCM_ACCESS_RW_NONINTERLEAVED (( snd_pcm_access_t) 4)
#define SNDRV_PCM_ACCESS_LAST SNDRV_PCM_ACCESS_RW_NONINTERLEAVED

typedef int  snd_pcm_format_t;
#define SNDRV_PCM_FORMAT_S8 (( snd_pcm_format_t) 0)
#define SNDRV_PCM_FORMAT_U8 (( snd_pcm_format_t) 1)
#define SNDRV_PCM_FORMAT_S16_LE (( snd_pcm_format_t) 2)
#define SNDRV_PCM_FORMAT_S16_BE (( snd_pcm_format_t) 3)
#define SNDRV_PCM_FORMAT_U16_LE (( snd_pcm_format_t) 4)
#define SNDRV_PCM_FORMAT_U16_BE (( snd_pcm_format_t) 5)
#define SNDRV_PCM_FORMAT_S24_LE (( snd_pcm_format_t) 6)
#define SNDRV_PCM_FORMAT_S24_BE (( snd_pcm_format_t) 7)
#define SNDRV_PCM_FORMAT_U24_LE (( snd_pcm_format_t) 8)
#define SNDRV_PCM_FORMAT_U24_BE (( snd_pcm_format_t) 9)
#define SNDRV_PCM_FORMAT_S32_LE (( snd_pcm_format_t) 10)
#define SNDRV_PCM_FORMAT_S32_BE (( snd_pcm_format_t) 11)
#define SNDRV_PCM_FORMAT_U32_LE (( snd_pcm_format_t) 12)
#define SNDRV_PCM_FORMAT_U32_BE (( snd_pcm_format_t) 13)
#define SNDRV_PCM_FORMAT_FLOAT_LE (( snd_pcm_format_t) 14)
#define SNDRV_PCM_FORMAT_FLOAT_BE (( snd_pcm_format_t) 15)
#define SNDRV_PCM_FORMAT_FLOAT64_LE (( snd_pcm_format_t) 16)
#define SNDRV_PCM_FORMAT_FLOAT64_BE (( snd_pcm_format_t) 17)
#define SNDRV_PCM_FORMAT_IEC958_SUBFRAME_LE (( snd_pcm_format_t) 18)
#define SNDRV_PCM_FORMAT_IEC958_SUBFRAME_BE (( snd_pcm_format_t) 19)
#define SNDRV_PCM_FORMAT_MU_LAW (( snd_pcm_format_t) 20)
#define SNDRV_PCM_FORMAT_A_LAW (( snd_pcm_format_t) 21)
#define SNDRV_PCM_FORMAT_IMA_ADPCM (( snd_pcm_format_t) 22)
#define SNDRV_PCM_FORMAT_MPEG (( snd_pcm_format_t) 23)
#define SNDRV_PCM_FORMAT_GSM (( snd_pcm_format_t) 24)
#define SNDRV_PCM_FORMAT_SPECIAL (( snd_pcm_format_t) 31)
#define SNDRV_PCM_FORMAT_S24_3LE (( snd_pcm_format_t) 32)
#define SNDRV_PCM_FORMAT_S24_3BE (( snd_pcm_format_t) 33)
#define SNDRV_PCM_FORMAT_U24_3LE (( snd_pcm_format_t) 34)
#define SNDRV_PCM_FORMAT_U24_3BE (( snd_pcm_format_t) 35)
#define SNDRV_PCM_FORMAT_S20_3LE (( snd_pcm_format_t) 36)
#define SNDRV_PCM_FORMAT_S20_3BE (( snd_pcm_format_t) 37)
#define SNDRV_PCM_FORMAT_U20_3LE (( snd_pcm_format_t) 38)
#define SNDRV_PCM_FORMAT_U20_3BE (( snd_pcm_format_t) 39)
#define SNDRV_PCM_FORMAT_S18_3LE (( snd_pcm_format_t) 40)
#define SNDRV_PCM_FORMAT_S18_3BE (( snd_pcm_format_t) 41)
#define SNDRV_PCM_FORMAT_U18_3LE (( snd_pcm_format_t) 42)
#define SNDRV_PCM_FORMAT_U18_3BE (( snd_pcm_format_t) 43)
#define SNDRV_PCM_FORMAT_G723_24 (( snd_pcm_format_t) 44)
#define SNDRV_PCM_FORMAT_G723_24_1B (( snd_pcm_format_t) 45)
#define SNDRV_PCM_FORMAT_G723_40 (( snd_pcm_format_t) 46)
#define SNDRV_PCM_FORMAT_G723_40_1B (( snd_pcm_format_t) 47)
#define SNDRV_PCM_FORMAT_LAST SNDRV_PCM_FORMAT_G723_40_1B

typedef int  snd_pcm_subformat_t;
#define SNDRV_PCM_SUBFORMAT_STD (( snd_pcm_subformat_t) 0)
#define SNDRV_PCM_SUBFORMAT_LAST SNDRV_PCM_SUBFORMAT_STD

#define SNDRV_PCM_INFO_MMAP 0x00000001
#define SNDRV_PCM_INFO_MMAP_VALID 0x00000002
#define SNDRV_PCM_INFO_DOUBLE 0x00000004
#define SNDRV_PCM_INFO_BATCH 0x00000010
#define SNDRV_PCM_INFO_INTERLEAVED 0x00000100
#define SNDRV_PCM_INFO_NONINTERLEAVED 0x00000200
#define SNDRV_PCM_INFO_COMPLEX 0x00000400
#define SNDRV_PCM_INFO_BLOCK_TRANSFER 0x00010000
#define SNDRV_PCM_INFO_OVERRANGE 0x00020000
#define SNDRV_PCM_INFO_RESUME 0x00040000
#define SNDRV_PCM_INFO_PAUSE 0x00080000
#define SNDRV_PCM_INFO_HALF_DUPLEX 0x00100000
#define SNDRV_PCM_INFO_JOINT_DUPLEX 0x00200000
#define SNDRV_PCM_INFO_SYNC_START 0x00400000
#define SNDRV_PCM_INFO_NO_PERIOD_WAKEUP 0x00800000
#define SNDRV_PCM_INFO_FIFO_IN_FRAMES 0x80000000

enum {
 SNDRV_PCM_MMAP_OFFSET_DATA = 0x00000000,
 SNDRV_PCM_MMAP_OFFSET_STATUS = 0x80000000,
 SNDRV_PCM_MMAP_OFFSET_CONTROL = 0x81000000,
};

union snd_pcm_sync_id {
 unsigned char id[16];
 unsigned short id16[8];
 unsigned int id32[4];
};

struct snd_pcm_info {
 unsigned int device;
 unsigned int subdevice;
 int stream;
 int card;
 unsigned char id[64];
 unsigned char name[80];
 unsigned char subname[32];
 int dev_class;
 int dev_subclass;
 unsigned int subdevices_count;
 unsigned int subdevices_avail;
 union snd_pcm_sync_id sync;
 unsigned char reserved[64];
};

typedef int snd_pcm_hw_param_t;
#define SNDRV_PCM_HW_PARAM_ACCESS 0
#define SNDRV_PCM_HW_PARAM_FORMAT 1
#define SNDRV_PCM_HW_PARAM_SUBFORMAT 2
#define SNDRV_PCM_HW_PARAM_FIRST_MASK SNDRV_PCM_HW_PARAM_ACCESS
#define SNDRV_PCM_HW_PARAM_LAST_MASK SNDRV_PCM_HW_PARAM_SUBFORMAT

#define SNDRV_PCM_HW_PARAM_SAMPLE_BITS 8
#define SNDRV_PCM_HW_PARAM_FRAME_BITS 9
#define SNDRV_PCM_HW_PARAM_CHANNELS 10
#define SNDRV_PCM_HW_PARAM_RATE 11
#define SNDRV_PCM_HW_PARAM_PERIOD_TIME 12
#define SNDRV_PCM_HW_PARAM_PERIOD_SIZE 13
#define SNDRV_PCM_HW_PARAM_PERIOD_BYTES 14
#define SNDRV_PCM_HW_PARAM_PERIODS 15
#define SNDRV_PCM_HW_PARAM_BUFFER_TIME 16
#define SNDRV_PCM_HW_PARAM_BUFFER_SIZE 17
#define SNDRV_PCM_HW_PARAM_BUFFER_BYTES 18
#define SNDRV_PCM_HW_PARAM_TICK_TIME 19
#define SNDRV_PCM_HW_PARAM_FIRST_INTERVAL SNDRV_PCM_HW_PARAM_SAMPLE_BITS
#define SNDRV_PCM_HW_PARAM_LAST_INTERVAL SNDRV_PCM_HW_PARAM_TICK_TIME

#define SNDRV_PCM_HW_PARAMS_NORESAMPLE (1<<0)
#define SNDRV_PCM_HW_PARAMS_EXPORT_BUFFER (1<<1)
#define SNDRV_PCM_HW_PARAMS_NO_PERIOD_WAKEUP (1<<2)

struct snd_interval {
 unsigned int min, max;
 unsigned int openmin:1,
 openmax:1,
 integer:1,
 empty:1;
};

#define SNDRV_MASK_MAX 256

struct snd_mask {
 uint bits[(SNDRV_MASK_MAX+31)/32];
};

struct snd_pcm_hw_params {
 unsigned int flags;
 struct snd_mask masks[SNDRV_PCM_HW_PARAM_LAST_MASK -
 SNDRV_PCM_HW_PARAM_FIRST_MASK + 1];
 struct snd_mask mres[5];
 struct snd_interval intervals[SNDRV_PCM_HW_PARAM_LAST_INTERVAL -
 SNDRV_PCM_HW_PARAM_FIRST_INTERVAL + 1];
 struct snd_interval ires[9];
 unsigned int rmask;
 unsigned int cmask;
 unsigned int info;
 unsigned int msbits;
 unsigned int rate_num;
 unsigned int rate_den;
 snd_pcm_uframes_t fifo_size;
 unsigned char reserved[64];
};

enum {
 SNDRV_PCM_TSTAMP_NONE = 0,
 SNDRV_PCM_TSTAMP_ENABLE,
 SNDRV_PCM_TSTAMP_LAST = SNDRV_PCM_TSTAMP_ENABLE,
};

struct snd_pcm_sw_params {
 int tstamp_mode;
 unsigned int period_step;
 unsigned int sleep_min;
 snd_pcm_uframes_t avail_min;
 snd_pcm_uframes_t xfer_align;
 snd_pcm_uframes_t start_threshold;
 snd_pcm_uframes_t stop_threshold;
 snd_pcm_uframes_t silence_threshold;
 snd_pcm_uframes_t silence_size;
 snd_pcm_uframes_t boundary;
 unsigned char reserved[64];
};

struct snd_pcm_channel_info {
 unsigned int channel;
 long offset;
 unsigned int first;
 unsigned int step;
};

struct snd_pcm_status {
 snd_pcm_state_t state;
 struct timespec trigger_tstamp;
 struct timespec tstamp;
 snd_pcm_uframes_t appl_ptr;
 snd_pcm_uframes_t hw_ptr;
 snd_pcm_sframes_t delay;
 snd_pcm_uframes_t avail;
 snd_pcm_uframes_t avail_max;
 snd_pcm_uframes_t overrange;
 snd_pcm_state_t suspended_state;
 unsigned char reserved[60];
};

struct snd_pcm_mmap_status {
 snd_pcm_state_t state;
 int pad1;
 snd_pcm_uframes_t hw_ptr;
 struct timespec tstamp;
 snd_pcm_state_t suspended_state;
};

struct snd_pcm_mmap_control {
 snd_pcm_uframes_t appl_ptr;
 snd_pcm_uframes_t avail_min;
};

#define SNDRV_PCM_SYNC_PTR_HWSYNC (1<<0)
#define SNDRV_PCM_SYNC_PTR_APPL (1<<1)
#define SNDRV_PCM_SYNC_PTR_AVAIL_MIN (1<<2)

struct snd_pcm_sync_ptr {
 unsigned int flags;
 union {
 struct snd_pcm_mmap_status status;
 unsigned char reserved[64];
 } s;
 union {
 struct snd_pcm_mmap_control control;
 unsigned char reserved[64];
 } c;
};

struct snd_xferi {
 snd_pcm_sframes_t result;
 void  *buf;
 snd_pcm_uframes_t frames;
};

struct snd_xfern {
 snd_pcm_sframes_t result;
 void  *  *bufs;
 snd_pcm_uframes_t frames;
};

enum {
 SNDRV_PCM_TSTAMP_TYPE_GETTIMEOFDAY = 0,
 SNDRV_PCM_TSTAMP_TYPE_MONOTONIC,
 SNDRV_PCM_TSTAMP_TYPE_LAST = SNDRV_PCM_TSTAMP_TYPE_MONOTONIC,
};

#include <asm-generic/ioctl.h>
#define SNDRV_PCM_IOCTL_PVERSION _IOR('A', 0x00, int)
#define SNDRV_PCM_IOCTL_INFO _IOR('A', 0x01, struct snd_pcm_info)
#define SNDRV_PCM_IOCTL_TSTAMP _IOW('A', 0x02, int)
#define SNDRV_PCM_IOCTL_TTSTAMP _IOW('A', 0x03, int)
#define SNDRV_PCM_IOCTL_HW_REFINE _IOWR('A', 0x10, struct snd_pcm_hw_params)
#define SNDRV_PCM_IOCTL_HW_PARAMS _IOWR('A', 0x11, struct snd_pcm_hw_params)
#define SNDRV_PCM_IOCTL_HW_FREE _IO('A', 0x12)
#define SNDRV_PCM_IOCTL_SW_PARAMS _IOWR('A', 0x13, struct snd_pcm_sw_params)
#define SNDRV_PCM_IOCTL_STATUS _IOR('A', 0x20, struct snd_pcm_status)
#define SNDRV_PCM_IOCTL_DELAY _IOR('A', 0x21, snd_pcm_sframes_t)
#define SNDRV_PCM_IOCTL_HWSYNC _IO('A', 0x22)
#define SNDRV_PCM_IOCTL_SYNC_PTR _IOWR('A', 0x23, struct snd_pcm_sync_ptr)
#define SNDRV_PCM_IOCTL_CHANNEL_INFO _IOR('A', 0x32, struct snd_pcm_channel_info)
#define SNDRV_PCM_IOCTL_PREPARE _IO('A', 0x40)
#define SNDRV_PCM_IOCTL_RESET _IO('A', 0x41)
#define SNDRV_PCM_IOCTL_START _IO('A', 0x42)
#define SNDRV_PCM_IOCTL_DROP _IO('A', 0x43)
#define SNDRV_PCM_IOCTL_DRAIN _IO('A', 0x44)
#define SNDRV_PCM_IOCTL_PAUSE _IOW('A', 0x45, int)
#define SNDRV_PCM_IOCTL_REWIND _IOW('A', 0x46, snd_pcm_uframes_t)
#define SNDRV_PCM_IOCTL_RESUME _IO('A', 0x47)
#define SNDRV_PCM_IOCTL_XRUN _IO('A', 0x48)
#define SNDRV_PCM_IOCTL_FORWARD _IOW('A', 0x49, snd_pcm_uframes_t)
#define SNDRV_PCM_IOCTL_WRITEI_FRAMES _IOW('A', 0x50, struct snd_xferi)
#define SNDRV_PCM_IOCTL_READI_FRAMES _IOR('A', 0x51, struct snd_xferi)
#define SNDRV_PCM_IOCTL_WRITEN_FRAMES _IOW('A', 0x52, struct snd_xfern)
#define SNDRV_PCM_IOCTL_READN_FRAMES _IOR('A', 0x53, struct snd_xfern)
#define SNDRV_PCM_IOCTL_LINK _IOW('A', 0x60, int)
#define SNDRV_PCM_IOCTL_UNLINK _IO('A', 0x61)

#define SNDRV_RAWMIDI_VERSION SNDRV_PROTOCOL_VERSION(2, 0, 0)

enum {
 SNDRV_RAWMIDI_STREAM_OUTPUT = 0,
 SNDRV_RAWMIDI_STREAM_INPUT,
 SNDRV_RAWMIDI_STREAM_LAST = SNDRV_RAWMIDI_STREAM_INPUT,
};

#define SNDRV_RAWMIDI_INFO_OUTPUT 0x00000001
#define SNDRV_RAWMIDI_INFO_INPUT 0x00000002
#define SNDRV_RAWMIDI_INFO_DUPLEX 0x00000004

struct snd_rawmidi_info {
 unsigned int device;
 unsigned int subdevice;
 int stream;
 int card;
 unsigned int flags;
 unsigned char id[64];
 unsigned char name[80];
 unsigned char subname[32];
 unsigned int subdevices_count;
 unsigned int subdevices_avail;
 unsigned char reserved[64];
};

struct snd_rawmidi_params {
 int stream;
 ulong buffer_size;
 ulong avail_min;
 unsigned int no_active_sensing: 1;
 unsigned char reserved[16];
};

struct snd_rawmidi_status {
 int stream;
 struct timespec tstamp;
 ulong avail;
 ulong xruns;
 unsigned char reserved[16];
};

#define SNDRV_RAWMIDI_IOCTL_PVERSION _IOR('W', 0x00, int)
#define SNDRV_RAWMIDI_IOCTL_INFO _IOR('W', 0x01, struct snd_rawmidi_info)
#define SNDRV_RAWMIDI_IOCTL_PARAMS _IOWR('W', 0x10, struct snd_rawmidi_params)
#define SNDRV_RAWMIDI_IOCTL_STATUS _IOWR('W', 0x20, struct snd_rawmidi_status)
#define SNDRV_RAWMIDI_IOCTL_DROP _IOW('W', 0x30, int)
#define SNDRV_RAWMIDI_IOCTL_DRAIN _IOW('W', 0x31, int)

#define SNDRV_TIMER_VERSION SNDRV_PROTOCOL_VERSION(2, 0, 6)

enum {
 SNDRV_TIMER_CLASS_NONE = -1,
 SNDRV_TIMER_CLASS_SLAVE = 0,
 SNDRV_TIMER_CLASS_GLOBAL,
 SNDRV_TIMER_CLASS_CARD,
 SNDRV_TIMER_CLASS_PCM,
 SNDRV_TIMER_CLASS_LAST = SNDRV_TIMER_CLASS_PCM,
};

enum {
 SNDRV_TIMER_SCLASS_NONE = 0,
 SNDRV_TIMER_SCLASS_APPLICATION,
 SNDRV_TIMER_SCLASS_SEQUENCER,
 SNDRV_TIMER_SCLASS_OSS_SEQUENCER,
 SNDRV_TIMER_SCLASS_LAST = SNDRV_TIMER_SCLASS_OSS_SEQUENCER,
};

#define SNDRV_TIMER_GLOBAL_SYSTEM 0
#define SNDRV_TIMER_GLOBAL_RTC 1
#define SNDRV_TIMER_GLOBAL_HPET 2
#define SNDRV_TIMER_GLOBAL_HRTIMER 3

#define SNDRV_TIMER_FLG_SLAVE (1<<0)

struct snd_timer_id {
 int dev_class;
 int dev_sclass;
 int card;
 int device;
 int subdevice;
};

struct snd_timer_ginfo {
 struct snd_timer_id tid;
 unsigned int flags;
 int card;
 unsigned char id[64];
 unsigned char name[80];
 unsigned long reserved0;
 unsigned long resolution;
 unsigned long resolution_min;
 unsigned long resolution_max;
 unsigned int clients;
 unsigned char reserved[32];
};

struct snd_timer_gparams {
 struct snd_timer_id tid;
 unsigned long period_num;
 unsigned long period_den;
 unsigned char reserved[32];
};

struct snd_timer_gstatus {
 struct snd_timer_id tid;
 unsigned long resolution;
 unsigned long resolution_num;
 unsigned long resolution_den;
 unsigned char reserved[32];
};

struct snd_timer_select {
 struct snd_timer_id id;
 unsigned char reserved[32];
};

struct snd_timer_info {
 unsigned int flags;
 int card;
 unsigned char id[64];
 unsigned char name[80];
 unsigned long reserved0;
 unsigned long resolution;
 unsigned char reserved[64];
};

#define SNDRV_TIMER_PSFLG_AUTO (1<<0)
#define SNDRV_TIMER_PSFLG_EXCLUSIVE (1<<1)
#define SNDRV_TIMER_PSFLG_EARLY_EVENT (1<<2)

struct snd_timer_params {
 unsigned int flags;
 unsigned int ticks;
 unsigned int queue_size;
 unsigned int reserved0;
 unsigned int filter;
 unsigned char reserved[60];
};

struct snd_timer_status {
 struct timespec tstamp;
 unsigned int resolution;
 unsigned int lost;
 unsigned int overrun;
 unsigned int queue;
 unsigned char reserved[64];
};

#define SNDRV_TIMER_IOCTL_PVERSION _IOR('T', 0x00, int)
#define SNDRV_TIMER_IOCTL_NEXT_DEVICE _IOWR('T', 0x01, struct snd_timer_id)
#define SNDRV_TIMER_IOCTL_TREAD _IOW('T', 0x02, int)
#define SNDRV_TIMER_IOCTL_GINFO _IOWR('T', 0x03, struct snd_timer_ginfo)
#define SNDRV_TIMER_IOCTL_GPARAMS _IOW('T', 0x04, struct snd_timer_gparams)
#define SNDRV_TIMER_IOCTL_GSTATUS _IOWR('T', 0x05, struct snd_timer_gstatus)
#define SNDRV_TIMER_IOCTL_SELECT _IOW('T', 0x10, struct snd_timer_select)
#define SNDRV_TIMER_IOCTL_INFO _IOR('T', 0x11, struct snd_timer_info)
#define SNDRV_TIMER_IOCTL_PARAMS _IOW('T', 0x12, struct snd_timer_params)
#define SNDRV_TIMER_IOCTL_STATUS _IOR('T', 0x14, struct snd_timer_status)

#define SNDRV_TIMER_IOCTL_START _IO('T', 0xa0)
#define SNDRV_TIMER_IOCTL_STOP _IO('T', 0xa1)
#define SNDRV_TIMER_IOCTL_CONTINUE _IO('T', 0xa2)
#define SNDRV_TIMER_IOCTL_PAUSE _IO('T', 0xa3)

struct snd_timer_read {
 unsigned int resolution;
 unsigned int ticks;
};

enum {
 SNDRV_TIMER_EVENT_RESOLUTION = 0,
 SNDRV_TIMER_EVENT_TICK,
 SNDRV_TIMER_EVENT_START,
 SNDRV_TIMER_EVENT_STOP,
 SNDRV_TIMER_EVENT_CONTINUE,
 SNDRV_TIMER_EVENT_PAUSE,
 SNDRV_TIMER_EVENT_EARLY,
 SNDRV_TIMER_EVENT_SUSPEND,
 SNDRV_TIMER_EVENT_RESUME,

 SNDRV_TIMER_EVENT_MSTART = SNDRV_TIMER_EVENT_START + 10,
 SNDRV_TIMER_EVENT_MSTOP = SNDRV_TIMER_EVENT_STOP + 10,
 SNDRV_TIMER_EVENT_MCONTINUE = SNDRV_TIMER_EVENT_CONTINUE + 10,
 SNDRV_TIMER_EVENT_MPAUSE = SNDRV_TIMER_EVENT_PAUSE + 10,
 SNDRV_TIMER_EVENT_MSUSPEND = SNDRV_TIMER_EVENT_SUSPEND + 10,
 SNDRV_TIMER_EVENT_MRESUME = SNDRV_TIMER_EVENT_RESUME + 10,
};

struct snd_timer_tread {
 int event;
 struct timespec tstamp;
 unsigned int val;
};

#define SNDRV_CTL_VERSION SNDRV_PROTOCOL_VERSION(2, 0, 6)

struct snd_ctl_card_info {
 int card;
 int pad;
 unsigned char id[16];
 unsigned char driver[16];
 unsigned char name[32];
 unsigned char longname[80];
 unsigned char reserved_[16];
 unsigned char mixername[80];
 unsigned char components[128];
};

typedef int  snd_ctl_elem_type_t;
#define SNDRV_CTL_ELEM_TYPE_NONE (( snd_ctl_elem_type_t) 0)
#define SNDRV_CTL_ELEM_TYPE_BOOLEAN (( snd_ctl_elem_type_t) 1)
#define SNDRV_CTL_ELEM_TYPE_INTEGER (( snd_ctl_elem_type_t) 2)
#define SNDRV_CTL_ELEM_TYPE_ENUMERATED (( snd_ctl_elem_type_t) 3)
#define SNDRV_CTL_ELEM_TYPE_BYTES (( snd_ctl_elem_type_t) 4)
#define SNDRV_CTL_ELEM_TYPE_IEC958 (( snd_ctl_elem_type_t) 5)
#define SNDRV_CTL_ELEM_TYPE_INTEGER64 (( snd_ctl_elem_type_t) 6)
#define SNDRV_CTL_ELEM_TYPE_LAST SNDRV_CTL_ELEM_TYPE_INTEGER64

typedef int  snd_ctl_elem_iface_t;
#define SNDRV_CTL_ELEM_IFACE_CARD (( snd_ctl_elem_iface_t) 0)
#define SNDRV_CTL_ELEM_IFACE_HWDEP (( snd_ctl_elem_iface_t) 1)
#define SNDRV_CTL_ELEM_IFACE_MIXER (( snd_ctl_elem_iface_t) 2)
#define SNDRV_CTL_ELEM_IFACE_PCM (( snd_ctl_elem_iface_t) 3)
#define SNDRV_CTL_ELEM_IFACE_RAWMIDI (( snd_ctl_elem_iface_t) 4)
#define SNDRV_CTL_ELEM_IFACE_TIMER (( snd_ctl_elem_iface_t) 5)
#define SNDRV_CTL_ELEM_IFACE_SEQUENCER (( snd_ctl_elem_iface_t) 6)
#define SNDRV_CTL_ELEM_IFACE_LAST SNDRV_CTL_ELEM_IFACE_SEQUENCER

#define SNDRV_CTL_ELEM_ACCESS_READ (1<<0)
#define SNDRV_CTL_ELEM_ACCESS_WRITE (1<<1)
#define SNDRV_CTL_ELEM_ACCESS_READWRITE (SNDRV_CTL_ELEM_ACCESS_READ|SNDRV_CTL_ELEM_ACCESS_WRITE)
#define SNDRV_CTL_ELEM_ACCESS_VOLATILE (1<<2)
#define SNDRV_CTL_ELEM_ACCESS_TIMESTAMP (1<<3)
#define SNDRV_CTL_ELEM_ACCESS_TLV_READ (1<<4)
#define SNDRV_CTL_ELEM_ACCESS_TLV_WRITE (1<<5)
#define SNDRV_CTL_ELEM_ACCESS_TLV_READWRITE (SNDRV_CTL_ELEM_ACCESS_TLV_READ|SNDRV_CTL_ELEM_ACCESS_TLV_WRITE)
#define SNDRV_CTL_ELEM_ACCESS_TLV_COMMAND (1<<6)
#define SNDRV_CTL_ELEM_ACCESS_INACTIVE (1<<8)
#define SNDRV_CTL_ELEM_ACCESS_LOCK (1<<9)
#define SNDRV_CTL_ELEM_ACCESS_OWNER (1<<10)
#define SNDRV_CTL_ELEM_ACCESS_TLV_CALLBACK (1<<28)
#define SNDRV_CTL_ELEM_ACCESS_USER (1<<29)

#define SNDRV_CTL_POWER_D0 0x0000
#define SNDRV_CTL_POWER_D1 0x0100
#define SNDRV_CTL_POWER_D2 0x0200
#define SNDRV_CTL_POWER_D3 0x0300
#define SNDRV_CTL_POWER_D3hot (SNDRV_CTL_POWER_D3|0x0000)
#define SNDRV_CTL_POWER_D3cold (SNDRV_CTL_POWER_D3|0x0001)

struct snd_ctl_elem_id {
 unsigned int numid;
 snd_ctl_elem_iface_t iface;
 unsigned int device;
 unsigned int subdevice;
 unsigned char name[44];
 unsigned int index;
};

struct snd_ctl_elem_list {
 unsigned int offset;
 unsigned int space;
 unsigned int used;
 unsigned int count;
 struct snd_ctl_elem_id  *pids;
 unsigned char reserved[50];
};

struct snd_ctl_elem_info {
 struct snd_ctl_elem_id id;
 snd_ctl_elem_type_t type;
 unsigned int access;
 unsigned int count;
 int owner;
 union {
 struct {
 long min;
 long max;
 long step;
 } integer;
 struct {
 long long min;
 long long max;
 long long step;
 } integer64;
 struct {
 unsigned int items;
 unsigned int item;
 char name[64];
 } enumerated;
 unsigned char reserved[128];
 } value;
 union {
 unsigned short d[4];
 unsigned short *d_ptr;
 } dimen;
 unsigned char reserved[64-4*sizeof(unsigned short)];
};

struct snd_aes_iec958 {
 unsigned char status[24];
 unsigned char subcode[147];
 unsigned char pad;
 unsigned char dig_subframe[4];
};

struct snd_ctl_elem_value {
 struct snd_ctl_elem_id id;
 unsigned int indirect: 1;
 union {
 union {
 long value[128];
 long *value_ptr;
 } integer;
 union {
 long long value[64];
 long long *value_ptr;
 } integer64;
 union {
 unsigned int item[128];
 unsigned int *item_ptr;
 } enumerated;
 union {
 unsigned char data[512];
 unsigned char *data_ptr;
 } bytes;
 struct snd_aes_iec958 iec958;
 } value;
 struct timespec tstamp;
 unsigned char reserved[128-sizeof(struct timespec)];
};

struct snd_ctl_tlv {
 unsigned int numid;
 unsigned int length;
 unsigned int tlv[0];
};

#define SNDRV_CTL_IOCTL_PVERSION _IOR('U', 0x00, int)
#define SNDRV_CTL_IOCTL_CARD_INFO _IOR('U', 0x01, struct snd_ctl_card_info)
#define SNDRV_CTL_IOCTL_ELEM_LIST _IOWR('U', 0x10, struct snd_ctl_elem_list)
#define SNDRV_CTL_IOCTL_ELEM_INFO _IOWR('U', 0x11, struct snd_ctl_elem_info)
#define SNDRV_CTL_IOCTL_ELEM_READ _IOWR('U', 0x12, struct snd_ctl_elem_value)
#define SNDRV_CTL_IOCTL_ELEM_WRITE _IOWR('U', 0x13, struct snd_ctl_elem_value)
#define SNDRV_CTL_IOCTL_ELEM_LOCK _IOW('U', 0x14, struct snd_ctl_elem_id)
#define SNDRV_CTL_IOCTL_ELEM_UNLOCK _IOW('U', 0x15, struct snd_ctl_elem_id)
#define SNDRV_CTL_IOCTL_SUBSCRIBE_EVENTS _IOWR('U', 0x16, int)
#define SNDRV_CTL_IOCTL_ELEM_ADD _IOWR('U', 0x17, struct snd_ctl_elem_info)
#define SNDRV_CTL_IOCTL_ELEM_REPLACE _IOWR('U', 0x18, struct snd_ctl_elem_info)
#define SNDRV_CTL_IOCTL_ELEM_REMOVE _IOWR('U', 0x19, struct snd_ctl_elem_id)
#define SNDRV_CTL_IOCTL_TLV_READ _IOWR('U', 0x1a, struct snd_ctl_tlv)
#define SNDRV_CTL_IOCTL_TLV_WRITE _IOWR('U', 0x1b, struct snd_ctl_tlv)
#define SNDRV_CTL_IOCTL_TLV_COMMAND _IOWR('U', 0x1c, struct snd_ctl_tlv)
#define SNDRV_CTL_IOCTL_HWDEP_NEXT_DEVICE _IOWR('U', 0x20, int)
#define SNDRV_CTL_IOCTL_HWDEP_INFO _IOR('U', 0x21, struct snd_hwdep_info)
#define SNDRV_CTL_IOCTL_PCM_NEXT_DEVICE _IOR('U', 0x30, int)
#define SNDRV_CTL_IOCTL_PCM_INFO _IOWR('U', 0x31, struct snd_pcm_info)
#define SNDRV_CTL_IOCTL_PCM_PREFER_SUBDEVICE _IOW('U', 0x32, int)
#define SNDRV_CTL_IOCTL_RAWMIDI_NEXT_DEVICE _IOWR('U', 0x40, int)
#define SNDRV_CTL_IOCTL_RAWMIDI_INFO _IOWR('U', 0x41, struct snd_rawmidi_info)
#define SNDRV_CTL_IOCTL_RAWMIDI_PREFER_SUBDEVICE _IOW('U', 0x42, int)
#define SNDRV_CTL_IOCTL_POWER _IOWR('U', 0xd0, int)
#define SNDRV_CTL_IOCTL_POWER_STATE _IOR('U', 0xd1, int)

enum sndrv_ctl_event_type {
 SNDRV_CTL_EVENT_ELEM = 0,
 SNDRV_CTL_EVENT_LAST = SNDRV_CTL_EVENT_ELEM,
};

#define SNDRV_CTL_EVENT_MASK_VALUE (1<<0)
#define SNDRV_CTL_EVENT_MASK_INFO (1<<1)
#define SNDRV_CTL_EVENT_MASK_ADD (1<<2)
#define SNDRV_CTL_EVENT_MASK_TLV (1<<3)
#define SNDRV_CTL_EVENT_MASK_REMOVE (~0U)

struct snd_ctl_event {
 int type;
 union {
 struct {
 unsigned int mask;
 struct snd_ctl_elem_id id;
 } elem;
 unsigned char data8[60];
 } data;
};

#define SNDRV_CTL_NAME_NONE ""
#define SNDRV_CTL_NAME_PLAYBACK "Playback "
#define SNDRV_CTL_NAME_CAPTURE "Capture "

#define SNDRV_CTL_NAME_IEC958_NONE ""
#define SNDRV_CTL_NAME_IEC958_SWITCH "Switch"
#define SNDRV_CTL_NAME_IEC958_VOLUME "Volume"
#define SNDRV_CTL_NAME_IEC958_DEFAULT "Default"
#define SNDRV_CTL_NAME_IEC958_MASK "Mask"
#define SNDRV_CTL_NAME_IEC958_CON_MASK "Con Mask"
#define SNDRV_CTL_NAME_IEC958_PRO_MASK "Pro Mask"
#define SNDRV_CTL_NAME_IEC958_PCM_STREAM "PCM Stream"
#define SNDRV_CTL_NAME_IEC958(expl,direction,what) "IEC958 " expl SNDRV_CTL_NAME_##direction SNDRV_CTL_NAME_IEC958_##what

#define PARAM_MAX SNDRV_PCM_HW_PARAM_LAST_INTERVAL
#define SNDRV_PCM_HW_PARAMS_NO_PERIOD_WAKEUP (1<<2)

static inline int param_is_mask(int p)
{
    return (p >= SNDRV_PCM_HW_PARAM_FIRST_MASK) &&
        (p <= SNDRV_PCM_HW_PARAM_LAST_MASK);
}

static inline int param_is_interval(int p)
{
    return (p >= SNDRV_PCM_HW_PARAM_FIRST_INTERVAL) &&
        (p <= SNDRV_PCM_HW_PARAM_LAST_INTERVAL);
}

static inline struct snd_interval *param_to_interval(struct snd_pcm_hw_params *p, int n)
{
    return &(p->intervals[n - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL]);
}

static inline struct snd_mask *param_to_mask(struct snd_pcm_hw_params *p, int n)
{
    return &(p->masks[n - SNDRV_PCM_HW_PARAM_FIRST_MASK]);
}

static void param_set_mask(struct snd_pcm_hw_params *p, int n, unsigned int bit)
{
    if (bit >= SNDRV_MASK_MAX)
        return;
    if (param_is_mask(n)) {
        struct snd_mask *m = param_to_mask(p, n);
        m->bits[0] = 0;
        m->bits[1] = 0;
        m->bits[bit >> 5] |= (1 << (bit & 31));
    }
}

static void param_set_min(struct snd_pcm_hw_params *p, int n, unsigned int val)
{
    if (param_is_interval(n)) {
        struct snd_interval *i = param_to_interval(p, n);
        i->min = val;
    }
}

/*static void param_set_max(struct snd_pcm_hw_params *p, int n, unsigned int val)
{
    if (param_is_interval(n)) {
        struct snd_interval *i = param_to_interval(p, n);
        i->max = val;
    }
}*/

static void param_set_int(struct snd_pcm_hw_params *p, int n, unsigned int val)
{
    if (param_is_interval(n)) {
        struct snd_interval *i = param_to_interval(p, n);
        i->min = val;
        i->max = val;
        i->integer = 1;
    }
}

static unsigned int param_get_int(struct snd_pcm_hw_params *p, int n)
{
    if (param_is_interval(n)) {
        struct snd_interval *i = param_to_interval(p, n);
        if (i->integer)
            return i->max;
    }
    return 0;
}

static void param_init(struct snd_pcm_hw_params *p)
{
    int n;

    clear(*p);
    for (n = SNDRV_PCM_HW_PARAM_FIRST_MASK;
         n <= SNDRV_PCM_HW_PARAM_LAST_MASK; n++) {
            struct snd_mask *m = param_to_mask(p, n);
            m->bits[0] = ~0;
            m->bits[1] = ~0;
    }
    for (n = SNDRV_PCM_HW_PARAM_FIRST_INTERVAL;
         n <= SNDRV_PCM_HW_PARAM_LAST_INTERVAL; n++) {
            struct snd_interval *i = param_to_interval(p, n);
            i->min = 0;
            i->max = ~0;
    }
}




unsigned int pcm_get_buffer_size(pcm& pcm)
{
    return pcm.buffer_size;
}

const char* pcm_get_error(pcm& pcm)
{
    return pcm.error;
}

static unsigned int pcm_format_to_alsa(enum pcm_format format)
{
    switch (format) {
    case PCM_FORMAT_S32_LE:
        return SNDRV_PCM_FORMAT_S32_LE;
    default:
    case PCM_FORMAT_S16_LE:
        return SNDRV_PCM_FORMAT_S16_LE;
    };
}

static unsigned int pcm_format_to_bits(enum pcm_format format)
{
    switch (format) {
    case PCM_FORMAT_S32_LE:
        return 32;
    default:
    case PCM_FORMAT_S16_LE:
        return 16;
    };
}

unsigned int pcm_bytes_to_frames(pcm& pcm, unsigned int bytes)
{
    return bytes / (pcm.config.channels *
        (pcm_format_to_bits(pcm.config.format) >> 3));
}

unsigned int pcm_frames_to_bytes(pcm& pcm, unsigned int frames)
{
    return frames * pcm.config.channels *
        (pcm_format_to_bits(pcm.config.format) >> 3);
}

static int pcm_sync_ptr(pcm& pcm, int flags) {
    if (pcm.sync_ptr) {
        pcm.sync_ptr->flags = flags;
        if (ioctl(pcm.fd, SNDRV_PCM_IOCTL_SYNC_PTR, pcm.sync_ptr) < 0)
            return -1;
    }
    return 0;
}

static int pcm_hw_mmap_status(pcm& pcm) {

    if (pcm.sync_ptr)
        return 0;

    int page_size = 4096;
    pcm.mmap_status = (snd_pcm_mmap_status*)mmap(0, page_size, PROT_READ, MAP_SHARED,
                            pcm.fd, SNDRV_PCM_MMAP_OFFSET_STATUS);
    if (pcm.mmap_status == 0)
        pcm.mmap_status = 0;
    if (!pcm.mmap_status)
        goto mmap_error;

    pcm.mmap_control = (snd_pcm_mmap_control*)mmap(0, page_size, PROT_READ | PROT_WRITE,
                             MAP_SHARED, pcm.fd, SNDRV_PCM_MMAP_OFFSET_CONTROL);
    if (pcm.mmap_control == 0)
        pcm.mmap_control = 0;
    if (!pcm.mmap_control) {
        munmap(pcm.mmap_status, page_size);
        pcm.mmap_status = 0;
        goto mmap_error;
    }
    pcm.mmap_control->avail_min = 1;

    return 0;

mmap_error:

    pcm.sync_ptr = &alloc<snd_pcm_sync_ptr>();
    assert(!pcm.sync_ptr);
    pcm.mmap_status = &pcm.sync_ptr->s.status;
    pcm.mmap_control = &pcm.sync_ptr->c.control;
    pcm.mmap_control->avail_min = 1;
    pcm_sync_ptr(pcm, 0);

    return 0;
}

static void pcm_hw_munmap_status(pcm& pcm) {
    if (pcm.sync_ptr) {
        free(pcm.sync_ptr);
        pcm.sync_ptr = 0;
    } else {
        int page_size = 4096;
        if (pcm.mmap_status)
            munmap(pcm.mmap_status, page_size);
        if (pcm.mmap_control)
            munmap(pcm.mmap_control, page_size);
    }
    pcm.mmap_status = 0;
    pcm.mmap_control = 0;
}

int pcm_get_htimestamp(pcm& pcm, unsigned int *avail,
                       struct timespec *tstamp)
{
    int frames;
    int rc;
    snd_pcm_uframes_t hw_ptr;

    if (!pcm_is_ready(pcm))
        return -1;

    rc = pcm_sync_ptr(pcm, SNDRV_PCM_SYNC_PTR_APPL|SNDRV_PCM_SYNC_PTR_HWSYNC);
    if (rc < 0)
        return -1;

    if ((pcm.mmap_status->state != PCM_STATE_RUNNING) &&
            (pcm.mmap_status->state != PCM_STATE_DRAINING))
        return -1;

    *tstamp = pcm.mmap_status->tstamp;
    if (tstamp->sec == 0 && tstamp->nsec == 0)
        return -1;

    hw_ptr = pcm.mmap_status->hw_ptr;
    if (pcm.flags & PCM_IN)
        frames = hw_ptr - pcm.mmap_control->appl_ptr;
    else
        frames = hw_ptr + pcm.buffer_size - pcm.mmap_control->appl_ptr;

    if (frames < 0)
        return -1;

    *avail = (unsigned int)frames;

    return 0;
}

#if 0
int pcm_write(pcm& pcm, const void *data, unsigned int count)
{
    struct snd_xferi x;

    if (pcm.flags & PCM_IN)
        return -EINVAL;

    x.buf = (void*)data;
    x.frames = count / (pcm.config.channels *
                        pcm_format_to_bits(pcm.config.format) / 8);

    for (;;) {
        if (!pcm.running) {
            if (ioctl(pcm.fd, SNDRV_PCM_IOCTL_PREPARE))
                return error(pcm, errno, "cannot prepare channel");
            if (ioctl(pcm.fd, SNDRV_PCM_IOCTL_WRITEI_FRAMES, &x))
                return error(pcm, errno, "cannot write initial data");
            pcm.running = 1;
            return 0;
        }
        if (ioctl(pcm.fd, SNDRV_PCM_IOCTL_WRITEI_FRAMES, &x)) {
            pcm.running = 0;
            if (errno == EPIPE) {
                /* we failed to make our window -- try to restart if we are
                 * allowed to do so.  Otherwise, simply allow the EPIPE error to
                 * propagate up to the app level */
                pcm.underruns++;
                if (pcm.flags & PCM_NORESTART)
                    return -EPIPE;
                continue;
            }
            return error(pcm, errno, "cannot write stream data");
        }
        return 0;
    }
}

int pcm_read(pcm& pcm, void *data, unsigned int count)
{
    struct snd_xferi x;

    if (!(pcm.flags & PCM_IN))
        return -EINVAL;

    x.buf = data;
    x.frames = count / (pcm.config.channels *
                        pcm_format_to_bits(pcm.config.format) / 8);

    for (;;) {
        if (!pcm.running) {
            if (pcm_start(pcm) < 0) {
                fprintf(stderr, "start error");
                return -errno;
            }
        }
        if (ioctl(pcm.fd, SNDRV_PCM_IOCTL_READI_FRAMES, &x)) {
            pcm.running = 0;
            if (errno == EPIPE) {
                    /* we failed to make our window -- try to restart */
                pcm.underruns++;
                continue;
            }
            return error(pcm, errno, "cannot read stream data");
        }
        return 0;
    }
}
#endif

int pcm_close(pcm& pcm)
{
    pcm_hw_munmap_status(pcm);

    if (pcm.flags & PCM_MMAP) {
        pcm_stop(pcm);
        munmap(pcm.mmap_buffer, pcm_frames_to_bytes(pcm, pcm.buffer_size));
    }

    if (pcm.fd >= 0)
        close(pcm.fd);
    pcm.running = 0;
    pcm.buffer_size = 0;
    pcm.fd = -1;
    return 0;
}

pcm pcm_open(unsigned int unused card, unsigned int unused device,
                     unsigned int flags, struct pcm_config *config)
{
    struct pcm pcm;
    struct snd_pcm_info info;
    struct snd_pcm_hw_params params;
    struct snd_pcm_sw_params sparams;
    const char* fn = "/dev/snd/pcmC0D0p";
    int rc;

    assert(config);

    pcm.config = *config;

    pcm.flags = flags;
    pcm.fd = open(fn, O_RDWR, 0);
    if (pcm.fd < 0) {
        error("cannot open device '%s'", fn);
        return pcm;
    }

    if (ioctl(pcm.fd, SNDRV_PCM_IOCTL_INFO, &info)) {
        error("cannot get info");
        goto fail_close;
    }

    param_init(&params);
    param_set_mask(&params, SNDRV_PCM_HW_PARAM_FORMAT,
                   pcm_format_to_alsa(config->format));
    param_set_mask(&params, SNDRV_PCM_HW_PARAM_SUBFORMAT,
                   SNDRV_PCM_SUBFORMAT_STD);
    param_set_min(&params, SNDRV_PCM_HW_PARAM_PERIOD_SIZE, config->period_size);
    param_set_int(&params, SNDRV_PCM_HW_PARAM_SAMPLE_BITS,
                  pcm_format_to_bits(config->format));
    param_set_int(&params, SNDRV_PCM_HW_PARAM_FRAME_BITS,
                  pcm_format_to_bits(config->format) * config->channels);
    param_set_int(&params, SNDRV_PCM_HW_PARAM_CHANNELS,
                  config->channels);
    param_set_int(&params, SNDRV_PCM_HW_PARAM_PERIODS, config->period_count);
    param_set_int(&params, SNDRV_PCM_HW_PARAM_RATE, config->rate);

    if (flags & PCM_NOIRQ) {

        if (!(flags & PCM_MMAP)) {
            error("noirq only currently supported with mmap().");
            goto fail;
        }

        params.flags |= SNDRV_PCM_HW_PARAMS_NO_PERIOD_WAKEUP;
        pcm.noirq_frames_per_msec = config->rate / 1000;
    }

    if (flags & PCM_MMAP)
        param_set_mask(&params, SNDRV_PCM_HW_PARAM_ACCESS,
                   SNDRV_PCM_ACCESS_MMAP_INTERLEAVED);
    else
        param_set_mask(&params, SNDRV_PCM_HW_PARAM_ACCESS,
                   SNDRV_PCM_ACCESS_RW_INTERLEAVED);

    if (ioctl(pcm.fd, SNDRV_PCM_IOCTL_HW_PARAMS, &params)) {
        error( "cannot set hw params");
        goto fail_close;
    }

    /* get our refined hw_params */
    config->period_size = param_get_int(&params, SNDRV_PCM_HW_PARAM_PERIOD_SIZE);
    config->period_count = param_get_int(&params, SNDRV_PCM_HW_PARAM_PERIODS);
    pcm.buffer_size = config->period_count * config->period_size;

    if (flags & PCM_MMAP) {
        pcm.mmap_buffer = mmap(0, pcm_frames_to_bytes(pcm, pcm.buffer_size),
                                PROT_READ | PROT_WRITE, MAP_FILE | MAP_SHARED, pcm.fd, 0);
        if (pcm.mmap_buffer == 0) {
            error("failed to mmap buffer %d bytes\n",
                 pcm_frames_to_bytes(pcm, pcm.buffer_size));
            goto fail_close;
        }
    }

    clear(sparams);
    sparams.tstamp_mode = SNDRV_PCM_TSTAMP_ENABLE;
    sparams.period_step = 1;
    sparams.avail_min = 1;

    if (!config->start_threshold)
        pcm.config.start_threshold = sparams.start_threshold =
            config->period_count * config->period_size / 2;
    else
        sparams.start_threshold = config->start_threshold;

    /* pick a high stop threshold - todo: does this need further tuning */
    if (!config->stop_threshold) {
        if (pcm.flags & PCM_IN)
            pcm.config.stop_threshold = sparams.stop_threshold =
                config->period_count * config->period_size * 10;
        else
            pcm.config.stop_threshold = sparams.stop_threshold =
                config->period_count * config->period_size;
    }
    else
        sparams.stop_threshold = config->stop_threshold;

    sparams.xfer_align = config->period_size / 2; /* needed for old kernels */
    sparams.silence_size = 0;
    sparams.silence_threshold = config->silence_threshold;
    pcm.boundary = sparams.boundary = pcm.buffer_size;

    while (pcm.boundary * 2 <= __INT_MAX__ - pcm.buffer_size)
		pcm.boundary *= 2;

    if (ioctl(pcm.fd, SNDRV_PCM_IOCTL_SW_PARAMS, &sparams)) {
        error("cannot set sw params");
        goto fail;
    }

    rc = pcm_hw_mmap_status(pcm);
    if (rc < 0) {
        error("mmap status failed");
        goto fail;
    }

    pcm.underruns = 0;
    return pcm;

fail:
    if (flags & PCM_MMAP)
        munmap(pcm.mmap_buffer, pcm_frames_to_bytes(pcm, pcm.buffer_size));
fail_close:
    close(pcm.fd);
    pcm.fd = -1;
    return pcm;
}

int pcm_is_ready(pcm& pcm)
{
    return pcm.fd >= 0;
}

int pcm_prepare(pcm& pcm) {
    if (ioctl(pcm.fd, SNDRV_PCM_IOCTL_PREPARE, 0) < 0)
        error("cannot prepare channel");
    return 0;
}

int pcm_start(pcm& pcm)
{
    if (ioctl(pcm.fd, SNDRV_PCM_IOCTL_PREPARE, 0) < 0)
        error("cannot prepare channel");

    if (pcm.flags & PCM_MMAP)
	    pcm_sync_ptr(pcm, 0);

    if (ioctl(pcm.fd, SNDRV_PCM_IOCTL_START, 0) < 0)
        error("cannot start channel");

    pcm.running = 1;
    return 0;
}

int pcm_stop(pcm& pcm)
{
    if (ioctl(pcm.fd, SNDRV_PCM_IOCTL_DROP,0) < 0)
        error("cannot stop channel");

    pcm.running = 0;
    return 0;
}

int pcm_drain(pcm& pcm)
{
    if (ioctl(pcm.fd, SNDRV_PCM_IOCTL_DRAIN,0) < 0)
        error("cannot stop channel");

    pcm.running = 0;
    return 0;
}


static inline int pcm_mmap_playback_avail(pcm& pcm)
{
    int avail;

    avail = pcm.mmap_status->hw_ptr + pcm.buffer_size - pcm.mmap_control->appl_ptr;

    if (avail < 0)
        avail += pcm.boundary;
    else if (avail > (int)pcm.boundary)
        avail -= pcm.boundary;

    return avail;
}

static inline int pcm_mmap_capture_avail(pcm& pcm)
{
    int avail = pcm.mmap_status->hw_ptr - pcm.mmap_control->appl_ptr;
    if (avail < 0)
        avail += pcm.boundary;
    return avail;
}

int pcm_mmap_avail(pcm& pcm)
{
    pcm_sync_ptr(pcm, SNDRV_PCM_SYNC_PTR_HWSYNC);
    if (pcm.flags & PCM_IN)
        return pcm_mmap_capture_avail(pcm);
    else
        return pcm_mmap_playback_avail(pcm);
}

static void pcm_mmap_appl_forward(pcm& pcm, int frames)
{
    unsigned int appl_ptr = pcm.mmap_control->appl_ptr;
    appl_ptr += frames;

    /* check for boundary wrap */
    if (appl_ptr > pcm.boundary)
         appl_ptr -= pcm.boundary;
    pcm.mmap_control->appl_ptr = appl_ptr;
}

int pcm_mmap_begin(pcm& pcm, void **areas, unsigned int *offset,
                   unsigned int *frames)
{
    unsigned int continuous, copy_frames, avail;

    /* return the mmap buffer */
    *areas = pcm.mmap_buffer;

    /* and the application offset in frames */
    *offset = pcm.mmap_control->appl_ptr % pcm.buffer_size;

    avail = pcm_mmap_avail(pcm);
    if (avail > pcm.buffer_size)
        avail = pcm.buffer_size;
    continuous = pcm.buffer_size - *offset;

    /* we can only copy frames if the are availabale and continuos */
    copy_frames = *frames;
    if (copy_frames > avail)
        copy_frames = avail;
    if (copy_frames > continuous)
        copy_frames = continuous;
    *frames = copy_frames;

    return 0;
}

int pcm_mmap_commit(pcm& pcm, unsigned int unused offset, unsigned int frames)
{
    /* update the application pointer in userspace and kernel */
    pcm_mmap_appl_forward(pcm, frames);
    pcm_sync_ptr(pcm, 0);

    return frames;
}

int pcm_avail_update(pcm& pcm)
{
    pcm_sync_ptr(pcm, 0);
    return pcm_mmap_avail(pcm);
}

int pcm_state(pcm& pcm)
{
    int err = pcm_sync_ptr(pcm, 0);
    if (err < 0)
        return err;

    return pcm.mmap_status->state;
}

#if 0
int pcm_wait(pcm& pcm, int timeout)
{
    pollfd pfd;
    unsigned short unused revents = 0;

    pfd.fd = pcm.fd;
    pfd.events = POLLOUT | POLLERR | POLLNVAL;

    do {
        /* let's wait for avail or timeout */
        int err= poll(&pfd, 1, timeout);

        /* timeout ? */
        if (err == 0)
            return 0;

        /* check for any errors */
        if (pfd.revents & (POLLERR | POLLNVAL)) {
            /*switch (pcm_state(pcm)) {
            case PCM_STATE_XRUN:
                return -EPIPE;
            case PCM_STATE_SUSPENDED:
                return -ESTRPIPE;
            case PCM_STATE_DISCONNECTED:
                return -ENODEV;
            default:
                return -EIO;
            }*/
            error("");
        }
    /* poll again if fd not ready for IO */
    } while (!(pfd.revents & (POLLIN | POLLOUT)));

    return 1;
}

int pcm_mmap_write(pcm& pcm, const void *buffer, unsigned int bytes)
{
    int err = 0, frames, avail;
    unsigned int offset = 0, count;

    if (bytes == 0)
        return 0;

    count = pcm_bytes_to_frames(pcm, bytes);

    while (count > 0) {

        /* get the available space for writing new frames */
        avail = pcm_avail_update(pcm);
        if (avail < 0) {
            error("cannot determine available mmap frames");
            return err;
        }

        /* start the audio if we reach the threshold */
	    if (!pcm.running &&
            (pcm.buffer_size - avail) >= pcm.config.start_threshold) {
            if (pcm_start(pcm) < 0) {
               error("start error: hw 0x%x app 0x%x avail 0x%x\n",
                    (unsigned int)pcm.mmap_status->hw_ptr,
                    (unsigned int)pcm.mmap_control->appl_ptr,
                    avail);
            }
        }

        /* sleep until we have space to write new frames */
        if (pcm.running &&
            (unsigned int)avail < pcm.mmap_control->avail_min) {
            int time = -1;

            if (pcm.flags & PCM_NOIRQ)
                time = (pcm.buffer_size - avail - pcm.mmap_control->avail_min)
                        / pcm.noirq_frames_per_msec;

            err = pcm_wait(pcm, time);
            if (err < 0) {
                pcm.running = 0;
                error("wait error: hw 0x%x app 0x%x avail 0x%x\n",
                    (unsigned int)pcm.mmap_status->hw_ptr,
                    (unsigned int)pcm.mmap_control->appl_ptr,
                    avail);
            }
            continue;
        }

        frames = count;
        if (frames > avail)
            frames = avail;

        if (!frames)
            break;

        /* copy frames from buffer */
        frames = pcm_mmap_write_areas(pcm, (const char*)buffer, offset, frames);
        if (frames < 0) {
            error( "write error: hw 0x%x app 0x%x avail 0x%x\n",
                    (unsigned int)pcm.mmap_status->hw_ptr,
                    (unsigned int)pcm.mmap_control->appl_ptr,
                    avail);
            return frames;
        }

        offset += frames;
        count -= frames;
    }

_end:
    return 0;
}
#endif
