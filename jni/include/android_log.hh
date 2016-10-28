#ifdef __ANDROID__       //android的编译器会自动识别到这个为真。

#ifndef __MYPRINTF__
#define __MYPRINTF__
#include <android/log.h>


#ifndef LOG_NDEBUG
	#ifdef NDEBUG
		#define LOG_NDEBUG 1	// no debug
	#else
		#define LOG_NDEBUG 0	// debug
	#endif
#endif

#ifndef LOG_TAG
#warning "you should define LOG_TAG in your source file. use default now!"
#define LOG_TAG "live"
#endif

#ifndef ALOG
#define ALOG(priority, tag, fmt...) \
    __android_log_print(ANDROID_##priority, tag, fmt)
#endif

#ifndef ALOGV
#if LOG_NDEBUG
#define ALOGV(...)   ((void)0)
#else
#define ALOGV(...) ((void)ALOG(LOG_VERBOSE, LOG_TAG, __VA_ARGS__))
#endif
#endif

#ifndef ALOGD
#define ALOGD(...) ((void)ALOG(LOG_DEBUG, LOG_TAG, __VA_ARGS__))
#endif

#ifndef ALOGI
#define ALOGI(...) ((void)ALOG(LOG_INFO, LOG_TAG, __VA_ARGS__))
#endif

#ifndef ALOGW
#define ALOGW(...) ((void)ALOG(LOG_WARN, LOG_TAG, __VA_ARGS__))
#endif

#ifndef ALOGE
#define ALOGE(...) ((void)ALOG(LOG_ERROR, LOG_TAG, __VA_ARGS__))
#endif


static inline void ALOG_ENV(const char *format, ...){
    va_list ap;
    va_start(ap, format);
    __android_log_vprint(ANDROID_LOG_INFO, "live555", format, ap);
    va_end(ap);
}

#endif 

#endif /*__ANDROID__*/