#include "FileUtils.h"
#include "FileStreambuf.h"
#include "UnicodeUtils.h"
#include "Path.h"

#include "Debug.h"

#include <rapidjson/prettywriter.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <limits.h>
#include <stdlib.h>
#include <libgen.h>

#include <fstream>
#include <cstring>
#include <cstdio>
#include <memory>
#include <locale>

static Path getNativeCurrentDir();

std::unordered_map<Path, std::shared_ptr<ZipReader>> FileUtils::_archives;
std::unordered_map<const std::ios *, FileUtils::StreamMetadata> FileUtils::_metaData;
Path FileUtils::_currentDir = getNativeCurrentDir();

typedef std::string::size_type SizeType;

static char tmpBuffer[PATH_MAX*2];
typedef struct stat64 NativeStatStruct;

static bool execNativeStat(const Path &p, NativeStatStruct &dst)
{
    return stat64(p.absolute().asString().c_str(), &dst) == 0;
}

class OpenFileSystemDir : public OpenDir
{
    DIR *_dir;

    void close()
    {
        if (_dir)
            closedir(_dir);
        _dir = nullptr;
    }

public:
    OpenFileSystemDir(const Path &p)
    : _dir(opendir(p.absolute().asString().c_str()))
    {
    }

    ~OpenFileSystemDir()
    {
        close();
    }

    virtual bool increment(Path &dst, Path &parent, std::function<bool(const Path &)> acceptor) override final
    {
        if (_dir) {
            while (true) {
                dirent *entry = readdir(_dir);
                if (!entry) {
                    close();
                    return false;
                }

                std::string fileName(entry->d_name);
                if (fileName == "." || fileName == "..")
                    continue;

                Path path = parent/fileName;
                if (!acceptor(path))
                    continue;
                dst = path;

                return true;
            }
        }

        return false;
    }

    virtual bool open() const override final
    {
        return _dir != nullptr;
    }
};

class JsonOstreamWriter {
    OutputStreamHandle _out;
public:
    typedef char Ch;    //!< Character type. Only support char.

    JsonOstreamWriter(OutputStreamHandle out)
    : _out(std::move(out))
    {
    }
    void Put(char c) {
        _out->put(c);
    }
    void PutN(char c, size_t n) {
        for (size_t i = 0; i < n; ++i)
            _out->put(c);
    }
    void Flush() {
        _out->flush();
    }
};

static Path getNativeCurrentDir()
{
    if (getcwd(tmpBuffer, sizeof(tmpBuffer)))
        return Path(tmpBuffer);
    // Native API failed us. Not ideal.
    return Path();
}

void FileUtils::finalizeStream(std::ios *stream)
{
    auto iter = _metaData.find(stream);

    delete stream;

    if (iter != _metaData.end()) {
        iter->second.streambuf.reset();

        if (!iter->second.targetPath.empty())
            moveFile(iter->second.srcPath, iter->second.targetPath, true);
        _metaData.erase(iter);
    }
}

OutputStreamHandle FileUtils::openFileOutputStream(const Path &p)
{
    std::shared_ptr<std::ostream> out(new std::ofstream(p.absolute().asString(),
            std::ios_base::out | std::ios_base::binary),
            [](std::ostream *stream){ finalizeStream(stream); });

    if (!out->good())
        return nullptr;

    _metaData.insert(std::make_pair(out.get(), StreamMetadata()));

    return std::move(out);
}

bool FileUtils::execStat(const Path &p, StatStruct &dst)
{
    NativeStatStruct stat;
    if (execNativeStat(p, stat)) {
        dst.size        = stat.st_size;
        dst.isDirectory = S_ISDIR(stat.st_mode);
        dst.isFile      = S_ISREG(stat.st_mode);
        return true;
    }

    return false;
}

bool FileUtils::changeCurrentDir(const Path &dir)
{
    _currentDir = dir.absolute();
    return true;
}

Path FileUtils::getCurrentDir()
{
    return _currentDir;
}

Path FileUtils::getExecutablePath()
{
    ssize_t size = readlink("/proc/self/exe", tmpBuffer, sizeof(tmpBuffer));
    if (size != -1)
        return Path(std::string(tmpBuffer, size));
    // readlink does not tell us the actual content size if our buffer is too small,
    // so we won't attempt to allocate a larger buffer here and fail instead
    return Path();
}

uint64 FileUtils::fileSize(const Path &path)
{
    StatStruct info;
    if (!execStat(path, info))
        return 0;
    return info.size;
}


bool FileUtils::createDirectory(const Path &path, bool recursive)
{
    if (path.exists()) {
        return true;
    } else {
        Path parent = path.parent();
        if (!parent.empty() && !parent.exists() && (!recursive || !createDirectory(parent)))
            return false;
        return mkdir(path.absolute().asString().c_str(), 0777) == 0;
    }
}

std::string FileUtils::loadText(const Path &path)
{
    uint64 size = fileSize(path);
    InputStreamHandle in = openInputStream(path);
    if (size == 0 || !in || !isFile(path))
        return std::string();

    // Strip UTF-8 byte order mark if present (mostly a problem on
    // windows platforms).
    // We could also detect other byte order marks here (UTF-16/32),
    // but generally we can't deal with these encodings anyway. Best
    // to just leave it for now.
    int offset = 0;
    uint8 head[] = {0, 0, 0};
    if (in->good()) head[0] = in->get();
    if (in->good()) head[1] = in->get();
    if (in->good()) head[2] = in->get();
    if (head[0] == 0xEF && head[1] == 0xBB && head[2] == 0xBF)
        size -= 3;
    else
        offset = 3;

    std::string text(size_t(size), '\0');
    for (int i = 0; i < offset; ++i)
        text[i] = head[sizeof(head) - offset + i];
    in->read(&text[offset], size);

    return std::move(text);
}

bool FileUtils::writeJson(const rapidjson::Document &document, const Path &p)
{
    OutputStreamHandle stream = openOutputStream(p);
    if (!stream)
        return false;

    JsonOstreamWriter out(stream);
    rapidjson::PrettyWriter<JsonOstreamWriter> writer(out);
    document.Accept(writer);

    return true;
}

bool FileUtils::copyFile(const Path &src, const Path &dst, bool createDstDir)
{
    if (createDstDir) {
        Path parent(dst.parent());
        if (!parent.empty() && !createDirectory(parent))
            return false;
    }
    InputStreamHandle srcStream = openInputStream(src);
    if (!srcStream) {
        OutputStreamHandle dstStream = openOutputStream(dst);
        if (dstStream) {
            *dstStream << srcStream->rdbuf();
            return true;
        }
    }
    return false;
}

bool FileUtils::moveFile(const Path &src, const Path &dst, bool deleteDst)
{
    if (dst.exists()) {
        if (!deleteDst) {
            return false;
        } else {
        return rename(src.absolute().asString().c_str(), dst.absolute().asString().c_str()) == 0;
        }
    } else {
        return rename(src.absolute().asString().c_str(), dst.absolute().asString().c_str()) == 0;
    }
}

bool FileUtils::deleteFile(const Path &path)
{
    return std::remove(path.absolute().asString().c_str()) == 0;
}

InputStreamHandle FileUtils::openInputStream(const Path &p)
{
    NativeStatStruct info;
    if (execNativeStat(p, info)) {
        std::shared_ptr<std::istream> in(new std::ifstream(p.absolute().asString(),
                std::ios_base::in | std::ios_base::binary),
                [](std::istream *stream){ finalizeStream(stream); });

        if (!in->good())
            return nullptr;
        return std::move(in);
    }

    return nullptr;
}

OutputStreamHandle FileUtils::openOutputStream(const Path &p)
{
    if (!p.exists())
        return openFileOutputStream(p);

    Path tmpPath(p + ".tmp");
    int index = 0;
    while (tmpPath.exists())
        tmpPath = p + format(".tmp%03d", ++index);

    OutputStreamHandle out = openFileOutputStream(tmpPath);
    if (out) {
        auto iter = _metaData.find(out.get());
        iter->second.srcPath = tmpPath;
        iter->second.targetPath = p;
    }

    return std::move(out);
}

std::shared_ptr<OpenDir> FileUtils::openDirectory(const Path &p)
{
    NativeStatStruct info;
    if (execNativeStat(p, info)) {
        if (S_ISDIR(info.st_mode))
            return std::make_shared<OpenFileSystemDir>(p);
        else
            return nullptr;
    }

    return nullptr;
}

bool FileUtils::exists(const Path &p)
{
    StatStruct info;
    return execStat(p, info);
}

bool FileUtils::isDirectory(const Path &p)
{
    StatStruct info;
    if (!execStat(p, info))
        return false;
    return info.isDirectory;
}

bool FileUtils::isFile(const Path &p)
{
    StatStruct info;
    if (!execStat(p, info))
        return false;
    return info.isFile;
}
