#ifndef SRC_UTIL_FILE_H_
#define SRC_UTIL_FILE_H_

#include <cstdio>
#include <cstdlib>
#include <string>
#include "util.h"
#include <functional>

#if defined(__linux__)
#include <limits.h>
#endif

#if defined(_WIN32)
#ifndef PATH_MAX
#define PATH_MAX 1024
#endif // !PATH_MAX

struct dirent{
    long d_ino;              /* inode number*/
    off_t d_off;             /* offset to this dirent*/
    unsigned short d_reclen; /* length of this d_name*/
    unsigned char d_type;    /* the type of d_name*/
    char d_name[1];          /* file name (null-terminated)*/
};
typedef struct _dirdesc {
    int     dd_fd;      /** file descriptor associated with directory */
    long    dd_loc;     /** offset in current buffer */
    long    dd_size;    /** amount of data returned by getdirentries */
    char    *dd_buf;    /** data buffer */
    int     dd_len;     /** size of data buffer */
    long    dd_seek;    /** magic cookie returned by getdirentries */
    HANDLE handle;
    struct dirent *index;
} DIR;
# define __dirfd(dp)    ((dp)->dd_fd)

int mkdir(const char *path, int mode);
DIR *opendir(const char *);
int closedir(DIR *);
struct dirent *readdir(DIR *);

#endif // defined(_WIN32)

#if defined(_WIN32) || defined(_WIN64)
#define fseek64 _fseeki64
#define ftell64 _ftelli64
#else
#define fseek64 fseek
#define ftell64 ftell
#endif

namespace toolkit {

class File {
public:
    //Create path
    static bool create_path(const std::string &file, unsigned int mod);

    //Create a new file, and the directory folder will be generated automatically
    static FILE *create_file(const std::string &file, const std::string &mode);

    //Determine if it is a directory
    static bool is_dir(const std::string &path);

    //Determine if it is a special directory (. or ..)
    static bool is_special_dir(const std::string &path);

    //Delete a directory or file
    static int delete_file(const std::string &path, bool del_empty_dir = false, bool backtrace = true);

    //Determine if a file exists
    static bool fileExist(const std::string &path);

    /**
     * Load file content to string
     * @param path The path of the file to load
     * @return The file content
     */
    static std::string loadFile(const std::string &path);

    /**
     * Save content to file
     * @param data The file content
     * @param path The path to save the file
     * @return Whether the save was successful
     */
    static bool saveFile(const std::string &data, const std::string &path);

    /**
     * Get the parent folder
     * @param path The path
     * @return The folder
     */
    static std::string parentDir(const std::string &path);

    /**
     * Replace "../" and get the absolute path
     * @param path The relative path, which may contain "../"
     * @param current_path The current directory
     * @param can_access_parent Whether it can access directories outside the parent directory
     * @return The path after replacing "../"
     */
    static std::string absolutePath(const std::string &path, const std::string &current_path, bool can_access_parent = false);

    /**
     * Traverse all files under the folder
     * @param path Folder path
     * @param cb Callback object, path is the absolute path, isDir indicates whether the path is a folder, returns true to continue scanning, otherwise stops
     * @param enter_subdirectory Whether to enter subdirectory scanning
     * @param show_hidden_file Whether to display hidden files
     */
    static void scanDir(const std::string &path, const std::function<bool(const std::string &path, bool isDir)> &cb,
                        bool enter_subdirectory = false, bool show_hidden_file = false);

    /**
     * Get file size
     * @param fp File handle
     * @param remain_size true: Get the remaining unread data size of the file, false: Get the total file size
     */
    static uint64_t fileSize(FILE *fp, bool remain_size = false);

    /**
     * Get file size
     * @param path File path
     * @return File size
     * @warning The caller should ensure the file exists
     */
    static uint64_t fileSize(const std::string &path);

    /**
     * Attempt to delete an empty folder
     * @param dir Folder path
     * @param backtrace Whether to backtrack to the upper-level folder, if the upper-level folder is empty, it will also be deleted, and so on
     */
    static void deleteEmptyDir(const std::string &dir, bool backtrace = true);

private:
    File();
    ~File();
};

} /* namespace toolkit */
#endif /* SRC_UTIL_FILE_H_ */
