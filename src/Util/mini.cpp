#include "mini.h"
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#if defined(_WIN32)
#include <io.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#include "logger.h"
#include "uv_errno.h"

using namespace std;

namespace toolkit {

// Writes, flushes to the disk and closes fp.
static bool writeAndClose(FILE *fp, const string &content) {
    bool ok = fwrite(content.data(), 1, content.size(), fp) == content.size() && fflush(fp) == 0;
#if defined(_WIN32)
    ok = ok && _commit(_fileno(fp)) == 0;
#else
    ok = ok && fsync(fileno(fp)) == 0;
#endif
    return fclose(fp) == 0 && ok;
}

bool saveIniFile(const string &fileName, const string &content) {
    static mutex s_mtx;
    lock_guard<mutex> lck(s_mtx);
    auto target = fileName;
#if !defined(_WIN32)
    struct stat st;
    // A symlink stays one: the file it points to is replaced
    char real_path[PATH_MAX];
    if (lstat(fileName.data(), &st) == 0 && S_ISLNK(st.st_mode) && realpath(fileName.data(), real_path)) {
        target = real_path;
    }
    const bool exists = stat(target.data(), &st) == 0;
#endif
    const auto tmp = target + ".tmp";
    bool ok = false;
    int err = 0;
    FILE *fp = fopen(tmp.data(), "wb");
    if (fp) {
#if !defined(_WIN32)
        if (exists) {
            // The new file keeps the mode and the owner of the one it replaces
            if (fchmod(fileno(fp), st.st_mode & 07777) != 0
                || (geteuid() == 0 && fchown(fileno(fp), st.st_uid, st.st_gid) != 0)) {
                WarnL << "Keep the mode/owner of " << target << " failed: " << get_uv_errmsg(false);
            }
        }
#endif
        ok = writeAndClose(fp, content);
#if defined(_WIN32)
        ok = ok && MoveFileExA(tmp.data(), target.data(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
#else
        ok = ok && rename(tmp.data(), target.data()) == 0;
#endif
        err = errno;
        if (!ok) {
            remove(tmp.data());
        }
    } else {
        err = errno;
    }
#if !defined(_WIN32)
    if (ok) {
        // The rename itself survives a power loss only once the directory is synced
        const auto pos = target.rfind('/');
        const auto dir = pos == string::npos ? string(".") : (pos == 0 ? string("/") : target.substr(0, pos));
        const int fd = open(dir.data(), O_RDONLY);
        if (fd >= 0) {
            fsync(fd);
            close(fd);
        }
    } else if (err == EACCES || err == EPERM || err == EBUSY || err == EXDEV) {
        // The directory is not writable or the file is a mount point (a bind mounted file): overwrite
        // it in place as before, not atomic
        WarnL << "Replace " << target << " failed (" << uv_strerror(uv_translate_posix_error(err))
              << "), overwriting it in place";
        fp = fopen(target.data(), "wb");
        ok = fp && writeAndClose(fp, content);
        err = errno;
    }
#endif
    if (!ok) {
        errno = err;
        WarnL << "Save " << fileName << " failed: " << get_uv_errmsg(false);
    }
    return ok;
}

template<>
mINI_basic<variant> &mINI_basic<variant>::Instance(){
    static mINI_basic<variant> instance;
    return instance;
}

template <>
bool variant::as<bool>() const {
    if (empty() || isdigit(front())) {
        //Starts with a number
        return as_default<bool>();
    }
    if (strToLower(std::string(*this)) == "true") {
        return true;
    }
    if (strToLower(std::string(*this)) == "false") {
        return false;
    }
    //Unrecognized string
    return as_default<bool>();
}

template<>
uint8_t variant::as<uint8_t>() const {
    return 0xFF & as_default<int>();
}

}  // namespace toolkit


