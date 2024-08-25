#ifndef MEMFILE_H
#define MEMFILE_H

#include <sstream>
#include <cstdio>
#include <cstring>

#include "defines.h"

class MemFile
{
    public:
        MemFile(size_t size = 1024 * 1024 * 8) :
          file(nullptr), data(nullptr), dlen(0), rpos(0), wpos(0), eptr(false)
        {
            dlen = size;
            data = new char[dlen];
        }

        MemFile(void* buffer, size_t size) :
          file(nullptr), data(nullptr), dlen(0), rpos(0), wpos(0), eptr(true)
        {
            dlen = size;
            data = reinterpret_cast<char*>(buffer);
        }

        MemFile(const void* buffer, size_t size) :
          file(nullptr), data(nullptr), dlen(0), rpos(0), wpos(0), eptr(false)
        {
            dlen = size;
            data = new char[dlen];

            memcpy(data, buffer, size);
        }

        MemFile(FILE* cfile) :
          file(cfile), data(nullptr), dlen(0), rpos(0), wpos(0), eptr(false)
        {
            if (!file) return;

            fseek(file, 0, SEEK_END);
            long fileSize = ftell(file);
            fseek(file, 0, SEEK_SET);

            if (fileSize > 0)
            {
                dlen = fileSize;
                data = new char[dlen];

                fread(data, 1, dlen, file);
            }
            else
            {
                dlen = 1024 * 1024 * 6;
                data = new char[dlen];
            }
        }

        ~MemFile()
        {
            close();
        }

        bool opened() const
        {
            return file != nullptr;
        }

        size_t write(const void* buffer, size_t size, size_t count)
        {
            size_t wsize = size * count;

            if (wpos + wsize > dlen)
            {
                dlen = wpos + wsize;
                data = reinterpret_cast<char*>(realloc(data, dlen));
            }

            memcpy(data + wpos, buffer, wsize);
            wpos += wsize;

            return count;
        }

        size_t read(void* buffer, size_t size, size_t count)
        {
            size_t rsize = size * count;
            if (rpos + rsize > dlen) rsize = dlen - rpos;

            memcpy(buffer, data + rpos, rsize);
            rpos += rsize;

            return count;
        }

        int seek(long offset, int origin)
        {
            switch (origin)
            {
                case SEEK_SET: rpos = wpos = offset; break;
                case SEEK_CUR: rpos = wpos = rpos + offset; break;
                case SEEK_END: rpos = wpos = dlen + offset; break;
                default: return -1;
            }

            if (rpos < 0 || rpos > dlen)
            {
              rpos = wpos = 0;
              return -1;
            }

            return 0;
        }

        long tell()
        {
            return rpos;
        }

        void close()
        {
            if (file)
            {
                fwrite(data, 1, dlen, file);
                fclose(file);
                file = nullptr;
            }

            if (!eptr && data)
            {
                delete[] data;
                data = nullptr;
            }
        }

    private:
        FILE* file;
        char* data;

        size_t dlen;
        size_t rpos;
        size_t wpos;

        bool eptr;
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
