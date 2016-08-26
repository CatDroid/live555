#ifdef __ANDROID__       //android的编译器会自动识别到这个为真。
#include <android/log.h>


static int my_fprintf(FILE *stream, const char *format, ...){
    va_list ap;
    va_start(ap, format);
    __android_log_vprint(ANDROID_LOG_DEBUG, "LIVE", format, ap);
    va_end(ap);
    return 0;
}


#ifdef fprintf
#undef fprintf
#endif
#define fprintf(fp,...) my_fprintf(fp, __VA_ARGS__)


#endif /*__ANDROID__*/