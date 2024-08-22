#ifndef MEMFILE_H
#define MEMFILE_H

#include <sstream>
#include <cstdio>
#include <vector>

#include "defines.h"

class MemFile
{
    public:
        MemFile() : stream(), file(nullptr) {}
        ~MemFile() { close(); }

        MemFile(FILE* cfile) : stream(), file(cfile)
        {
            if (!file) return;

            fseek(file, 0, SEEK_END);
            long fileSize = ftell(file);
            fseek(file, 0, SEEK_SET);

            if (fileSize > 0)
            {
              std::vector<char> content(fileSize);
              fread(content.data(), 1, fileSize, file);

              stream.write(content.data(), fileSize);
              stream.seekg(0, std::ios::beg);
              stream.seekp(0, std::ios::beg);
            }
        }

        bool opened() const
        {
            return file != nullptr;
        }

        size_t write(const void* buffer, size_t size, size_t count)
        {
            stream.write(static_cast<const char*>(buffer), size * count);
            return size;
        }

        size_t read(void* buffer, size_t size, size_t count)
        {
            stream.read(static_cast<char*>(buffer), size * count);
            return size;
        }

        int seek(long offset, int origin)
        {
            std::ios_base::seekdir dir;

            switch (origin)
            {
                case SEEK_SET: dir = std::ios::beg; break;
                case SEEK_CUR: dir = std::ios::cur; break;
                case SEEK_END: dir = std::ios::end; break;
                default: return -1;
            }

            stream.seekg(offset, dir);
            stream.seekp(offset, dir);

            return stream.fail() ? -1 : 0;
        }

        long tell()
        {
            return static_cast<long>(stream.tellg());
        }

        void close()
        {
            if (!file) return;

            std::string content = stream.str();
            fwrite(content.data(), 1, content.size(), file);
            fclose(file);

            file = nullptr;
        }

    private:
        std::stringstream stream;
        FILE* file;
};

FORCE_INLINE size_t fread(void* buffer, size_t size, size_t count, MemFile &file)
{
    return file.read(buffer, size, count);
}

FORCE_INLINE size_t fwrite(const void* buffer, size_t size, size_t count, MemFile &file)
{
    return file.write(buffer, size, count);
}

FORCE_INLINE int fseek(MemFile &file, long offset, int origin)
{
    return file.seek(offset, origin);
}

FORCE_INLINE long ftell(MemFile &file)
{
    return file.tell();
}

FORCE_INLINE void fclose(MemFile &file)
{
    return file.close();
}

#endif // MEMFILE_H
