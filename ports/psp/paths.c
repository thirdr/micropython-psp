// Path fixes for the PSP port, wrapping two libcglue functions (linked with
// --wrap, so neither the SDK nor MicroPython's sources are changed).
//
// getcwd(): at the top of a drive the PSP reports the working directory as
// "umd0:" or "ms0:", but chdir() needs "umd0:/". Add the slash, so the result
// of os.getcwd() can always be passed back to os.chdir().
//
// __path_absolute(): every libcglue file call (open, stat, opendir, chdir,
// mkdir, rmdir, remove, rename, ...) turns its path into an absolute one with
// this. The PSP rejects repeated slashes ("umd0://x"), which a join such as
// os.getcwd() + "/" + name produces at the top of a drive, so collapse them,
// as POSIX systems treat them.
#include <string.h>
#include <unistd.h>

char *__real_getcwd(char *buf, size_t size);
int __real___path_absolute(const char *in, char *out, int len);

char *__wrap_getcwd(char *buf, size_t size) {
    char *ret = __real_getcwd(buf, size);
    if (ret != NULL) {
        size_t len = strlen(ret);
        if (len > 0 && ret[len - 1] == ':' && len + 1 < size) {
            ret[len] = '/';
            ret[len + 1] = '\0';
        }
    }
    return ret;
}

int __wrap___path_absolute(const char *in, char *out, int len) {
    int ret = __real___path_absolute(in, out, len);
    if (ret == 0) {
        char *w = out;
        for (const char *r = out; *r != '\0'; ++r) {
            if (!(*r == '/' && w > out && w[-1] == '/')) {
                *w++ = *r;
            }
        }
        *w = '\0';
    }
    return ret;
}
