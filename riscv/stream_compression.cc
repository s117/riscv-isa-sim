//
// Created by john on 8/3/24.
//

#include "stream_compression.h"
#include "sim.h"
#include <zlib.h>
#include <cassert>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

extern bool logging_on;

namespace {

constexpr size_t ZERO_PAGE_SIZE = 4096;
constexpr size_t ZERO_BLOCK_LARGE = size_t(1) << 20; // 1 MiB
constexpr int DATA_LEVEL = 6;

// Raw deflate blocks, byte-aligned and not final, that decode to n zero bytes without referring to earlier output.
// A fresh deflater starts with a literal zero, and Z_FULL_FLUSH ends its output on a byte boundary.
std::string make_zero_block(size_t n)
{
  std::vector<char> zeros(n, 0);
  std::vector<char> out(n + 1024);
  z_stream zs;
  memset(&zs, 0, sizeof(zs));
  if (deflateInit2(&zs, Z_BEST_COMPRESSION, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) != Z_OK)
    throw std::runtime_error("zlib::deflateInit2 failed");
  zs.next_in = (Bytef *) zeros.data();
  zs.avail_in = n;
  zs.next_out = (Bytef *) out.data();
  zs.avail_out = out.size();
  int zret = deflate(&zs, Z_FULL_FLUSH);
  const size_t produced = out.size() - zs.avail_out;
  deflateEnd(&zs);
  if (zret != Z_OK || zs.avail_in != 0 || zs.avail_out == 0)
    throw std::runtime_error("zlib::deflate failed to encode a zero block");
  return std::string(out.data(), produced);
}

const std::string &zero_block(size_t n)
{
  static const std::string large = make_zero_block(ZERO_BLOCK_LARGE);
  static const std::string page = make_zero_block(ZERO_PAGE_SIZE);
  assert(n == ZERO_BLOCK_LARGE || n == ZERO_PAGE_SIZE);
  return n == ZERO_BLOCK_LARGE ? large : page;
}

// The Adler-32 of a run of n zero bytes
uLong adler32_of_zeros(size_t n)
{
  return (uLong(n % 65521) << 16) | 1;
}

}

void stream_compression_t::compress_region(const void *src, size_t len, size_t out_chunk_sz, const std::function<void(const char *data, size_t len)> &on_chunk_compressed_cb)
{
  // The stream is a standard zlib stream, which any inflater reads, built from:
  // - the data pages, deflated at level DATA_LEVEL by a raw deflater flushed with Z_FULL_FLUSH after each run, which
  //   byte-aligns its output and forgets the data before, so it never refers back across the zero pages it didn't see;
  // - the runs of zero pages, as precomputed blocks (see make_zero_block()): most of a memory image is zeros, which
  //   deflate itself would scan byte by byte;
  // - the zlib header, a final empty block, and the Adler-32 of the whole region.
  constexpr size_t DEFLATE_CHUNK_SIZE = size_t(16) << 20; // 16 MB, the most fed to deflate (or adler32) at once
  assert(out_chunk_sz != 0);

  const char *const base = (const char *) src;
  static const char zero_page[ZERO_PAGE_SIZE] = {0};

  // Send the output in packets of exactly out_chunk_sz bytes, but the last
  std::vector<char> pending;
  pending.reserve(out_chunk_sz);
  auto emit = [&](const char *data, size_t n) {
    while (n)
    {
      const size_t k = std::min(n, out_chunk_sz - pending.size());
      pending.insert(pending.end(), data, data + k);
      data += k;
      n -= k;
      if (pending.size() == out_chunk_sz)
      {
        on_chunk_compressed_cb(pending.data(), pending.size());
        pending.clear();
      }
    }
  };

  z_stream zs;
  memset(&zs, 0, sizeof(zs));
  int zret;
  if ((zret = deflateInit2(&zs, DATA_LEVEL, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY)) != Z_OK)
    throw std::runtime_error("zlib::deflateInit2 failed with return code " + std::to_string(zret));
  // Free the deflater however this returns, also when the callback throws
  struct deflate_end_t
  {
    z_stream *zs;
    ~deflate_end_t() { deflateEnd(zs); }
  } deflate_end{&zs};
  std::vector<char> zout_buf(size_t(256) << 10);
  // Feed the deflater n bytes (none for Z_FINISH) and emit what it produces, until it has consumed them and flushed
  auto deflate_run = [&](const char *data, size_t n, int zflush) {
    do
    {
      const size_t assigned_in = std::min(n, DEFLATE_CHUNK_SIZE);
      const bool last_piece = assigned_in == n;
      zs.next_in = (z_const Bytef *) data;
      zs.avail_in = assigned_in;
      do
      {
        zs.next_out = (Bytef *) zout_buf.data();
        zs.avail_out = zout_buf.size();
        zret = deflate(&zs, last_piece ? zflush : Z_NO_FLUSH);
        if (zret != Z_OK && zret != Z_BUF_ERROR && zret != Z_STREAM_END)
          throw std::runtime_error("zlib::deflate failed with return code " + std::to_string(zret));
        emit(zout_buf.data(), zout_buf.size() - zs.avail_out);
      } while (zs.avail_out == 0 && zret != Z_STREAM_END); // more output pending for the same flush
      assert(zs.avail_in == 0);
      data += assigned_in;
      n -= assigned_in;
    } while (n);
  };

  static const char zlib_header[2] = {0x78, char(0x9c)}; // deflate, 32K window, default level
  emit(zlib_header, sizeof(zlib_header));

  uLong adler = adler32(0, Z_NULL, 0);
  size_t progress_mark = 0;
  size_t pos = 0;
  while (pos < len)
  {
    // A run of whole zero pages
    size_t end = pos;
    while (end + ZERO_PAGE_SIZE <= len && memcmp(base + end, zero_page, ZERO_PAGE_SIZE) == 0)
      end += ZERO_PAGE_SIZE;
    if (end > pos)
    {
      const size_t n = end - pos;
      for (size_t i = 0; i < n / ZERO_BLOCK_LARGE; i++)
        emit(zero_block(ZERO_BLOCK_LARGE).data(), zero_block(ZERO_BLOCK_LARGE).size());
      for (size_t i = 0; i < n % ZERO_BLOCK_LARGE / ZERO_PAGE_SIZE; i++)
        emit(zero_block(ZERO_PAGE_SIZE).data(), zero_block(ZERO_PAGE_SIZE).size());
      adler = adler32_combine(adler, adler32_of_zeros(n), n);
      pos = end;
    }

    // A run of data pages (a partial last page is data too)
    while (end < len && (end + ZERO_PAGE_SIZE > len || memcmp(base + end, zero_page, ZERO_PAGE_SIZE) != 0))
      end = std::min(end + ZERO_PAGE_SIZE, len);
    if (end > pos)
    {
      for (size_t p = pos; p < end; p += DEFLATE_CHUNK_SIZE)
        adler = adler32(adler, (const Bytef *) base + p, std::min(end - p, DEFLATE_CHUNK_SIZE));
      deflate_run(base + pos, end - pos, Z_FULL_FLUSH);
      pos = end;
    }

    if (unlikely(logging_on && (progress_mark << 20) <= pos))
    {
      fprintf(stderr, "Compressed %" PRIu64 "MB memory.\n", uint64_t(pos) >> 20);
      progress_mark += 1024;
    }
  }

  // The final (empty) block, then the trailer
  deflate_run(nullptr, 0, Z_FINISH);
  if (zret != Z_STREAM_END)
    throw std::runtime_error("zlib::deflate didn't finish the stream, return code " + std::to_string(zret));
  const char trailer[4] = {char(adler >> 24), char(adler >> 16), char(adler >> 8), char(adler)};
  emit(trailer, sizeof(trailer));
  if (!pending.empty())
    on_chunk_compressed_cb(pending.data(), pending.size());
}

size_t stream_compression_t::decompress_region(void *dst, size_t dst_limit, const std::function<void(char *&input, size_t &len)> &chunk_supplying_cb)
{
  constexpr size_t INFLATE_CHUNK_SIZE = size_t(16) << 20; // 16 MB
  assert(dst_limit > 0);
  assert(dst);
  z_stream zs;
  memset(&zs, 0, sizeof(zs));
  int zret;

  if ((zret = inflateInit(&zs)) != Z_OK)
  {
    inflateEnd(&zs);
    throw std::runtime_error("zlib::inflateInit failed with return code " + std::to_string(zret));
  }

  char *input_data = nullptr;
  size_t input_len = 0;
  size_t output_count = 0;
  size_t progress_mark = 0;
  do { // decompress the stream
    // chunk supplying function
    chunk_supplying_cb(input_data, input_len);
    assert(input_len < (size_t(1) << 32));
    if (input_data == nullptr || input_len == 0)
      throw std::runtime_error("The compressed chunk supplier stopped providing data before reaching the end of the compressed stream.");
    zs.next_in = (z_const Bytef *) input_data;
    zs.avail_in = input_len;
    do { // decompress the obtained chunk
      zs.next_out = (z_const Bytef *) dst + output_count;
      size_t assigned_out = std::min(dst_limit - output_count, INFLATE_CHUNK_SIZE);
      zs.avail_out = assigned_out;
      const uInt avail_in_before = zs.avail_in;

      // Keep calling inflate even when the destination is full (avail_out == 0): the end of the stream
      // (end-of-block code and adler32 trailer) produces no output, and may arrive in a later chunk.
      zret = inflate(&zs, Z_NO_FLUSH);

      if (zret != Z_OK && zret != Z_BUF_ERROR && zret != Z_STREAM_END)
      {
        inflateEnd(&zs);
        throw std::runtime_error("zlib::inflate failed with return code " + std::to_string(zret));
      }

      if (assigned_out == 0 && zret != Z_STREAM_END && zs.avail_in == avail_in_before)
      {
        // The destination is full, but the stream still has data that needs output space.
        inflateEnd(&zs);
        throw std::runtime_error("zlib::inflate cannot decompress the full stream because the destination is too small.");
      }

      assert(zs.avail_out <= assigned_out);
      output_count += assigned_out - zs.avail_out;

      if (unlikely(logging_on && (progress_mark << 20) <= output_count))
      {
        fprintf(stderr, "Decompressed %" PRIu64 "MB memory.\n", uint64_t(output_count) >> 20);
        progress_mark += 1024;
      }
      // Once the stream has ended, inflate consumes no more input, so stop even if the chunk has bytes left.
    } while (zret != Z_STREAM_END && zs.avail_in != 0);
  } while (zret != Z_STREAM_END);

  if (zs.avail_in != 0)
  {
    inflateEnd(&zs);
    throw std::runtime_error("The compressed chunk supplier has extra data after the compressed stream is ended.");
  }

  zret = inflateEnd(&zs);
  if (zret != Z_OK)
    throw std::runtime_error("zlib::inflateEnd failed with return code " + std::to_string(zret));

  chunk_supplying_cb(input_data, input_len);
  if (input_data != nullptr || input_len != 0)
    throw std::runtime_error("The compressed chunk supplier has extra data after the compressed stream is ended.");

  return output_count;
}
