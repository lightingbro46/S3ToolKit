#if defined(_WIN32)
#include <io.h>   
#include <direct.h>
#else
#include <dirent.h>
#include <limits.h>
#endif // WIN32

#include <sys/stat.h>
#include "File.h"
#include "util.h"
#include "logger.h"
#include "uv_errno.h"

using namespace std;
using namespace toolkit;

#if !defined(_WIN32)
#define    _unlink    unlink
#define    _rmdir    rmdir
#define    _access    access
#endif

#if defined(_WIN32)

int mkdir(const char *path, int mode) {
    return _mkdir(path);
}

DIR *opendir(const char *name) {
    char namebuf[512];
    snprintf(namebuf, sizeof(namebuf), "%s\\*.*", name);

    WIN32_FIND_DATAA FindData;
    auto hFind = FindFirstFileA(namebuf, &FindData);
    if (hFind == INVALID_HANDLE_VALUE) {
        return nullptr;
    }
    DIR *dir = (DIR *)malloc(sizeof(DIR));
    memset(dir, 0, sizeof(DIR));
    dir->dd_fd = 0;   // simulate return  
    dir->handle = hFind;
    return dir;
}

struct dirent *readdir(DIR *d) {
    HANDLE hFind = d->handle;
    WIN32_FIND_DATAA FileData;
    //fail or end  
    if (!FindNextFileA(hFind, &FileData)) {
        return nullptr;
    }
    struct dirent *dir = (struct dirent *)malloc(sizeof(struct dirent) + sizeof(FileData.cFileName));
    strcpy(dir->d_name, (char *)FileData.cFileName);
    dir->d_reclen = (uint16_t)strlen(dir->d_name);
    //check there is file or directory  
    if (FileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        dir->d_type = 2;
    }
    else {
        dir->d_type = 1;
    }
    if (d->index) {
        //Release memory before covering
        free(d->index);
        d->index = nullptr;
    }
    d->index = dir;
    return dir;
}

int closedir(DIR *d) {
    if (!d) {
        return -1;
    }
    //Close handle
    if (d->handle != INVALID_HANDLE_VALUE) {
        FindClose(d->handle);
        d->handle = INVALID_HANDLE_VALUE;
    }
    //Release memory
    if (d->index) {
        free(d->index);
        d->index = nullptr;
    }
    free(d);
    return 0;
}
#endif // defined(_WIN32)

namespace toolkit {

FILE *File::create_file(const std::string &file, const std::string &mode) {
    std::string path = file;
    std::string dir;
    size_t index = 1;
    FILE *ret = nullptr;
    while (true) {
        index = path.find('/', index) + 1;
        dir = path.substr(0, index);
        if (dir.length() == 0) {
            break;
        }
        if (_access(dir.data(), 0) == -1) { //The access function is to check whether it exists
            if (mkdir(dir.data(), 0777) == -1) { //If it does not exist, use the mkdir function to create it
                TraceL << "mkdir " << dir << " failed: " << get_uv_errmsg();
            }
        }
    }
    if (path[path.size() - 1] != '/') {
        ret = fopen(file.data(), mode.data());
    }
    return ret;
}

bool File::create_path(const std::string &file, unsigned int mod) {
    std::string path = file;
    std::string dir;
    size_t index = 1;
    while (true) {
        index = path.find('/', index) + 1;
        dir = path.substr(0, index);
        if (dir.length() == 0) {
            break;
        }
        if (_access(dir.data(), 0) == -1) { //The access function is to check whether it exists
            if (mkdir(dir.data(), mod) == -1) {  //If it does not exist, use the mkdir function to create
                WarnL << "mkdir " << dir << " failed: " << get_uv_errmsg();
                return false;
            }
        }
    }
    return true;
}

//Determine if it is a directory
bool File::is_dir(const std::string &path) {
    auto dir = opendir(path.data());
    if (!dir) {
        return false;
    }
    closedir(dir);
    return true;
}

//Determine if it is a regular file
bool File::fileExist(const std::string &path) {
    auto fp = fopen(path.data(), "rb");
    if (!fp) {
        return false;
    }
    fclose(fp);
    return true;
}

//Determine if it is a special directory
bool File::is_special_dir(const std::string &path) {
    return path == "." || path == "..";
}

static int delete_file_l(const std::string &path_in) {
    DIR *dir;
    dirent *dir_info;
    auto path = path_in;
    if (path.back() == '/') {
        path.pop_back();
    }
    if (File::is_dir(path)) {
        if ((dir = opendir(path.data())) == nullptr) {
            return _rmdir(path.data());
        }
        while ((dir_info = readdir(dir)) != nullptr) {
            if (File::is_special_dir(dir_info->d_name)) {
                continue;
            }
            File::delete_file(path + "/" + dir_info->d_name);
        }
        auto ret = _rmdir(path.data());
        closedir(dir);
        return ret;
    }
    return remove(path.data()) ? _unlink(path.data()) : 0;
}

int File::delete_file(const std::string &path, bool del_empty_dir, bool backtrace) {
    auto ret = delete_file_l(path);
    if (!ret && del_empty_dir) {
        // delete success
        File::deleteEmptyDir(parentDir(path), backtrace);
    }
    return ret;
}

string File::loadFile(const std::string &path) {
    FILE *fp = fopen(path.data(), "rb");
    if (!fp) {
        return "";
    }
    fseek(fp, 0, SEEK_END);
    auto len = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    string str(len, '\0');
    if (len != (decltype(len))fread((char *)str.data(), 1, str.size(), fp)) {
        WarnL << "fread " << path << " failed: " << get_uv_errmsg();
    }
    fclose(fp);
    return str;
}

bool File::saveFile(const string &data, const std::string &path) {
    FILE *fp = fopen(path.data(), "wb");
    if (!fp) {
        return false;
    }
    fwrite(data.data(), data.size(), 1, fp);
    fclose(fp);
    return true;
}

string File::parentDir(const std::string &path) {
    auto parent_dir = path;
    if (parent_dir.back() == '/') {
        parent_dir.pop_back();
    }
    auto pos = parent_dir.rfind('/');
    if (pos != string::npos) {
        parent_dir = parent_dir.substr(0, pos + 1);
    }
    return parent_dir;
}

string File::absolutePath(const std::string &path, const std::string &current_path, bool can_access_parent) {
    string currentPath = current_path;
    if (!currentPath.empty()) {
        //Current directory is not empty
        if (currentPath.front() == '.') {
            //If the current directory is a relative path, convert it to an absolute path first
            currentPath = absolutePath(current_path, exeDir(), true);
        }
    } else {
        currentPath = exeDir();
    }

    if (path.empty()) {
        //Relative path is empty, return the current directory
        return currentPath;
    }

    if (currentPath.back() != '/') {
        //Ensure the last byte of the current directory is '/
        currentPath.push_back('/');
    }
    auto rootPath = currentPath;
    auto dir_vec = split(path, "/");
    for (auto &dir : dir_vec) {
        if (dir.empty() || dir == ".") {
            //Ignore empty or current folder
            continue;
        }
        if (dir == "..") {
            //Access parent directory
            if (!can_access_parent && currentPath.size() <= rootPath.size()) {
                //Cannot access directories outside the root, return to root
                return rootPath;
            }
            currentPath = parentDir(currentPath);
            continue;
        }
        currentPath.append(dir);
        currentPath.append("/");
    }

    if (path.back() != '/' && currentPath.back() == '/') {
        //Prevent conversion to directory when path is a file
        currentPath.pop_back();
    }
    return currentPath;
}

void File::scanDir(const std::string &path_in, const function<bool(const string &path, bool is_dir)> &cb,
                   bool enter_subdirectory, bool show_hidden_file) {
    string path = path_in;
    if (path.back() == '/') {
        path.pop_back();
    }

    DIR *pDir;
    dirent *pDirent;
    if ((pDir = opendir(path.data())) == nullptr) {
        //Invalid folder
        return;
    }
    while ((pDirent = readdir(pDir)) != nullptr) {
        if (is_special_dir(pDirent->d_name)) {
            continue;
        }
        if (!show_hidden_file && pDirent->d_name[0] == '.') {
            //Hidden file
            continue;
        }
        string strAbsolutePath = path + "/" + pDirent->d_name;
        bool isDir = is_dir(strAbsolutePath);
        if (!cb(strAbsolutePath, isDir)) {
            //Stop scanning
            break;
        }

        if (isDir && enter_subdirectory) {
            //If it's a folder and scanning subfolders, then recursively scan
            scanDir(strAbsolutePath, cb, enter_subdirectory);
        }
    }
    closedir(pDir);
}

uint64_t File::fileSize(FILE *fp, bool remain_size) {
    if (!fp) {
        return 0;
    }
    auto current = ftell64(fp);
    fseek64(fp, 0L, SEEK_END); /* Position to the end of the file */
    auto end = ftell64(fp); /* Get file size */
    fseek64(fp, current, SEEK_SET);
    return end - (remain_size ? current : 0);
}

uint64_t File::fileSize(const std::string &path) {
    if (path.empty()) {
        return 0;
    }
    auto fp = std::unique_ptr<FILE, decltype(&fclose)>(fopen(path.data(), "rb"), fclose);
    return fileSize(fp.get());
}

static bool isEmptyDir(const std::string &path) {
    bool empty = true;
    File::scanDir(path,[&](const std::string &path, bool isDir) {
        empty = false;
        return false;
    }, true, true);
    return empty;
}

void File::deleteEmptyDir(const std::string &dir, bool backtrace) {
    if (!File::is_dir(dir) || !isEmptyDir(dir)) {
        //Not a folder or not empty
        return;
    }
    File::delete_file(dir);
    if (backtrace) {
        deleteEmptyDir(File::parentDir(dir), true);
    }
}

} /* namespace toolkit */
