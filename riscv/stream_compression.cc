//
// Created by john on 8/3/24.
//

#include "stream_compression.h"
#include "sim.h"
#include <zlib.h>
#include <cassert>

extern bool logging_on;

void stream_compression_t::compress_region(const void *src, size_t len, size_t out_chunk_sz, const std::function<void(const char *data, size_t len)> &on_chunk_compressed_cb)
{
  constexpr size_t DEFLATE_CHUNK_SIZE = size_t(16) << 20; // 16 MB
  static_assert(DEFLATE_CHUNK_SIZE < (uint64_t(1) << 32), "DEFLATE_CHUNK_SIZE must be less than 4GB.");

  assert(DEFLATE_CHUNK_SIZE >= (2 * out_chunk_sz));
  assert(out_chunk_sz != 0);

  int zret;
  std::vector<char> zout_buf(DEFLATE_CHUNK_SIZE);

  z_stream zs;
  memset(&zs, 0, sizeof(zs));
  if ((zret = deflateInit(&zs, Z_BEST_COMPRESSION)) != Z_OK)
  {
    deflateEnd(&zs);
    throw std::runtime_error("zlib::deflateInit failed with return code " + std::to_string(zret));
  }

  size_t input_consumed = 0;
  size_t output_remain = 0;
  size_t assigned_in;
  size_t progress_mark = 0;

  // Chunk-by-chunk stream compression.
  // Note that zs.avail_in is u32 type cannot handle chunk size larger than 1^32-1.
  do {
    zs.next_in = (z_const Bytef *) src + input_consumed;
    assigned_in = std::min(len - input_consumed, DEFLATE_CHUNK_SIZE);
    zs.avail_in = assigned_in;

    int zflush = Z_NO_FLUSH;
    if (zs.avail_in == 0)
      zflush = Z_FINISH;

    // Compress and send the current input chunk
    do {
      zs.next_out = (Bytef *) zout_buf.data() + output_remain;
      zs.avail_out = zout_buf.size() - output_remain;
      zret = deflate(&zs, zflush);

      if (zret != Z_OK && zret != Z_BUF_ERROR && zret != Z_STREAM_END)
      {
        deflateEnd(&zs);
        throw std::runtime_error("zlib::deflate failed with return code " + std::to_string(zret));
      }

      size_t zout_accumulated = zout_buf.size() - zs.avail_out;

      size_t send_offset = 0;
      while (send_offset + out_chunk_sz <= zout_accumulated)
      {
        // prioritize sending packets in exact `out_chunk_sz` bytes
        on_chunk_compressed_cb(zout_buf.data() + send_offset, out_chunk_sz);
        send_offset += out_chunk_sz;
      }
      output_remain = zout_accumulated - send_offset;
      assert(output_remain < out_chunk_sz);
      if (output_remain)
        std::move(zout_buf.begin() + send_offset, zout_buf.begin() + zout_accumulated, zout_buf.begin());
    } while (zs.avail_out == 0 && zret != Z_STREAM_END); // If deflate returns with avail_out == 0, this function must be called again with the same value of the flush parameter and more output space (updated avail_out), until the flush is complete (deflate returns with non-zero avail_out).

    input_consumed += assigned_in - zs.avail_in;

    if (logging_on && (progress_mark << 20) <= input_consumed)
    {
      ifprintf(logging_on, stderr, "Compressed %" PRIu64 "MB memory.\n", uint64_t(input_consumed) >> 20);
      progress_mark += 1024;
    }
  } while (zret != Z_STREAM_END);

  assert(input_consumed == len);
  if (output_remain) // the compressed stream is finished, send whatever left in the buffer
    on_chunk_compressed_cb(zout_buf.data(), output_remain);

  zret = deflateEnd(&zs);
  if (zret != Z_OK)
    throw std::runtime_error("zlib::deflateEnd failed with return code " + std::to_string(zret));
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
      if (zs.avail_out == 0)
      {
        inflateEnd(&zs);
        throw std::runtime_error("zlib::inflate cannot decompress the full stream because the destination is too small.");
      }

      zret = inflate(&zs, Z_NO_FLUSH);

      if (zret != Z_OK && zret != Z_BUF_ERROR && zret != Z_STREAM_END)
      {
        inflateEnd(&zs);
        throw std::runtime_error("zlib::inflate failed with return code " + std::to_string(zret));
      }

      assert(zs.avail_out <= assigned_out);
      output_count += assigned_out - zs.avail_out;

      if (logging_on && (progress_mark << 20) <= output_count)
      {
        ifprintf(logging_on, stderr, "Decompressed %" PRIu64 "MB memory.\n", uint64_t(output_count) >> 20);
        progress_mark += 1024;
      }
    } while (zs.avail_in != 0);
  } while (zret != Z_STREAM_END);

  zret = inflateEnd(&zs);
  if (zret != Z_OK)
    throw std::runtime_error("zlib::inflateEnd failed with return code " + std::to_string(zret));

  chunk_supplying_cb(input_data, input_len);
  if (input_data != nullptr || input_len != 0)
    throw std::runtime_error("The compressed chunk supplier has extra data after the compressed stream is ended.");

  return output_count;
}
