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
//
// rename(): sceIoRename only renames within a folder. It drops any folder in
// the new name, so rename("a/x", "b/y") would quietly give "a/y". Fail with
// EXDEV instead when the two folders differ, as rename() does across devices
// elsewhere.
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#define PATHS_MAX (1024)

char *__real_getcwd(char *buf, size_t size);
int __real___path_absolute(const char *in, char *out, int len);
int __real_rename(const char *old_path, const char *new_path);

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

// Length of an absolute path's folder part, up to and including its last '/'.
static size_t folder_len(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash == NULL ? 0 : (size_t)(slash - path + 1);
}

int __wrap_rename(const char *old_path, const char *new_path) {
    static char old_abs[PATHS_MAX];
    static char new_abs[PATHS_MAX];
    if (__wrap___path_absolute(old_path, old_abs, PATHS_MAX) == 0
        && __wrap___path_absolute(new_path, new_abs, PATHS_MAX) == 0) {
        size_t len = folder_len(old_abs);
        // FAT names are case-insensitive, so "A/x" and "a/y" share a folder.
        if (len != folder_len(new_abs) || strncasecmp(old_abs, new_abs, len) != 0) {
            errno = EXDEV;
            return -1;
        }
    }
    return __real_rename(old_path, new_path);
}
