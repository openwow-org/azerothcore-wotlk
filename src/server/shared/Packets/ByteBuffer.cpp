/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Affero General Public License as published by the
 * Free Software Foundation; either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "ByteBuffer.h"
#include "Errors.h"
#include "Log.h"
#include "MessageBuffer.h"
#include "Timer.h"
#include <ctime>
#include <sstream>
#include <utf8.h>

ByteBuffer::ByteBuffer(MessageBuffer&& buffer) :
    _rpos(0), _wpos(0), _storage(buffer.Move()) { }

ByteBufferPositionException::ByteBufferPositionException(bool add, std::size_t pos, std::size_t size, std::size_t valueSize)
{
    std::ostringstream ss;

    ss << "Attempted to " << (add ? "put" : "get") << " value with size: "
       << valueSize << " in ByteBuffer (pos: " << pos << " size: " << size
       << ")";

    message().assign(ss.str());
}

ByteBufferSourceException::ByteBufferSourceException(std::size_t pos, std::size_t size, std::size_t valueSize)
{
    std::ostringstream ss;

    ss << "Attempted to put a "
       << (valueSize > 0 ? "NULL-pointer" : "zero-sized value")
       << " in ByteBuffer (pos: " << pos << " size: " << size << ")";

    message().assign(ss.str());
}

ByteBufferInvalidValueException::ByteBufferInvalidValueException(char const* type, char const* value)
{
    message().assign(Acore::StringFormat("Invalid %s value (%s) found in ByteBuffer", type, value));
}

ByteBuffer& ByteBuffer::operator>>(float& value)
{
    value = read<float>();

    if (!std::isfinite(value))
        throw ByteBufferInvalidValueException("float", "infinity");

    return *this;
}

ByteBuffer& ByteBuffer::operator>>(double& value)
{
    value = read<double>();

    if (!std::isfinite(value))
        throw ByteBufferInvalidValueException("double", "infinity");

    return *this;
}

//=============================================================================
ByteBuffer &ByteBuffer::Get (uint32_t &val) {
  val = read<uint32_t>();
  return *this;
}

//=============================================================================
ByteBuffer &ByteBuffer::Get (uint8_t &val) {
  val = read<uint8_t>();
  return *this;
}

//=============================================================================
ByteBuffer& ByteBuffer::GetString(char* string, uint32_t maxChars)
{
    ASSERT(string);
    if (_rpos == size()) {
        *string = '\0';
    }
    else {
        for (uint32_t i = 0; i < maxChars; ++i) {
            string[i] = read<char>();
            if (string[i] == '\0') {
                break;
            }
            if (_rpos == size()) {
                string[i + 1] = '\0';
                break;
            }
        }
    }
    return *this;
}

std::string ByteBuffer::ReadCString(bool requireValidUtf8 /*= true*/)
{
    std::string value;

    while (rpos() < size()) // prevent crash the wrong string format in a packet
    {
        char c = read<char>();
        if (c == 0)
            break;
        value += c;
    }

    if (requireValidUtf8 && !utf8::is_valid(value.begin(), value.end()))
        throw ByteBufferInvalidValueException("string", value.c_str());

    return value;
}

uint32 ByteBuffer::ReadPackedTime()
{
    auto packedDate = read<uint32>();
    tm lt = tm();

    lt.tm_min = packedDate & 0x3F;
    lt.tm_hour = (packedDate >> 6) & 0x1F;
    //lt.tm_wday = (packedDate >> 11) & 7;
    lt.tm_mday = ((packedDate >> 14) & 0x3F) + 1;
    lt.tm_mon = (packedDate >> 20) & 0xF;
    lt.tm_year = ((packedDate >> 24) & 0x1F) + 100;

    return uint32(mktime(&lt));
}

void ByteBuffer::append(uint8 const* src, std::size_t cnt)
{
    ASSERT(src, "Attempted to put a NULL-pointer in ByteBuffer (pos: {} size: {})", _wpos, size());
    ASSERT(cnt, "Attempted to put a zero-sized value in ByteBuffer (pos: {} size: {})", _wpos, size());
    ASSERT(size() < 10000000);

    std::size_t const newSize = _wpos + cnt;

    if (_storage.capacity() < newSize) // custom memory allocation rules
    {
        if (newSize < 100)
            _storage.reserve(300);
        else if (newSize < 750)
            _storage.reserve(2500);
        else if (newSize < 6000)
            _storage.reserve(10000);
        else
            _storage.reserve(400000);
    }

    if (_storage.size() < newSize)
        _storage.resize(newSize);

    std::memcpy(&_storage[_wpos], src, cnt);
    _wpos = newSize;
}

void ByteBuffer::AppendPackedTime(time_t time)
{
    tm lt = Acore::Time::TimeBreakdown(time);
    append<uint32>((lt.tm_year - 100) << 24 | lt.tm_mon << 20 | (lt.tm_mday - 1) << 14 | lt.tm_wday << 11 | lt.tm_hour << 6 | lt.tm_min);
}

void ByteBuffer::put(std::size_t pos, uint8 const* src, std::size_t cnt)
{
    ASSERT(pos + cnt <= size(), "Attempted to put value with size: {} in ByteBuffer (pos: {} size: {})", cnt, pos, size());
    ASSERT(src, "Attempted to put a NULL-pointer in ByteBuffer (pos: {} size: {})", pos, size());
    ASSERT(cnt, "Attempted to put a zero-sized value in ByteBuffer (pos: {} size: {})", pos, size());

    std::memcpy(&_storage[pos], src, cnt);
}

void ByteBuffer::print_storage() const
{
    if (!sLog->ShouldLog("network.opcode.buffer", LogLevel::LOG_LEVEL_TRACE)) // optimize disabled trace output
        return;

    std::ostringstream o;
    o << "STORAGE_SIZE: " << size();

    for (uint32 i = 0; i < size(); ++i)
        o << read<uint8>(i) << " - ";

    o << " ";

    LOG_TRACE("network.opcode.buffer", "{}", o.str());
}

void ByteBuffer::textlike() const
{
    if (!sLog->ShouldLog("network.opcode.buffer", LogLevel::LOG_LEVEL_TRACE)) // optimize disabled trace output
        return;

    std::ostringstream o;
    o << "STORAGE_SIZE: " << size();

    for (uint32 i = 0; i < size(); ++i)
    {
        char buf[2];
        snprintf(buf, 2, "%c", read<uint8>(i));
        o << buf;
    }

    o << " ";

    LOG_TRACE("network.opcode.buffer", "{}", o.str());
}

void ByteBuffer::hexlike() const
{
    if (!sLog->ShouldLog("network.opcode.buffer", LogLevel::LOG_LEVEL_TRACE)) // optimize disabled trace output
        return;

    uint32 j = 1, k = 1;

    std::ostringstream o;
    o << "STORAGE_SIZE: " << size();

    for (uint32 i = 0; i < size(); ++i)
    {
        char buf[4];
        snprintf(buf, 4, "%2X ", read<uint8>(i));

        if ((i == (j * 8)) && ((i != (k * 16))))
        {
            o << "| ";
            ++j;
        }
        else if (i == (k * 16))
        {
            o << "\n";
            ++k;
            ++j;
        }

        o << buf;
    }

    o << " ";

    LOG_TRACE("network.opcode.buffer", "{}", o.str());
}
