#pragma once

class MemoryStream
{
public:
    MemoryStream(const char* buffer, size_t length) : m_buffer(buffer), m_length(length), m_pos(0) {}

    template<typename T>
    bool read(T& result)
    {
        size_t remain = m_length - m_pos;
        if (sizeof(T) > remain)
            return false;
        result = *(T*)(m_buffer + m_pos);
        m_pos += sizeof(T);
        return true;
    }

    template<typename T>
    bool readArray(T* result, size_t length)
    {
        size_t remain = m_length - m_pos;
        size_t readSize = length * sizeof(T);
        if (readSize > remain)
            return false;
        memcpy(result, m_buffer + m_pos, readSize);
        m_pos += readSize;
        return true;
    }

    bool seek(size_t pos)
    {
        if (pos > m_length)
            return false;
        m_pos = pos;
        return true;
    }
    
    bool advance(size_t offset)
    {
        if (m_pos + offset > m_length)
            return false;
        m_pos += offset;
        return true;
    }

    const char* buffer() const
    {
        return m_buffer;
    }

    const char* bufferAtPos() const
    {
        return m_buffer + m_pos;
    }

    size_t pos() const { return m_pos; }
    size_t length() const { return m_length; }
private:
    const char* m_buffer;
    size_t m_length;
    size_t m_pos;
};

template<typename StreamType, size_t MaxLineLength = 512>
class TextReader
{
public:
    TextReader(StreamType& stream) : m_stream(stream) {}

    const char* readLine()
    {
        thread_local static char tempBuffer[MaxLineLength];

        size_t remain = m_stream.length() - m_stream.pos();
        size_t pos = 0;
        const char* buf = m_stream.bufferAtPos();
        while (pos < remain)
        {
            char c = buf[pos];
            if (c == '\r' || c == '\n' || pos >= MaxLineLength)
            {
                // For files that have \r\n line endings
                if (c == '\r' &&
                    pos < remain - 1 &&
                    buf[pos + 1] == '\n')
                {
                    m_stream.advance(pos + 2);
                }
                else
                {
                    m_stream.advance(pos + 1);
                }
                tempBuffer[pos] = 0;
                return tempBuffer;
            }
            tempBuffer[pos++] = c;
        };
        return nullptr;
    }
private:
    StreamType m_stream;
};